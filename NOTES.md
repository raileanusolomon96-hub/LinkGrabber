# LinkGrabber Notes

## Status
- **Current Status:** 🚧 Stage 2 (engine phases 2.0-2.3 verified; Logic drag-out confirmed)
- **Version:** N/A
- **Type:** Utility (YouTube Audio Downloader)

## Lifecycle Timeline

- **2026-11-07:** Creative brief created
- **2026-11-07 (Stage 0):** Research & Planning complete - Architecture and plan documented (Complexity 5.0, phased)
- **2026-11-07 (Stage 0):** UI mockup v1 created (Minesweeper, 480x360)
- **2026-11-07 (Stage 0):** UI mockup v1 finalized; scaffolding generated (parameter-spec.md, v1-ui.html reference, native JUCE integration checklist). WebView PluginEditor/CMake templates intentionally skipped (D1).
- **2026-11-07 (Stage 1):** Foundation complete - CMake, passthrough processor, APVTS `format` (non-automatable), extras tree, placeholder editor

- **2026-11-07 (Stage 2.0):** Drag-out spike code written (temporary `FileHandle` in the placeholder editor, dummy WAV/MP3 generation, Show in Finder). Host tests pending.

- **2026-11-07 (Stage 2.1):** State and History Store code written (`HistoryStore.h`, `StateSanitiser.h`, sanitising in `setStateInformation`, `getHistoryStore()`). Self-tests passed; temporary code removed.

- **2026-11-07 (Stage 2.2):** Tools, command, parser and Download Manager code written (`DownloadCommand.h`, `ToolLocator.h`, `DownloadManager.h/.cpp`, `getDownloadManager()`/`startDownload()` on the processor, history entry added on `done`). Self-tests passed; temporary code removed.2) code to be removed after the phase.

- **2026-11-07 (Stage 2.3):** Hardening code written (build + self-tests verified, temp code removed): `ProcessRunner.h/.cpp` (posix_spawn in its own process group, `killpg` SIGTERM -> 500 ms -> SIGKILL -> waitpid, poll() read timeout) replaces `juce::ChildProcess`; cancel/destroy kill the whole tree (yt-dlp, ffmpeg, deno) and delete the job's partial files (`JobCleanup.h`); new failure kinds `tooLong` (`--match-filter "duration <= 3600"`) and `cannotWrite`; reviewed user messages and classifier; `LogFile.h` (`~/Library/Logs/LinkGrabber.log`, 512 KB cap); re-download resolution. Self-tests passed; temporary code removed.3)` must be removed after the phase.

## Phase 2.3 Behaviour Notes

**Cancel / destroy.** `cancel()` is non-blocking: it sends SIGTERM to the whole process group at once; the worker waits up to 500 ms, then SIGKILLs the group, reaps the leader, deletes partial files and publishes state `idle` with message "Cancelled" (no `downloadFinished` callback, same as before). The destructor does the same and joins the worker (1.5 s, then SIGKILL, then 2.5 s more); typical total is well under 1 s.

