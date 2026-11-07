#pragma once
#include <juce_core/juce_core.h>

//==============================================================================
// Diagnostics log: ~/Library/Logs/LinkGrabber.log (architecture.md "Failure mapping").
//
//  - 512 KB cap. When an append would exceed the cap, the OLDEST part is dropped (the newest
//    ~half of the cap is kept, cut at a line boundary) and the append continues, so the file
//    never exceeds maxBytes.
//  - Thread-safe: every call takes one process-wide lock (rates are tiny: a few lines per job).
//    Safe from the message thread and the download worker; NEVER call from the audio thread.
//  - Failures to open/write are ignored silently (the log is best effort).
//  - Content policy: links, state transitions, classification and the tail of the tool output
//    only. NEVER environment variables, never the child's argv (it carries PATH=...).
//  - Test seam: setBaseDirOverride(dir) redirects the file to <dir>/LinkGrabber.log.
namespace LogFile
{
    inline constexpr juce::int64 maxBytes = 512 * 1024;
    inline constexpr int maxLineChars = 4000;

    namespace detail
    {
        inline juce::CriticalSection& getLock()
        {
            static juce::CriticalSection lock;
            return lock;
        }

        inline juce::String& getOverridePath()
        {
            static juce::String path;   // guarded by getLock()
            return path;
        }

        inline juce::File resolveFileUnlocked()
        {
            const auto& o = getOverridePath();
            if (o.isNotEmpty())
                return juce::File(o).getChildFile("LinkGrabber.log");
            return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library").getChildFile("Logs").getChildFile("LinkGrabber.log");
        }

        // Keep roughly the newest `keepBytes`, starting at a line boundary.
        inline void trimToNewest(const juce::File& file, juce::int64 keepBytes)
        {
            const auto size = file.getSize();
            if (size <= keepBytes)
                return;

            juce::MemoryBlock block;
            {
                juce::FileInputStream in(file);   // closed at the end of this scope
                if (! in.openedOk())
                    return;

                in.setPosition(size - keepBytes);
                in.readIntoMemoryBlock(block, static_cast<ssize_t>(keepBytes));
            }

            const auto* bytes = static_cast<const char*>(block.getData());
            size_t start = 0;
            while (start < block.getSize() && bytes[start] != '\n')
                ++start;
            if (start < block.getSize())
                ++start;   // drop the (possibly partial) first line including its newline

            file.replaceWithData(bytes + start, block.getSize() - start);
        }

        inline juce::String clean(const juce::String& text)
        {
            juce::String out;
            for (auto c : text)
            {
                if (c == '\t')
                    out << ' ';
                else if (c >= 32 && c != 127)
                    out << juce::String::charToString(c);
            }
            if (out.length() > maxLineChars)
                out = out.substring(0, maxLineChars) + "...";
            return out;
        }
    }

    // Test seam. An empty File restores the default location.
    inline void setBaseDirOverride(const juce::File& dir)
    {
        const juce::ScopedLock sl(detail::getLock());
        detail::getOverridePath() = dir == juce::File() ? juce::String() : dir.getFullPathName();
    }

    inline juce::File getLogFile()
    {
        const juce::ScopedLock sl(detail::getLock());
        return detail::resolveFileUnlocked();
    }

    // Appends the lines (each prefixed with `prefix`, the first with a timestamp) as ONE write.
    inline void appendLines(const juce::StringArray& lines, const juce::String& prefix = {})
    {
        if (lines.isEmpty())
            return;

        juce::String text;
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S");
        bool first = true;
        for (const auto& l : lines)
        {
            if (first)
                text << "[" << stamp << "] " << detail::clean(l) << "\n";
            else
                text << prefix << detail::clean(l) << "\n";
            first = false;
        }

        const auto utf8Size = static_cast<juce::int64>(text.getNumBytesAsUTF8());

        const juce::ScopedLock sl(detail::getLock());
        const auto file = detail::resolveFileUnlocked();
        if (! file.getParentDirectory().createDirectory().wasOk())
            return;

        if (utf8Size >= maxBytes / 2)
            return;   // a single block that large is never legitimate; drop it

        if (file.getSize() + utf8Size > maxBytes)
            detail::trimToNewest(file, maxBytes / 2);

        juce::FileOutputStream out(file);
        if (! out.openedOk())
            return;   // ignored silently
        out.setPosition(out.getFile().getSize());
        out.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
        out.flush();
    }

    inline void append(const juce::String& line)
    {
        appendLines(juce::StringArray(line));
    }

    // First line timestamped, the continuation lines indented with "  | ".
    inline void appendBlock(const juce::String& header, const juce::StringArray& body)
    {
        juce::StringArray all;
        all.add(header);
        all.addArray(body);
        appendLines(all, "    | ");
    }
}
