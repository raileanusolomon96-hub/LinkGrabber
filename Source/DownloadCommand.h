#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <string>

//==============================================================================
// Pure, UI-free helpers for the download pipeline (architecture.md, Core
// Component 5). Nothing here spawns a process, touches the UI, the state tree or
// the history. Safe to call from any thread.
//
//  - isValidLink          : strict https + host allow-list check
//  - buildYtDlpArgs/Argv  : argv (never a shell string); the link is ONE element after "--"
//  - OutputParser         : incremental parser for the merged stdout/stderr stream
//  - classifyFailure      : maps tool output to a FailureKind
namespace DownloadCommand
{
    //==========================================================================
    enum class Format { mp3 = 0, wav = 1 };

    inline Format formatFromIndex(int index) { return index == 1 ? Format::wav : Format::mp3; }
    inline juce::String formatName(Format f) { return f == Format::wav ? "wav" : "mp3"; }
    inline juce::String extensionFor(Format f) { return f == Format::wav ? ".wav" : ".mp3"; }

    //==========================================================================
    enum class FailureKind { none, invalidLink, unavailable, network, outdated, toolMissing, generic,
                             tooLong, cannotWrite };

    inline const char* kindName(FailureKind k)
    {
        switch (k)
        {
            case FailureKind::none:        return "none";
            case FailureKind::invalidLink: return "invalidLink";
            case FailureKind::unavailable: return "unavailable";
            case FailureKind::network:     return "network";
            case FailureKind::outdated:    return "outdated";
            case FailureKind::toolMissing: return "toolMissing";
            case FailureKind::generic:     return "generic";
            case FailureKind::tooLong:     return "tooLong";
            case FailureKind::cannotWrite: return "cannotWrite";
        }
        return "generic";
    }

    // Duration guard (--match-filter): videos longer than this are skipped by yt-dlp.
    inline constexpr int maxDurationSeconds = 3600;

    // Shown when the user cancels (state idle, errorKind none) or the plugin is destroyed mid-job.
    inline juce::String cancelledMessage() { return "Cancelled"; }

    // User-facing messages (architecture.md "Failure mapping"). Short, plain language, each says
    // what to do next. The UI never shows raw tool output; details go to LinkGrabber.log.
    inline juce::String userMessage(FailureKind k)
    {
        switch (k)
        {
            case FailureKind::none:        return {};
            case FailureKind::invalidLink: return "Invalid link: paste a YouTube video URL";
            case FailureKind::unavailable: return "Video unavailable: private, removed, region-locked or age-restricted";
            case FailureKind::network:     return "Network error: check your internet connection and try again";
            case FailureKind::outdated:    return "YouTube changed: update yt-dlp and try again";
            case FailureKind::toolMissing: return "yt-dlp or ffmpeg not found: install them first";
            case FailureKind::generic:     return "Download failed: see LinkGrabber.log in Library/Logs";
            case FailureKind::tooLong:     return "Video is longer than 1 hour.";
            case FailureKind::cannotWrite: return "Cannot write to the output folder: check free space and permissions";
        }
        return "Download failed: see LinkGrabber.log in Library/Logs";
    }

    //==========================================================================
    // Link validation
    inline constexpr int maxLinkLength = 2048;

    // https only; host (exact, case-insensitive) must be on the allow-list; no '@', ':' or '%'
    // in the authority; no whitespace/control/non-ASCII/backslash anywhere; no leading '-';
    // length <= 2048. The host is compared for equality, never with contains().
    inline bool isValidLink(const juce::String& link)
    {
        if (link.isEmpty() || link.length() > maxLinkLength)
            return false;
        if (link.startsWithChar('-'))
            return false;

        for (auto c : link)
            if (c <= 32 || c >= 127 || c == '\\')
                return false;

        if (! link.startsWithIgnoreCase("https://"))
            return false;

        const auto rest = link.substring(8);
        const int authorityEnd = rest.indexOfAnyOf("/?#");
        const auto authority = authorityEnd < 0 ? rest : rest.substring(0, authorityEnd);

        if (authority.isEmpty() || authority.containsAnyOf("@:%"))
            return false;

        const auto host = authority.toLowerCase();

        // Second opinion from the JUCE URL parser: it must see the very same host.
        if (juce::URL(link).getDomain().toLowerCase() != host)
            return false;

        static const char* const allowed[] = { "youtube.com", "www.youtube.com", "m.youtube.com",
                                               "music.youtube.com", "youtu.be" };
        for (auto* a : allowed)
            if (host == a)
                return true;

        return false;
    }

