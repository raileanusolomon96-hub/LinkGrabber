#include "ProcessRunner.h"

#if JUCE_WINDOWS

// Not a target platform (architecture.md: macOS first). Stub so the project still compiles.
ProcessRunner::~ProcessRunner() = default;
bool ProcessRunner::start(const juce::StringArray&) { return false; }
int ProcessRunner::read(void*, int, int) { return -1; }
bool ProcessRunner::hasLeaderExited() { return true; }
void ProcessRunner::requestTerminate() {}
void ProcessRunner::forceKill() {}
int ProcessRunner::killProcessGroup(int) { return unknownExitCode; }
int ProcessRunner::finish(int) { return unknownExitCode; }
bool ProcessRunner::isRunning() const { return false; }
int ProcessRunner::getPid() const { return 0; }
void ProcessRunner::signalGroup(int) {}
bool ProcessRunner::waitLeader(int) { return true; }
int ProcessRunner::reap() { return unknownExitCode; }

#else

#include <spawn.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string>
#include <vector>

#if JUCE_MAC
 #include <crt_externs.h>
#else
 extern char** environ;
#endif

namespace
{
    char** getEnvironmentBlock()
    {
       #if JUCE_MAC
        return *_NSGetEnviron();
       #else
        return environ;
       #endif
    }

    // Moves *fd to a descriptor >= 3 with FD_CLOEXEC (a host may have closed 0..2, which would
    // otherwise let a pipe end land on a standard descriptor and confuse the dup2 actions).
    bool moveToHighCloexec(int& fd)
    {
        const int moved = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        ::close(fd);
        fd = moved;
        return moved >= 0;
    }
}

//==============================================================================
ProcessRunner::~ProcessRunner()
{
    bool needsKill = false;
    {
        const juce::ScopedLock sl(lock);
        needsKill = (pid > 0 && ! reaped);
    }

    if (needsKill)
        killProcessGroup(200);

    if (readFd >= 0)
    {
        ::close(readFd);
        readFd = -1;
    }
}

bool ProcessRunner::start(const juce::StringArray& argv)
{
    {
        const juce::ScopedLock sl(lock);
        if (pid != 0 || argv.isEmpty())
            return false;
    }

    int fds[2] = { -1, -1 };
    if (::pipe(fds) != 0)
        return false;

    if (! moveToHighCloexec(fds[0]))
    {
        if (fds[1] >= 0) ::close(fds[1]);
        return false;
    }
    if (! moveToHighCloexec(fds[1]))
    {
        ::close(fds[0]);
        return false;
    }

    const int rfd = fds[0];
    const int wfd = fds[1];

    // Non-blocking read end: read() is always preceded by poll(), but never block regardless.
    ::fcntl(rfd, F_SETFL, ::fcntl(rfd, F_GETFL) | O_NONBLOCK);

    // argv as C strings (kept alive in `storage`).
    std::vector<std::string> storage;
    storage.reserve(static_cast<size_t>(argv.size()));
    for (const auto& a : argv)
        storage.emplace_back(a.toRawUTF8());

    std::vector<char*> cargv;
    cargv.reserve(storage.size() + 1);
    for (auto& s : storage)
        cargv.push_back(s.data());
    cargv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attr);

    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, wfd, 1);
    posix_spawn_file_actions_adddup2(&actions, wfd, 2);

    int flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
   #ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;   // close every inherited fd we did not set up above
   #endif
    posix_spawnattr_setflags(&attr, static_cast<short>(flags));
    posix_spawnattr_setpgroup(&attr, 0);    // new process group, pgid == child pid

    sigset_t noSignals;
    sigemptyset(&noSignals);
    posix_spawnattr_setsigmask(&attr, &noSignals);

    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);   // a host may ignore it; ignored dispositions survive exec
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGHUP);
    sigaddset(&defaults, SIGQUIT);
    sigaddset(&defaults, SIGCHLD);
    posix_spawnattr_setsigdefault(&attr, &defaults);

    pid_t child = 0;
    const int rc = ::posix_spawn(&child, cargv[0], &actions, &attr, cargv.data(), getEnvironmentBlock());

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    ::close(wfd);   // the parent must not hold the write end, or EOF would never arrive

    if (rc != 0)
    {
        ::close(rfd);
        return false;
    }

    readFd = rfd;
    {
        const juce::ScopedLock sl(lock);
        pid = static_cast<int>(child);
        reaped = false;
        exitCode = unknownExitCode;
    }
    return true;
}

