#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

//==============================================================================
// Key and BPM estimation (Key/BPM finder, research note key-bpm-research.md).
//
//  - Plain C++17, no JUCE: unit-testable with a bare compiler. Own FFT, so no
//    GPL dependency.
//  - Input: mono samples at kSampleRate (the caller decodes with ffmpeg to
//    22.05 kHz mono). Never throws, never reads out of range.
//  - Pure function of its input; safe on any thread. Cancellable via the
//    `shouldCancel` callback (polled between frames).
//  - Both results carry a validity flag: unclear input gives "unknown", never a
//    made-up number.
namespace keybpm
{
constexpr double kSampleRate = 22050.0;

struct Result
{
    bool bpmValid = false;
    double bpm = 0.0;          // folded into [70, 180)
    double bpmScore = 0.0;     // peak / mean of the tempo score curve (diagnostic)
    bool keyValid = false;
    int tonic = 0;             // 0 = C ... 11 = B
    bool minor = false;
    double keyCorrelation = 0.0;  // Pearson r of the best key (diagnostic)
    double keyMargin = 0.0;       // best r minus second best r (diagnostic)
    bool cancelled = false;
};

namespace detail
{
using Complex = std::complex<double>;

// Iterative radix-2 FFT, n a power of two.
class Fft
{
public:
    explicit Fft(std::size_t sizePow2) : n(sizePow2), twiddle(sizePow2 / 2), rev(sizePow2)
    {
        for (std::size_t i = 0; i < n / 2; ++i)
            twiddle[i] = std::polar(1.0, -2.0 * M_PI * (double) i / (double) n);

        std::size_t bits = 0;
        while ((std::size_t(1) << bits) < n) ++bits;
        for (std::size_t i = 0; i < n; ++i)
        {
            std::size_t r = 0;
            for (std::size_t b = 0; b < bits; ++b)
                if (i & (std::size_t(1) << b)) r |= std::size_t(1) << (bits - 1 - b);
            rev[i] = r;
        }
    }

    void transform(std::vector<Complex>& a) const
    {
        for (std::size_t i = 0; i < n; ++i)
            if (i < rev[i]) std::swap(a[i], a[rev[i]]);

        for (std::size_t len = 2; len <= n; len <<= 1)
        {
            const std::size_t half = len / 2, step = n / len;
            for (std::size_t i = 0; i < n; i += len)
                for (std::size_t j = 0; j < half; ++j)
                {
                    const Complex u = a[i + j];
                    const Complex v = a[i + j + half] * twiddle[j * step];
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
    }

    std::size_t size() const { return n; }

private:
    std::size_t n;
    std::vector<Complex> twiddle;
    std::vector<std::size_t> rev;
};

inline std::vector<double> hann(std::size_t n)
{
    std::vector<double> w(n);
    for (std::size_t i = 0; i < n; ++i)
        w[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n);
    return w;
}
} // namespace detail

//==============================================================================
// BPM: spectral-flux onset envelope -> autocorrelation -> comb-style tempo score
// with a log-normal prior near 120 BPM -> folded into [70, 180).
template <typename CancelFn>
inline void estimateBpm(const float* x, std::size_t n, Result& out, CancelFn&& shouldCancel)
{
    constexpr std::size_t N = 1024, hop = 256;
    const double fps = kSampleRate / (double) hop;
    if (n < N * 8) return;

    detail::Fft fft(N);
    const auto win = detail::hann(N);
    const std::size_t frames = (n - N) / hop + 1;
    const std::size_t bins = 400;  // up to ~8.6 kHz

    std::vector<double> env(frames, 0.0);
    std::vector<double> prev(bins, 0.0);
    std::vector<detail::Complex> buf(N);

    for (std::size_t f = 0; f < frames; ++f)
    {
        if ((f & 255) == 0 && shouldCancel()) { out.cancelled = true; return; }

        for (std::size_t i = 0; i < N; ++i)
            buf[i] = detail::Complex((double) x[f * hop + i] * win[i], 0.0);
        fft.transform(buf);

        double flux = 0.0;
        for (std::size_t k = 1; k < bins; ++k)
        {
            const double amp = std::abs(buf[k]) / (N / 4.0);
            const double lm = std::log1p(100.0 * amp);
            if (f > 0) flux += std::max(0.0, lm - prev[k]);
            prev[k] = lm;
        }
        env[f] = flux;
    }

    // Remove the slow trend (about 1 s moving mean), keep positive part.
    {
        const std::size_t r = (std::size_t) fps / 2;
        std::vector<double> prefix(frames + 1, 0.0);
        for (std::size_t i = 0; i < frames; ++i) prefix[i + 1] = prefix[i] + env[i];
        std::vector<double> detr(frames);
        for (std::size_t i = 0; i < frames; ++i)
        {
            const std::size_t a = i > r ? i - r : 0, b = std::min(frames, i + r + 1);
            const double mean = (prefix[b] - prefix[a]) / (double) (b - a);
            detr[i] = std::max(0.0, env[i] - mean);
        }
        env.swap(detr);
    }

    double mean = 0.0;
    for (double v : env) mean += v;
    mean /= (double) frames;
    double energy = 0.0;
    for (double& v : env) { v -= mean; energy += v * v; }
    if (energy < 1e-9) return;  // nothing rhythmic

    // Autocorrelation up to 4x the slowest lag we score (60 BPM).
    const std::size_t maxLag = std::min(frames - 1, (std::size_t) (4.0 * 60.0 * fps / 60.0) + 4);
    std::vector<double> acf(maxLag + 1, 0.0);
    for (std::size_t l = 0; l <= maxLag; ++l)
    {
        double s = 0.0;
        for (std::size_t i = 0; i + l < frames; ++i) s += env[i] * env[i + l];
        acf[l] = s / (double) (frames - l) / (energy / (double) frames);
    }

    auto acfAt = [&](double lag) -> double
    {
        if (lag < 0.0 || lag >= (double) maxLag) return 0.0;
        const std::size_t i = (std::size_t) lag;
        const double fr = lag - (double) i;
        return acf[i] * (1.0 - fr) + acf[std::min(i + 1, maxLag)] * fr;
    };

    constexpr double lo = 60.0, hi = 200.0, stepBpm = 0.25;
    const std::size_t count = (std::size_t) ((hi - lo) / stepBpm) + 1;
    std::vector<double> score(count, 0.0);
    double best = -1.0, sum = 0.0;
    std::size_t bestI = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const double bpm = lo + (double) i * stepBpm;
        const double lag = 60.0 * fps / bpm;
        double s = 0.0;
        for (int k = 1; k <= 4; ++k) s += acfAt(lag * k);
        s = std::max(0.0, s);
        const double oct = std::log2(bpm / 130.0);
        s *= std::exp(-0.5 * (oct / 1.1) * (oct / 1.1));
        score[i] = s;
        sum += s;
        if (s > best) { best = s; bestI = i; }
    }
    if (best <= 0.0) return;

