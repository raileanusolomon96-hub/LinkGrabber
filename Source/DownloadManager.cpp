#include "DownloadManager.h"
#include "JobCleanup.h"
#include "LogFile.h"

using namespace DownloadCommand;

namespace
{
    constexpr int pollTimeoutMs = 100;          // how quickly the worker notices a cancel
    constexpr int killGraceMs = 500;            // SIGTERM -> SIGKILL
    constexpr int normalExitWaitMs = 15000;     // leader still alive after EOF
    constexpr int leaderGoneDrainMs = 2000;     // leader exited but a helper still holds the pipe

    juce::String jobTag(juce::uint32 serial) { return "job #" + juce::String(serial); }
}

DownloadManager::DownloadManager()
    : juce::Thread("LinkGrabber download")
{
}

DownloadManager::~DownloadManager()
{
    // Destruction order (architecture.md): cancel the job, kill the whole process group, join
    // the worker (bounded), cancel the pending async update. Listeners are released last.
    // The worker removes the partial files itself while it winds down.
    cancelRequested.store(true);
    signalThreadShouldExit();
    signalChild(false);                       // SIGTERM to the group, non-blocking

    if (! waitForThreadToExit(1500))
    {
        signalChild(true);                    // SIGKILL to the group
        if (! waitForThreadToExit(2500))
            stopThread(1000);                 // last resort; never expected to be reached
    }

    cancelPendingUpdate();
}

//==============================================================================
void DownloadManager::setToolSearchDirsOverride(const juce::StringArray& dirs)
{
    useSearchOverride = true;
    searchOverride = dirs;
}

void DownloadManager::clearToolSearchDirsOverride()
{
    useSearchOverride = false;
    searchOverride.clear();
}

DownloadManager::Status DownloadManager::getStatus() const
{
    const juce::ScopedLock sl(statusLock);
    auto s = status;
    if (s.isActive())
        s.elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - jobStartMs) / 1000.0;
    return s;
}

//==============================================================================
bool DownloadManager::start(const juce::String& link, int formatIndex, const juce::String& outputFolder)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (busy.load())
        return false;  // one job at a time

    // busy is cleared as the worker's last publish; make sure the thread really left run().
    if (isThreadRunning() && ! waitForThreadToExit(2000))
        return false;

    const auto trimmed = link.trim();
    const auto format = formatFromIndex(formatIndex);

    ++serialCounter;
    {
        const juce::ScopedLock sl(statusLock);
        status = Status();
        status.jobSerial = serialCounter;
        status.format = formatName(format);
        jobStartMs = juce::Time::getMillisecondCounterHiRes();
    }

    // Validation is pure and cheap: an invalid link never spawns anything (not even the worker).
    if (! isValidLink(trimmed))
    {
        publishError(FailureKind::invalidLink, userMessage(FailureKind::invalidLink),
                     "rejected link: " + trimmed.substring(0, 200));
        return false;
    }

    job.link = trimmed;
    job.outputFolder = outputFolder;
    job.format = format;
    job.searchDirs = useSearchOverride ? searchOverride : ToolLocator::getDefaultSearchDirs();
    job.startTime = juce::Time::getCurrentTime();
    job.linkVideoId = extractVideoIdFromLink(trimmed);

    LogFile::append(jobTag(serialCounter) + " start format=" + formatName(format)
                    + " id=" + (job.linkVideoId.isNotEmpty() ? job.linkVideoId : juce::String("?"))
                    + " link=" + trimmed.substring(0, 300));

    cancelRequested.store(false);
    busy.store(true);

    publish([](Status& s) { s.state = State::lookingUp; }, false);
    startThread();
    return true;
}

void DownloadManager::cancel()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! busy.load())
        return;
    cancelRequested.store(true);
    signalChild(false);   // immediate SIGTERM to the group; the worker escalates and cleans up
}

void DownloadManager::signalChild(bool force)
{
    const juce::ScopedLock sl(childLock);
    if (activeRunner != nullptr)
    {
        if (force)
            activeRunner->forceKill();
        else
            activeRunner->requestTerminate();
    }
}

//==============================================================================
void DownloadManager::publish(const std::function<void(Status&)>& mutate, bool finalState)
{
    {
        const juce::ScopedLock sl(statusLock);
        mutate(status);
        if (finalState)
            status.elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - jobStartMs) / 1000.0;
    }
    if (finalState)
        busy.store(false);
    triggerAsyncUpdate();
}

void DownloadManager::publishError(FailureKind kind, const juce::String& message, const juce::String& detail)
{
    // Diagnostics go to the log (links + tool output tail + classification; never the environment).
    juce::StringArray body;
    if (detail.isNotEmpty())
        body = juce::StringArray::fromLines(detail);
    LogFile::appendBlock(jobTag(getStatus().jobSerial) + " error kind=" + kindName(kind)
                             + " message=\"" + message + "\"",
                         body);

    publish([&](Status& s)
            {
                s.state = State::error;
                s.errorKind = kind;
                s.errorMessage = message;
                s.detail = detail;
            },
            true);
}

