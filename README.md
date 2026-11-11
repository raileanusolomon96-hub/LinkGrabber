# LinkGrabber

A JUCE 8 audio plugin (VST3 / AU / Standalone) with a Windows 95 Minesweeper look. Paste a YouTube link and it downloads the audio as MP3 320 or WAV 24-bit, then finds the key and BPM, tags the file name (`Title [id] Am 128bpm.wav`), and lets you drag the result into your DAW. Audio passes through unchanged.

Current version: 1.1.1 (see CHANGELOG.md, NOTES.md).

## Requirements (macOS)
`yt-dlp`, `ffmpeg` and `deno` installed, for example `brew install yt-dlp ffmpeg deno`. If downloads stop working, run `brew upgrade yt-dlp`.

## Build
This folder is a plugin inside the Plugin Freedom System layout. Put it at `plugins/LinkGrabber` of a checkout of that project (JUCE 8 at `~/JUCE`), then:

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target LinkGrabber_VST3 LinkGrabber_AU LinkGrabber_Standalone

The key/BPM engine (`Source/KeyBpmAnalyser.h`) is header-only and has its own test: `cd Tests && clang++ -std=c++17 -O2 -I../Source keybpm_test.cpp && ./a.out`.

Only download content you have the right to use. Windows is not supported yet.
