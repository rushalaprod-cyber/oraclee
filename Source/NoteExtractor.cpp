#include "NoteExtractor.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>

namespace mex
{
namespace
{
constexpr double kSr = 22050.0;  // analysis sample rate
constexpr int kOnsetN = 1024;    // short window used for attacks
const double kOnsetLatency = 0.25 * kOnsetN / kSr; // flux peaks ~N/4 before the real attack

thread_local double gTuning = 0.0; // global detune of the sample, in semitones (-0.5..0.5)
inline double mtof (double m) { return 440.0 * std::pow (2.0, (m + gTuning - 69.0) / 12.0); }
inline double ftom (double f) { return 69.0 + 12.0 * std::log2 (f / 440.0) - gTuning; }

struct Prog
{
    const ProgressFn& fn;
    float lo, hi;
    bool operator() (float f) const { return ! fn || fn (lo + (hi - lo) * juce::jlimit (0.0f, 1.0f, f)); }
};

//==============================================================================
// Mixdown to mono, remove DC, resample to kSr, normalise to peak 1.
std::vector<float> prepare (const juce::AudioBuffer<float>& audio, double sr)
{
    const int n = audio.getNumSamples();
    const int chans = juce::jmax (1, audio.getNumChannels());
    std::vector<float> mono ((size_t) n, 0.0f);

    for (int c = 0; c < audio.getNumChannels(); ++c)
    {
        auto* d = audio.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] += d[i] / (float) chans;
    }
    if (n == 0)
        return mono;

    const double mean = std::accumulate (mono.begin(), mono.end(), 0.0) / n;
    for (auto& s : mono)
        s -= (float) mean;

    if (std::abs (sr - kSr) > 1.0)
    {
        if (sr > kSr)
        {
            auto coeffs = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod (
                (float) (0.45 * kSr), sr, 8);
            for (auto* c : coeffs)
            {
                juce::dsp::IIR::Filter<float> f (c);
                for (auto& s : mono)
                    s = f.processSample (s);
            }
        }

        const double ratio = sr / kSr;
        const int outN = juce::jmax (0, (int) std::floor ((n - 8) / ratio));
        std::vector<float> out ((size_t) outN, 0.0f);
        juce::LagrangeInterpolator interp;
        interp.process (ratio, mono.data(), out.data(), outN);
        mono = std::move (out);
    }

    float peak = 0.0f;
    for (auto s : mono)
        peak = std::max (peak, std::abs (s));
    if (peak > 1.0e-6f)
        for (auto& s : mono)
            s /= peak;
    return mono;
}

std::vector<float> hann (int n)
{
    std::vector<float> w ((size_t) n);
    for (int i = 0; i < n; ++i)
        w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) n);
    return w;
}

//==============================================================================
// Log-magnitude spectral flux with a short window: precise onset timing.
std::vector<float> onsetStrength (const std::vector<float>& x, int hop, int frames)
{
    constexpr int order = 10, N = 1 << order;
    juce::dsp::FFT fft (order);
    auto win = hann (N);
    std::vector<float> buf (2 * N), prev (N / 2 + 1, 0.0f), flux ((size_t) frames, 0.0f);

    for (int t = 0; t < frames; ++t)
    {
        const int start = t * hop - N / 2;
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int idx = start + i;
            if (idx >= 0 && idx < (int) x.size())
                buf[(size_t) i] = x[(size_t) idx] * win[(size_t) i];
        }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);

        float sum = 0.0f;
        for (int k = 1; k <= N / 2; ++k)
        {
            const float lm = std::log1p (100.0f * buf[(size_t) k]);
            const float d = lm - prev[(size_t) k];
            if (d > 0.0f)
                sum += d;
            prev[(size_t) k] = lm;
        }
        flux[(size_t) t] = sum;
    }
    return flux;
}

std::vector<bool> pickOnsets (const std::vector<float>& flux, float sensitivity, double fps)
{
    const int n = (int) flux.size();
    std::vector<bool> on ((size_t) n, false);
    if (n == 0)
        return on;
    const float mx = *std::max_element (flux.begin(), flux.end());
    if (mx <= 0.0f)
        return on;

    const int half = juce::jmax (2, (int) std::round (0.10 * fps));
    const int peakWin = juce::jmax (1, (int) std::round (0.03 * fps));
    const int minGap = juce::jmax (1, (int) std::round (0.05 * fps));
    const float delta = (0.14f - 0.10f * sensitivity) * mx;

    int last = -minGap;
    for (int t = 0; t < n; ++t)
    {
        const float v = flux[(size_t) t];
        bool isMax = true;
        for (int k = juce::jmax (0, t - peakWin); k <= juce::jmin (n - 1, t + peakWin) && isMax; ++k)
            if (flux[(size_t) k] > v)
                isMax = false;
        if (! isMax)
            continue;

        float mean = 0.0f;
        int cnt = 0;
        for (int k = juce::jmax (0, t - half); k <= juce::jmin (n - 1, t + half); ++k, ++cnt)
            mean += flux[(size_t) k];
        mean /= (float) juce::jmax (1, cnt);

        if (v > mean * 1.25f + delta && t - last >= minGap)
        {
            on[(size_t) t] = true;
            last = t;
        }
    }
    return on;
}

int nearestOnset (const std::vector<bool>& onsets, int t, int radius)
{
    for (int d = 0; d <= radius; ++d)
    {
        if (t - d >= 0 && t - d < (int) onsets.size() && onsets[(size_t) (t - d)]) return t - d;
        if (t + d >= 0 && t + d < (int) onsets.size() && onsets[(size_t) (t + d)]) return t + d;
    }
    return -1;
}

//==============================================================================
// Tempo: autocorrelation of the onset envelope with a perceptual prior,
// preferring tempos that make the sample an exact number of bars (loops).
double detectTempo (const std::vector<float>& flux, double fps, double lengthSec)
{
    const int n = (int) flux.size();
    if (n < (int) (fps * 2.0))
        return 0.0;

    std::vector<double> pre ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        pre[(size_t) i + 1] = pre[(size_t) i] + flux[(size_t) i];
    const int w = (int) (fps * 0.5);
    std::vector<float> f ((size_t) n);
    for (int t = 0; t < n; ++t)
    {
        const int a = juce::jmax (0, t - w), b = juce::jmin (n, t + w + 1);
        f[(size_t) t] = std::max (0.0f, flux[(size_t) t] - (float) ((pre[(size_t) b] - pre[(size_t) a]) / (b - a)));
    }

    const int maxLag = juce::jmin (n - 1, (int) (fps * 60.0 / 40.0 * 4.0) + 2);
    std::vector<double> ac ((size_t) maxLag + 1, 0.0);
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int t = 0; t + lag < n; ++t)
            s += f[(size_t) t] * f[(size_t) (t + lag)];
        ac[(size_t) lag] = s / (n - lag);
    }
    if (ac[0] <= 0.0)
        return 0.0;

    auto A = [&] (double lag)
    {
        if (lag < 1.0 || lag >= maxLag) return 0.0;
        const int i = (int) lag;
        const double fr = lag - i;
        return ac[(size_t) i] * (1.0 - fr) + ac[(size_t) i + 1] * fr;
    };
    auto rawScore = [&] (double bpm)
    {
        const double L = fps * 60.0 / bpm;
        return A (L) + 0.5 * A (2.0 * L) + 0.5 * A (4.0 * L);
    };
    auto prior = [] (double bpm) { const double z = std::log2 (bpm / 115.0) / 0.8; return std::exp (-0.5 * z * z); };

    double best = 0.0, bestScore = 0.0;
    for (double bpm = 60.0; bpm <= 190.0; bpm += 0.25)
    {
        const double s = rawScore (bpm) * prior (bpm);
        if (s > bestScore) { bestScore = s; best = bpm; }
    }
    if (best <= 0.0 || rawScore (best) < 0.08 * ac[0])
        return 0.0;

    // loop-length candidates (1, 2, 4, 8, 16 bars...)
    double loopBest = 0.0, loopScore = 0.0;
    for (int beats : { 4, 8, 12, 16, 24, 32, 48, 64 })
    {
        const double b = 60.0 * beats / lengthSec;
        if (b < 60.0 || b > 190.0)
            continue;
        const double s = rawScore (b) * prior (b);
        if (s >= 0.85 * bestScore && s > loopScore) { loopScore = s; loopBest = b; }
    }

    double bpm = loopBest > 0.0 ? loopBest : best;
    if (std::abs (bpm - std::round (bpm)) < 0.3)
        bpm = std::round (bpm);
    return bpm;
}

