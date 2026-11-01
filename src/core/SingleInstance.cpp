#include "SingleInstance.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ravenspot {

std::string SingleInstance::socketPath() {
    const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (runtimeDir && runtimeDir[0] != '\0') {
        return std::string(runtimeDir) + "/ravenspot.sock";
    }
    return "/tmp/ravenspot.sock";
}

std::string SingleInstance::lockPath() {
    const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (runtimeDir && runtimeDir[0] != '\0') {
        return std::string(runtimeDir) + "/ravenspot.lock";
    }
    return "/tmp/ravenspot.lock";
}

SingleInstance::SingleInstance(const std::string &customSocketPath)
    : m_customSocketPath(customSocketPath) {}

SingleInstance::~SingleInstance() {
    releaseLock();
}

SingleInstance::SingleInstance(SingleInstance &&other) noexcept
    : m_lockFd(other.m_lockFd)
    , m_existingPid(other.m_existingPid)
    , m_customSocketPath(std::move(other.m_customSocketPath))
{
    other.m_lockFd = -1;
    other.m_existingPid = 0;
}

SingleInstance &SingleInstance::operator=(SingleInstance &&other) noexcept {
    if (this != &other) {
        releaseLock();
        m_lockFd = other.m_lockFd;
        m_existingPid = other.m_existingPid;
        m_customSocketPath = std::move(other.m_customSocketPath);
        other.m_lockFd = -1;
        other.m_existingPid = 0;
    }
    return *this;
}

bool SingleInstance::tryAcquireLock() {
    const std::string lPath = m_customSocketPath.empty() 
        ? lockPath() 
        : (m_customSocketPath + ".lock");

    m_lockFd = ::open(lPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (m_lockFd < 0) {
        return false;
    }

    if (::flock(m_lockFd, LOCK_EX | LOCK_NB) != 0) {
        // Another instance holds the lock
        char buf[64] = {0};
        ssize_t n = ::read(m_lockFd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            m_existingPid = static_cast<pid_t>(std::strtol(buf, nullptr, 10));
        }
        ::close(m_lockFd);
        m_lockFd = -1;
        return false;
    }

    // Lock acquired: write current process PID
    if (::ftruncate(m_lockFd, 0) == 0) {
        (void)::lseek(m_lockFd, 0, SEEK_SET);
        std::string pidStr = std::to_string(::getpid()) + "\n";
        (void)::write(m_lockFd, pidStr.data(), pidStr.size());
    }

    // Clean up stale socket from previous crashes
    const std::string sPath = m_customSocketPath.empty() ? socketPath() : m_customSocketPath;
    ::unlink(sPath.c_str());

    return true;
}

void SingleInstance::releaseLock() {
    if (m_lockFd >= 0) {
        const std::string sPath = m_customSocketPath.empty() ? socketPath() : m_customSocketPath;
        const std::string lPath = m_customSocketPath.empty() ? lockPath() : (m_customSocketPath + ".lock");

        ::unlink(sPath.c_str());
        ::unlink(lPath.c_str());
        ::close(m_lockFd);
        m_lockFd = -1;
    }
}

bool SingleInstance::sendCommand(std::string_view command, std::string *response, int timeoutMs) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }

    // Set non-blocking mode for connect timeout control
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const std::string sPath = socketPath();
    if (sPath.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, sPath.c_str(), sizeof(addr.sun_path) - 1);

    int res = ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (res < 0) {
        if (errno == EINPROGRESS) {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int pRes = ::poll(&pfd, 1, timeoutMs);
            if (pRes <= 0) {
                ::close(fd);
                return false;
            }
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
                ::close(fd);
                return false;
            }
        } else {
            ::close(fd);
            return false;
        }
    }

    // Revert to blocking mode with explicit socket send/receive timeouts
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags);
    }
    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Send payload
    std::string payload = std::string(command);
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }
    ssize_t sent = ::write(fd, payload.data(), payload.size());
    if (sent <= 0) {
        ::close(fd);
        return false;
    }

    // Read response if requested
    if (response) {
        response->clear();
        char buf[1024];
        while (true) {
            ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                response->append(buf, static_cast<size_t>(n));
                if (std::memchr(buf, '\n', static_cast<size_t>(n)) != nullptr) {
                    break;
                }
            } else {
                break;
            }
        }
    }

    ::close(fd);
    return true;
}

} // namespace ravenspot
