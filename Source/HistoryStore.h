#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>

//==============================================================================
// One row of the global download history.
struct HistoryEntry
{
    juce::String id;         // unique per download (e.g. video id + job time)
    juce::String title;
    juce::String path;       // absolute path of the downloaded file
    juce::String format;     // "mp3" | "wav"
    juce::String timestamp;  // ISO 8601
    juce::int64 bytes = 0;
    juce::String key;        // "A min" / "C# maj"; empty = unknown (see `analysed`)
    int bpm = 0;             // 0 = unknown
    bool analysed = false;   // false: entry predates Key/BPM (shown blank); true + empty = "--"
};

//==============================================================================
// Global, UI-free download history (architecture.md, Core Component 6, R7).
//
//  - File: <baseDir>/history.json, default baseDir =
//    ~/Library/Application Support/LinkGrabber
//  - Schema: { "version":1, "entries":[ {id,title,path,format,timestamp,bytes[,key,bpm,analysed]} ] }
//    (key/bpm/analysed are optional: old files stay valid)
//  - Newest first, capped at maxEntries. Oldest rows are dropped from the list;
//    files on disk are NEVER deleted by this class.
//  - MESSAGE THREAD ONLY (all I/O). Never call from the audio thread.
//  - Every mutation is: InterProcessLock -> read file -> merge -> atomic write
//    (TemporaryFile + overwriteTargetFileWithTemporary), so several instances or
//    processes never lose each other's entries.
//  - Corrupt/empty/wrong-version file: renamed to history.corrupt-<time>.json,
//    store starts empty, never throws.
//  - The constructor does NOT touch the disk (cheap during plugin scanning);
//    call reload() when the editor opens.
//  - The baseDir argument exists for tests only; production uses the default.
class HistoryStore
{
public:
    static constexpr int maxEntries = 200;
    static constexpr int fileVersion = 1;

    explicit HistoryStore(juce::File baseDir = juce::File())
        : baseDirectory(baseDir == juce::File() ? getDefaultBaseDirectory() : baseDir)
    {
    }

    static juce::File getDefaultBaseDirectory()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Application Support").getChildFile("LinkGrabber");
       #else
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("LinkGrabber");
       #endif
    }

    juce::File getBaseDirectory() const { return baseDirectory; }
    juce::File getHistoryFile() const   { return baseDirectory.getChildFile("history.json"); }

    // Newest first.
    const std::vector<HistoryEntry>& getEntries() const { return entries; }

    // Re-read the file from disk (message thread).
    void reload()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        juce::InterProcessLock lock(lockName);
        lock.enter(1000);  // best effort: a read of an atomically replaced file is safe either way

        std::vector<HistoryEntry> list;
        readFromDisk(list);
        sortAndCap(list);
        entries = std::move(list);
        noteFileState();
    }

    // Cheap check meant for the editor's own 1-2 Hz timer: reloads only if the
    // file's modification time / size / existence changed. Returns true if reloaded.
    bool refreshIfChanged()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (currentFileState() == lastState)
            return false;
        reload();
        return true;
    }

    // Add (or replace the row with the same id+path) and persist. Returns true if
    // the file was written. The in-memory list is updated either way.
    bool add(HistoryEntry entry)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        entry.id = entry.id.trim();
        if (entry.id.isEmpty() || entry.path.isEmpty())
            return false;
        entry.format = entry.format.equalsIgnoreCase("wav") ? "wav" : "mp3";
        if (entry.timestamp.isEmpty())
            entry.timestamp = juce::Time::getCurrentTime().toISO8601(true);

        return mutate([&entry](std::vector<HistoryEntry>& list)
        {
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&entry](const HistoryEntry& e)
                                      { return e.id == entry.id && e.path == entry.path; }),
                       list.end());
            list.insert(list.begin(), entry);
        });
    }

    // Remove every row with this id and persist. The file on disk is untouched.
    bool remove(const juce::String& id)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        return mutate([&id](std::vector<HistoryEntry>& list)
        {
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&id](const HistoryEntry& e) { return e.id == id; }),
                       list.end());
        });
    }

    // Flag only. A missing file never removes the history row.
    static bool isMissingOnDisk(const HistoryEntry& entry)
    {
        return ! juce::File(entry.path).existsAsFile();
    }

