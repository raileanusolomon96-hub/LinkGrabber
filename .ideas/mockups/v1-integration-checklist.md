# Stage 3 (GUI) Integration Checklist - v1 (NATIVE JUCE)

**Plugin:** LinkGrabber
**Mockup Version:** v1 (Classic Windows 95 Minesweeper, 480x360, non-resizable)
**Generated:** 2026-11-07
**UI technology:** Native JUCE `juce::Graphics` (architecture.md D1). NO WebView, NO `NEEDS_WEB_BROWSER`, no relays, no resource provider.

**Sources of truth:** `mockups/v1-ui.yaml` (all rects/colors/fonts below refer to it); `mockups/v1-ui.html` and `v1-ui-test.html` are visual references only. All coordinates are window-relative, 1px = 1 logical pixel.

## 0. Skipped WebView artifacts (intentional)
- [ ] Do NOT create v1-PluginEditor.h/.cpp, v1-CMakeLists.txt, Source/ui/public/*, juce_add_binary_data for UI, `JUCE_WEB_BROWSER=1`, or `juce_gui_extra` WebView linking.

## 1. Editor shell
- [ ] `LinkGrabberAudioProcessorEditor : AudioProcessorEditor, DragAndDropContainer, Timer, DownloadManager::Listener` (listener interface only; editor never owns manager or history)
- [ ] `setSize(480, 360)`, `setResizable(false, false)`
- [ ] Background `#c0c0c0`, outer raised bevel thickness 2 (white top/left, `#808080` bottom/right)
- [ ] Member order: processor ref, ComboBoxAttachment declared AFTER the ComboBox it attaches to (destroyed first)

## 2. Custom paint helpers (shared)
- [ ] `drawBevel(g, rect, raised|sunken, thickness, outline)`: raised = N px `#ffffff` top+left, N px `#808080` bottom+right, optional 1px `#000000` outline (buttons, smiley only); sunken = `#808080` top+left, `#ffffff` bottom+right
- [ ] Fonts: 12px labels/buttons/status, bold 14 title, bold 16 tile numbers, bold 13 drag strip (`juce::Font(juce::FontOptions(...))`); optional bundled pixel font

## 3. Top bar (panel rect 15,15,450x48, sunken 2)
- [ ] LED percent counter rect (22,22,53x34), 3 digits, range 0..100
- [ ] LED elapsed counter rect (405,22,53x34), 3 digits, range 0..999, holds final value after done/error
- [ ] LED digit: 13x24, gap 3, padding L4/T5, 7 segments thickness 3 (a(2,0,9,3) g(2,10,9,3) d(2,21,9,3) f(0,2,3,9) b(10,2,3,9) e(0,12,3,10) c(10,12,3,10)); background `#000000`, on `#ff0000`, off `#2a0000`
- [ ] Smiley button rect (223,22,34x34), raised 2 / pressed sunken, face d=24 `#ffff00` 1px black outline; faces: idle smile, working "oh" mouth (ellipse 5x6 at 0,+5), done sunglasses, error X eyes + frown; click = reset to idle (cancels active job via DownloadManager)

## 4. Link row (y=71, h=26)
- [ ] `juce::TextEditor` link_field rect (15,71,200x26), sunken 2, white background, placeholder "Paste YouTube link" in `#808080`; bound to `link` state-tree property (not trusted on restore)
- [ ] Paste button (225,71,60x26) -> `SystemClipboard::getTextFromClipboard()` into link field
- [ ] **Format `juce::ComboBox` (295,71,90x26)** items "MP3 320", "WAV 24-bit" (ids 1,2), raised bevel with down arrow (custom LookAndFeel); popup flat white list with 1px black border. Create with `AudioProcessorValueTreeState::ComboBoxAttachment(apvts, "format", combo)`
- [ ] Download button (395,71,70x26), bold; label becomes "Cancel" while a job is active

## 5. Board (frame rect 15,107,450x174, sunken 3)
- [ ] 16 cols x 6 rows = 96 tiles, 27px, tile (c,r) at x=24+27c, y=113+27r (grid 432x162)
- [ ] Hidden tile: raised 2 (`#ffffff`/`#808080`) on `#c0c0c0`; revealed: `#c0c0c0` with 1px `#808080` border; mine: `#ff0000` + black mine glyph (10px circle, 8 spokes, 2px white highlight); wave tile `#e6e6e6`
- [ ] Reveal rule: downloading reveals `floor(percent/100*96*0.75)` tiles (max 72) in fixed seeded shuffled order; converting sweeps columns left to right (front column flashes) revealing the remaining; error mine = next unrevealed tile in order
- [ ] Numbers bold 16: 1 `#0000ff`, 2 `#008000`, 3 `#ff0000`, 4 `#000080`; ~12%/6%/3% distribution; cleared on done
- [ ] Result tile (visible when done) rect (30,119,420x150), raised 3, `#c0c0c0`: grip icon (44,133,28x56; 3x6 dots, 4px, 8px pitch, `#808080` + 1px `#ffffff` offset), title (88,133, bold 14, ellipsis), meta (88,158, 12px `<FORMAT> | <SIZE>`), drag strip (44,213,392x40; 2px dashed `#000080`, fill `#d8d8f0`, text "Drag me to your DAW  >>" bold 13 `#000080`)
- [ ] Drag: result tile and history rows call `DragAndDropContainer::performExternalDragDropOfFiles({path}, false, this)` synchronously from `mouseDrag` after >= 4px movement, once per gesture (guard flag cleared on mouseUp/callback). No callAsync/timers. Only for existing files

## 6. History (header rect 15,289,340x22; Show in Finder 365,289,100x22)
- [ ] Expander bar raised 2 / pressed sunken, label "History (N)", arrow right when collapsed, down when expanded
- [ ] Expanded list overlays board rect (15,107,450x174), sunken 2, white; row height 22; title (x+8, ellipsis), format (right-aligned 80px block at x+276), status ('OK'/'Missing', 60px at x+366); selected row `#000080`/white text; missing rows `#808080` and not draggable; existing rows draggable
- [ ] Show in Finder -> `File::revealToUser()` on selected row or latest entry
- [ ] History data from History Store (global history.json), reloaded on editor open and by mod-time poll

## 7. Status line (15,321,450x24, sunken 1, inset 6, 12px)
- [ ] Messages exactly as in v1-ui.yaml `status_line.messages`; normal `#000000`, error `#800000`

## 8. Timer / threading
- [ ] `startTimerHz(30)` ONLY while a job is active or a reveal/sweep animation is playing; `stopTimer()` otherwise (zero idle timers)
- [ ] Editor listens to DownloadManager (AsyncUpdater-driven, message thread); closing editor does not affect a running job
- [ ] `FileChooser` (if used) is a member guarded with `Component::SafePointer`

## 9. Build and test
- [ ] Standalone + VST3 + AU build, no WebView modules/flags in CMake
- [ ] Screenshot compare each state (idle, downloading, converting, done, error, history expanded) against v1-ui-test.html
- [ ] `format` round-trips through host session save/load; not listed for automation
- [ ] Drag-out per host table (Phase 2.0 spike) recorded in NOTES.md
- [ ] Open/close editor 10x including mid-download; no stray yt-dlp/ffmpeg processes

## Parameter List (from parameter-spec.md)
| ID | Type | Choices | Default | UI binding |
|----|------|---------|---------|-----------|
| format | Choice | MP3 320 / WAV 24-bit | MP3 320 | juce::ComboBox + ComboBoxAttachment (no relay) |
