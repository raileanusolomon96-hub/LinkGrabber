#pragma once
#include <juce_core/juce_core.h>
#include <unistd.h>

//==============================================================================
// Finds yt-dlp, ffmpeg, ffprobe and deno without relying on the host's PATH
// (a DAW's PATH lacks /opt/homebrew/bin) and builds the explicit PATH handed to the
// child via `/usr/bin/env PATH=...`. Pure and thread-safe (no shared state).
struct ToolPaths
{
    juce::File ytdlp, ffmpeg, ffprobe, deno;   // File() when not found
    juce::String childPath;                    // explicit PATH for the child

    bool hasRequired() const { return ytdlp != juce::File() && ffmpeg != juce::File(); }

    juce::String describeMissing() const
    {
        juce::StringArray missing;
        if (ytdlp == juce::File())  missing.add("yt-dlp");
        if (ffmpeg == juce::File()) missing.add("ffmpeg");
        return missing.joinIntoString(", ");
    }
};

class ToolLocator
{
public:
    // Reserved for Phase 2.4 (managed tools). May not exist yet.
    static juce::File getManagedBinDirectory()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Application Support").getChildFile("LinkGrabber").getChildFile("bin");
       #else
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("LinkGrabber").getChildFile("bin");
       #endif
    }

    // Resolution order: managed copy first, then Homebrew (arm64 / Intel) and system folders.
    static juce::StringArray getDefaultSearchDirs()
    {
        return juce::StringArray {
            getManagedBinDirectory().getFullPathName(),
            "/opt/homebrew/bin",
            "/usr/local/bin",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/bin").getFullPathName(),
            "/usr/bin",
            "/opt/local/bin"
        };
    }

    // PATH given to the child: managed bin, Homebrew, /usr/local, system.
    static juce::String buildChildPath()
    {
        return getManagedBinDirectory().getFullPathName()
               + ":/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin";
    }

    static bool isExecutableFile(const juce::File& f)
    {
        return f.existsAsFile() && ::access(f.getFullPathName().toRawUTF8(), X_OK) == 0;
    }

    static juce::File findTool(const juce::String& name, const juce::StringArray& dirs)
    {
        for (const auto& d : dirs)
        {
            const auto candidate = juce::File(d).getChildFile(name);
            if (isExecutableFile(candidate))
                return candidate;
        }
        return {};
    }

    static ToolPaths locate(const juce::StringArray& dirs)
    {
        ToolPaths t;
        t.ytdlp   = findTool("yt-dlp", dirs);
        t.ffmpeg  = findTool("ffmpeg", dirs);
        t.ffprobe = findTool("ffprobe", dirs);
        t.deno    = findTool("deno", dirs);
        t.childPath = buildChildPath();
        return t;
    }

    static ToolPaths locate() { return locate(getDefaultSearchDirs()); }
};