void DownloadManager::finishCancelled(const juce::File& outDir, const juce::String& videoId)
{
    // Cancelled or destroyed mid-job: the job did not finish, so its leftovers go.
    const auto removed = JobCleanup::cleanup(outDir, videoId, job.startTime, true);

    LogFile::appendBlock(jobTag(getStatus().jobSerial) + " cancelled, removed "
                             + juce::String(removed.deleted) + " partial file(s)",
                         removed.names);

    publish([](Status& s)
            {
                s.state = State::idle;
                s.errorKind = FailureKind::none;
                s.errorMessage = cancelledMessage();
            },
            true);
}

void DownloadManager::handleAsyncUpdate()
{
    const auto s = getStatus();

    listeners.call([&s](Listener& l) { l.downloadStatusChanged(s); });

    if (s.isFinished() && s.jobSerial != lastFinishedSerial)
    {
        lastFinishedSerial = s.jobSerial;
        listeners.call([&s](Listener& l) { l.downloadFinished(s); });
    }
}

//==============================================================================
juce::File DownloadManager::resolveFinalFile(const OutputParser& parser, const juce::File& outDir) const
{
    const auto ext = extensionFor(job.format);

    auto isDirectChild = [&outDir](const juce::File& f)
    {
        const auto parent = f.getParentDirectory();
        if (parent == outDir)
            return true;
        const auto idA = parent.getFileIdentifier();
        return idA != 0 && idA == outDir.getFileIdentifier();
    };

    auto acceptable = [&](const juce::File& f)
    {
        return f.existsAsFile() && isDirectChild(f) && f.getFileName().endsWithIgnoreCase(ext);
    };

    // 1. The path yt-dlp printed (--print after_move:filepath). On a re-download yt-dlp can print
    //    the source file (.webm) instead; its sibling with the requested extension is the result.
    if (parser.getFinalPath().isNotEmpty())
    {
        const juce::File f(parser.getFinalPath());
        if (acceptable(f))
            return f;
        if (f.existsAsFile() || f.getParentDirectory() == outDir)
        {
            const auto sibling = f.withFileExtension(ext);
            if (acceptable(sibling))
                return sibling;
        }
    }

    // 2. Re-download with the target already present: "Post-process file <abs> exists, skipping".
    if (parser.getPostProcessExistingPath().isNotEmpty())
    {
        const juce::File f(parser.getPostProcessExistingPath());
        if (acceptable(f))
            return f;
    }

    // 3. "[download] <abs> has already been downloaded" -> sibling with the requested extension.
    if (parser.getAlreadyDownloadedPath().isNotEmpty())
    {
        const auto sibling = juce::File(parser.getAlreadyDownloadedPath()).withFileExtension(ext);
        if (acceptable(sibling))
            return sibling;
    }

    // 4. Fallback: newest file of the requested format modified since the job started.
    const auto threshold = job.startTime - juce::RelativeTime::seconds(2.0);

    juce::File best;
    juce::Time bestTime;
    for (const auto& f : outDir.findChildFiles(juce::File::findFiles, false, "*" + ext))
    {
        const auto t = f.getLastModificationTime();
        if (t >= threshold && (best == juce::File() || t > bestTime))
        {
            best = f;
            bestTime = t;
        }
    }
    if (best != juce::File())
        return best;

    // 5. Last resort for a pure re-download (nothing new written): the file of this video id.
    const auto id = parser.getVideoId().isNotEmpty() ? parser.getVideoId() : job.linkVideoId;
    if (id.isNotEmpty())
    {
        for (const auto& f : outDir.findChildFiles(juce::File::findFiles, false, "*[" + id + "]*" + ext))
        {
            const auto t = f.getLastModificationTime();
            if (best == juce::File() || t > bestTime)
            {
                best = f;
                bestTime = t;
            }
        }
    }
    return best;
}

