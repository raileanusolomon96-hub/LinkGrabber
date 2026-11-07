#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include "DownloadCommand.h"
#include "ToolLocator.h"
#include "ProcessRunner.h"

//==============================================================================
// Processor-OWNED job runner (architecture.md, Core Component 3, D3, R6).
//
//  - One job at a time; start() while busy is ignored (returns false).
//  - A worker juce::Thread validates/locates tools, creates the output folder, spawns
//    `/usr/bin/env PATH=... yt-dlp ...` through ProcessRunner (posix_spawn in its OWN PROCESS
//    GROUP), reads the merged stdout/stderr with a poll() timeout, feeds
//    DownloadCommand::OutputParser and publishes a status snapshot under a CriticalSection.
//  - Cancel / destroy signal the WHOLE process group (yt-dlp, ffmpeg, deno helper): SIGTERM,
//    a short grace period, SIGKILL, then waitpid. No orphans, no zombies.
//  - A job that does not finish `done` (cancelled, destroyed, failed) gets its partial files
//    removed from the output folder (JobCleanup.h rules: only files of this job's video id).
//  - The worker NEVER touches the UI, the state tree or the history store. It only
//    updates the snapshot, appends to LinkGrabber.log (lock-protected) and calls
//    triggerAsyncUpdate().
//  - Listeners are called on the MESSAGE thread (juce::AsyncUpdater). They come and go
//    (the editor listens without owning), so closing the editor never stops a job.
//  - start(), cancel(), listeners, destructor: message thread. getStatus(): any thread.
//  - Nothing here is ever called from the audio thread.
class DownloadManager : private juce::Thread,
                        private juce::AsyncUpdater
{
public:
    enum class State { idle, lookingUp, downloading, converting, done, error };

    struct Status
    {
        State state = State::idle;
        int percent = 0;                 // monotonic non-decreasing within a job, 0..100
        bool convertingSeen = false;     // true once the converting phase was entered
        juce::String filePath;           // final file (state == done)
        juce::String title, videoId;     // derived from the file name / yt-dlp output when possible
        juce::String format;             // "mp3" | "wav"
        DownloadCommand::FailureKind errorKind = DownloadCommand::FailureKind::none;
        juce::String errorMessage;       // user-facing message ("Cancelled" for state idle after a cancel)
        juce::String detail;             // last tool output lines (diagnostics only, never shown raw in the UI)
        double elapsedSeconds = 0.0;
        juce::int64 bytes = 0;
        juce::uint32 jobSerial = 0;      // increments per start()

        bool isActive() const noexcept   { return state == State::lookingUp || state == State::downloading || state == State::converting; }
        bool isFinished() const noexcept { return state == State::done || state == State::error; }
    };

    // Message-thread callbacks.
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void downloadStatusChanged(const Status&) {}   // coalesced; every change
        virtual void downloadFinished(const Status&) {}        // exactly once per job (done or error)
    };

    DownloadManager();
    ~DownloadManager() override;

    void addListener(Listener* l)    { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

    // Message thread. `formatIndex`: 0 = MP3 320, 1 = WAV 24-bit (the `format` parameter),
    // `outputFolder`: absolute path. Returns true if a job was started. Returns false if busy
    // (ignored) or if the link is invalid (status becomes error/invalidLink, nothing spawned).
    bool start(const juce::String& link, int formatIndex, const juce::String& outputFolder);

    // Message thread, non-blocking. Sends SIGTERM to the whole process group right away; the
    // worker escalates to SIGKILL after a short grace period, deletes the job's partial files and
    // publishes state idle with message "Cancelled".
    void cancel();

    bool isBusy() const noexcept { return busy.load(); }

    // Any thread. A copy; elapsedSeconds is live while a job is active.
    Status getStatus() const;

    // Test seam: replaces the tool search directories (an empty array finds nothing).
    void setToolSearchDirsOverride(const juce::StringArray& dirs);
    void clearToolSearchDirsOverride();

private:
    struct Job
    {
        juce::String link, outputFolder;
        DownloadCommand::Format format = DownloadCommand::Format::mp3;
        juce::StringArray searchDirs;
        juce::Time startTime;
        juce::String linkVideoId;   // from the link, may be empty
    };

    // Thread
    void run() override;
    // AsyncUpdater
    void handleAsyncUpdate() override;

    void publish(const std::function<void(Status&)>& mutate, bool finalState);
    void publishError(DownloadCommand::FailureKind kind, const juce::String& message,
                      const juce::String& detail = {});
    void finishCancelled(const juce::File& outDir, const juce::String& videoId);
    void signalChild(bool force);
    juce::File resolveFinalFile(const DownloadCommand::OutputParser& parser,
                                const juce::File& outDir) const;

    Job job;                                   // written by start() before the thread starts, then read-only
    mutable juce::CriticalSection statusLock;
    Status status;                             // guarded by statusLock
    double jobStartMs = 0.0;                   // guarded by statusLock

    juce::CriticalSection childLock;
    ProcessRunner* activeRunner = nullptr;     // guarded by childLock (worker registers its stack object)

    std::atomic<bool> busy { false };
    std::atomic<bool> cancelRequested { false };

    juce::ListenerList<Listener> listeners;    // message thread
    juce::uint32 serialCounter = 0;            // message thread
    juce::uint32 lastFinishedSerial = 0;       // message thread

    bool useSearchOverride = false;            // message thread
    juce::StringArray searchOverride;          // message thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DownloadManager)
};