//==============================================================================
// Global tuning: circular mean of the deviation of strong spectral peaks from the
// equal-tempered grid (samples are often pitched / detuned).
double estimateTuning (const std::vector<float>& x)
{
    constexpr int order = 13, N = 1 << order, hop = 2048;
    juce::dsp::FFT fft (order);
    auto win = hann (N);
    std::vector<float> buf (2 * N);
    double sx = 0.0, sy = 0.0;
    const double binHz = kSr / N;
    for (int start = 0; start + N <= (int) x.size(); start += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
            buf[(size_t) i] = x[(size_t) (start + i)] * win[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        float mx = 0.0f;
        for (int k = 4; k < N / 2 - 1; ++k) mx = std::max (mx, buf[(size_t) k]);
        if (mx <= 0.0f) continue;
        for (int k = 4; k < N / 2 - 1; ++k)
        {
            const float m = buf[(size_t) k];
            if (m < 0.1f * mx || m <= buf[(size_t) k - 1] || m < buf[(size_t) k + 1])
                continue;
            const double a = std::log (buf[(size_t) k - 1] + 1e-12), b = std::log (m + 1e-12), c = std::log (buf[(size_t) k + 1] + 1e-12);
            const double den = a - 2.0 * b + c;
            const double f = (k + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0)) * binHz;
            if (f < 60.0 || f > 2500.0) continue;
            const double semi = 69.0 + 12.0 * std::log2 (f / 440.0);
            const double ang = 2.0 * juce::MathConstants<double>::pi * (semi - std::round (semi));
            sx += m * std::cos (ang);
            sy += m * std::sin (ang);
        }
    }
    if (std::hypot (sx, sy) <= 0.0)
        return 0.0;
    double t = std::atan2 (sy, sx) / (2.0 * juce::MathConstants<double>::pi);
    return std::abs (t) < 0.03 ? 0.0 : t; // ignore < 3 cents
}

//==============================================================================
// Harmonic / percussive separation (median filtering, Fitzgerald 2010).
struct HpssOut
{
    std::vector<float> harmonic;
    std::array<std::vector<float>, 3> pBand, hBand; // kick, snare, hat band energies per frame
};

constexpr int kHpssHop = 256;

bool hpss (const std::vector<float>& x, HpssOut& out, const Prog& prog)
{
    constexpr int order = 10, N = 1 << order, hop = kHpssHop, B = N / 2 + 1, K = 8;
    const int frames = (int) x.size() / hop + 1;
    juce::dsp::FFT fft (order);
    auto win = hann (N);
    std::vector<float> buf (2 * N), S ((size_t) frames * B);

    auto loadFrame = [&] (int t)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int idx = t * hop - N / 2 + i;
            if (idx >= 0 && idx < (int) x.size())
                buf[(size_t) i] = x[(size_t) idx] * win[(size_t) i];
        }
    };

    for (int t = 0; t < frames; ++t)
    {
        if ((t % 256) == 0 && ! prog (0.25f * (float) t / (float) frames))
            return false;
        loadFrame (t);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        std::copy (buf.begin(), buf.begin() + B, S.begin() + (long) t * B);
    }

    // calibrate the inverse transform scaling
    std::fill (buf.begin(), buf.end(), 0.0f);
    std::copy (win.begin(), win.end(), buf.begin());
    fft.performRealOnlyForwardTransform (buf.data(), false);
    fft.performRealOnlyInverseTransform (buf.data());
    const float scale = std::abs (buf[N / 2]) > 1.0e-9f ? win[N / 2] / buf[N / 2] : 1.0f;

    struct Band { int lo, hi; };
    const Band bands[3] = { { 2, 7 }, { 14, 232 }, { 325, 511 } }; // ~40-150 Hz, 300-5000 Hz, 7-11 kHz
    for (int b = 0; b < 3; ++b)
    {
        out.pBand[(size_t) b].assign ((size_t) frames, 0.0f);
        out.hBand[(size_t) b].assign ((size_t) frames, 0.0f);
    }
    out.harmonic.assign (x.size(), 0.0f);
    std::vector<float> wsum (x.size(), 0.0f), tmp (2 * K + 1), mask (B);

    for (int t = 0; t < frames; ++t)
    {
        if ((t % 128) == 0 && ! prog (0.25f + 0.75f * (float) t / (float) frames))
            return false;

        const float* row = S.data() + (long) t * B;
        for (int k = 0; k < B; ++k)
        {
            for (int d = -K; d <= K; ++d) // outside the file = silence (so a hit at t=0 stays percussive)
                tmp[(size_t) (d + K)] = (t + d >= 0 && t + d < frames) ? S[(size_t) (t + d) * B + (size_t) k] : 0.0f;
            std::nth_element (tmp.begin(), tmp.begin() + K, tmp.end());
            const float H = tmp[K];
            for (int d = -K; d <= K; ++d)
                tmp[(size_t) (d + K)] = row[juce::jlimit (0, B - 1, k + d)];
            std::nth_element (tmp.begin(), tmp.begin() + K, tmp.end());
            const float P = tmp[K];

            const float m = (H * H) / (H * H + P * P + 1.0e-12f);
            mask[(size_t) k] = m;
            for (int b = 0; b < 3; ++b)
                if (k >= bands[b].lo && k <= bands[b].hi)
                {
                    out.pBand[(size_t) b][(size_t) t] += row[k] * (1.0f - m);
                    out.hBand[(size_t) b][(size_t) t] += row[k] * m;
                }
        }

        loadFrame (t);
        fft.performRealOnlyForwardTransform (buf.data(), false);
        for (int k = 0; k < N; ++k)
        {
            const float m = mask[(size_t) (k <= N / 2 ? k : N - k)];
            buf[(size_t) (2 * k)] *= m;
            buf[(size_t) (2 * k + 1)] *= m;
        }
        fft.performRealOnlyInverseTransform (buf.data());
        for (int i = 0; i < N; ++i)
        {
            const int idx = t * hop - N / 2 + i;
            if (idx >= 0 && idx < (int) x.size())
            {
                out.harmonic[(size_t) idx] += buf[(size_t) i] * scale * win[(size_t) i];
                wsum[(size_t) idx] += win[(size_t) i] * win[(size_t) i];
            }
        }
    }
    for (size_t i = 0; i < x.size(); ++i)
        out.harmonic[i] /= std::max (wsum[i], 0.5f);
    return true;
}