//==============================================================================
int ProcessRunner::read(void* dest, int maxBytes, int timeoutMs)
{
    if (readFd < 0 || maxBytes <= 0)
        return -1;

    struct pollfd p;
    p.fd = readFd;
    p.events = POLLIN;
    p.revents = 0;

    const int pr = ::poll(&p, 1, juce::jmax(0, timeoutMs));
    if (pr < 0)
        return errno == EINTR ? 0 : -1;
    if (pr == 0)
        return 0;

    const auto n = ::read(readFd, dest, static_cast<size_t>(maxBytes));
    if (n > 0)
        return static_cast<int>(n);
    if (n == 0)
        return -1;  // EOF
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        return 0;
    return -1;
}

//==============================================================================
bool ProcessRunner::hasLeaderExited()
{
    int p = 0;
    {
        const juce::ScopedLock sl(lock);
        if (reaped)
            return true;
        p = pid;
    }
    if (p <= 0)
        return false;

    siginfo_t info {};
    info.si_pid = 0;
    const int r = ::waitid(P_PID, static_cast<id_t>(p), &info, WEXITED | WNOHANG | WNOWAIT);
    if (r == 0)
        return info.si_pid != 0;
    return errno == ECHILD;  // somebody else already reaped it
}

bool ProcessRunner::waitLeader(int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + juce::jmax(0, timeoutMs);
    for (;;)
    {
        if (hasLeaderExited())
            return true;
        if (juce::Time::getMillisecondCounterHiRes() >= deadline)
            return false;
        juce::Thread::sleep(5);
    }
}

void ProcessRunner::signalGroup(int signalNumber)
{
    // Under the lock so we never signal a pid that was reaped (and could be recycled).
    const juce::ScopedLock sl(lock);
    if (pid > 0 && ! reaped)
        ::killpg(static_cast<pid_t>(pid), signalNumber);
}

void ProcessRunner::requestTerminate() { signalGroup(SIGTERM); }
void ProcessRunner::forceKill()        { signalGroup(SIGKILL); }

int ProcessRunner::reap()
{
    const juce::ScopedLock sl(lock);
    if (reaped || pid <= 0)
        return exitCode;

    int status = 0;
    pid_t r = -1;

    // The leader is known to be dead (or has just been SIGKILLed), so this returns promptly.
    // WNOHANG keeps even a pathological uninterruptible process from freezing the worker.
    for (int attempt = 0; attempt < 400; ++attempt)   // <= ~2 s
    {
        do { r = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG); }
        while (r < 0 && errno == EINTR);

        if (r != 0)
            break;
        juce::Thread::sleep(5);
    }

    if (r == static_cast<pid_t>(pid))
    {
        if (WIFEXITED(status))
            exitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            exitCode = 128 + WTERMSIG(status);
        else
            exitCode = unknownExitCode;
        reaped = true;
    }
    else if (r < 0)
    {
        exitCode = unknownExitCode;  // ECHILD: reaped elsewhere
        reaped = true;
    }
    // r == 0 after 2 s: leave it unreaped (the destructor tries once more); never hang.

    return exitCode;
}

int ProcessRunner::killProcessGroup(int graceMs)
{
    {
        const juce::ScopedLock sl(lock);
        if (pid <= 0 || reaped)
            return exitCode;
    }

    signalGroup(SIGTERM);
    waitLeader(graceMs);
    signalGroup(SIGKILL);   // always: takes the leader if it ignored TERM and any grandchildren
    waitLeader(2000);
    return reap();
}

int ProcessRunner::finish(int waitLeaderMs)
{
    {
        const juce::ScopedLock sl(lock);
        if (pid <= 0 || reaped)
            return exitCode;
    }

    if (! waitLeader(waitLeaderMs))
        return killProcessGroup(500);

    signalGroup(SIGKILL);   // leftovers of a finished job (e.g. a helper that outlived yt-dlp)
    return reap();
}

bool ProcessRunner::isRunning() const
{
    const juce::ScopedLock sl(lock);
    return pid > 0 && ! reaped;
}

int ProcessRunner::getPid() const
{
    const juce::ScopedLock sl(lock);
    return pid;
}

#endif