//==============================================================================
void DownloadManager::run()
{
    // Worker thread. Touches only: job (read-only), the snapshot (publish), the log, the child.
    const juce::File outDir(job.outputFolder);

    if (threadShouldExit() || cancelRequested.load())
    {
        finishCancelled(outDir, job.linkVideoId);
        return;
    }

    const auto tools = ToolLocator::locate(job.searchDirs);
    if (! tools.hasRequired())
    {
        publishError(FailureKind::toolMissing, userMessage(FailureKind::toolMissing),
                     "Missing: " + tools.describeMissing());
        return;
    }

    if (! outDir.createDirectory().wasOk() || ! outDir.isDirectory() || ! outDir.hasWriteAccess())
    {
        publishError(FailureKind::cannotWrite, userMessage(FailureKind::cannotWrite),
                     "output folder not writable: " + job.outputFolder);
        return;
    }

    const auto argv = buildFullArgv(tools.childPath,
                                    tools.ytdlp.getFullPathName(),
                                    tools.ffmpeg.getParentDirectory().getFullPathName(),
                                    outDir.getFullPathName(),
                                    job.format,
                                    job.link);

    ProcessRunner runner;
    if (! runner.start(argv))
    {
        publishError(FailureKind::toolMissing, userMessage(FailureKind::toolMissing), "Could not start /usr/bin/env");
        return;
    }

    {
        const juce::ScopedLock sl(childLock);
        activeRunner = &runner;
    }

    // cancel()/destructor may have fired before the runner was registered.
    if (cancelRequested.load() || threadShouldExit())
        runner.requestTerminate();

    OutputParser parser;
    int lastPercent = -1;
    bool lastConverting = false;
    juce::String lastId;
    char buffer[4096];
    double leaderGoneSince = 0.0;

    for (;;)
    {
        // Returns after at most pollTimeoutMs, so a cancel is noticed quickly even when yt-dlp is silent.
        const int n = runner.read(buffer, static_cast<int>(sizeof(buffer)), pollTimeoutMs);

        if (n < 0)
            break;   // EOF: every writer closed

        if (n > 0)
        {
            parser.feed(buffer, static_cast<size_t>(n));

            const int percent = parser.getPercent();
            const bool converting = parser.isConverting();
            const auto id = parser.getVideoId();

            if (percent != lastPercent || converting != lastConverting || id != lastId)
            {
                lastPercent = percent;
                lastConverting = converting;
                lastId = id;
                publish([percent, converting, id](Status& s)
                        {
                            s.percent = juce::jmax(s.percent, percent);
                            s.state = converting ? State::converting : (s.percent > 0 ? State::downloading : State::lookingUp);
                            s.convertingSeen = s.convertingSeen || converting;
                            if (id.isNotEmpty())
                                s.videoId = id;
                        },
                        false);
            }
        }
        else if (runner.hasLeaderExited())
        {
            // yt-dlp is gone but something it started still holds the pipe open: give the
            // remaining output a moment, then stop waiting (finish() kills the leftovers).
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (leaderGoneSince == 0.0)
                leaderGoneSince = now;
            else if (now - leaderGoneSince > leaderGoneDrainMs)
                break;
        }

        if (threadShouldExit() || cancelRequested.load())
            break;
    }
    parser.finish();

    const bool wasCancelled = cancelRequested.load() || threadShouldExit();
    const int exitCode = wasCancelled ? runner.killProcessGroup(killGraceMs)
                                      : runner.finish(normalExitWaitMs);

    {
        const juce::ScopedLock sl(childLock);
        activeRunner = nullptr;   // the runner is about to go out of scope
    }

    const auto knownId = parser.getVideoId().isNotEmpty() ? parser.getVideoId() : job.linkVideoId;

    if (wasCancelled || cancelRequested.load() || threadShouldExit())
    {
        finishCancelled(outDir, knownId);
        return;
    }

    const auto tail = parser.getTail().joinIntoString("\n");

    if (exitCode == 0 || exitCode == ProcessRunner::unknownExitCode)
    {
        const auto file = resolveFinalFile(parser, outDir);
        if (file.existsAsFile())
        {
            juce::String title, id;
            splitTitleAndId(file, title, id);
            const auto videoId = id.isNotEmpty() ? id : parser.getVideoId();
            const auto path = file.getFullPathName();
            const auto bytes = file.getSize();

            LogFile::append(jobTag(getStatus().jobSerial) + " done file=\"" + file.getFileName()
                            + "\" bytes=" + juce::String(bytes));

            publish([&](Status& s)
                    {
                        s.state = State::done;
                        s.percent = 100;
                        s.filePath = path;
                        s.title = title;
                        s.videoId = videoId;
                        s.bytes = bytes;
                    },
                    true);
            return;
        }

        // yt-dlp exits 0 without a file when --match-filter rejected the video.
        if (looksLikeFilterSkip(parser.getTail()))
        {
            publishError(FailureKind::tooLong, userMessage(FailureKind::tooLong), tail);
            return;
        }

        publishError(FailureKind::generic, userMessage(FailureKind::generic),
                     "finished without a file\n" + tail);
        return;
    }

    // Failed: remove yt-dlp's partial files of this video (never finished media files).
    const auto removed = JobCleanup::cleanup(outDir, knownId, job.startTime, false);

    auto kind = classifyFailure(parser.getTail());
    if (exitCode == 126 || exitCode == 127)
        kind = FailureKind::toolMissing;  // env could not execute the tool

    auto detail = "exit code " + juce::String(exitCode);
    if (removed.deleted > 0)
        detail << "\nremoved " << juce::String(removed.deleted) << " partial file(s)";
    detail << "\n" << tail;

    publishError(kind, userMessage(kind), detail);
}