//==============================================================================
std::vector<Note> extractDrums (const HpssOut& h, const Settings& s)
{
    const double fps = kSr / kHpssHop;
    const float sens = juce::jlimit (0.0f, 1.0f, s.sensitivity);
    const int pitches[3] = { kKick, kSnare, kHat };
    std::array<std::vector<std::pair<int, float>>, 3> hits;
    std::array<float, 3> maxHit {};

    for (int b = 0; b < 3; ++b)
    {
        const auto& P = h.pBand[(size_t) b];
        const auto& H = h.hBand[(size_t) b];
        const int frames = (int) P.size();
        if (frames < 4)
            continue;
        const float maxP = *std::max_element (P.begin(), P.end());
        if (maxP <= 1.0e-9f)
            continue;

        std::vector<float> L ((size_t) frames), flux ((size_t) frames, 0.0f);
        for (int t = 0; t < frames; ++t)
            L[(size_t) t] = std::log10 (P[(size_t) t] / maxP + 1.0e-3f);
        flux[0] = std::max (0.0f, L[0] + 3.0f);
        for (int t = 1; t < frames; ++t)
            flux[(size_t) t] = std::max (0.0f, L[(size_t) t] - L[(size_t) t - 1]);

        const auto on = pickOnsets (flux, sens, fps);
        const float gate = juce::Decibels::decibelsToGain (-26.0f - 14.0f * sens) * maxP;
        const float minRatio = 0.45f - 0.12f * sens;

        for (int t = 0; t < frames; ++t)
        {
            if (! on[(size_t) t])
                continue;
            int pk = t;
            for (int k = t; k <= juce::jmin (frames - 1, t + 3); ++k)
                if (P[(size_t) k] > P[(size_t) pk]) pk = k;
            const float p = P[(size_t) pk], hv = H[(size_t) pk];
            if (p < gate || p / (p + hv + 1.0e-12f) < minRatio)
                continue;
            hits[(size_t) b].push_back ({ t, p });
            maxHit[(size_t) b] = std::max (maxHit[(size_t) b], p);
        }
    }

    auto near = [] (const std::vector<std::pair<int, float>>& v, int t, int r)
    {
        for (auto& e : v)
            if (std::abs (e.first - t) <= r) return true;
        return false;
    };
    auto strengthNear = [] (const std::vector<std::pair<int, float>>& v, int t, int r)
    {
        float m = 0.0f;
        for (auto& e : v)
            if (std::abs (e.first - t) <= r) m = std::max (m, e.second);
        return m;
    };

    // kick and snare bleed into each other's band: when they coincide keep the one
    // that is clearly stronger relative to its own band, keep both when both are strong
    std::array<std::vector<std::pair<int, float>>, 3> kept = hits;
    for (int b = 0; b < 2; ++b)
    {
        const int o = 1 - b;
        kept[(size_t) b].clear();
        for (auto& hit : hits[(size_t) b])
        {
            const float other = strengthNear (hits[(size_t) o], hit.first, 2);
            const float mine = hit.second / std::max (1.0e-12f, maxHit[(size_t) b]);
            const float theirs = other / std::max (1.0e-12f, maxHit[(size_t) o]);
            if (other > 0.0f && theirs > mine * 1.25f && mine < 0.8f)
                continue;
            kept[(size_t) b].push_back (hit);
        }
    }
    hits = kept;

    // a weak "snare" whose hat band is relatively stronger is an open/off-beat hi-hat
    {
        std::vector<std::pair<int, float>> snares;
        const auto& PH = h.pBand[2];
        const float maxHat = PH.empty() ? 0.0f : *std::max_element (PH.begin(), PH.end());
        for (auto& hit : hits[1])
        {
            const float rs = hit.second / std::max (1.0e-12f, maxHit[1]);
            float hv = 0.0f;
            for (int k = hit.first; k <= juce::jmin ((int) PH.size() - 1, hit.first + 3); ++k) hv = std::max (hv, PH[(size_t) k]);
            const float rh = hv / std::max (1.0e-12f, maxHat);
            if (rs < 0.5f && rh > rs * 1.2f)
            {
                if (! near (hits[2], hit.first, 2))
                    hits[2].push_back ({ hit.first, hv });
                continue;
            }
            snares.push_back (hit);
        }
        hits[1] = snares;
    }

    std::vector<Note> notes;
    for (int b = 0; b < 3; ++b)
        for (auto& [t, p] : hits[(size_t) b])
        {
            // kicks leak a click into the snare band and snares leak noise into the hat band
            if (b == 2 && near (hits[1], t, 2) && p < 0.5f * maxHit[2]) continue;

            Note n;
            n.pitch = pitches[b];
            n.start = std::max (0.0, t / fps + kOnsetLatency);
            n.end = n.start + 0.1;
            n.track = drums;
            n.strength = p;
            notes.push_back (n);
        }
    return notes;
}