**Cleanup rules.** Only direct children of the output folder, never symlinks, only files modified since the job started (-2 s slack). If the video id is known (from the link or yt-dlp's `[youtube] <id>:` line) a file is touched only if its name contains the id; if unknown, only yt-dlp partial suffixes (`.part`, `.ytdl`, `.temp`, `.part-Frag*`, `.temp.<ext>`). Cancel additionally removes `.mp3/.wav/.webm/.m4a/.opus` files with the id (half-written output or the downloaded source). A failed (non-cancel) job removes partial suffixes only. A job that finished `done` is never cleaned.

**Duration guard.** `--match-filter "duration <= 3600"`. yt-dlp skips a rejected video with exit 0 and no file ("... does not pass filter ..."); the manager maps that to `tooLong` ("Video is longer than 1 hour."). A missing duration (live stream) also fails the filter. `--no-live-from-start` was deliberately not added.

**Log file.** `~/Library/Logs/LinkGrabber.log`, 512 KB cap: when an append would exceed it the oldest part is dropped (newest 256 KB kept, cut at a line boundary). Contains job start (link, id, format), state transitions, classification and the last ~25 tool output lines of a failure. Never environment variables and never the child's argv (it carries `PATH=`). Open/write failures are ignored.

**Re-downloading the same video + format (documented behaviour).** The file name template `%(title).140B [%(id)s].%(ext)s` makes the target name identical, so a repeat download lands on the same path. yt-dlp then either re-downloads the source and re-encodes, or skips ("`[download] ... has already been downloaded`" for the source, "`[ExtractAudio] Post-process file ... exists, skipping`" for the finished target). In the skip case yt-dlp may print no `after_move` path line, or the path of the leftover source (`.webm`). The manager therefore resolves the result in this order, always requiring the requested extension: (1) the printed path (or its sibling with the requested extension), (2) the path from the "Post-process file ... exists" line, (3) the sibling of the "has already been downloaded" path, (4) newest file of that format modified since the job started, (5) any file of that format whose name contains the video id. The job then ends `done` with the SAME path, and the history row is replaced (the history key is id + path), not duplicated. Known side effect: in the skip case yt-dlp may leave the downloaded source (`.webm`/`.m4a`) next to the audio file; the plugin does not delete files of a job that finished `done`.

## Known Issues

None

## Additional Notes

Successor concept to the removed LinkGrab plugin. See `.ideas/creative-brief.md`.

## Stage 0 Decisions

- Native JUCE UI (no WebView) so file drag-out comes from a real Component.
- `format` is an APVTS AudioParameterChoice, non-automatable. Link/output folder are extra state.
- History is a global file `~/Library/Application Support/LinkGrabber/history.json`.
- Tools: Homebrew discovery first; managed tool folder in Application Support later (phase 2.4). Not embedded in the signed bundle.
- Drag-out is unverified in every DAW: Phase 2.0 spike comes first. Per-host results table goes here.

## Phase 2.0 Drag-out Test Results

Spike: drag the "Drag test file" box in the editor (WAV/MP3 toggle selects the file; "Show in Finder" is the fallback). Files are generated in the output folder (default `~/Music/LinkGrabber/LinkGrabber drag test.wav|.mp3`). Test with a Debug build (to surface JUCE assertions) installed via `scripts/build-and-install.sh` (pattern #10), caches cleared (pattern #18). Drop target: an audio track / arrangement.

Dummy file notes: the WAV is real audio (1 s, 440 Hz, 24-bit stereo 44.1 kHz). JUCE has no MP3 encoder, so the "MP3" is ~1 s of SILENT, hand-assembled MPEG-1 Layer III frames (128 kbps, 44.1 kHz, 417-byte frames, header `FF FB 90 00` + zero payload). Whether each host decodes it is itself UNTESTED. If a host rejects it, replace the file by hand with a real MP3 (for example, in Terminal: `ffmpeg -f lavfi -i sine=frequency=440:duration=1 -b:a 320k "LinkGrabber drag test.mp3"`) in the output folder; the plugin only regenerates the file when it is missing (or tiny).

| Host / format | Drag result (WAV) | Drag result (MP3) | Notes | Date |
|---------------|-------------------|-------------------|-------|------|
| Standalone -> Finder | NOT YET TESTED | NOT YET TESTED | | |
| Logic Pro, AU | WORKS (user report, format unspecified) | not separately reported | User confirmed dragging from the plugin into Logic works; WAV vs MP3 not separately reported. Release build, not Debug | 2026-11-07 |
| Ableton Live, VST3 | NOT YET TESTED | NOT YET TESTED | | |
| Ableton Live, AU | NOT YET TESTED | NOT YET TESTED | | |
| Reaper, VST3 | NOT YET TESTED | NOT YET TESTED | | |
| Reaper, AU | NOT YET TESTED | NOT YET TESTED | | |

Also record per host: JUCE assertion in Debug (Y/N), any second drag from one gesture (Y/N), whether "Show in Finder" fallback was needed.

- **2026-11-07 (Stage 2, Phase 2.0):** Drag-out confirmed working in Logic Pro (AU) by user. Other hosts untested; risk R1 retired for the primary host.

- **2026-11-07 (Stage 2.3 verified):** Hardening verified: unit, fake, log, repeat, cancel during download, cancel during conversion (no ffmpeg survivor), 10x create/destroy mid-job (worst destructor 34 ms), no strays or zombies. Fixed during testing: extractVideoIdFromLink path segments (leading empty token). Temporary self-test removed.

- **2026-11-07 (Stage 3, Phase 3.1 code complete):** Native Minesweeper editor (480x360): LED counters, smiley, link field/Paste/format/Download-Cancel, seeded 16x6 tile board (reveal/wave/clear/mine), status bar, history header label (non-functional), Show in Finder. Spike drag box removed from the editor (processor getSpikeDummyFile left for Phase 3.2). Editor listens to DownloadManager, never owns it; 30 Hz timer only while a job is active. TEMP SNAPSHOT code (env LINKGRABBER_SNAPSHOT_DIR, Standalone only) present until phase verified.