    double bpm = lo + (double) bestI * stepBpm;
    out.bpmScore = best / (sum / (double) count);
    while (bpm < 70.0) bpm *= 2.0;
    while (bpm >= 180.0) bpm *= 0.5;
    out.bpm = bpm;
    out.bpmValid = out.bpmScore >= 12.0;
}

//==============================================================================
// Key: peak-picked chromagram (36 bins/octave -> tuning estimate -> 12 classes)
// correlated with 24 key profiles.
enum class Profile { krumhansl, temperley };

template <typename CancelFn>
inline void estimateKey(const float* x, std::size_t n, Result& out, Profile profile,
                        CancelFn&& shouldCancel)
{
    constexpr std::size_t N = 8192, hop = 4096;
    if (n < N * 2) return;

    detail::Fft fft(N);
    const auto win = detail::hann(N);
    const std::size_t frames = (n - N) / hop + 1;
    const double binHz = kSampleRate / (double) N;
    const std::size_t kLo = (std::size_t) std::ceil(65.0 / binHz);
    const std::size_t kHi = std::min(N / 2 - 2, (std::size_t) (2000.0 / binHz));

    std::vector<detail::Complex> buf(N);
    std::vector<double> mag(N / 2 + 1);
    std::vector<double> c36(36, 0.0);
    int usedFrames = 0;

    for (std::size_t f = 0; f < frames; ++f)
    {
        if ((f & 15) == 0 && shouldCancel()) { out.cancelled = true; return; }

        double rms = 0.0;
        for (std::size_t i = 0; i < N; ++i)
        {
            const double v = (double) x[f * hop + i];
            rms += v * v;
            buf[i] = detail::Complex(v * win[i], 0.0);
        }
        if (std::sqrt(rms / (double) N) < 1e-4) continue;  // silent frame

        fft.transform(buf);
        double frameMax = 0.0;
        for (std::size_t k = 0; k <= N / 2; ++k)
        {
            mag[k] = std::abs(buf[k]);
            if (k >= kLo && k <= kHi) frameMax = std::max(frameMax, mag[k]);
        }
        if (frameMax <= 0.0) continue;

        std::vector<double> frameChroma(36, 0.0);
        double frameSum = 0.0;
        for (std::size_t k = kLo; k <= kHi; ++k)
        {
            if (!(mag[k] > mag[k - 1] && mag[k] >= mag[k + 1])) continue;  // spectral peak
            if (mag[k] < 0.02 * frameMax) continue;

            const double a = std::log(mag[k - 1] + 1e-12), b = std::log(mag[k] + 1e-12),
                         c = std::log(mag[k + 1] + 1e-12);
            const double denom = a - 2.0 * b + c;
            const double delta = denom < -1e-12 ? 0.5 * (a - c) / denom : 0.0;
            const double freq = ((double) k + delta) * binHz;
            const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
            long bin = std::lround(midi * 3.0) % 36;
            if (bin < 0) bin += 36;
            const double w = std::sqrt(mag[k]);
            frameChroma[(std::size_t) bin] += w;
            frameSum += w;
        }
        if (frameSum <= 0.0) continue;
        for (std::size_t i = 0; i < 36; ++i) c36[i] += frameChroma[i] / frameSum;
        ++usedFrames;
    }
    if (usedFrames < 4) return;

    // Tuning: which of the three sub-bin offsets holds the most energy.
    int tune = 0;
    double tuneBest = -1.0;
    for (int r = 0; r < 3; ++r)
    {
        double s = 0.0;
        for (int pc = 0; pc < 12; ++pc) s += c36[(std::size_t) ((3 * pc + r) % 36)];
        if (s > tuneBest) { tuneBest = s; tune = r; }
    }
    double chroma[12];
    for (int pc = 0; pc < 12; ++pc)
    {
        double s = 0.0;
        for (int d = -1; d <= 1; ++d)
            s += c36[(std::size_t) ((3 * pc + tune + d + 36) % 36)];
        chroma[pc] = s;
    }

    static const double kkMajor[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double kkMinor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    static const double tpMajor[12] = { 5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0 };
    static const double tpMinor[12] = { 5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0 };
    const double* majP = profile == Profile::krumhansl ? kkMajor : tpMajor;
    const double* minP = profile == Profile::krumhansl ? kkMinor : tpMinor;

    auto pearson = [&](const double* prof, int rot) -> double
    {
        double mc = 0.0, mp = 0.0;
        for (int i = 0; i < 12; ++i) { mc += chroma[i]; mp += prof[i]; }
        mc /= 12.0; mp /= 12.0;
        double num = 0.0, dc = 0.0, dp = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double cc = chroma[(i + rot) % 12] - mc, pp = prof[i] - mp;
            num += cc * pp; dc += cc * cc; dp += pp * pp;
        }
        return (dc > 0.0 && dp > 0.0) ? num / std::sqrt(dc * dp) : 0.0;
    };

    double best = -2.0, second = -2.0;
    int bestTonic = 0;
    bool bestMinor = false;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            const double r = pearson(m ? minP : majP, t);
            if (r > best) { second = best; best = r; bestTonic = t; bestMinor = m != 0; }
            else if (r > second) second = r;
        }