//==============================================================================
// POLYPHONIC: spectral peaks -> harmonic salience -> iterative estimate & cancel
std::vector<Note> extractPoly (const std::vector<float>& x, const std::vector<float>& xOnset,
                               const Settings& s, const Prog& prog, int refAbove = -1)
{
    constexpr int N = 4096, order = 13, F = 1 << order, hop = 256;
    const double fps = kSr / hop;
    const double binHz = kSr / F;
    const int frames = (int) x.size() / hop + 1;
    const float sens = juce::jlimit (0.0f, 1.0f, s.sensitivity);

    juce::dsp::FFT fft (order);
    auto win = hann (N);
    const float ampScale = 2.0f / std::accumulate (win.begin(), win.end(), 0.0f);

    const float gateAmp = juce::Decibels::decibelsToGain (-48.0f - 24.0f * sens);
    const float relToBest = 0.32f - 0.24f * sens;
    const float relToFrameMax = 0.22f - 0.16f * sens;

    struct Peak { float freq, amp; };
    std::vector<Peak> peaks;
    std::vector<float> buf (2 * F), rem;
    std::vector<std::vector<float>> act ((size_t) frames, std::vector<float> (128, 0.0f));
    std::vector<std::vector<char>> held ((size_t) frames, std::vector<char> (128, 0)); // kept alive by hysteresis
    std::vector<std::vector<float>> famp ((size_t) frames, std::vector<float> (128, 0.0f)); // fundamental amplitude
    std::array<float, 128> fundPeak {};   // loudest fundamental of the note currently sounding on each pitch
    std::array<int, 128> lastOn;
    lastOn.fill (-1000);
    const int holdGap = (int) std::round (0.07 * (kSr / hop));
    const float contRatio = juce::Decibels::decibelsToGain (-14.0f - 4.0f * sens);
    float globalMaxSal = 0.0f;

    auto findPeak = [&] (double f, double tolSemis) -> int
    {
        auto it = std::lower_bound (peaks.begin(), peaks.end(), (float) f,
                                    [] (const Peak& p, float v) { return p.freq < v; });
        int best = -1;
        double bestD = tolSemis;
        for (auto j : { (int) (it - peaks.begin()) - 1, (int) (it - peaks.begin()) })
        {
            if (j < 0 || j >= (int) peaks.size())
                continue;
            const double d = std::abs (12.0 * std::log2 (peaks[(size_t) j].freq / f));
            if (d < bestD) { bestD = d; best = j; }
        }
        return best;
    };

    for (int t = 0; t < frames; ++t)
    {
        if ((t % 32) == 0 && ! prog (0.9f * (float) t / (float) frames))
            return {};

        const int start = t * hop - N / 2;
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int idx = start + i;
            if (idx >= 0 && idx < (int) x.size())
                buf[(size_t) i] = x[(size_t) idx] * win[(size_t) i];
        }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);

        peaks.clear();
        for (int k = 2; k < F / 2 - 1; ++k)
        {
            const float m = buf[(size_t) k];
            if (m * ampScale < gateAmp * 0.5f || m <= buf[(size_t) k - 1] || m < buf[(size_t) k + 1])
                continue;
            const float a = std::log (buf[(size_t) k - 1] + 1.0e-12f);
            const float b = std::log (m + 1.0e-12f);
            const float c = std::log (buf[(size_t) k + 1] + 1.0e-12f);
            const float den = a - 2.0f * b + c;
            const float p = std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f;
            const float freq = (float) ((k + p) * binHz);
            const float amp = std::exp (b - 0.25f * (a - c) * p) * ampScale;
            if (freq > 25.0f && freq < 6000.0f)
                peaks.push_back ({ freq, amp });
        }
        if (peaks.empty())
            continue;

        std::sort (peaks.begin(), peaks.end(), [] (const Peak& l, const Peak& r) { return l.amp > r.amp; });
        if (peaks.size() > 80)
            peaks.resize (80);
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& l, const Peak& r) { return l.freq < r.freq; });

        rem.resize (peaks.size());
        for (size_t i = 0; i < peaks.size(); ++i)
            rem[i] = peaks[i].amp;

        float firstSal = 0.0f;
        std::array<int, 12> hIdx {};
        std::array<float, 12> hAmp {};
        for (int iter = 0; iter < s.maxPolyphony; ++iter)
        {
            int best = -1;
            float bestSal = 0.0f;
            for (int p = s.minPitch; p <= s.maxPitch; ++p)
            {
                if (act[(size_t) t][(size_t) p] > 0.0f)
                    continue;
                const double f0 = mtof (p);
                const int fi = findPeak (f0, 0.45);
                if (fi < 0 || rem[(size_t) fi] < gateAmp)
                    continue;

                float sal = 0.0f, w = 1.0f;
                for (int h = 1; h <= 10; ++h, w *= 0.8f)
                {
                    const double fh = f0 * h;
                    if (fh > 6000.0)
                        break;
                    const int idx = (h == 1) ? fi : findPeak (fh, 0.5);
                    if (idx >= 0)
                        sal += rem[(size_t) idx] * w;
                }
                if (sal > bestSal) { bestSal = sal; best = p; }
            }

            if (best < 0)
                break;
            if (firstSal > 0.0f && bestSal < firstSal * relToBest)
                break;
            if (firstSal <= 0.0f && best > refAbove)
                firstSal = bestSal; // loudness reference (in multi mode the bass is not the reference)

            act[(size_t) t][(size_t) best] = bestSal;
            globalMaxSal = std::max (globalMaxSal, bestSal);

            const double f0 = mtof (best);
            hIdx.fill (-1);
            hAmp.fill (0.0f);
            for (int h = 1; h <= 10; ++h)
            {
                hIdx[(size_t) h] = findPeak (f0 * h, h == 1 ? 0.45 : 0.5);
                if (hIdx[(size_t) h] >= 0)
                    hAmp[(size_t) h] = rem[(size_t) hIdx[(size_t) h]];
            }
            // in multi mode the bass is tracked separately: only clear its strongest partials so
            // that its weak upper harmonics do not swallow the chord notes sitting on them
            const int lastH = best <= refAbove ? 3 : 10;
            for (int h = 1; h <= lastH; ++h)
            {
                const int idx = hIdx[(size_t) h];
                if (idx < 0)
                    continue;
                if (h == 1) { rem[(size_t) idx] = 0.0f; continue; }
                // spectral smoothness: a harmonic much louder than its neighbours also carries another note
                float nb = 0.0f;
                int cnt = 0;
                if (hAmp[(size_t) h - 1] > 0.0f) { nb += hAmp[(size_t) h - 1]; ++cnt; }
                if (h < 10 && hAmp[(size_t) h + 1] > 0.0f) { nb += hAmp[(size_t) h + 1]; ++cnt; }
                const float smooth = cnt > 0 ? nb / (float) cnt : hAmp[(size_t) h];
                const float expected = std::min (rem[(size_t) idx], 1.3f * smooth);
                rem[(size_t) idx] = std::max (0.0f, rem[(size_t) idx] - expected);
            }
        }

        // Hysteresis: once a note has started, it keeps sounding as long as its own fundamental
        // is still clearly there - even when a louder note dominates the frame (pad under a lead,
        // chorus beating, a new attack elsewhere). Starting a note still needs the strict test above.
        for (int p = s.minPitch; p <= s.maxPitch; ++p)
        {
            const int fi = findPeak (mtof (p), 0.45);
            const float fa = fi >= 0 ? peaks[(size_t) fi].amp : 0.0f;
            const float fres = fi >= 0 ? rem[(size_t) fi] : 0.0f; // not explained by the notes accepted in this frame
            famp[(size_t) t][(size_t) p] = fa;
            if (act[(size_t) t][(size_t) p] > 0.0f)
            {
                fundPeak[(size_t) p] = (t - lastOn[(size_t) p] > holdGap) ? fa : std::max (fundPeak[(size_t) p], fa);
                lastOn[(size_t) p] = t;
                continue;
            }
            if (t - lastOn[(size_t) p] > 2 || fi < 0)
                continue;
            if (fres >= std::max (gateAmp, contRatio * fundPeak[(size_t) p]))
            {
                held[(size_t) t][(size_t) p] = 1;
                lastOn[(size_t) p] = t;
            }
        }
    }

    for (auto& fr : act)
        for (auto& v : fr)
            if (v < globalMaxSal * relToFrameMax * 0.25f)
                v = 0.0f;
    // held frames only extend notes that really exist: walk forward from real activations
    for (int p = 0; p < 128; ++p)
    {
        bool alive = false;
        int since = 1000;
        for (int t = 0; t < frames; ++t)
        {
            if (act[(size_t) t][(size_t) p] > 0.0f) { alive = true; since = 0; continue; }
            if (alive && held[(size_t) t][(size_t) p]) { act[(size_t) t][(size_t) p] = 1.0e-9f; since = 0; continue; }
            if (++since > 2) alive = false;
        }
    }

    if (! prog (0.92f))
        return {};

    const auto flux = onsetStrength (xOnset, hop, frames);
    const auto onsets = pickOnsets (flux, sens, fps);

    const int minFrames = juce::jmax (2, (int) std::round (s.minNoteMs / 1000.0 * fps));
    const int maxGap = (int) std::round (0.07 * fps); // bridge flickering detections; re-attacks are split below
    const double quarterWin = (N / 4) / kSr;
    const int snapRadius = (int) std::round (0.07 * fps);

    // A real re-attack makes the note's own frequency jump within a few ms; chorus beating,
    // tremolo or another instrument's hit only change it slowly. Short-window check at f0.
    auto fastRise = [&] (int p, int o)
    {
        const double f0 = mtof (p);
        auto magAt = [&] (double centreSec)
        {
            constexpr int W = 1024;
            const long c = (long) std::llround (centreSec * kSr);
            double re = 0.0, im = 0.0;
            const double w = juce::MathConstants<double>::twoPi * f0 / kSr;
            for (int i = 0; i < W; ++i)
            {
                const long idx = c - W / 2 + i;
                if (idx < 0 || idx >= (long) xOnset.size()) continue;
                const double win = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / W);
                re += xOnset[(size_t) idx] * win * std::cos (w * i);
                im -= xOnset[(size_t) idx] * win * std::sin (w * i);
            }
            return std::sqrt (re * re + im * im);
        };
        const double t0 = o / fps + kOnsetLatency;
        const double before = magAt (t0 - 0.035), after = std::max (magAt (t0 + 0.03), magAt (t0 + 0.05));
        return after > 2.2 * std::max (before, 1.0e-9);
    };
    const int attackFrames = (int) std::round (0.09 * fps);

    std::vector<Note> notes;
    auto emit = [&] (int p, int a, int b)
    {
        if (b - a + 1 < minFrames)
            return;
        double st = a / fps + quarterWin;
        int stFrame = a;
        int o = -1;
        {
            const int c = (int) std::round (st * fps);
            for (int d = 0; d <= snapRadius * 2 && o < 0; ++d) // attacks precede the long-window detection
            {
                if (d <= snapRadius / 2 && c + d < (int) onsets.size() && onsets[(size_t) (c + d)]) o = c + d;
                else if (c - d >= 0 && c - d < (int) onsets.size() && onsets[(size_t) (c - d)]) o = c - d;
            }
        }
        if (o >= 0)
        {
            st = o / fps + kOnsetLatency;
            stFrame = o;
        }
        else
        {
            // soft attack (pad, swell, bowed...): walk back while the fundamental is still rising
            const float plateau = famp[(size_t) juce::jmin (b, a + 6)][(size_t) p];
            int k = a;
            const int limit = juce::jmax (0, a - (int) std::round (0.45 * fps));
            while (k - 1 >= limit && famp[(size_t) (k - 1)][(size_t) p] > 0.2f * plateau
                   && famp[(size_t) (k - 1)][(size_t) p] <= famp[(size_t) k][(size_t) p] * 1.05f)
                --k;
            st = std::min (st, k / fps + 0.25 * quarterWin);
        }
        // attack loudness = strongest salience in the first ~90 ms
        float attack = 0.0f;
        for (int t = juce::jmax (a, stFrame); t <= juce::jmin (b, juce::jmax (a, stFrame) + attackFrames); ++t)
            attack = std::max (attack, act[(size_t) t][(size_t) p]);
        if (attack <= 0.0f)
            for (int t = a; t <= b; ++t)
                attack = std::max (attack, act[(size_t) t][(size_t) p]);

        Note n;
        n.pitch = p;
        n.start = std::max (0.0, st);
        n.end = std::max (n.start + s.minNoteMs / 1000.0, b / fps - quarterWin * 0.5);
        n.strength = attack;
        notes.push_back (n);
    };

    for (int p = s.minPitch; p <= s.maxPitch; ++p)
    {
        int t = 0;
        while (t < frames)
        {
            if (act[(size_t) t][(size_t) p] <= 0.0f) { ++t; continue; }

            const int a = t;
            int b = t, gap = 0;
            for (int u = t + 1; u < frames; ++u)
            {
                if (act[(size_t) u][(size_t) p] > 0.0f) { b = u; gap = 0; }
                else if (++gap > maxGap) break;
            }

            int segStart = a;
            for (int o = a + minFrames; o <= b - minFrames; ++o)
            {
                if (! onsets[(size_t) o])
                    continue;
                float before = 1.0e9f, after = 0.0f, sofar = 0.0f;
                for (int k = juce::jmax (a, o - 4); k < o; ++k) before = std::min (before, act[(size_t) k][(size_t) p]);
                for (int k = o; k <= juce::jmin (b, o + 8); ++k) after = std::max (after, act[(size_t) k][(size_t) p]);
                for (int k = segStart; k < o; ++k) sofar = std::max (sofar, act[(size_t) k][(size_t) p]);
                // a real re-attack: the note had decayed (or stopped) and comes back clearly louder
                if (after > 2.0f * before && before < 0.6f * sofar && after > 0.5f * sofar && o - segStart >= minFrames
                    && fastRise (p, o))
                {
                    emit (p, segStart, o - 1 - (int) std::round (quarterWin * fps));
                    segStart = o - (int) std::round (quarterWin * fps);
                }
            }
            emit (p, segStart, b);
            t = b + 1;
        }
    }
    return notes;
}

