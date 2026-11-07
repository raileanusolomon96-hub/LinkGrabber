#pragma once
#include <juce_core/juce_core.h>

//==============================================================================
// Removes the leftovers of a job that did NOT finish (cancelled, destroyed mid-job, or
// failed). Pure file helper, safe on any thread. It is NEVER called for a job that
// finished `done`.
//
// Safety rules (architecture.md R6, plan Phase 2.3):
//  - only DIRECT children of the output folder are considered; symbolic links are skipped
//    (never followed, never deleted); nothing outside the folder is ever touched
//  - only files modified since the job started (minus 2 s of filesystem timestamp slack)
//  - when the video id is KNOWN, a file is eligible only if its name contains that id
//    (yt-dlp's template is "<title> [<id>].<ext>", so every temp file of the job has it)
//  - when the id is UNKNOWN, only yt-dlp's partial-file suffixes are eligible
//  - includeMedia (cancel only): also .mp3 .wav .webm .m4a .opus files that carry the id,
//    i.e. the half-written output or the downloaded source of the cancelled job
namespace JobCleanup
{
    struct Result
    {
        int deleted = 0;
        juce::StringArray names;   // file names only (for the log)
    };

    // yt-dlp partials: x.part, x.ytdl, x.temp, x.part-Frag12, x.temp.<media ext> (post-processor temp)
    inline bool isPartialName(const juce::String& name)
    {
        const auto n = name.toLowerCase();
        if (n.endsWith(".part") || n.endsWith(".ytdl") || n.endsWith(".temp"))
            return true;
        if (n.contains(".part-frag"))
            return true;
        for (auto* ext : { ".mp3", ".wav", ".webm", ".m4a", ".opus" })
            if (n.endsWith(juce::String(".temp") + ext))
                return true;
        return false;
    }

    inline bool isMediaName(const juce::String& name)
    {
        const auto n = name.toLowerCase();
        for (auto* ext : { ".mp3", ".wav", ".webm", ".m4a", ".opus" })
            if (n.endsWith(ext))
                return true;
        return false;
    }

    inline Result cleanup(const juce::File& outputFolder,
                          const juce::String& videoId,
                          juce::Time jobStart,
                          bool includeMedia)
    {
        Result result;
        if (! outputFolder.isDirectory())
            return result;

        const auto threshold = jobStart - juce::RelativeTime::seconds(2.0);
        const bool idKnown = videoId.isNotEmpty();

        for (const auto& f : outputFolder.findChildFiles(juce::File::findFiles, false))
        {
            if (f.getParentDirectory() != outputFolder)
                continue;
            if (f.isSymbolicLink())
                continue;

            const auto name = f.getFileName();
            const bool partial = isPartialName(name);
            const bool media = includeMedia && isMediaName(name);

            if (! partial && ! media)
                continue;
            if (idKnown && ! name.contains(videoId))
                continue;
            if (! idKnown && ! partial)
                continue;
            if (f.getLastModificationTime() < threshold)
                continue;

            if (f.deleteFile())
            {
                ++result.deleted;
                result.names.add(name);
            }
        }
        return result;
    }
}
