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
    enum class FailureKind { none, invalidLink, unavailable, network, outdated, toolMissing, generic };

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
        }
        return "generic";
    }

    // User-facing messages (architecture.md "Failure mapping"). The UI never shows raw tool output.
    inline juce::String userMessage(FailureKind k)
    {
        switch (k)
        {
            case FailureKind::none:        return {};
            case FailureKind::invalidLink: return "Invalid link: paste a YouTube URL";
            case FailureKind::unavailable: return "Video unavailable, private or age-restricted";
            case FailureKind::network:     return "Network error: check your connection";
            case FailureKind::outdated:    return "YouTube changed: update yt-dlp";
            case FailureKind::toolMissing: return "yt-dlp or ffmpeg not found";
            case FailureKind::generic:     return "Download failed (see log)";
        }
        return "Download failed (see log)";
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

        if (format == Format::wav)
            a.addArray(juce::StringArray { "--audio-format", "wav",
                                           "--postprocessor-args", "ffmpeg:-c:a pcm_s24le" });
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

    private:
        std::string pending;
        int percent = 0;
        bool converting = false;
        juce::String finalPath, videoId;
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
    inline FailureKind classifyFailure(const juce::StringArray& tail)
    {
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

        if (any({ "private video", "video unavailable", "video is unavailable", "video is not available",
                  "members-only", "members only", "join this channel", "not available in your country",
                  "age-restricted", "age restricted", "confirm your age", "has been removed",
                  "removed by the uploader", "account associated with this video has been terminated" }))
            return FailureKind::unavailable;

        if (any({ "not a bot", "http error 403", "unable to extract", " nsig", "n challenge",
                  "no supported javascript runtime", "requested format is not available" }))
            return FailureKind::outdated;

        if (any({ "getaddrinfo", "temporary failure", "network is unreachable", "timed out",
                  "unable to download webpage", "nodename nor servname", "name or service not known",
                  "connection" }))
            return FailureKind::network;

        return FailureKind::generic;
    }
}