//==============================================================================
// MONOPHONIC: YIN (FFT-accelerated) + segmentation
std::vector<Note> extractMono (const std::vector<float>& x, const Settings& s, const Prog& prog)
{
    constexpr int W = 1024, order = 12, F = 1 << order, hop = 128;
    const double fps = kSr / hop;
    const int frames = (int) x.size() / hop + 1;
    const float sens = juce::jlimit (0.0f, 1.0f, s.sensitivity);

    const int tauMin = juce::jmax (2, (int) std::floor (kSr / mtof (s.maxPitch + 1)));
    const int tauMax = juce::jmin (W - 2, (int) std::ceil (kSr / mtof (s.minPitch - 1)));
    const float yinThresh = 0.10f + 0.15f * sens;
    const float gateDb = -42.0f - 22.0f * sens;

    juce::dsp::FFT fft (order);
    std::vector<float> seg (2 * W), bx (2 * F), by (2 * F), r (2 * F), cum (2 * W + 1), d ((size_t) W), cmnd ((size_t) W);
    std::vector<float> midi ((size_t) frames, -1.0f), rmsDb ((size_t) frames, -120.0f);

    for (int t = 0; t < frames; ++t)
    {
        if ((t % 64) == 0 && ! prog (0.9f * (float) t / (float) frames))
            return {};

        const int s0 = t * hop - W;
        for (int i = 0; i < 2 * W; ++i)
        {
            const int idx = s0 + i;
            seg[(size_t) i] = (idx >= 0 && idx < (int) x.size()) ? x[(size_t) idx] : 0.0f;
        }

        double e = 0.0;
        for (int i = W / 2; i < W / 2 + W; ++i)
            e += seg[(size_t) i] * seg[(size_t) i];
        rmsDb[(size_t) t] = juce::Decibels::gainToDecibels ((float) std::sqrt (e / W), -120.0f);
        if (rmsDb[(size_t) t] < gateDb)
            continue;

        std::fill (bx.begin(), bx.end(), 0.0f);
        std::fill (by.begin(), by.end(), 0.0f);
        std::copy (seg.begin(), seg.end(), bx.begin());
        std::copy (seg.begin(), seg.begin() + W, by.begin());
        fft.performRealOnlyForwardTransform (bx.data(), false);
        fft.performRealOnlyForwardTransform (by.data(), false);
        for (int k = 0; k < F; ++k)
        {
            const float xr = bx[(size_t) (2 * k)], xi = bx[(size_t) (2 * k + 1)];
            const float yr = by[(size_t) (2 * k)], yi = by[(size_t) (2 * k + 1)];
            r[(size_t) (2 * k)] = yr * xr + yi * xi;
            r[(size_t) (2 * k + 1)] = yr * xi - yi * xr;
        }
        fft.performRealOnlyInverseTransform (r.data());

        cum[0] = 0.0f;
        for (int i = 0; i < 2 * W; ++i)
            cum[(size_t) i + 1] = cum[(size_t) i] + seg[(size_t) i] * seg[(size_t) i];
        const float e0 = cum[(size_t) W];
        if (e0 <= 1.0e-9f || std::abs (r[0]) < 1.0e-12f)
            continue;
        const float scale = e0 / r[0];

        float running = 0.0f;
        cmnd[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const float etau = cum[(size_t) (tau + W)] - cum[(size_t) tau];
            d[(size_t) tau] = std::max (0.0f, e0 + etau - 2.0f * r[(size_t) tau] * scale);
            running += d[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0f ? d[(size_t) tau] * (float) tau / running : 1.0f;
        }

        int tau = -1;
        for (int k = tauMin; k <= tauMax; ++k)
        {
            if (cmnd[(size_t) k] < yinThresh)
            {
                while (k + 1 <= tauMax && cmnd[(size_t) k + 1] < cmnd[(size_t) k])
                    ++k;
                tau = k;
                break;
            }
        }
        if (tau < 0)
            continue;

        double betterTau = tau;
        if (tau > 1 && tau < tauMax)
        {
            const float a = cmnd[(size_t) tau - 1], b = cmnd[(size_t) tau], c = cmnd[(size_t) tau + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1.0e-9f)
                betterTau = tau + 0.5 * (a - c) / den;
        }
        const double m = ftom (kSr / betterTau);
        if (m >= s.minPitch - 0.5 && m <= s.maxPitch + 0.5)
            midi[(size_t) t] = (float) m;
    }

    if (! prog (0.92f))
        return {};

    std::vector<float> med (midi);
    for (int t = 0; t < frames; ++t)
    {
        if (midi[(size_t) t] < 0.0f)
            continue;
        std::vector<float> v;
        for (int k = juce::jmax (0, t - 2); k <= juce::jmin (frames - 1, t + 2); ++k)
            if (midi[(size_t) k] > 0.0f)
                v.push_back (midi[(size_t) k]);
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        med[(size_t) t] = v[v.size() / 2];
    }

    const auto flux = onsetStrength (x, hop, frames);
    const auto onsets = pickOnsets (flux, sens, fps);

    const int minFrames = juce::jmax (2, (int) std::round (s.minNoteMs / 1000.0 * fps));
    const int maxUnvoiced = juce::jmax (2, (int) std::round (0.03 * fps));
    const int changeFrames = juce::jmax (2, (int) std::round (0.025 * fps));
    const int attackFrames = (int) std::round (0.06 * fps);

    std::vector<Note> notes;
    auto emit = [&] (int a, int b)
    {
        if (b - a + 1 < minFrames)
            return;
        std::vector<float> v;
        float attack = -120.0f;
        for (int t = a; t <= b; ++t)
        {
            if (med[(size_t) t] > 0.0f)
                v.push_back (med[(size_t) t]);
            if (t <= a + attackFrames)
                attack = std::max (attack, rmsDb[(size_t) t]);
        }
        if ((int) v.size() < minFrames / 2 + 1)
            return;
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        Note n;
        n.pitch = (int) std::round (v[v.size() / 2]);
        n.start = a / fps;
        // the pitch tracker needs a few frames to switch to a new note: use the real attack if close
        for (int d = 0; d <= (int) std::round (0.08 * fps); ++d)
        {
            if (a - d >= 0 && onsets[(size_t) (a - d)]) { n.start = (a - d) / fps + kOnsetLatency * 0.5; break; }
            if (d <= (int) std::round (0.02 * fps) && a + d < frames && onsets[(size_t) (a + d)]) { n.start = (a + d) / fps + kOnsetLatency * 0.5; break; }
        }
        n.start = std::max (0.0, n.start);
        n.end = (b + 1) / fps;
        n.strength = juce::Decibels::decibelsToGain (attack);
        notes.push_back (n);
    };

    // split a voiced segment on energy dips (repeated notes of the same pitch)
    const float dipDb = 6.0f - 3.0f * sens;
    std::function<void (int, int)> emitSplit = [&] (int a, int b)
    {
        int bestK = -1;
        float bestDepth = 0.0f;
        for (int k = a + minFrames; k <= b - minFrames; ++k)
        {
            if (rmsDb[(size_t) k] > rmsDb[(size_t) k - 1] || rmsDb[(size_t) k] > rmsDb[(size_t) k + 1])
                continue;
            float before = -120.0f, after = -120.0f;
            for (int j = a; j < k; ++j) before = std::max (before, rmsDb[(size_t) j]);
            for (int j = k; j <= juce::jmin (b, k + 20); ++j) after = std::max (after, rmsDb[(size_t) j]);
            const float depth = std::min (before, after) - rmsDb[(size_t) k];
            if (depth > bestDepth) { bestDepth = depth; bestK = k; }
        }
        if (bestK > 0 && bestDepth > dipDb)
        {
            emitSplit (a, bestK - 1);
            emitSplit (bestK, b);
        }
        else
            emit (a, b);
    };

    int noteStart = -1, lastVoiced = -1, unvoiced = 0, deviate = 0;
    float cur = 0.0f;
    for (int t = 0; t < frames; ++t)
    {
        const float m = med[(size_t) t];
        if (m <= 0.0f)
        {
            if (noteStart >= 0 && ++unvoiced > maxUnvoiced)
            {
                emitSplit (noteStart, lastVoiced);
                noteStart = -1;
            }
            continue;
        }

        unvoiced = 0;
        if (noteStart < 0)
        {
            noteStart = t; cur = std::round (m); deviate = 0;
        }
        else if (std::abs (m - cur) > 0.6f)
        {
            if (++deviate >= changeFrames)
            {
                emitSplit (noteStart, t - deviate);
                noteStart = t - deviate + 1;
                cur = std::round (m);
                deviate = 0;
            }
        }
        else
        {
            deviate = 0;
            if (onsets[(size_t) t] && t - noteStart >= minFrames && t >= 3
                && rmsDb[(size_t) juce::jmin (frames - 1, t + 2)] > rmsDb[(size_t) t - 3] + 3.0f)
            {
                emitSplit (noteStart, t - 1);
                noteStart = t;
            }
        }
        lastVoiced = t;
    }
    if (noteStart >= 0)
        emitSplit (noteStart, lastVoiced);

    // one line at a time: a note ends where the next one begins
    for (size_t i = 0; i + 1 < notes.size(); ++i)
        if (notes[i].end > notes[i + 1].start)
            notes[i].end = std::max (notes[i].start + 0.02, notes[i + 1].start);

    return notes;
}

//==============================================================================
// Removes what is not a played note:
//  - legato glitches: a very short note glued to a longer neighbour a semitone or two away
//    (scoops, vibrato) is merged into that neighbour
//  - harmonic ghosts: a short note sitting on an overtone (octave, 12th, 2 octaves, 17th...)
//    of a louder note sounding at the same time
//  - weak crumbs: short notes much quieter than the typical note of the file
void removeGhostNotes (std::vector<Note>& notes, const Settings& s)
{
    std::vector<size_t> pitched;
    for (size_t i = 0; i < notes.size(); ++i)
        if (notes[i].track != drums) pitched.push_back (i);
    if (pitched.size() < 2)
        return;

    const double shortSec = std::max (0.15, 2.0 * s.minNoteMs / 1000.0);
    std::vector<float> str;
    for (auto i : pitched) str.push_back (notes[i].strength);
    std::nth_element (str.begin(), str.begin() + (long) str.size() / 2, str.end());
    const float medianStrength = str[str.size() / 2];
    const float weakRatio = 0.4f - 0.25f * s.sensitivity;

    std::vector<bool> drop (notes.size(), false);
    for (auto i : pitched)
    {
        auto& n = notes[i];
        const double dur = n.end - n.start;
        // long ghost: sits on an overtone of a much louder note for most of its life
        if (dur >= shortSec)
        {
            for (auto j : pitched)
            {
                if (j == i || drop[j]) continue;
                const auto& m = notes[j];
                const int iv = n.pitch - m.pitch;
                const bool overtone = iv == 12 || iv == 19 || iv == 24 || iv == 28 || iv == 31 || iv == 34 || iv == 36;
                const double overlap = std::min (n.end, m.end) - std::max (n.start, m.start);
                if (overtone && overlap > 0.7 * dur && n.strength < 0.3f * m.strength)
                {
                    drop[i] = true;
                    break;
                }
            }
            continue;
        }

        // legato glitch -> give its time to the neighbour
        if (s.mode == Mode::mono)
            for (auto j : pitched)
            {
                if (j == i || drop[j]) continue;
                auto& m = notes[j];
                if (m.end - m.start <= dur || std::abs (m.pitch - n.pitch) > 2) continue;
                if (std::abs (m.start - n.end) < 0.03) { m.start = std::min (m.start, n.start); drop[i] = true; break; }
                if (std::abs (n.start - m.end) < 0.03) { m.end = std::max (m.end, n.end); drop[i] = true; break; }
            }
        if (drop[i]) continue;

        // overtone of a louder note sounding at the same time
        for (auto j : pitched)
        {
            if (j == i || drop[j]) continue;
            const auto& m = notes[j];
            const int iv = n.pitch - m.pitch;
            const bool overtone = iv == 12 || iv == 19 || iv == 24 || iv == 28 || iv == 31 || iv == 34 || iv == 36;
            if (overtone && m.start <= n.start + 0.03 && m.end >= n.start + dur * 0.5 && m.strength >= n.strength * 0.8f)
            {
                drop[i] = true;
                break;
            }
        }
        if (drop[i]) continue;

        if (n.strength < weakRatio * medianStrength)
            drop[i] = true;
    }
    std::vector<Note> kept;
    for (size_t i = 0; i < notes.size(); ++i)
        if (! drop[i]) kept.push_back (notes[i]);
    notes = std::move (kept);
}

//==============================================================================
// Bass = low register; melody = the top voice when it is not part of a block chord.
void assignLayers (std::vector<Note>& notes, const Settings& s)
{
    std::vector<Note*> pitched;
    for (auto& n : notes)
        if (n.track != drums)
            pitched.push_back (&n);
    if (pitched.empty())
        return;

    if (s.mode == Mode::mono)
    {
        std::vector<int> p;
        for (auto* n : pitched) p.push_back (n->pitch);
        std::nth_element (p.begin(), p.begin() + (long) p.size() / 2, p.end());
        const int t = p[p.size() / 2] <= s.bassSplit ? bass : melody;
        for (auto* n : pitched) n->track = t;
        return;
    }
    if (s.mode == Mode::poly)
    {
        for (auto* n : pitched) n->track = chords;
        return;
    }

    std::vector<Note*> v;
    for (auto* n : pitched)
    {
        if (n->pitch <= s.bassSplit) n->track = bass;
        else v.push_back (n);
    }
    std::sort (v.begin(), v.end(), [] (Note* a, Note* b) { return a->start < b->start; });

    size_t i = 0;
    while (i < v.size())
    {
        size_t j = i;
        while (j < v.size() && v[j]->start - v[i]->start < 0.06)
            ++j;
        std::vector<Note*> cl (v.begin() + (long) i, v.begin() + (long) j);
        std::sort (cl.begin(), cl.end(), [] (Note* a, Note* b) { return a->pitch > b->pitch; });

        for (auto* n : cl)
            n->track = chords;

        if (cl.size() >= 3)
        {
            if (cl[0]->pitch - cl[1]->pitch >= 7)
                cl[0]->track = melody;
        }
        else
        {
            bool highest = true;
            for (size_t k = 0; k < i; ++k)
                if (v[k]->end > cl[0]->start + 0.03 && v[k]->pitch >= cl[0]->pitch && v[k]->start > cl[0]->start - 8.0)
                    highest = false;
            if (highest)
                cl[0]->track = melody;
        }
        i = j;
    }
}

// Velocity from attack loudness, normalised per layer (and per drum piece).
void assignVelocities (std::vector<Note>& notes)
{
    std::map<int, std::vector<float>> groups;
    auto key = [] (const Note& n) { return n.track * 1000 + (n.track == drums ? n.pitch : 0); };
    for (auto& n : notes)
        groups[key (n)].push_back (n.strength);

    std::map<int, float> ref;
    for (auto& [k, v] : groups)
    {
        std::sort (v.begin(), v.end());
        ref[k] = std::max (1.0e-9f, v[(size_t) std::floor (0.9 * (double) (v.size() - 1))]);
    }
    for (auto& n : notes)
    {
        const float db = juce::Decibels::gainToDecibels (n.strength / ref[key (n)], -80.0f);
        n.velocity = juce::jlimit (1, 127, (int) std::round (juce::jmap (juce::jlimit (-24.0f, 4.0f, db), -24.0f, 4.0f, 28.0f, 127.0f)));
    }
}
} // namespace

