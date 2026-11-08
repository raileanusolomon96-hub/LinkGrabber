# Key and BPM Finder - Research (Level 2, 2026-11-08)

## Recommendation
Write our own small analyser (about 300 lines of C++, JUCE `dsp::FFT` only). No third-party library.

**Decode:** run the `ffmpeg` we already require to make a temporary mono, 22.05 kHz, 16-bit WAV from the finished file, then read it with `WavAudioFormat`. Reasons:
- `JUCE_USE_MP3AUDIOFORMAT` is 0 by default and carries a patent/licence disclaimer, so reading our own MP3s with `AudioFormatManager` is not available without enabling it.
- One decode path for MP3 and WAV, and 5.1 or odd sample rates are normalised.
- Analyse at most ~3 minutes from the middle of the track (bounded time on 1 h files). Delete the temp file afterwards.

**BPM:** spectral-flux onset envelope (FFT 1024, hop 512 at 22.05 kHz, ~43 frames/s), autocorrelation of the envelope, weighted by a log-Gaussian prior centred near 120 BPM, then fold the result into 70-180 BPM. Refine with parabolic interpolation of the peak. Report an integer (or one decimal if the peak is sharp).

**Key:** chromagram from the FFT (about 60-5000 Hz), 36 bins per octave to estimate tuning offset, folded to 12 pitch classes and averaged over the track; correlate against 24 key profiles (12 major + 12 minor) and take the best. Start with the Krumhansl profiles and compare Temperley and EDMA (electronic-dance-music profile) on real downloads; keep whichever scores best on our test set.

## Findings
- No permissively licensed header-only C++ library does both. libkeyfinder (key) is GPL; Essentia and aubio are GPL/AGPL-family. Unverified, not worth chasing: libtempo (MIT, tempo only), libsonare (licence unknown). Own code avoids licence risk and a new dependency.
- Octave errors (double/half tempo) are the main BPM failure; accuracy tolerant of x2/x3 (Accuracy2) is far higher than strict (Accuracy1). Source: [arXiv 2107.09208](https://arxiv.org/pdf/2107.09208), [tempoeval](https://tempoeval.github.io/tempo_eval_report/wjd.html). A tempo prior and fixed range reduce it ([arXiv 2008.00756](https://arxiv.org/pdf/2008.00756)).
- Key profile choice matters less than genre; pop/rock/electronic is harder than classical. Essentia docs: EDMA profile is derived from electronic music and normally beats Shaath; Krumhansl suits pop; Temperley suits classical ([std_Key](https://essentia.upf.edu/reference/std_Key.html)). A 2006 UPF evaluation found profiles gave similar results, with pop/rock worse than classical.
- I found no head-to-head GiantSteps numbers for these exact profiles, so I will not promise a percentage. Expect right-ish, not perfect: typical errors are the relative major/minor or a fifth away.

## Risks and how we handle them
- Wrong or doubled BPM: show the number as a best guess; tests with known-tempo synthetic and real tracks.
- Tempo drift / DJ-mixes / live music: result may be unstable; low-confidence cases show "?".
- Mixed or atonal audio: low correlation -> show "?" instead of a wrong key.
- Analysis must never fail a download and must be cancellable.

## Test plan
- Synthetic: click tracks at 70, 90, 128, 174 BPM; sine chords for a few major/minor keys.
- Real: 5-10 tracks whose key/BPM the user knows, in MP3 and WAV.
- Confidence thresholds tuned from these.

Confidence in the approach: MEDIUM-HIGH (standard, well-documented methods); accuracy numbers to be measured by us.
