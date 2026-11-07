# LinkGrabber - Implementation Plan

**Date:** 2026-11-07
**Complexity Score:** 5.0 (Complex; literal sum 8.2, capped)
**Strategy:** Phase-based implementation

complexity_score: 5.0
phased_implementation: true

---

## Complexity Factors

- **Parameters:** 1 parameter (`format`) (1/5 points, capped at 2.0) = 0.2
- **Algorithms / components:** 8 (there is no DSP; these are the Core Components of architecture.md) = 8
  - Passthrough Audio Path, Plugin State, Download Manager, Tool Locator/Manager, Command/Progress Parser/Failure Classifier, History Store, File Drag-Out Source, Minesweeper Board UI
- **Features:** 0 points (none of feedback / FFT / multiband / modulation / MIDI apply)
  - Not scored by the formula but real complexity drivers: child-process management, external tool distribution, persisted history, host-dependent OS drag-and-drop
- **Total:** 8.2, capped at 5.0

The score is driven by non-DSP system work, not by audio algorithms. Stage 0 tier: 5 (DEEP).

---

## Stages

- Stage 0: Research ✓
- Stage 1: Planning ✓
- Stage 1: Foundation + Shell ← Next (build system, passthrough, APVTS `format`)
- Stage 2: Engine (non-DSP, phased 2.0-2.4)
- Stage 3: GUI (native JUCE, phased 3.1-3.3)
- Stage 3: Validation

Note: UI mockup does not exist yet (`/dream LinkGrabber` option 3). Stage 3 needs it; the engine in Stage 2 does not. If the mockup arrives later, Stage 1 and 2 can still proceed.

---

## Complex Implementation (Score >= 3.0)

### Stage 1: Foundation + Shell

- CMake per `plugins/*/CMakeLists.txt` conventions: formats VST3 AU Standalone, `IS_SYNTH FALSE`, no MIDI, `NEEDS_WEB_BROWSER FALSE`, `juce_generate_juce_header`, link `juce_audio_utils`, `juce_gui_extra` as needed
- Stereo in/out passthrough (`processBlock` leaves audio untouched), zero latency
- APVTS with `format` (`AudioParameterChoice`, non-automatable, default MP3 320 kbps)
- Extra state tree for `link` and `outputFolder`; `getStateInformation/setStateInformation`
- Placeholder editor with the `format` ComboBox attached

**Test Criteria:**
- [ ] VST3 + AU + Standalone build with 0 warnings
- [ ] Audio passes bit-exact in a host (null test)
- [ ] `format` and extras survive save/reload of a host session
- [ ] `format` is not listed as automatable in the host

### Stage 2: Engine Phases (processor and worker work, no GUI polish)

#### Phase 2.0: Drag-out spike (do first; retires risk R1)

**Goal:** Prove file drag-out from a native JUCE editor in each target host before any other UI work.

**Components:**
- Temporary native `FileHandle` component in the placeholder editor that calls `performExternalDragDropOfFiles({path}, false, this, cb)` from `mouseDrag` after 4 px
- Two dummy files (a short `.wav`, a short `.mp3`) generated into the output folder

**Test Criteria:**
- [ ] Drag succeeds from Standalone to Finder
- [ ] Drag to a track in Logic Pro (AU), Ableton Live (VST3 and AU), Reaper (VST3 and AU); result recorded in a per-host table in `NOTES.md`
- [ ] No assertion in a Debug build; no second drag started from one gesture
- [ ] Failing hosts documented with "Show in Finder" as the fallback

#### Phase 2.1: State and History Store

**Goal:** Persistent link/folder state and the global history file.

**Components:**
- History Store (`history.json`, atomic write via `TemporaryFile`, `InterProcessLock`, merge by id, 200-entry cap, corrupt-file quarantine)
- Output folder default `~/Music/LinkGrabber` and sanitising on restore
- Unit-style self-test (temporary, removed after phase)

**Test Criteria:**
- [ ] Add/reload/remove entries; ordering newest first
- [ ] Two processes/instances adding concurrently lose no entry
- [ ] Corrupt/empty/missing file handled without a crash
- [ ] Missing-on-disk file flagged, not deleted from history

#### Phase 2.2: Tools, command, parser, Download Manager

**Goal:** A working end-to-end download in a console test and then inside a host.

