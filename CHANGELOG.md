# Changelog

All notable changes to LinkGrabber will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-11-08

### Added
- Key and BPM finder: after every download LinkGrabber analyses the audio (ffmpeg decode + built-in analyser, no new tools) and shows the Key (e.g. "A min") and BPM
- Result tile shows two red LED boxes (KEY, BPM); History rows show Key and BPM columns (old entries stay blank, unknown shows `--`)
- File name carries the result, e.g. `Title [id] Am 128bpm.wav`; unknown results leave the name unchanged
- New "analysing" step: sunglasses smiley, sweeping highlight on the board, status "Listening for key and tempo..."
- Cancel during analysing keeps the downloaded file (untagged) and adds it to History

### Technical Details
- `Source/KeyBpmAnalyser.h`: plain C++17, own FFT, onset autocorrelation tempo (folded into 70-180, prior near 130) and chroma + Krumhansl key profiles; analyses the middle 3 minutes; never fails a download
- History entries gain optional `key`, `bpm`, `analysed` fields (backward compatible)
- Restore point for 1.0.0: git tag `linkgrabber-v1.0.0`

### Known limits
- Accuracy verified on synthetic music only (Tests/keybpm_test.cpp); real-song accuracy to be confirmed. Typical errors: relative major/minor, half/double tempo
- Downloading the same video again replaces the earlier tagged file

## [1.0.0] - 2026-11-07

### Added
- Silent-passthrough utility plugin (VST3, AU, Standalone) that downloads YouTube audio inside the DAW
- Output formats: MP3 320 kbps and WAV 24-bit stereo (`format` parameter, not automatable)
- Native Windows 95 Minesweeper UI (fixed 480x360): progress LEDs, status smiley, 16x6 tile board that reveals with download progress, sweeps during conversion, and shows a mine on errors
- Draggable result tile and persistent download history (drag any past download into the DAW; missing files greyed out; remove rows from the list)
- Download engine: yt-dlp + ffmpeg (+ deno) located without relying on the host PATH, background worker, cancel that stops the whole process tree and removes partial files, plain-language failure messages, 1 hour duration guard, log at `~/Library/Logs/LinkGrabber.log`
- First-run rights notice (global acknowledgement)
- Keyboard: Enter starts the download, Cmd+V pastes the link, Esc cancels or closes history

### Technical Details
- Requires yt-dlp, ffmpeg and deno installed (Homebrew paths searched: /opt/homebrew/bin, /usr/local/bin)
- History stored globally at `~/Library/Application Support/LinkGrabber/history.json` (atomic writes, inter-process lock, 200 entry cap)
- Validated with pluginval strictness level 10 (VST3 and AU)
- Drag-out verified in Logic Pro (AU); other hosts untested
- For personal use; downloading may violate YouTube's Terms of Service and copyright law