    //==========================================================================
    // Argv builder. The first element is the yt-dlp executable.
    inline juce::StringArray buildYtDlpArgs(const juce::String& ytdlpPath,
                                            const juce::String& ffmpegDir,
                                            const juce::String& outputFolder,
                                            Format format,
                                            const juce::String& link)
    {
        juce::StringArray a;
        a.add(ytdlpPath);
        a.addArray(juce::StringArray { "--ignore-config", "--no-playlist", "--no-colors", "--newline",
                                       "--progress",
                                       "--progress-template", "download:LGP %(progress._percent_str)s",
                                       "--print", "after_move:filepath",
                                       "--ffmpeg-location", ffmpegDir,
                                       "--paths", outputFolder,
                                       "--output", "%(title).140B [%(id)s].%(ext)s",
                                       "-x" });

        // Duration guard (Phase 2.3): a missing duration (live stream) also fails the filter,
        // which is intended. Skipped videos exit 0 without a file -> FailureKind::tooLong.
        a.add("--match-filter");
        a.add("duration <= " + juce::String(maxDurationSeconds));

        if (format == Format::wav)
            a.addArray(juce::StringArray { "--audio-format", "wav",
                                           "--postprocessor-args", "ffmpeg:-c:a pcm_s24le -ac 2" });
        else
            a.addArray(juce::StringArray { "--audio-format", "mp3", "--audio-quality", "320K" });

        a.add("--");
        a.add(link);
        return a;
    }

    // `/usr/bin/env PATH=<childPath> <yt-dlp> ...` because juce::ChildProcess has no environment parameter.
    inline juce::StringArray buildFullArgv(const juce::String& childPath,
                                           const juce::String& ytdlpPath,
                                           const juce::String& ffmpegDir,
                                           const juce::String& outputFolder,
                                           Format format,
                                           const juce::String& link)
    {
        juce::StringArray a;
        a.add("/usr/bin/env");
        a.add("PATH=" + childPath);
        a.addArray(buildYtDlpArgs(ytdlpPath, ffmpegDir, outputFolder, format, link));
        return a;
    }

    //==========================================================================
    inline bool isPlausibleVideoId(const juce::String& s)
    {
        if (s.length() != 11)
            return false;
        for (auto c : s)
            if (! (juce::CharacterFunctions::isLetterOrDigit(c) && c < 128) && c != '-' && c != '_')
                return false;
        return true;
    }

    // Best-effort video id from an (already validated) link: youtu.be/<id>, /watch?v=<id>,
    // /shorts/<id>, /live/<id>, /embed/<id>, /v/<id>. Empty if it cannot be determined.
    // Used to scope cancel cleanup to files of THIS job before yt-dlp has printed the id.
    inline juce::String extractVideoIdFromLink(const juce::String& link)
    {
        const auto afterScheme = link.fromFirstOccurrenceOf("://", false, false);
        const int hostEnd = afterScheme.indexOfAnyOf("/?#");
        const auto host = (hostEnd < 0 ? afterScheme : afterScheme.substring(0, hostEnd)).toLowerCase();
        if (hostEnd < 0)
            return {};

        auto remainder = afterScheme.substring(hostEnd).upToFirstOccurrenceOf("#", false, false);
        const auto pathPart = remainder.upToFirstOccurrenceOf("?", false, false);
        const auto query = remainder.containsChar('?') ? remainder.fromFirstOccurrenceOf("?", false, false)
                                                       : juce::String();
        auto segments = juce::StringArray::fromTokens(pathPart, "/", "");
        segments.removeEmptyStrings();

        juce::String candidate;
        if (host == "youtu.be")
        {
            if (segments.size() >= 1)
                candidate = segments[0];
        }
        else if (segments.size() >= 2 && (segments[0] == "shorts" || segments[0] == "live"
                                          || segments[0] == "embed" || segments[0] == "v"))
        {
            candidate = segments[1];
        }
        else if (segments.size() >= 1 && segments[0] == "watch")
        {
            for (const auto& pair : juce::StringArray::fromTokens(query, "&", ""))
                if (pair.startsWith("v="))
                    candidate = pair.substring(2);
        }

        return isPlausibleVideoId(candidate) ? candidate : juce::String();
    }

