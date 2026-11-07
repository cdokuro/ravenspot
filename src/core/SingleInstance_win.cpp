// Windows single-instance lock + fast CLI client.
//  - Lock: a named mutex in the session namespace (released by the OS on crash,
//    so there is no stale lock file to clean up).
//  - IPC: DaemonServer's QLocalServer listens on a named pipe; this client talks
//    to it with raw Win32 calls so "ravenspot toggle" needs no Qt event loop.
#include "SingleInstance.hpp"

#include <windows.h>
#include <string>

namespace ravenspot {

namespace {
std::string userTag() {
    char name[256] = {0};
    DWORD len = sizeof(name);
    if (!::GetUserNameA(name, &len) || name[0] == '\0') return "user";
    std::string s(name);
    for (char &c : s) {
        if (c == '\\' || c == '/' || c == ':' || c == ' ') c = '_';
    }
    return s;
}
std::wstring toWide(const std::string &s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
} // namespace

// QLocalServer name; the pipe is \\.\pipe\<name>
std::string SingleInstance::socketPath() {
    return "ravenspot-" + userTag();
}

std::string SingleInstance::lockPath() {
    return "Local\\RavenSpot.Daemon." + userTag();
}

SingleInstance::SingleInstance(const std::string &customSocketPath)
    : m_customSocketPath(customSocketPath) {}

SingleInstance::~SingleInstance() {
    releaseLock();
}

SingleInstance::SingleInstance(SingleInstance &&other) noexcept
    : m_mutex(other.m_mutex)
    , m_existingPid(other.m_existingPid)
    , m_customSocketPath(std::move(other.m_customSocketPath))
{
    other.m_mutex = nullptr;
    other.m_existingPid = 0;
}

SingleInstance &SingleInstance::operator=(SingleInstance &&other) noexcept {
    if (this != &other) {
        releaseLock();
        m_mutex = other.m_mutex;
        m_existingPid = other.m_existingPid;
        m_customSocketPath = std::move(other.m_customSocketPath);
        other.m_mutex = nullptr;
        other.m_existingPid = 0;
    }
    return *this;
}

bool SingleInstance::tryAcquireLock() {
    if (m_mutex) return true;
    const std::string name = m_customSocketPath.empty() ? lockPath() : ("Local\\RavenSpot." + m_customSocketPath);
    HANDLE h = ::CreateMutexW(nullptr, TRUE, toWide(name).c_str());
    if (!h) return false;
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(h);
        m_existingPid = 0; // not tracked on Windows ("ravenspot status" reports it)
        return false;
    }
    m_mutex = h;
    return true;
}

void SingleInstance::releaseLock() {
    if (m_mutex) {
        ::ReleaseMutex(static_cast<HANDLE>(m_mutex));
        ::CloseHandle(static_cast<HANDLE>(m_mutex));
        m_mutex = nullptr;
    }
}

bool SingleInstance::sendCommand(std::string_view command, std::string *response, int timeoutMs) {
    const std::wstring pipe = L"\\\\.\\pipe\\" + toWide(socketPath());
    const ULONGLONG deadline = ::GetTickCount64() + static_cast<ULONGLONG>(timeoutMs > 0 ? timeoutMs : 200);

    HANDLE h = INVALID_HANDLE_VALUE;
    while (true) {
        h = ::CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) break;
        const DWORD err = ::GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) return false; // daemon not running
        if (err != ERROR_PIPE_BUSY || ::GetTickCount64() >= deadline) return false;
        ::WaitNamedPipeW(pipe.c_str(), 50);
    }

    // The daemon was started by another process: let it take the foreground
    // when it shows the window ("toggle"/"show" from a shortcut or script).
    ::AllowSetForegroundWindow(ASFW_ANY);

    std::string payload(command);
    if (payload.empty() || payload.back() != '\n') payload.push_back('\n');
    DWORD written = 0;
    if (!::WriteFile(h, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) || written == 0) {
        ::CloseHandle(h);
        return false;
    }

    if (response) {
        response->clear();
        char buf[1024];
        while (::GetTickCount64() < deadline + 300) {
            DWORD avail = 0;
            if (!::PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) break; // server closed
            if (avail == 0) { ::Sleep(5); continue; }
            DWORD n = 0;
            if (!::ReadFile(h, buf, sizeof(buf), &n, nullptr) || n == 0) break;
            response->append(buf, n);
            if (response->find('\n') != std::string::npos) break;
        }
    }
    ::CloseHandle(h);
    return true;
}

} // namespace ravenspot
