// Standalone test for Source/KeyBpmAnalyser.h:
//   clang++ -std=c++17 -O2 -I../Source keybpm_test.cpp -o /tmp/keybpm_test && /tmp/keybpm_test
#include "KeyBpmAnalyser.h"
#include <cstdio>
#include <random>

using namespace keybpm;
static int failures = 0;
static auto never = [] { return false; };

static void check(bool ok, const char* what) { if (!ok) { ++failures; std::printf("  FAIL: %s\n", what); } }

// Drum pattern: kick on every beat, hat on off-beats, a little noise.
static std::vector<float> drums(double bpm, double seconds, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.f, 1.f);
    const size_t n = (size_t) (seconds * kSampleRate);
    std::vector<float> x(n, 0.f);
    const double beat = 60.0 / bpm * kSampleRate;
    for (double t = 0; t < (double) n; t += beat)
    {
        for (size_t i = 0; i < 2000 && (size_t) t + i < n; ++i)
            x[(size_t) t + i] += 0.8f * std::sin(2.0 * M_PI * (55.0 + 80.0 * std::exp(-(double) i / 300.0)) * (double) i / kSampleRate) * std::exp(-(double) i / 700.0);
        const size_t h = (size_t) (t + beat / 2);
        for (size_t i = 0; i < 600 && h + i < n; ++i) x[h + i] += 0.25f * nd(rng) * std::exp(-(double) i / 150.0);
    }
    return x;
}

// I-IV-V-I with 5-harmonic tones + bass; changes every 2 s. cents = detune.
static std::vector<float> chords(int tonic, bool minor, double seconds, double cents = 0.0)
{
    const size_t n = (size_t) (seconds * kSampleRate);
    std::vector<float> x(n, 0.f);
    const int degrees[4][3] = { { 0, minor ? 3 : 4, 7 }, { 5, minor ? 8 : 9, 12 }, { 7, minor ? 10 : 11, 14 }, { 0, minor ? 3 : 4, 7 } };
    const double tune = std::pow(2.0, cents / 1200.0);
    for (size_t i = 0; i < n; ++i)
    {
        const int chord = (int) ((double) i / kSampleRate / 2.0) % 4;
        double s = 0.0;
        for (int d = 0; d < 3; ++d)
        {
            const double midi = 48 + tonic + degrees[chord][d];
            const double f = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0) * tune;
            for (int h = 1; h <= 5; ++h) s += std::sin(2.0 * M_PI * f * h * (double) i / kSampleRate) / h;
        }
        const double bass = 440.0 * std::pow(2.0, (36 + tonic + degrees[chord][0] - 69.0) / 12.0) * tune;
        s += 1.5 * std::sin(2.0 * M_PI * bass * (double) i / kSampleRate);
        x[i] = (float) (0.1 * s);
    }
    return x;
}

int main()
{
    std::printf("== BPM ==\n");
    for (double bpm : { 75.0, 85.0, 90.0, 100.0, 110.0, 120.0, 128.0, 140.0, 150.0, 160.0, 174.0 })
    {
        const auto r = analyse(drums(bpm, 60.0, 1), never);
        std::printf("true %.0f -> %.2f (valid=%d score=%.2f)\n", bpm, r.bpm, r.bpmValid, r.bpmScore);
        // Genuinely ambiguous octave: drum & bass at 174 is felt at 87. Real-song tuning (Tomorrow,
        // true 80, must not read 160) wins, so for fast tempi the half value is also accepted.
        const bool ok = std::fabs(r.bpm - bpm) <= 1.0 || (bpm >= 170.0 && std::fabs(r.bpm - bpm / 2.0) <= 1.0);
        check(r.bpmValid && ok, "bpm within 1 (or half for >= 170)");
    }

    std::printf("== KEY (all 24, clean) ==\n");
    int keyOk = 0;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            const auto r = analyse(chords(t, m != 0, 16.0), never);
            const bool ok = r.keyValid && r.tonic == t && r.minor == (m != 0);
            keyOk += ok;
            if (!ok) std::printf("  miss: true %d%s -> %s (r=%.2f)\n", t, m ? "m" : "", keyName(r).c_str(), r.keyCorrelation);
        }
    std::printf("%d / 24 correct\n", keyOk);
    check(keyOk >= 23, "at least 23 of 24 keys");

    std::printf("== KEY detuned +35 cents ==\n");
    int detOk = 0;
    for (int t : { 0, 4, 9 })
        for (int m = 0; m < 2; ++m)
        {
            const auto r = analyse(chords(t, m != 0, 16.0, 35.0), never);
            detOk += r.keyValid && r.tonic == t && r.minor == (m != 0);
        }
    std::printf("%d / 6 correct\n", detOk);
    check(detOk >= 5, "detuned keys");

    std::printf("== degenerate input ==\n");
    check(!analyse(std::vector<float>(22050 * 30, 0.f), never).bpmValid, "silence bpm invalid");
    check(!analyse(std::vector<float>(22050 * 30, 0.f), never).keyValid, "silence key invalid");
    check(!analyse(std::vector<float>(), never).keyValid, "empty");
    check(!analyse(std::vector<float>(1000, 0.3f), never).bpmValid, "too short");
    {
        for (unsigned seed : { 7u, 8u, 9u, 10u })
        {
            std::mt19937 rng(seed); std::normal_distribution<float> nd(0.f, 0.1f);
            std::vector<float> noise(22050 * 30);
            for (auto& v : noise) v = nd(rng);
            const auto r = analyse(noise, never);
            std::printf("white noise: bpmValid=%d (score %.2f) keyValid=%d (r %.2f)\n", r.bpmValid, r.bpmScore, r.keyValid, r.keyCorrelation);
            check(!r.bpmValid, "noise has no tempo");
            check(!r.keyValid, "noise has no key");
        }
    }
    {
        int calls = 0;
        const auto r = analyse(drums(120, 60.0, 3), [&] { return ++calls > 3; });
        check(r.cancelled, "cancel honoured");
    }
    {
        // 10 minute input is clipped to the middle 180 s and finishes quickly
        const auto big = drums(128.0, 600.0, 5);
        const auto r = analyse(big, never);
        check(r.bpmValid && std::fabs(r.bpm - 128.0) <= 1.0, "long input");
    }
    {
        Result r; r.keyValid = true; r.tonic = 9; r.minor = true; r.bpmValid = true; r.bpm = 127.6;
        check(keyName(r) == "A min", "keyName");
        check(fileTag(r) == "Am 128bpm", "fileTag");
    }
    std::printf(failures ? "\n%d FAILURES\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
