#include "SoundModel.h"
#include <algorithm>
#include <cmath>

namespace mex
{
namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;

double median (std::vector<double> v, double fallback)
{
    if (v.empty()) return fallback;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

// Hann-windowed single-frequency magnitude (Goertzel-style DFT bin) around a centre sample
float toneMag (const std::vector<float>& x, double sr, double centreSec, int N, double freq)
{
    const long c = (long) std::llround (centreSec * sr);
    double re = 0.0, im = 0.0, wsum = 0.0;
    const double w = twoPi * freq / sr;
    for (int i = 0; i < N; ++i)
    {
        const long idx = c - N / 2 + i;
        if (idx < 0 || idx >= (long) x.size()) continue;
        const double win = 0.5 - 0.5 * std::cos (twoPi * i / N);
        re += x[(size_t) idx] * win * std::cos (w * i);
        im -= x[(size_t) idx] * win * std::sin (w * i);
        wsum += win;
    }
    return wsum > 0.0 ? (float) (2.0 * std::sqrt (re * re + im * im) / wsum) : 0.0f;
}

// tolerant to slight inharmonicity / detune: best of three probes
float partialMag (const std::vector<float>& x, double sr, double t, int N, double f)
{
    float m = 0.0f;
    for (double k : { 0.994, 1.0, 1.006 })
        m = std::max (m, toneMag (x, sr, t, N, f * k));
    return m;
}

bool cutSlice (const std::vector<float>& x, double sr, double from, double to, juce::AudioBuffer<float>& out, float* originalPeak = nullptr)
{
    long a = std::max (0L, (long) (from * sr)), b = std::min ((long) x.size(), (long) (to * sr));
    if (b - a < (long) (0.03 * sr)) return false;
    float peak = 0.0f;
    for (long i = a; i < b; ++i) peak = std::max (peak, std::abs (x[(size_t) i]));
    if (peak < 1.0e-5f) return false;
    if (originalPeak != nullptr) *originalPeak = peak / 0.8f;
    // trim silence before the attack, keep 1 ms of pre-roll
    long s = a;
    while (s < b && std::abs (x[(size_t) s]) < 0.02f * peak) ++s;
    a = std::max (a, s - (long) (0.001 * sr));

    const int n = (int) (b - a);
    out.setSize (1, n);
    auto* d = out.getWritePointer (0);
    const int fin = std::max (1, (int) (0.002 * sr)), fout = std::max (1, std::min (n / 4, (int) (0.02 * sr)));
    for (int i = 0; i < n; ++i)
    {
        float g = 0.8f / peak;
        if (i < fin) g *= (float) i / fin;
        if (i > n - fout) g *= (float) (n - i) / fout;
        d[i] = x[(size_t) (a + i)] * g;
    }
    return true;
}
} // namespace

//==============================================================================
SoundKit buildSoundKit (const juce::AudioBuffer<float>& audio, double sr, const std::vector<Note>& notes, double tuning)
{
    SoundKit kit;
    const int n = audio.getNumSamples();
    if (n == 0 || sr <= 0.0)
        return kit;

    std::vector<float> x ((size_t) n, 0.0f);
    for (int c = 0; c < audio.getNumChannels(); ++c)
    {
        auto* d = audio.getReadPointer (c);
        for (int i = 0; i < n; ++i) x[(size_t) i] += d[i] / (float) audio.getNumChannels();
    }
    const double len = n / sr;
    const int N = juce::nextPowerOfTwo ((int) (sr * 0.09)); // ~93 ms analysis window
    auto f0Of = [tuning] (int p) { return 440.0 * std::pow (2.0, (p + tuning - 69.0) / 12.0); };

    auto nextOnsetAfter = [&] (double t, const Note* except)
    {
        double best = len;
        for (auto& o : notes)
            if (&o != except && o.start > t + 0.01) best = std::min (best, o.start);
        return best;
    };

    //== pitched layers
    for (int t = 0; t < 3; ++t)
    {
        auto& L = kit.layers[(size_t) t];
        std::vector<const Note*> mine;
        for (auto& o : notes)
            if (o.track == t && o.end - o.start > 0.06) mine.push_back (&o);
        if (mine.empty())
            continue;

        // 1. a clean note to sample: nothing else sounds while it rings
        const Note* clean = nullptr;
        double bestScore = 0.0;
        for (auto* m : mine)
        {
            const double win = std::min (m->end - m->start, 0.8) + 0.03;
            bool alone = true;
            for (auto& o : notes)
                if (&o != m && o.start < m->start + win && o.end > m->start - 0.02) { alone = false; break; }
            const double dur = m->end - m->start;
            if (alone && dur >= 0.12)
            {
                const double score = std::min (dur, 1.5) * (0.5 + m->velocity / 127.0);
                if (score > bestScore) { bestScore = score; clean = m; }
            }
        }
        if (clean != nullptr)
        {
            const double to = std::min ({ clean->end + 0.35, nextOnsetAfter (clean->start, clean) - 0.004, clean->start + 4.0 });
            if (cutSlice (x, sr, clean->start - 0.012, to, L.slice, &L.sliceGain))
            {
                L.hasSlice = true;
                L.sliceRate = sr;
                L.rootPitch = clean->pitch;
                const int sl = L.slice.getNumSamples();
                if (sl > (int) (0.5 * sr) && clean->end - clean->start > 0.4)
                {
                    L.loopStart = (int) (0.35 * sl);
                    L.loopEnd = (int) (0.8 * sl);
                }
            }
        }

        // 2. additive model from the strongest / longest notes
        std::sort (mine.begin(), mine.end(), [] (const Note* a, const Note* b)
                   { return (a->end - a->start) * a->velocity > (b->end - b->start) * b->velocity; });
        if (mine.size() > 16) mine.resize (16);

        std::array<std::vector<double>, 32> harmDb;
        std::vector<double> attacks, decays, releases, brights, fundAmps;
        const double hop = 0.01;
        for (auto* m : mine)
        {
            const double f0 = f0Of (m->pitch);
            const double dur = m->end - m->start;
            const double tEarly = m->start + std::min (0.06, dur * 0.3);
            const float a1 = partialMag (x, sr, tEarly, N, f0);
            if (a1 <= 1.0e-6f) continue;
            fundAmps.push_back (a1);
            std::array<double, 32> early {};
            for (int h = 1; h <= 32; ++h)
            {
                if (f0 * h > 0.45 * sr) break;
                const float a = partialMag (x, sr, tEarly, N, f0 * h);
                early[(size_t) h - 1] = 20.0 * std::log10 (std::max (1.0e-6, (double) a / a1));
                harmDb[(size_t) h - 1].push_back (early[(size_t) h - 1]);
            }

            // envelope of the fundamental
            const double tEnd = std::min ({ m->end + 0.35, nextOnsetAfter (m->end - 0.01, m), len });
            std::vector<double> env;
            for (double tt = m->start; tt < tEnd; tt += hop)
                env.push_back (20.0 * std::log10 (std::max (1.0e-7f, toneMag (x, sr, tt, N, f0))));
            if (env.size() < 4) continue;
            const size_t pk = (size_t) (std::max_element (env.begin(), env.begin() + (long) std::min (env.size(), (size_t) 30)) - env.begin());
            attacks.push_back (std::max (0.001, pk * hop));
            const size_t endIdx = std::min (env.size() - 1, (size_t) ((m->end - m->start) / hop));
            if (endIdx > pk + 3) // dB/s slope while held (least squares)
            {
                double sx = 0, sy = 0, sxx = 0, sxy = 0, k = 0;
                for (size_t i = pk; i <= endIdx; ++i, ++k) { sx += i * hop; sy += env[i]; sxx += i * hop * i * hop; sxy += i * hop * env[i]; }
                const double slope = (k * sxy - sx * sy) / std::max (1.0e-9, k * sxx - sx * sx);
                decays.push_back (juce::jlimit (0.0, 80.0, -slope));
            }
            if (env.size() > endIdx + 5)
            {
                const double drop = env[endIdx] - env[std::min (env.size() - 1, endIdx + 15)];
                const double dt = (std::min (env.size() - 1, endIdx + 15) - endIdx) * hop;
                if (drop > 1.0) releases.push_back (juce::jlimit (0.02, 3.0, 60.0 / (drop / dt)));
            }
            if (dur > 0.25) // upper partials dying faster than the fundamental
            {
                const double tLate = m->start + dur * 0.65;
                const float l1 = partialMag (x, sr, tLate, N, f0);
                if (l1 > 1.0e-6f)
                {
                    double acc = 0.0;
                    int cnt = 0;
                    for (int h = 2; h <= 8 && f0 * h < 0.45 * sr; ++h)
                    {
                        const double late = 20.0 * std::log10 (std::max (1.0e-6, (double) partialMag (x, sr, tLate, N, f0 * h) / l1));
                        acc += (early[(size_t) h - 1] - late) / (h - 1);
                        ++cnt;
                    }
                    if (cnt > 0) brights.push_back (juce::jlimit (0.0, 30.0, acc / cnt / (tLate - tEarly)));
                }
            }
        }

        if (! harmDb[0].empty())
        {
            L.valid = true;
            double lastDb = 0.0;
            for (int h = 0; h < 32; ++h)
            {
                double db = harmDb[(size_t) h].size() >= 2 ? median (harmDb[(size_t) h], lastDb - 6.0) : lastDb - 6.0;
                db = std::max (db, -70.0);
                L.harm[(size_t) h] = (float) std::pow (10.0, db / 20.0);
                lastDb = db;
            }
            L.harm[0] = 1.0f;
            L.attack = (float) juce::jlimit (0.001, 0.25, median (attacks, 0.005));
            L.decayDb = (float) median (decays, 6.0);
            L.release = (float) median (releases, 0.15);
            L.brightDecay = (float) median (brights, 3.0);
            L.fundAmp = (float) median (fundAmps, 0.1);
        }
    }

    //== drums: the cleanest real hit of each piece
    const int pieces[3] = { kKick, kSnare, kHat };
    for (int k = 0; k < 3; ++k)
    {
        const Note* best = nullptr;
        double bestScore = -1.0;
        for (auto& o : notes)
        {
            if (o.track != drums || o.pitch != pieces[k]) continue;
            double score = o.velocity;
            for (auto& q : notes)
                if (&q != &o && std::abs (q.start - o.start) < 0.035)
                    score -= q.track == drums ? 80.0 : 25.0; // something else hits at the same time
            if (score > bestScore) { bestScore = score; best = &o; }
        }
        if (best == nullptr) continue;
        double nextHit = len;
        for (auto& q : notes)
            if (q.track == drums && q.start > best->start + 0.02) nextHit = std::min (nextHit, q.start);
        const double to = std::min (best->start + (k == 2 ? 0.25 : 0.6), nextHit - 0.003);
        if (cutSlice (x, sr, best->start - 0.012, to, kit.drums.hit[(size_t) k], &kit.drums.gain[(size_t) k]))
            kit.drums.has[(size_t) k] = true;
    }
    kit.drums.rate = sr;
    return kit;
}

void prepareTables (SoundKit& kit, double outRate)
{
    for (auto& L : kit.layers)
    {
        L.tables.clear();
        if (! L.valid) continue;
        for (int o = 0; o < 11; ++o)
        {
            const double fTop = 440.0 * std::pow (2.0, (12.0 * o + 11.0 - 69.0) / 12.0) * 1.06;
            const int maxH = juce::jlimit (1, 32, (int) std::floor (0.45 * outRate / fTop));
            std::array<std::vector<float>, 2> t { std::vector<float> (kTableSize + 1, 0.0f), std::vector<float> (kTableSize + 1, 0.0f) };
            for (int h = 0; h < maxH; ++h)
            {
                const float ab = L.harm[(size_t) h];
                const float ad = ab * (float) std::pow (10.0, -L.brightDecay * h * 1.0 / 20.0); // after ~1 s
                if (ab < 1.0e-4f) continue;
                for (int i = 0; i <= kTableSize; ++i)
                {
                    const float s = (float) std::sin (twoPi * (h + 1) * i / kTableSize);
                    t[0][(size_t) i] += ab * s;
                    t[1][(size_t) i] += ad * s;
                }
            }
            float pk = 1.0e-6f;
            for (auto v : t[0]) pk = std::max (pk, std::abs (v));
            for (auto& tab : t)
                for (auto& v : tab) v /= pk;
            if (o == 5) L.tableGain = pk; // reference octave
            L.tables.push_back (std::move (t));
        }
    }
    kit.tablesForRate = outRate;
}

juce::String SoundKit::describe() const
{
    juce::StringArray parts;
    for (int t = 0; t < 3; ++t)
    {
        const auto& L = layers[(size_t) t];
        if (L.hasSlice) parts.add (trackName (t) + fr (" : échantillonné"));
        else if (L.valid) parts.add (trackName (t) + fr (" : modélisé"));
    }
    const int d = (int) drums.has[0] + (int) drums.has[1] + (int) drums.has[2];
    if (d > 0) parts.add ("Batterie : " + juce::String (d) + fr ("/3 coups réels"));
    return parts.joinIntoString (fr (" · "));
}
} // namespace mex
