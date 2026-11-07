#include "DownloadManager.h"

using namespace DownloadCommand;

DownloadManager::DownloadManager()
    : juce::Thread("LinkGrabber download")
{
}

DownloadManager::~DownloadManager()
{
    // Destruction order (architecture.md): cancel the job, kill the child, join the worker,
    // cancel the pending async update. Listeners are released last (member destruction).
    cancelRequested.store(true);
    signalThreadShouldExit();
    killChild();
    stopThread(8000);
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
        publishError(FailureKind::invalidLink, userMessage(FailureKind::invalidLink));
        return false;
    }

    job.link = trimmed;
    job.outputFolder = outputFolder;
    job.format = format;
    job.searchDirs = useSearchOverride ? searchOverride : ToolLocator::getDefaultSearchDirs();
    job.startTime = juce::Time::getCurrentTime();

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
    killChild();
}

void DownloadManager::killChild()
{
    const juce::ScopedLock sl(childLock);
    if (activeChild != nullptr)
        activeChild->kill();
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
    publish([&](Status& s)
            {
                s.state = State::error;
                s.errorKind = kind;
                s.errorMessage = message;
                s.detail = detail;
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
    auto isDirectChild = [&outDir](const juce::File& f)
    {
        const auto parent = f.getParentDirectory();
        if (parent == outDir)
            return true;
        const auto idA = parent.getFileIdentifier();
        return idA != 0 && idA == outDir.getFileIdentifier();
    };

    // 1. The path yt-dlp printed (--print after_move:filepath), if it is an existing file in the output folder.
    if (parser.getFinalPath().isNotEmpty())
    {
        const juce::File f(parser.getFinalPath());
        if (f.existsAsFile() && isDirectChild(f))
            return f;
    }

    // 2. Fallback: newest .mp3/.wav (matching the requested format) modified since the job started.
    const auto ext = extensionFor(job.format);
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
    return best;
}

//==============================================================================
void DownloadManager::run()
{
    // Worker thread. Touches only: job (read-only), the snapshot (publish), the child.
    if (threadShouldExit() || cancelRequested.load())
    {
        publish([](Status& s) { s.state = State::idle; s.errorMessage = "Cancelled"; }, true);
        return;
    }

    const auto tools = ToolLocator::locate(job.searchDirs);
    if (! tools.hasRequired())
    {
        publishError(FailureKind::toolMissing, userMessage(FailureKind::toolMissing),
                     "Missing: " + tools.describeMissing());
        return;
    }

    const juce::File outDir(job.outputFolder);
    if (! outDir.createDirectory().wasOk() || ! outDir.isDirectory() || ! outDir.hasWriteAccess())
    {
        publishError(FailureKind::generic, "Cannot write to the output folder", job.outputFolder);
        return;
    }

    const auto argv = buildFullArgv(tools.childPath,
                                    tools.ytdlp.getFullPathName(),
                                    tools.ffmpeg.getParentDirectory().getFullPathName(),
                                    outDir.getFullPathName(),
                                    job.format,
                                    job.link);

    juce::ChildProcess child;
    if (! child.start(argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        publishError(FailureKind::toolMissing, userMessage(FailureKind::toolMissing), "Could not start /usr/bin/env");
        return;
    }

    {
        const juce::ScopedLock sl(childLock);
        activeChild = &child;
    }

    // cancel()/destructor may have fired before the child was registered.
    if (cancelRequested.load() || threadShouldExit())
        child.kill();

    OutputParser parser;
    int lastPercent = -1;
    bool lastConverting = false;
    juce::String lastId;
    char buffer[4096];

    for (;;)
    {
        // Blocks until data or EOF. Cancel works by kill(): the child dies, EOF follows.
        const int n = child.readProcessOutput(buffer, static_cast<int>(sizeof(buffer)));
        if (n <= 0)
            break;

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

        if (threadShouldExit() || cancelRequested.load())
            killChild();
    }
    parser.finish();

    if (! child.waitForProcessToFinish(15000))
    {
        child.kill();
        child.waitForProcessToFinish(2000);
    }

    {
        const juce::ScopedLock sl(childLock);
        activeChild = nullptr;
    }

    if (cancelRequested.load() || threadShouldExit())
    {
        publish([](Status& s) { s.state = State::idle; s.errorMessage = "Cancelled"; }, true);
        return;
    }

    const int exitCode = static_cast<int>(child.getExitCode());
    const auto tail = parser.getTail().joinIntoString("\n");

    if (exitCode == 0)
    {
        const auto file = resolveFinalFile(parser, outDir);
        if (file.existsAsFile())
        {
            juce::String title, id;
            splitTitleAndId(file, title, id);
            const auto videoId = id.isNotEmpty() ? id : parser.getVideoId();
            const auto path = file.getFullPathName();
            const auto bytes = file.getSize();

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

        publishError(FailureKind::generic, "Download finished but no file was found", tail);
        return;
    }

    auto kind = classifyFailure(parser.getTail());
    if (exitCode == 126 || exitCode == 127)
        kind = FailureKind::toolMissing;  // env could not execute the tool

    publishError(kind, userMessage(kind), "exit code " + juce::String(exitCode) + "\n" + tail);
}