//==============================================================================
Result analyse (const juce::AudioBuffer<float>& audio, double sampleRate, const Settings& settings,
                const ProgressFn& progress)
{
    Result r;
    if (audio.getNumSamples() == 0 || sampleRate <= 0.0)
        return r;
    r.lengthSec = audio.getNumSamples() / sampleRate;

    if (progress && ! progress (0.0f))
        return r;

    const auto x = prepare (audio, sampleRate);
    if (x.size() < 2048)
        return r;

    gTuning = 0.0;
    r.tuning = estimateTuning (x);
    gTuning = r.tuning;

    {
        const int hop = 256;
        const auto flux = onsetStrength (x, hop, (int) x.size() / hop + 1);
        r.bpm = detectTempo (flux, kSr / hop, r.lengthSec);
    }

    std::vector<Note> notes;
    switch (settings.mode)
    {
        case Mode::mono: notes = extractMono (x, settings, Prog { progress, 0.05f, 0.95f }); break;
        case Mode::poly: notes = extractPoly (x, x, settings, Prog { progress, 0.05f, 0.95f }); break;
        case Mode::multi:
        {
            HpssOut h;
            if (! hpss (x, h, Prog { progress, 0.05f, 0.45f }))
                return r;
            // bass: monophonic pitch tracking on the low-passed harmonic part
            std::vector<float> low (h.harmonic);
            {
                auto coeffs = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod (
                    (float) mtof (settings.bassSplit + 3), kSr, 8);
                for (auto* c : coeffs)
                {
                    juce::dsp::IIR::Filter<float> f (c);
                    for (auto& v : low)
                        v = f.processSample (v);
                }
            }
            auto bs = settings;
            bs.maxPitch = settings.bassSplit + 12;
            auto bassAll = extractMono (low, bs, Prog { progress, 0.45f, 0.6f });
            std::vector<Note> bassNotes;
            const float bassGate = juce::Decibels::decibelsToGain (-26.0f - 10.0f * settings.sensitivity);
            for (auto& n : bassAll)
                if (n.pitch <= settings.bassSplit && n.strength >= bassGate)
                {
                    n.track = bass;
                    bassNotes.push_back (n);
                }

            auto ps = settings;
            ps.maxPolyphony = settings.maxPolyphony + 1;
            auto poly = extractPoly (h.harmonic, x, ps, Prog { progress, 0.6f, 0.95f }, settings.bassSplit);

            // Is there a drum part at all? Drum loops put most of the high band in the percussive
            // component (hats, snare noise); melodic material keeps it mostly harmonic.
            auto share = [&h] (int b)
            {
                double P = 0.0, H = 0.0;
                for (size_t i = 0; i < h.pBand[(size_t) b].size(); ++i) { P += h.pBand[(size_t) b][i]; H += h.hBand[(size_t) b][i]; }
                return P / (P + H + 1.0e-12);
            };
            const double hatShare = share (2), snareShare = share (1);
            const bool drumPart = hatShare > 0.72 - 0.08 * settings.sensitivity || snareShare > 0.4;

            std::vector<Note> drumNotes;
            if (settings.drums && drumPart)
                drumNotes = extractDrums (h, settings);

            // the kick's pitch sweep pollutes the start of bass notes: drop those fragments
            // and move the real bass note back to the kick
            std::vector<double> kicks, hits;
            for (auto& d : drumNotes)
            {
                if (d.pitch == kKick) kicks.push_back (d.start);
                if (d.pitch != kHat) hits.push_back (d.start);
            }
            auto kickNear = [&] (double t, double before, double after) -> double
            {
                for (auto k : kicks)
                    if (t - k <= before && k - t <= after) return k;
                return -1.0;
            };
            std::sort (bassNotes.begin(), bassNotes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
            std::vector<Note> cleanBass;
            for (size_t i = 0; i < bassNotes.size(); ++i)
            {
                auto n = bassNotes[i];
                if (i + 1 < bassNotes.size() && n.end - n.start < 0.35 && kickNear (n.start, 0.2, 0.04) >= 0.0
                    && bassNotes[i + 1].start - n.end < 0.1)
                {
                    bassNotes[i + 1].start = n.start; // fragment -> belongs to the next note
                    bassNotes[i + 1].strength = std::max (bassNotes[i + 1].strength, n.strength);
                    continue;
                }
                double k = kickNear (n.start, 0.3, 0.0);
                if (k < 0.0)
                    for (auto h : hits) // otherwise align on a kick/snare just before
                        if (n.start - h >= 0.0 && n.start - h < 0.08) k = h;
                const double prevStart = cleanBass.empty() ? -1.0 : cleanBass.back().start;
                if (k >= 0.0 && k > prevStart + 0.05)
                {
                    n.start = k;
                    if (! cleanBass.empty() && cleanBass.back().end > k)
                        cleanBass.back().end = k; // the previous note's release overlapped the new attack
                }
                cleanBass.push_back (n);
            }
            bassNotes = cleanBass;

            notes = bassNotes;
            for (auto& n : poly)
            {
                if (n.pitch <= settings.bassSplit)
                    continue;
                const bool shortNote = n.end - n.start < 0.12;
                bool ghost = false;
                if (shortNote)
                {
                    for (auto& b : bassNotes) // octave / fifth "ghosts" of the bass attack
                    {
                        const int iv = n.pitch - b.pitch;
                        if (b.start <= n.start + 0.05 && b.end > n.start
                            && (iv == 12 || iv == 19 || iv == 24 || iv == 28 || iv == 31))
                            ghost = true;
                    }
                    for (auto& d : drumNotes) // tonal residue of drum hits
                        if (std::abs (d.start - n.start) < 0.035)
                            ghost = true;
                }
                if (! ghost)
                    notes.push_back (n);
            }
            notes.insert (notes.end(), drumNotes.begin(), drumNotes.end());
            break;
        }
    }
    if (progress && ! progress (0.96f))
        return r;

    removeGhostNotes (notes, settings);
    assignLayers (notes, settings);
    assignVelocities (notes);
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
               { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    r.notes = std::move (notes);

    if (progress)
        progress (1.0f);
    return r;
}

double matchTempoOctave (double detected, double session)
{
    if (detected <= 0.0 || session <= 0.0)
        return detected;
    double best = detected, bestD = 1.0e9;
    for (double f : { 0.5, 1.0, 2.0 })
    {
        const double d = std::abs (std::log2 (detected * f / session));
        if (d < bestD) { bestD = d; best = detected * f; }
    }
    return best;
}

juce::String trackName (int t)
{
    switch (t)
    {
        case melody: return fr ("Mélodie");
        case chords: return "Accords";
        case bass:   return "Basse";
        case drums:  return "Batterie";
        default:     return "Tout";
    }
}

int trackChannel (int t)
{
    static const int ch[] = { 1, 2, 3, 10 };
    return ch[juce::jlimit (0, 3, t)];
}

juce::String noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}

juce::String drumName (int n)
{
    if (n == kKick) return "Kick";
    if (n == kSnare) return "Snare";
    if (n == kHat) return "Hi-hat";
    return noteName (n);
}
} // namespace mex
