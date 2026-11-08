# LinkGrabber v2 - Integration checklist (native JUCE, no WebView)

Spec: v2-ui.yaml (finalized). Reference preview: v2-ui-test.html. WebView C++/CMake files intentionally skipped (NEEDS_WEB_BROWSER FALSE).

Native changes (Editor / ui/LinkGrabberUI.h):
- [ ] New DownloadManager state `analysing` -> sunglasses smiley, Cancel button, board sweep band (2 cols, 110 ms/col), status "Listening for key and tempo..."
- [ ] ResultTile: KEY LED (dot-matrix 5x7, 2px cells, 6 chars) at x118 y181 w78 h26; BPM LED (3 seven-seg digits 9x14) at x244 y181 w50 h26; labels per yaml
- [ ] Unknown: grey `--` on #161616
- [ ] HistoryList: columns Title / Format / Key / BPM / Status (offsets in yaml); old entries blank
- [ ] Done status: "Done. A minor, 128 BPM. Drag the file tile into your DAW."; unknown: "Done. Key and BPM unknown. Drag the file tile into your DAW."
- [ ] Cancel during analysing: keep untagged file, add history entry with unknown key/bpm
- [ ] Keep 480x360 fixed; no new parameters
