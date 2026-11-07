# Changelog

All notable changes to LinkGrabber will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
