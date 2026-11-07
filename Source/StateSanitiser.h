#pragma once
#include <juce_core/juce_core.h>

// Defensive handling of restored session state (setStateInformation). Pure helpers,
// message thread. A host session file is untrusted input.
namespace LinkGrabberState
{
    inline constexpr int maxPathLength = 1024;
    inline constexpr int maxLinkLength = 2048;

    inline bool hasControlChars(const juce::String& s)
    {
        for (auto c : s)
            if (c < 32 || c == 127)
                return true;
        return false;
    }

    // Returns `path` (normalised) if it is a safe output folder, else defaultPath.
    // Rejected: empty, over-long, control characters, relative, any ".." segment,
    // filesystem root, an existing regular file, or a folder that does not exist and
    // cannot be created.
    inline juce::String sanitiseOutputFolder(const juce::String& path, const juce::String& defaultPath)
    {
        const auto p = path.trim();

        if (p.isEmpty() || p.length() > maxPathLength || hasControlChars(p))
            return defaultPath;
        if (! juce::File::isAbsolutePath(p))
            return defaultPath;

        for (const auto& segment : juce::StringArray::fromTokens(p, "/\\", ""))
            if (segment == "..")
                return defaultPath;

        const juce::File folder(p);
        if (folder.isRoot() || folder.getFullPathName().isEmpty())
            return defaultPath;
        if (folder.existsAsFile())
            return defaultPath;
        if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
            return defaultPath;

        return folder.getFullPathName();
    }

    // Strip control characters, trim, cap the length. No URL validation yet (done at
    // download time by the command helpers in a later phase).
    inline juce::String sanitiseLink(const juce::String& link)
    {
        juce::String out;
        for (auto c : link)
            if (c >= 32 && c != 127)
                out << juce::String::charToString(c);

        out = out.trim();
        if (out.length() > maxLinkLength)
            out = out.substring(0, maxLinkLength);
        return out;
    }
}