    // "<title> [<id>].<ext>" -> title + id (id empty if the pattern is absent).
    inline void splitTitleAndId(const juce::File& file, juce::String& title, juce::String& id)
    {
        const auto stem = file.getFileNameWithoutExtension();
        title = stem;
        id = {};

        if (stem.endsWithChar(']'))
        {
            const int open = stem.lastIndexOfChar('[');
            if (open > 0)
            {
                const auto candidate = stem.substring(open + 1, stem.length() - 1);
                if (isPlausibleVideoId(candidate))
                {
                    id = candidate;
                    title = stem.substring(0, open).trim();
                }
            }
        }
    }

    //==========================================================================
    // Incremental parser for the merged stdout/stderr stream of the child.
    //  - handles arbitrary chunking (partial lines, split UTF-8)
    //  - "LGP <pct>%"   -> monotonic non-decreasing integer percent (floor), clamped 0..100
    //  - converting     -> percent reached 100, or an ffmpeg/post-processor banner line
    //  - final path     -> last absolute-path line (from --print after_move:filepath)
    //  - tail           -> last lines (LGP progress lines excluded) for the classifier
    class OutputParser
    {
    public:
        static constexpr int maxTailLines = 25;
        static constexpr size_t maxLineBytes = 8192;

        void feed(const void* data, size_t numBytes)
        {
            const auto* p = static_cast<const char*>(data);
            for (size_t i = 0; i < numBytes; ++i)
            {
                const char c = p[i];
                if (c == '\n' || c == '\r')
                {
                    flushPending();
                }
                else if (pending.size() < maxLineBytes)
                {
                    pending.push_back(c);
                }
            }
        }

        void feed(const juce::String& text) { feed(text.toRawUTF8(), text.getNumBytesAsUTF8()); }

        // Call at EOF: handles a last line without a newline.
        void finish() { flushPending(); }

        int getPercent() const noexcept             { return percent; }
        bool isConverting() const noexcept          { return converting; }
        const juce::String& getFinalPath() const    { return finalPath; }
        const juce::String& getVideoId() const      { return videoId; }
        const juce::StringArray& getTail() const    { return tail; }

        // Re-download case (the file already exists):
        //   "[ExtractAudio] Post-process file <abs> exists, skipping"  -> the finished target
        //   "[download] <abs> has already been downloaded"             -> the source download
        // yt-dlp may then print no after_move path line (or a stale source path), so the
        // manager uses these to resolve the existing file.
        const juce::String& getPostProcessExistingPath() const { return postProcessExisting; }
        const juce::String& getAlreadyDownloadedPath() const   { return alreadyDownloaded; }

    private:
        std::string pending;
        int percent = 0;
        bool converting = false;
        juce::String finalPath, videoId, postProcessExisting, alreadyDownloaded;
        juce::StringArray tail;

        void flushPending()
        {
            if (pending.empty())
                return;
            handleLine(juce::String::fromUTF8(pending.data(), static_cast<int>(pending.size())).trim());
            pending.clear();
        }