private:
    struct FileState
    {
        bool exists = false;
        juce::int64 modified = 0;
        juce::int64 size = 0;
        bool operator==(const FileState& o) const
        {
            return exists == o.exists && modified == o.modified && size == o.size;
        }
    };

    static inline const char* lockName = "LinkGrabberHistory";

    juce::File baseDirectory;
    std::vector<HistoryEntry> entries;
    FileState lastState;

    FileState currentFileState() const
    {
        FileState s;
        const auto f = getHistoryFile();
        s.exists = f.existsAsFile();
        if (s.exists)
        {
            s.modified = f.getLastModificationTime().toMilliseconds();
            s.size = f.getSize();
        }
        return s;
    }

    void noteFileState() { lastState = currentFileState(); }

    static juce::int64 parseMillis(const juce::String& iso)
    {
        return juce::Time::fromISO8601(iso).toMilliseconds();
    }

    static void sortAndCap(std::vector<HistoryEntry>& list)
    {
        std::stable_sort(list.begin(), list.end(),
                         [](const HistoryEntry& a, const HistoryEntry& b)
                         { return parseMillis(a.timestamp) > parseMillis(b.timestamp); });
        if (static_cast<int>(list.size()) > maxEntries)
            list.resize(static_cast<size_t>(maxEntries));
    }

    template <typename Fn>
    bool mutate(Fn&& fn)
    {
        if (! baseDirectory.createDirectory().wasOk())
            return false;

        juce::InterProcessLock lock(lockName);
        lock.enter(2000);  // if the lock cannot be taken we still proceed (R7 fallback)

        std::vector<HistoryEntry> list;
        readFromDisk(list);
        fn(list);
        sortAndCap(list);
        const bool ok = writeToDisk(list);

        entries = std::move(list);
        noteFileState();
        return ok;
    }

    // Reads the file into `out`. Missing file -> empty. Unusable file -> quarantined
    // and empty. Individual malformed rows are skipped.
    void readFromDisk(std::vector<HistoryEntry>& out) const
    {
        out.clear();
        const auto file = getHistoryFile();
        if (! file.existsAsFile())
            return;

        const auto text = file.loadFileAsString();
        juce::var parsed;
        bool ok = false;

        if (text.trim().isNotEmpty() && juce::JSON::parse(text, parsed).wasOk())
        {
            if (auto* root = parsed.getDynamicObject())
            {
                const auto& entriesVar = root->getProperty("entries");
                if (static_cast<int>(root->getProperty("version")) == fileVersion
                    && entriesVar.isArray())
                {
                    ok = true;
                    for (const auto& row : *entriesVar.getArray())
                    {
                        auto* obj = row.getDynamicObject();
                        if (obj == nullptr)
                            continue;

                        HistoryEntry e;
                        e.id = obj->getProperty("id").toString().trim();
                        e.path = obj->getProperty("path").toString();
                        if (e.id.isEmpty() || e.path.isEmpty())
                            continue;
                        e.title = obj->getProperty("title").toString();
                        e.format = obj->getProperty("format").toString().equalsIgnoreCase("wav") ? "wav" : "mp3";
                        e.timestamp = obj->getProperty("timestamp").toString();
                        e.bytes = static_cast<juce::int64>(obj->getProperty("bytes"));
                        e.analysed = static_cast<bool>(obj->getProperty("analysed"));
                        e.key = e.analysed ? obj->getProperty("key").toString().substring(0, 12) : juce::String();
                        e.bpm = e.analysed ? juce::jlimit(0, 400, static_cast<int>(obj->getProperty("bpm"))) : 0;
                        out.push_back(std::move(e));
                    }
                }
            }
        }

        if (! ok)
        {
            out.clear();
            quarantine(file);
        }
    }

    static void quarantine(const juce::File& file)
    {
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
        auto target = file.getSiblingFile("history.corrupt-" + stamp + ".json");
        for (int n = 1; target.exists(); ++n)
            target = file.getSiblingFile("history.corrupt-" + stamp + "-" + juce::String(n) + ".json");
        file.moveFileTo(target);  // if this fails the next successful write replaces the bad file
    }

    bool writeToDisk(const std::vector<HistoryEntry>& list) const
    {
        juce::Array<juce::var> rows;
        for (const auto& e : list)
        {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty("id", e.id);
            o->setProperty("title", e.title);
            o->setProperty("path", e.path);
            o->setProperty("format", e.format);
            o->setProperty("timestamp", e.timestamp);
            o->setProperty("bytes", e.bytes);
            if (e.analysed)
            {
                o->setProperty("analysed", true);
                o->setProperty("key", e.key);
                o->setProperty("bpm", e.bpm);
            }
            rows.add(juce::var(o.get()));
        }

        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("version", fileVersion);
        root->setProperty("entries", rows);

        const auto text = juce::JSON::toString(juce::var(root.get()), false);

        juce::TemporaryFile temp(getHistoryFile());
        if (! temp.getFile().replaceWithText(text))
            return false;
        return temp.overwriteTargetFileWithTemporary();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoryStore)
};
