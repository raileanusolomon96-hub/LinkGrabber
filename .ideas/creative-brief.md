# LinkGrabber - Creative Brief

## Overview

**Type:** Utility
**Core Concept:** A YouTube-to-MP3/WAV downloader living inside the DAW: paste a link, wait for the Minesweeper board to clear, then drag the audio straight onto a track.
**Status:** 💡 Ideated
**Created:** 2026-11-07

## Vision

LinkGrabber brings the "YouTube to MP3" web service into the DAW so producers never leave their session to grab a sample or reference. The user pastes a YouTube link into the plugin, the audio is downloaded and converted, and the finished file can be dragged directly from the plugin into the arrangement or onto a track.

The plugin does not touch the audio passing through it. It is a silent passthrough utility whose entire job is acquiring and handing over files.

## Parameters

LinkGrabber has no audio-processing parameters (audio in = audio out). Its state is non-automatable UI/session state:

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Format | MP3 320 kbps / WAV 24-bit | MP3 320 | Output format for the next download |
| Link | text | empty | YouTube URL to fetch |
| History | list | empty | Persisted list of past downloads (path, title, format, timestamp) |

## UI Concept

**Layout:** Single-window Minesweeper-style board with a link entry field, format selector, and a download history list.
**Visual Style:** Minesweeper (classic grid tiles, counters, smiley/status face).
**Key Elements:**
- Link input (paste a YouTube URL)
- Format toggle (MP3 320 / WAV 24-bit)
- Progress grid: download and conversion progress reveal tiles; completion clears the board
- Draggable result item for the finished file (drag to DAW)
- Download history list; past downloads can be re-dragged later

## Use Cases

- Grab a sample or loop source from YouTube without leaving the DAW
- Pull a reference track into a session for A/B comparison
- Download a lossless-ish WAV 24-bit to chop and resample

## Inspirations

- Online "YouTube to MP3" converter services
- Classic Windows Minesweeper aesthetic
- Previous LinkGrab plugin (removed): native Minesweeper UI, DownloadManager, yt-dlp + ffmpeg producing MP3 320 and WAV 24-bit

## Technical Notes

- Passthrough audio path: processBlock leaves audio unmodified.
- Prior LinkGrab approach used yt-dlp for download and ffmpeg for conversion (MP3 320, WAV 24-bit); worth re-evaluating for LinkGrabber (bundling, signing, notarization, and updating yt-dlp as YouTube changes).
- Drag-out to the DAW requires native drag-and-drop of files from the plugin window; behavior varies per host and should be researched in Stage 0.
- Persistent download history stored in plugin state / app data folder.
- Downloading must run off the audio thread; the plugin must remain real-time safe.
- Legal note: downloading YouTube content may violate YouTube's Terms of Service and copyright law; intended for content the user has rights to use.

## Next Steps

- [ ] Create UI mockup (`/dream LinkGrabber` → option 3)
- [ ] Start implementation (`/implement LinkGrabber`)
