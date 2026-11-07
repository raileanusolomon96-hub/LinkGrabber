#pragma once
#include <juce_core/juce_core.h>

//==============================================================================
// Minimal child-process runner for the download pipeline (architecture.md R6).
//
// Why not juce::ChildProcess: kill() only hits the direct child, so an ffmpeg (or deno)
// grandchild could survive a cancel/destroy and keep the pipe open. ProcessRunner spawns
// the child with posix_spawn in a NEW PROCESS GROUP and signals the whole group.
//
//  - stdout and stderr of the child are merged into one pipe (dup2 to fd 1 and 2); stdin is
//    /dev/null. Every other inherited fd is closed in the child (macOS CLOEXEC_DEFAULT), the
//    parent's read end is FD_CLOEXEC + O_NONBLOCK.
//  - read() polls with a timeout so the owner thread can notice a cancel request.
//  - No zombies: the leader is always reaped (waitpid), EINTR is handled.
//
// Thread rules:
//  - OWNER thread (the worker): start(), read(), hasLeaderExited(), finish(),
//    killProcessGroup(), destructor.
//  - ANY thread: requestTerminate(), forceKill() (non-blocking, just killpg; guarded by an
//    internal lock so they can never signal a recycled pid after the leader was reaped).
class ProcessRunner
{
public:
    static constexpr int unknownExitCode = -2;   // leader reaped by someone else (SIGCHLD ignored)

    ProcessRunner() = default;
    ~ProcessRunner();

    // argv[0] must be an absolute path (it is executed with posix_spawn, no PATH search).
    // Returns false if the process could not be spawned. Inherits the host environment.
    bool start(const juce::StringArray& argv);

    // Owner thread. Waits up to timeoutMs for output.
    //   > 0 : number of bytes read
    //     0 : timeout / interrupted, no data yet
    //    -1 : EOF (every writer closed) or error
    int read(void* dest, int maxBytes, int timeoutMs);

    // Owner thread. True once the group leader has terminated (not yet reaped is fine).
    bool hasLeaderExited();

    // Any thread, non-blocking: SIGTERM / SIGKILL to the whole process group.
    void requestTerminate();
    void forceKill();

    // Owner thread, blocking (bounded: ~graceMs + 2 s). SIGTERM to the group, wait up to
    // graceMs for the leader, SIGKILL to the group (also takes stragglers), reap.
    // Returns the exit code (128+signal if killed by a signal). Idempotent.
    int killProcessGroup(int graceMs = 500);

    // Owner thread. Waits up to waitLeaderMs for the leader to exit by itself; if it does not,
    // falls back to killProcessGroup(). Always SIGKILLs leftover group members and reaps.
    int finish(int waitLeaderMs);

    bool isRunning() const;
    int getPid() const;

private:
    void signalGroup(int signalNumber);
    bool waitLeader(int timeoutMs);
    int reap();

    mutable juce::CriticalSection lock;   // guards pid/reaped/exitCode against signalGroup()
    int pid = 0;
    bool reaped = false;
    int exitCode = unknownExitCode;
    int readFd = -1;                      // owner thread only

    JUCE_DECLARE_NON_COPYABLE(ProcessRunner)
};