    out.tonic = bestTonic;
    out.minor = bestMinor;
    out.keyCorrelation = best;
    out.keyMargin = best - second;
    out.keyValid = best >= 0.6;
}

//==============================================================================
// Analyse the middle `maxSeconds` of the track (bounded time on long files).
template <typename CancelFn>
inline Result analyse(const std::vector<float>& mono, CancelFn&& shouldCancel,
                      double maxSeconds = 180.0, Profile profile = Profile::krumhansl)
{
    Result r;
    std::size_t start = 0, len = mono.size();
    const std::size_t cap = (std::size_t) (maxSeconds * kSampleRate);
    if (len > cap) { start = (len - cap) / 2; len = cap; }

    double rms = 0.0;
    for (std::size_t i = 0; i < len; ++i) rms += (double) mono[start + i] * (double) mono[start + i];
    if (len == 0 || std::sqrt(rms / (double) len) < 1e-4) return r;  // silence

    estimateBpm(mono.data() + start, len, r, shouldCancel);
    if (r.cancelled) return r;
    estimateKey(mono.data() + start, len, r, profile, shouldCancel);
    return r;
}

inline std::string keyName(const Result& r)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (!r.keyValid) return "";
    return std::string(names[r.tonic % 12]) + (r.minor ? " min" : " maj");
}

// Compact file-name tag, e.g. "Am 128bpm" / "C 90bpm". Empty parts are dropped.
inline std::string fileTag(const Result& r)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    std::string tag;
    if (r.keyValid) tag += std::string(names[r.tonic % 12]) + (r.minor ? "m" : "");
    if (r.bpmValid)
    {
        if (!tag.empty()) tag += " ";
        tag += std::to_string((int) std::lround(r.bpm)) + "bpm";
    }
    return tag;
}
} // namespace keybpm