        void handleLine(const juce::String& line)
        {
            if (line.isEmpty())
                return;

            if (line.startsWith("LGP"))
            {
                const auto body = line.substring(3).trim();
                if (body.containsChar('%') && body.containsAnyOf("0123456789"))
                {
                    const double value = body.getDoubleValue();
                    const int p = juce::jlimit(0, 100, static_cast<int>(std::floor(value)));
                    percent = juce::jmax(percent, p);  // monotonic across streams/fragments
                    if (percent >= 100)
                        converting = true;
                }
                return;  // progress lines never enter the tail
            }

            tail.add(line);
            while (tail.size() > maxTailLines)
                tail.remove(0);

            {
                static const juce::String ppPrefix("Post-process file ");
                static const juce::String ppSuffix(" exists, skipping");
                static const juce::String dlSuffix(" has already been downloaded");

                const int a = line.indexOf(ppPrefix);
                const int b = line.lastIndexOf(ppSuffix);
                if (a >= 0 && b > a)
                {
                    const auto p = line.substring(a + ppPrefix.length(), b).trim();
                    if (juce::File::isAbsolutePath(p))
                        postProcessExisting = p;
                }

                if (line.startsWith("[download] ") && line.endsWith(dlSuffix))
                {
                    const auto p = line.substring(11, line.length() - dlSuffix.length()).trim();
                    if (juce::File::isAbsolutePath(p))
                        alreadyDownloaded = p;
                }
            }

            if (line.startsWith("[ExtractAudio]") || line.startsWith("[Merger]")
                || line.startsWith("[VideoConvertor]") || line.startsWith("[ffmpeg]")
                || line.startsWith("[Fixup"))
            {
                converting = true;
            }
            else if (line.startsWith("[youtube] "))
            {
                const auto rest = line.substring(10);
                const int colon = rest.indexOfChar(':');
                if (colon > 0 && videoId.isEmpty())
                {
                    const auto candidate = rest.substring(0, colon);
                    if (isPlausibleVideoId(candidate))
                        videoId = candidate;
                }
            }
            else if (line.startsWithChar('/') && juce::File::isAbsolutePath(line))
            {
                finalPath = line;  // the last such line wins
            }
        }
    };

    //==========================================================================
    // Failure classifier. `tail` = last output lines. ERROR: lines are preferred over the
    // whole tail so a harmless WARNING cannot decide the outcome.
    // True if yt-dlp skipped the video because of --match-filter ("... does not pass filter
    // (duration <= 3600), skipping .."). That run exits 0 and produces no file.
    inline bool looksLikeFilterSkip(const juce::StringArray& tail)
    {
        for (const auto& l : tail)
            if (l.containsIgnoreCase("does not pass filter"))
                return true;
        return false;
    }

    inline FailureKind classifyFailure(const juce::StringArray& tail)
    {
        if (looksLikeFilterSkip(tail))
            return FailureKind::tooLong;

        juce::String text;
        for (const auto& l : tail)
            if (l.startsWithIgnoreCase("ERROR:"))
                text << l.toLowerCase() << "\n";

        if (text.isEmpty())
            for (const auto& l : tail)
                text << l.toLowerCase() << "\n";

        auto any = [&text](std::initializer_list<const char*> needles)
        {
            for (auto* n : needles)
                if (text.contains(n))
                    return true;
            return false;
        };

        if (any({ "unsupported url", "is not a valid url", "incomplete youtube id", "invalid url" }))
            return FailureKind::invalidLink;

        for (const auto& l : tail)
        {
            const auto lower = l.toLowerCase();
            if (lower.startsWith("env:") || lower.contains("command not found"))
                return FailureKind::toolMissing;
        }

        // Local write problems first: "permission denied" must not fall into the generic bucket.
        if (any({ "no space left on device", "disk full", "not enough space", "read-only file system",
                  "permission denied", "operation not permitted", "errno 28", "errno 13", "errno 30" }))
            return FailureKind::cannotWrite;

        if (any({ "private video", "this video is private", "video unavailable", "video is unavailable",
                  "video is not available", "this video is not available", "no longer available",
                  "members-only", "members only", "join this channel", "not available in your country",
                  "not made this video available", "blocked it", "age-restricted", "age restricted",
                  "confirm your age", "has been removed", "removed by the uploader",
                  "account associated with this video has been terminated", "live event will begin",
                  "premieres in", "copyright grounds" }))
            return FailureKind::unavailable;

        if (any({ "sign in to confirm", "not a bot", "http error 403", "unable to extract", "failed to extract",
                  " nsig", "n challenge", "challenge solving failed", "signature extraction",
                  "no supported javascript runtime", "requested format is not available",
                  "no video formats found", "po token", "po_token" }))
            return FailureKind::outdated;

        if (any({ "getaddrinfo", "temporary failure", "network is unreachable", "timed out",
                  "unable to download webpage", "nodename nor servname", "name or service not known",
                  "failed to resolve", "no route to host", "urlopen error", "offline", "connection" }))
            return FailureKind::network;

        return FailureKind::generic;
    }
}