**Components:**
- `DownloadCommand.h`-style pure helpers (link validation, argv builder, parser, classifier), written fresh with fixtures captured from real yt-dlp runs
- Tool Locator (Homebrew/system paths, executable check, explicit child PATH incl. deno)
- Processor-owned `DownloadManager` (Thread + ChildProcess + CriticalSection snapshot + AsyncUpdater + 1 Hz Timer)

**Test Criteria:**
- [ ] MP3 320 and WAV downloads of a short public video; `ffprobe` confirms `mp3` at 320 kbps and `pcm_s24le`
- [ ] Percent is monotonic; converting detected; final path resolved
- [ ] Invalid link, private/unavailable video, offline, missing tool all map to the right mode with no crash
- [ ] Real download works inside Logic, Ableton and Reaper (host smoke test for R2)
- [ ] Manager survives editor close/reopen mid-download

#### Phase 2.3: Hardening

**Goal:** Cancel, cleanup, failure messages, logging.

**Components:**
- Cancel kills the child and deletes `*.part/*.ytdl/*.temp` created since job start
- `errorOutdated` / `errorUnavailable` / `errorNetwork` classification, `~/Library/Logs/LinkGrabber.log` (512 KB cap)
- Optional `--match-filter "duration <= 3600"` guard
- Orphan-process check; switch to `posix_spawn` + `killpg` only if orphans are found

**Test Criteria:**
- [ ] 10x open/close including mid-download close leaves no stray `yt-dlp`/`ffmpeg` (`ps` check)
- [ ] Destroying the plugin mid-download does not hang or crash the host
- [ ] Re-downloading the same video yields a deterministic result (documented)

#### Phase 2.4: Managed tools and update (optional, needed for distribution)

**Goal:** Remove the Homebrew requirement and keep yt-dlp updatable.

**Components:**
- `~/Library/Application Support/LinkGrabber/bin/` managed folder, SHA-256 verified seeding of yt-dlp, ffmpeg, JS runtime (arm64 and x86_64), `yt-dlp -U` update action, version check on editor open
- Signing notes for distribution builds (inside-out signing, hardened runtime entitlements)

**Test Criteria:**
- [ ] Clean machine without Homebrew downloads successfully
- [ ] Update replaces yt-dlp without touching the plugin bundle (signature stays valid)
- [ ] Works on Apple Silicon and Intel (or documented limitation)

### Stage 3: GUI Phases (native JUCE; supersedes the WebView phases in the generic template)

#### Phase 3.1: Layout and board

**Goal:** Implement the mockup (once created) as native components.

**Components:**
- Window, link field + Paste, format ComboBox (APVTS attachment), Download/Cancel, LED counters, smiley, tile grid with reveal/convert-sweep/clear/mine states, output-folder display + Choose, message line

**Test Criteria:**
- [ ] Rendering verified with a real screenshot against the mockup
- [ ] Board reflects percent monotonically; converting sweep visible; done clears; each error mode shows a mine
- [ ] Timer stops when idle (no CPU use)
- [ ] Editor open/close at any moment is safe

#### Phase 3.2: Result row, history list, drag-out

**Goal:** Productionise the Phase 2.0 spike.

**Components:**
- Draggable result row and `ListBox` history with per-row drag, Show in Finder, Remove, "file missing" state

**Test Criteria:**
- [ ] Drag works in every host recorded as passing in Phase 2.0, from both the result row and history rows
- [ ] History persists across sessions and shows in a second instance
- [ ] Missing files are greyed and not draggable

#### Phase 3.3: Polish and validation

**Components:** first-run rights notice (ToS/copyright), keyboard (Enter to download, Cmd+V paste), resizing policy (fixed size), pluginval, changelog, install via `scripts/build-and-install.sh`.

**Test Criteria:**
- [ ] pluginval passes at the repo's standard level
- [ ] Full host matrix repeated with the installed build (pattern #10), caches cleared (pattern #18)

---

## Implementation Notes

**Key Considerations:**
- Native UI only; none of the WebView critical patterns apply. Patterns #1, #2, #4, #5, #10, #14, #18, #22 apply.
- The audio thread must stay free of locks, allocation and string work.
- Test with Debug builds so JUCE assertions in the drag path surface.
- Homebrew yt-dlp/ffmpeg/deno are present on the dev machine; deno must be on the child PATH.
- Legal: personal-use tool; YouTube Terms of Service and copyright caveats are recorded in architecture.md (R5).

**Estimated duration:** Stage 1 about 1 session; Stage 2 about 3-5 sessions (2.0 and 2.2 are the long ones; 2.4 optional); Stage 3 about 2-3 sessions plus mockup work.
