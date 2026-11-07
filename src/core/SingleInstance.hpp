#pragma once

#include <string>
#include <string_view>
#ifdef _WIN32
namespace ravenspot { using pid_t = long; } // MSVC has no pid_t
#else
#include <sys/types.h>
#endif

namespace ravenspot {

class SingleInstance {
public:
    explicit SingleInstance(const std::string &customSocketPath = "");
    ~SingleInstance();

    SingleInstance(const SingleInstance &) = delete;
    SingleInstance &operator=(const SingleInstance &) = delete;
    SingleInstance(SingleInstance &&other) noexcept;
    SingleInstance &operator=(SingleInstance &&other) noexcept;

    // --- Daemon-Side Lock Operations ---
    /**
     * @brief Attempts to acquire non-blocking exclusive flock on ravenspot.lock.
     * @return true if this process is the sole primary daemon instance; false if another is running.
     */
    bool tryAcquireLock();

    /**
     * @brief Alias for tryAcquireLock() for backward/forward compatibility.
     */
    bool acquire() { return tryAcquireLock(); }

    /**
     * @brief Releases the lock and unlinks lock and socket files.
     */
    void releaseLock();

    /**
     * @brief Alias for releaseLock().
     */
    void release() { releaseLock(); }

    /**
     * @brief Checks if lock is currently held by this instance.
     */
#ifdef _WIN32
    [[nodiscard]] bool isLocked() const noexcept { return m_mutex != nullptr; }
#else
    [[nodiscard]] bool isLocked() const noexcept { return m_lockFd >= 0; }
#endif

    /**
     * @brief Returns PID of currently running daemon if lock acquisition failed.
     */
    [[nodiscard]] pid_t existingPid() const noexcept { return m_existingPid; }

    // --- Client-Side Fast Probe Operations (<1ms) ---
    /**
     * @brief Fast POSIX socket client probe. Transmits newline-terminated command to daemon.
     * @param command Command string ("toggle", "show", "hide", "quit", "status", "reload")
     * @param response Optional pointer to receive server reply ("ok\n" or status JSON)
     * @param timeoutMs Timeout in milliseconds for connection and reply (default 200ms)
     * @return true if connected and successfully transmitted; false if daemon is not running.
     */
    static bool sendCommand(
        std::string_view command,
        std::string *response = nullptr,
        int timeoutMs = 200
    );

    // --- Standard Path Resolvers ---
    static std::string socketPath();
    static std::string lockPath();

private:
#ifdef _WIN32
    void *m_mutex = nullptr; // named mutex held by the daemon (Local\\RavenSpot.<user>)
#else
    int m_lockFd = -1;
#endif
    pid_t m_existingPid = 0;
    std::string m_customSocketPath;
};

} // namespace ravenspot
