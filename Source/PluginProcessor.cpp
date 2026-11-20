#include "PluginProcessor.h"
#include "PluginEditor.h"

using mex::fr;

namespace
{
//==============================================================================
// Preview instruments: a soft keys sound for pitched layers, a tiny drum machine on channel 10.
// One sound per layer (selected by MIDI channel); the voice plays it with the adaptive kit.
struct LayerSound : juce::SynthesiserSound
{
    explicit LayerSound (int l) : layer (l) {}
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int ch) override { return ch == mex::trackChannel (layer); }
    const int layer;
};

struct KitVoice : juce::SynthesiserVoice
{
    KitVoice (const std::shared_ptr<const mex::SoundKit>* src, std::atomic<float>* adaptiveParam)
        : kitSource (src), adaptive (adaptiveParam) {}

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<LayerSound*> (s) != nullptr; }

    void startNote (int note, float vel, juce::SynthesiserSound* s, int) override
    {
        layer = static_cast<LayerSound*> (s)->layer;
        pitch = note;
        velocity = vel;
        kit = adaptive->load() > 0.5f ? std::atomic_load (kitSource) : nullptr;
        t = 0.0; pos = 0.0; phase = 0.0; prevNoise = 0.0f; releasing = false; relGain = 1.0f; relCoef = 1.0f;
        timbre = nullptr; table = nullptr; hit = nullptr;
        const double sr = getSampleRate();
        mode = Mode::basic;
        if (kit != nullptr)
        {
            if (layer == mex::drums)
            {
                const int k = note == mex::kKick ? 0 : note == mex::kSnare ? 1 : 2;
                if (kit->drums.has[(size_t) k]) { mode = Mode::drumHit; hit = &kit->drums.hit[(size_t) k]; rate = kit->drums.rate / sr; hitGain = kit->drums.gain[(size_t) k]; }
            }
            else
            {
                const auto& L = kit->layers[(size_t) layer];
                if (L.hasSlice)
                {
                    mode = Mode::slice;
                    rate = std::pow (2.0, (note - L.rootPitch) / 12.0) * L.sliceRate / sr;
                    timbre = &L;
                }
                else if (L.valid && ! L.tables.empty() && std::abs (kit->tablesForRate - sr) < 1.0)
                {
                    mode = Mode::additive;
                    timbre = &L;
                    table = &L.tables[(size_t) juce::jlimit (0, (int) L.tables.size() - 1, note / 12)];
                    inc = juce::MidiMessage::getMidiNoteInHertz (note) / sr * mex::kTableSize;
                }
            }
        }
        if (mode == Mode::basic)
        {
            inc = juce::MidiMessage::getMidiNoteInHertz (note) * juce::MathConstants<double>::twoPi / sr;
            adsr.setSampleRate (sr);
            adsr.setParameters ({ 0.004f, 0.6f, 0.45f, 0.12f });
            adsr.noteOn();
        }
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (! allowTailOff) { clearCurrentNote(); kit.reset(); return; }
        if (mode == Mode::drumHit || (mode == Mode::basic && layer == mex::drums)) return; // one-shots ring out
        releasing = true;
        const double relSec = timbre != nullptr ? std::max (0.03, (double) timbre->release) : 0.12;
        relCoef = (float) std::exp (std::log (0.001) / (relSec * getSampleRate()));
        if (mode == Mode::basic) adsr.noteOff();
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive())
            return;
        const double sr = getSampleRate(), dt = 1.0 / sr;
        // level matched to the original: the kit stores how loud a typical (strong) note was
        const float velGain = std::pow (velocity / 0.85f, 1.3f);
        float gain = 0.5f * std::pow (velocity, 1.3f);
        if (mode == Mode::slice) gain = timbre->sliceGain * velGain;
        else if (mode == Mode::additive) gain = timbre->fundAmp * timbre->tableGain * velGain / 0.6f;
        else if (mode == Mode::drumHit) gain = hitGain * velGain / 1.1f;
        for (int i = 0; i < num; ++i)
        {
            float s = 0.0f;
            bool done = false;
            switch (mode)
            {
                case Mode::drumHit:
                {
                    const int n = hit->getNumSamples();
                    const int i0 = (int) pos;
                    if (i0 + 1 >= n) { done = true; break; }
                    const float* d = hit->getReadPointer (0);
                    s = (d[i0] + (float) (pos - i0) * (d[i0 + 1] - d[i0])) * 1.1f * gain;
                    pos += rate;
                    break;
                }
                case Mode::slice:
                {
                    const auto& L = *timbre;
                    const int n = L.slice.getNumSamples();
                    const float* d = L.slice.getReadPointer (0);
                    const bool looping = ! releasing && L.loopEnd > L.loopStart;
                    if (looping && pos >= L.loopEnd)
                        pos -= (L.loopEnd - L.loopStart); // sustain loop while the key is held
                    const int i0 = (int) pos;
                    if (i0 + 1 >= n) { done = true; break; }
                    s = d[i0] + (float) (pos - i0) * (d[i0 + 1] - d[i0]);
                    if (looping) // short crossfade before the loop point
                    {
                        const double xf = std::min (0.02 * L.sliceRate, (L.loopEnd - L.loopStart) * 0.25);
                        if (pos > L.loopEnd - xf)
                        {
                            const double back = pos - (L.loopEnd - L.loopStart);
                            const int j0 = (int) back;
                            const float b = d[j0] + (float) (back - j0) * (d[j0 + 1] - d[j0]);
                            const float a = (float) ((pos - (L.loopEnd - xf)) / xf);
                            s = s * (1.0f - a) + b * a;
                        }
                    }
                    s *= gain;
                    pos += rate;
                    break;
                }
                case Mode::additive:
                {
                    const auto& L = *timbre;
                    const auto& tb = *table;
                    const int i0 = (int) phase;
                    const float fr = (float) (phase - i0);
                    const float b = tb[0][(size_t) i0] + fr * (tb[0][(size_t) i0 + 1] - tb[0][(size_t) i0]);
                    const float dk = tb[1][(size_t) i0] + fr * (tb[1][(size_t) i0 + 1] - tb[1][(size_t) i0]);
                    const float mix = (float) (1.0 - std::exp (-t)); // bright attack -> darker sustain
                    const float att = (float) std::min (1.0, t / std::max (0.001f, L.attack));
                    const float dec = (float) std::pow (10.0, -L.decayDb * t / 20.0);
                    s = (b + (dk - b) * mix) * att * dec * gain * 0.6f;
                    phase += inc;
                    if (phase >= mex::kTableSize) phase -= mex::kTableSize;
                    if (dec < 0.001f) done = true;
                    break;
                }
                case Mode::basic:
                {
                    if (layer == mex::drums)
                    {
                        const float noise = rng.nextFloat() * 2.0f - 1.0f;
                        if (pitch == mex::kKick)
                        {
                            phase += (48.0 + 110.0 * std::exp (-t * 35.0)) * juce::MathConstants<double>::twoPi * dt;
                            s = (float) (std::sin (phase) * std::exp (-t * 9.0)) * 1.3f;
                        }
                        else if (pitch == mex::kSnare)
                            s = noise * (float) std::exp (-t * 20.0) * 0.6f
                              + (float) (std::sin (t * 185.0 * juce::MathConstants<double>::twoPi) * std::exp (-t * 28.0)) * 0.4f;
                        else
                        {
                            s = (noise - prevNoise) * (float) std::exp (-t * 70.0) * 0.3f;
                            prevNoise = noise;
                        }
                        s *= 0.4f * gain;
                        if (t > 0.6) done = true;
                    }
                    else
                    {
                        if (! adsr.isActive()) { done = true; break; }
                        s = (float) (std::sin (phase) + 0.3 * std::sin (2.0 * phase) + 0.12 * std::sin (3.0 * phase)) * 0.2f * gain * adsr.getNextSample();
                        phase += inc;
                        if (phase > juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi;
                    }
                    break;
                }
            }
            if (done) { clearCurrentNote(); kit.reset(); break; }
            if (releasing && mode != Mode::basic)
            {
                relGain *= relCoef;
                s *= relGain;
                if (relGain < 0.001f) { clearCurrentNote(); kit.reset(); break; }
            }
            t += dt;
            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addSample (c, start + i, s);
        }
    }

    enum class Mode { basic, additive, slice, drumHit };
    const std::shared_ptr<const mex::SoundKit>* kitSource;
    std::atomic<float>* adaptive;
    std::shared_ptr<const mex::SoundKit> kit;
    const mex::LayerTimbre* timbre = nullptr;
    const std::array<std::vector<float>, 2>* table = nullptr;
    const juce::AudioBuffer<float>* hit = nullptr;
    Mode mode = Mode::basic;
    int layer = 0, pitch = 60;
    float velocity = 1.0f, relGain = 1.0f, relCoef = 1.0f, prevNoise = 0.0f, hitGain = 1.0f;
    double t = 0.0, pos = 0.0, rate = 1.0, phase = 0.0, inc = 0.0;
    bool releasing = false;
    juce::ADSR adsr;
    juce::Random rng;
};

const char* kAnalysisParams[] = { "mode", "sensitivity", "minNote", "lowNote", "highNote", "polyphony", "bassSplit", "drums" };
} // namespace

//==============================================================================
Sample2MidiProcessor::Sample2MidiProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formatManager.registerBasicFormats();

    for (int i = 0; i < 24; ++i)
        synth.addVoice (new KitVoice (&kitAudio, apvts.getRawParameterValue ("adaptive")));
    for (int l = 0; l < mex::numTracks; ++l)
        synth.addSound (new LayerSound (l));

    for (auto* id : kAnalysisParams)
        apvts.addParameterListener (id, this);

    startTimerHz (20);
}

Sample2MidiProcessor::~Sample2MidiProcessor()
{
    aliveFlag->store (false);
    stopTimer();
    for (auto* id : kAnalysisParams)
        apvts.removeParameterListener (id, this);
    ++generation;
    pool.removeAllJobs (true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout Sample2MidiProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto boolP = [&] (const String& id, const String& name, bool def) { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                   StringArray { "Multi-instruments", fr ("Mélodie seule (mono)"), "Accords (poly)" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sensitivity", 1 }, fr ("Sensibilité"),
                                                  NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "minNote", 1 }, fr ("Durée min (ms)"),
                                                  NormalisableRange<float> (20.0f, 400.0f, 1.0f, 0.6f), 70.0f));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "lowNote", 1 }, "Note la plus grave", 21, 72, 28));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "highNote", 1 }, fr ("Note la plus aiguë"), 48, 108, 100));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "polyphony", 1 }, "Polyphonie max", 1, 10, 6));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "bassSplit", 1 }, fr ("Limite basse"), 36, 64, 50));
    boolP ("drums", fr ("Détecter la batterie"), true);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dynamics", 1 }, fr ("Vélocité (dynamique)"),
                                                  NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "quantize", 1 }, "Quantize",
                                                   StringArray { "Off", "1/4", "1/8", "1/16", "1/32" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "BPM session (manuel)",
                                                  NormalisableRange<float> (40.0f, 240.0f, 0.01f), 120.0f));
    boolP ("syncHost", "Tempo du DAW", true);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sampleBpm", 1 }, "BPM du sample",
                                                  NormalisableRange<float> (40.0f, 240.0f, 0.01f), 120.0f));
    boolP ("sampleBpmAuto", fr ("BPM du sample auto"), true);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "timing", 1 }, "Calage",
                                                   StringArray { "Caler sur le tempo de la session", fr ("Garder le temps réel") }, 0));
    boolP ("followHost", "Jouer avec le DAW", false);
    boolP ("loop", "Boucler", true);
    for (int t = 0; t < mex::numTracks; ++t)
        boolP ("layer" + String (t), "Piste " + mex::trackName (t), true);
    boolP ("adaptive", fr ("Synthé adaptatif"), true);
    boolP ("playOriginal", fr ("Écouter le sample"), false);
    boolP ("playSynth", fr ("Écouter le MIDI"), true);
    return l;
}

const juce::StringArray& Sample2MidiProcessor::supportedExtensions()
{
    static const juce::StringArray ext { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a", ".caf" };
    return ext;
}

bool Sample2MidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void Sample2MidiProcessor::prepareToPlay (double sampleRate, int)
{
    hostSr = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    expectedPpq = -1.0;
    if (kit != nullptr && std::abs (kit->tablesForRate - sampleRate) > 1.0)
        juce::MessageManager::callAsync ([alive = aliveFlag, this, sampleRate]
        {
            if (! alive->load() || kit == nullptr) return;
            auto k = std::make_shared<mex::SoundKit> (*kit);
            mex::prepareTables (*k, sampleRate);
            kit = k;
            std::atomic_store (&kitAudio, std::shared_ptr<const mex::SoundKit> (k));
        });
}

void Sample2MidiProcessor::allNotesOff (juce::MidiBuffer& midi, int offset)
{
    for (int ch = 1; ch <= 16; ++ch)
        for (int p = 0; p < 128; ++p)
            if (activeOut[(size_t) ch][(size_t) p])
            {
                midi.addEvent (juce::MidiMessage::noteOff (ch, p), offset);
                activeOut[(size_t) ch][(size_t) p] = false;
            }
}

//==============================================================================
void Sample2MidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    midi.clear();
    buffer.clear();

    bool transportRolling = false, hasPpq = false;
    double ppq0 = 0.0, bpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { bpm = *b; hostBpm.store (bpm); }
            if (auto p = pos->getPpqPosition()) { ppq0 = *p; hasPpq = true; }
            transportRolling = pos->getIsPlaying();
        }
    hostPlaying.store (transportRolling);

    if (stopRequested.exchange (false))
    {
        allNotesOff (midi, 0);
        synth.allNotesOff (0, true);
    }
    if (restartRequested.exchange (false))
    {
        allNotesOff (midi, 0);
        synth.allNotesOff (0, false);
        playPos = playFrom.load() * hostSr;
    }

    const bool follow = apvts.getRawParameterValue ("followHost")->load() > 0.5f;
    const bool loop = apvts.getRawParameterValue ("loop")->load() > 0.5f;
    const bool wantSynth = apvts.getRawParameterValue ("playSynth")->load() > 0.5f;
    const bool wantOriginal = apvts.getRawParameterValue ("playOriginal")->load() > 0.5f;

    //== 1. MIDI in sync with the DAW timeline (bar 1 = start of the sample)
    if (follow && transportRolling && hasPpq && bpm > 0.0)
    {
        if (playing.exchange (false)) // the DAW takes over from the preview
        {
            allNotesOff (midi, 0);
            synth.allNotesOff (0, true);
        }

        const double ppqPerSample = bpm / 60.0 / hostSr;
        const double ppq1 = ppq0 + n * ppqPerSample;
        if (! wasHostPlaying || std::abs (ppq0 - expectedPpq) > 0.01) // start or jump
            allNotesOff (midi, 0);
        expectedPpq = ppq1;
        wasHostPlaying = true;

        juce::SpinLock::ScopedTryLockType lock (audioLock);
        if (lock.isLocked() && ! hostNotes.empty())
        {
            const double L = loop ? juce::jmax (1.0, patternBeats) : 1.0e9;
            const long k0 = loop ? (long) std::floor (ppq0 / L) : 0;
            const long k1 = loop ? (long) std::floor (ppq1 / L) : 0;
            auto offsetOf = [&] (double beat) { return juce::jlimit (0, n - 1, (int) ((beat - ppq0) / ppqPerSample)); };

            for (long k = k0; k <= k1; ++k)
            {
                const double base = (double) k * L;
                for (auto& e : hostNotes)
                {
                    const double end = base + std::min (e.end, L - 1.0e-4);
                    if (end >= ppq0 && end < ppq1 && activeOut[(size_t) e.channel][(size_t) e.pitch])
                    {
                        midi.addEvent (juce::MidiMessage::noteOff (e.channel, e.pitch), offsetOf (end));
                        activeOut[(size_t) e.channel][(size_t) e.pitch] = false;
                    }
                }
                for (auto& e : hostNotes)
                {
                    if (e.start >= L)
                        continue;
                    const double st = base + e.start;
                    if (st >= ppq0 && st < ppq1)
                    {
                        midi.addEvent (juce::MidiMessage::noteOn (e.channel, e.pitch, (juce::uint8) e.velocity), offsetOf (st));
                        activeOut[(size_t) e.channel][(size_t) e.pitch] = true;
                    }
                }
            }
        }
    }
    else if (wasHostPlaying)
    {
        allNotesOff (midi, 0);
        synth.allNotesOff (0, true);
        wasHostPlaying = false;
    }

    //== 2. Preview of the sample ("Lecture" button)
    double length = 0.0;
    const bool previewing = playing.load();
    if (previewing)
    {
        const double t0 = playPos / hostSr, t1 = (playPos + n) / hostSr;
        juce::SpinLock::ScopedTryLockType lock (audioLock);
        if (lock.isLocked())
        {
            auto offsetOf = [&] (double t) { return juce::jlimit (0, n - 1, (int) ((t - t0) * hostSr)); };
            for (auto& e : previewNotes)
                if (e.end >= t0 && e.end < t1 && activeOut[(size_t) e.channel][(size_t) e.pitch])
                {
                    midi.addEvent (juce::MidiMessage::noteOff (e.channel, e.pitch), offsetOf (e.end));
                    activeOut[(size_t) e.channel][(size_t) e.pitch] = false;
                }
            for (auto& e : previewNotes)
                if (e.start >= t0 && e.start < t1)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (e.channel, e.pitch, (juce::uint8) e.velocity), offsetOf (e.start));
                    activeOut[(size_t) e.channel][(size_t) e.pitch] = true;
                }

            if (playSample != nullptr)
            {
                length = playSample->getNumSamples() / playSampleRate;
                if (wantOriginal)
                {
                    const double ratio = playSampleRate / hostSr;
                    const int srcLen = playSample->getNumSamples();
                    const int srcCh = playSample->getNumChannels();
                    for (int c = 0; c < buffer.getNumChannels(); ++c)
                    {
                        auto* src = playSample->getReadPointer (juce::jmin (c, srcCh - 1));
                        auto* dst = buffer.getWritePointer (c);
                        for (int i = 0; i < n; ++i)
                        {
                            const double sp = (playPos + i) * ratio;
                            const int i0 = (int) sp;
                            if (i0 + 1 >= srcLen)
                                break;
                            const float frac = (float) (sp - i0);
                            dst[i] += 0.8f * (src[i0] + frac * (src[i0 + 1] - src[i0]));
                        }
                    }
                }
            }
        }
    }

    //== 3. auditions from the piano roll (synth only, not sent to the DAW)
    juce::MidiBuffer synthMidi;
    if (wantSynth)
        synthMidi.addEvents (midi, 0, n, 0);
    for (auto& po : pendingOffs)
        if (po.pitch >= 0 && (po.samplesLeft -= n) <= 0)
        {
            synthMidi.addEvent (juce::MidiMessage::noteOff (po.channel, po.pitch), n - 1);
            po.pitch = -1;
        }
    {
        int s1, n1, s2, n2;
        previewFifo.prepareToRead (previewFifo.getNumReady(), s1, n1, s2, n2);
        auto take = [&] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto& e = previewEvents[(size_t) (start + i)];
                synthMidi.addEvent (juce::MidiMessage::noteOn (e.channel, e.pitch, (juce::uint8) e.velocity), 0);
                for (auto& po : pendingOffs)
                    if (po.pitch < 0) { po = { e.pitch, e.channel, (int) (0.35 * hostSr) }; break; }
            }
        };
        take (s1, n1);
        take (s2, n2);
        previewFifo.finishedRead (n1 + n2);
    }
    synth.renderNextBlock (buffer, synthMidi, 0, n);

    if (previewing)
    {
        playPos += n;
        if (length > 0.0 && playPos / hostSr > length + 0.05)
        {
            allNotesOff (midi, n - 1);
            synth.allNotesOff (0, true);
            if (loop)
                playPos = 0.0; // loop the preview
            else
            {
                playing.store (false);
                playPos = playFrom.load() * hostSr;
            }
        }
        playPosSec.store (playPos / hostSr);
    }
}

//==============================================================================
bool Sample2MidiProcessor::loadSample (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        error = fr ("Format non reconnu : ") + file.getFileName();
        return false;
    }
    if (reader->lengthInSamples > (juce::int64) (600.0 * reader->sampleRate))
    {
        error = "Sample trop long (max 10 minutes).";
        return false;
    }

    auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) juce::jmin (2u, reader->numChannels),
                                                         (int) reader->lengthInSamples);
    reader->read (buf.get(), 0, buf->getNumSamples(), 0, true, buf->getNumChannels() > 1);

    stop();
    sample = buf;
    fileSampleRate = reader->sampleRate;
    currentFile = file;
    {
        std::shared_ptr<const juce::AudioBuffer<float>> old;
        {
            const juce::SpinLock::ScopedLockType lock (audioLock);
            old = std::exchange (playSample, sample);
            playSampleRate = fileSampleRate;
        }
    }

    notes.clear();
    detectedBpm = 0.0;
    playbackSignature.clear();
    reanalyse();
    return true;
}

double Sample2MidiProcessor::getSampleLengthSeconds() const
{
    return sample != nullptr ? sample->getNumSamples() / fileSampleRate : 0.0;
}

mex::Settings Sample2MidiProcessor::currentSettings() const
{
    mex::Settings s;
    const int m = (int) apvts.getRawParameterValue ("mode")->load();
    s.mode = m == 1 ? mex::Mode::mono : m == 2 ? mex::Mode::poly : mex::Mode::multi;
    s.sensitivity = apvts.getRawParameterValue ("sensitivity")->load();
    s.minNoteMs = apvts.getRawParameterValue ("minNote")->load();
    s.minPitch = (int) apvts.getRawParameterValue ("lowNote")->load();
    s.maxPitch = juce::jmax (s.minPitch + 12, (int) apvts.getRawParameterValue ("highNote")->load());
    s.maxPolyphony = (int) apvts.getRawParameterValue ("polyphony")->load();
    s.bassSplit = (int) apvts.getRawParameterValue ("bassSplit")->load();
    s.drums = apvts.getRawParameterValue ("drums")->load() > 0.5f;
    return s;
}

void Sample2MidiProcessor::reanalyse()
{
    if (sample == nullptr)
        return;

    paramsDirty = false;
    const int gen = ++generation;
    analysing = true;
    progress = 0.0f;
    sendChangeMessage();

    auto audio = sample;
    const double sr = fileSampleRate;
    const auto settings = currentSettings();

    pool.addJob ([this, audio, sr, settings, gen]
    {
        auto result = mex::analyse (*audio, sr, settings, [this, gen] (float p)
        {
            progress.store (p * 0.92f);
            return generation.load() == gen;
        });
        if (generation.load() != gen)
            return;
        auto newKit = std::make_shared<mex::SoundKit> (mex::buildSoundKit (*audio, sr, result.notes, result.tuning));
        mex::prepareTables (*newKit, hostSr);
        progress.store (1.0f);

        if (generation.load() == gen)
        {
            const juce::ScopedLock sl (resultLock);
            pendingResult = std::move (result);
            pendingKit = newKit;
            pendingGeneration = gen;
        }
    });
}

void Sample2MidiProcessor::parameterChanged (const juce::String&, float)
{
    dirtySince = juce::Time::getMillisecondCounter();
    paramsDirty = true;
}

//==============================================================================
double Sample2MidiProcessor::getSessionBpm() const
{
    if (apvts.getRawParameterValue ("syncHost")->load() > 0.5f && hostBpm.load() > 0.0)
        return hostBpm.load();
    return apvts.getRawParameterValue ("bpm")->load();
}

double Sample2MidiProcessor::getSampleBpm() const
{
    return apvts.getRawParameterValue ("sampleBpm")->load();
}

bool Sample2MidiProcessor::isConforming() const
{
    return apvts.getRawParameterValue ("timing")->load() < 0.5f;
}

bool Sample2MidiProcessor::isLayerOn (int layer) const
{
    return apvts.getRawParameterValue ("layer" + juce::String (layer))->load() > 0.5f;
}

int Sample2MidiProcessor::countNotes (int layer) const
{
    int c = 0;
    for (auto& n : notes)
        if (layer < 0 ? isLayerOn (n.track) : n.track == layer)
            ++c;
    return c;
}

mex::ExportOptions Sample2MidiProcessor::exportOptions (int layer) const
{
    static const int divs[] = { 0, 4, 8, 16, 32 };
    mex::ExportOptions o;
    o.sessionBpm = getSessionBpm();
    o.sampleBpm = isConforming() ? getSampleBpm() : 0.0;
    o.gridDivision = divs[juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("quantize")->load())];
    o.dynamics = apvts.getRawParameterValue ("dynamics")->load();
    if (layer >= 0)
        o.layerMask = 1u << layer;
    else
    {
        o.layerMask = 0;
        for (int t = 0; t < mex::numTracks; ++t)
            if (isLayerOn (t)) o.layerMask |= 1u << t;
    }
    const auto base = currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension() : juce::String ("oraclee");
    o.name = layer >= 0 ? base + " - " + mex::trackName (layer) : base;
    return o;
}

void Sample2MidiProcessor::rebuildPlaybackData()
{
    const auto o = exportOptions (-1);
    std::vector<PlayNote> host, preview;
    for (auto& b : mex::toBeatNotes (notes, o))
        host.push_back ({ b.start, b.end, b.pitch, b.channel, b.velocity });
    for (auto& nt : notes)
        if (o.layerMask & (1u << nt.track))
            preview.push_back ({ nt.start, nt.end, nt.pitch, mex::trackChannel (nt.track),
                                 juce::jlimit (1, 127, (int) std::round (100.0f + (nt.velocity - 100.0f) * o.dynamics)) });

    const double lenBeats = mex::secondsToBeats (getSampleLengthSeconds(), o);
    const double pattern = juce::jmax (4.0, std::round (lenBeats / 4.0) * 4.0);

    const juce::SpinLock::ScopedLockType lock (audioLock);
    std::swap (hostNotes, host);
    std::swap (previewNotes, preview);
    patternBeats = pattern;
}

void Sample2MidiProcessor::timerCallback()
{
    if (paramsDirty.load() && juce::Time::getMillisecondCounter() - dirtySince > 300)
        reanalyse();

    bool got = false;
    {
        const juce::ScopedLock sl (resultLock);
        if (pendingGeneration == generation.load())
        {
            if (hasRestoredNotes) // a saved session: keep the retouched notes
            {
                notes = std::move (restoredNotes);
                hasRestoredNotes = false;
                userEdited = true;
            }
            else
            {
                if (userEdited) // re-analysis replaces the retouches - keep them undoable
                {
                    undoStack.push_back (notes);
                    redoStack.clear();
                }
                notes = std::move (pendingResult.notes);
                userEdited = false;
            }
            ++editVersion;
            if (pendingKit != nullptr)
            {
                kit = pendingKit;
                std::atomic_store (&kitAudio, std::shared_ptr<const mex::SoundKit> (pendingKit));
                pendingKit.reset();
            }
            detectedBpm = pendingResult.bpm;
            tuning = pendingResult.tuning;
            pendingResult = {};
            pendingGeneration = -1;
            got = true;
        }
    }
    if (got)
    {
        analysing = false;
        progress = 1.0f;
        playbackSignature.clear();
    }

    // keep the sample tempo matched to the session (bpm, x2 or /2) while in auto mode
    if (apvts.getRawParameterValue ("sampleBpmAuto")->load() > 0.5f && detectedBpm > 0.0)
    {
        const double want = mex::matchTempoOctave (detectedBpm, getSessionBpm());
        if (std::abs (want - getSampleBpm()) > 0.005)
            if (auto* p = apvts.getParameter ("sampleBpm"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) want));
    }

    const auto o = exportOptions (-1);
    const juce::String sig = juce::String (editVersion) + "|" + juce::String ((int) notes.size()) + "|" + juce::String (o.sessionBpm, 3) + "|"
                           + juce::String (o.sampleBpm, 3) + "|" + juce::String (o.gridDivision) + "|"
                           + juce::String (o.dynamics, 3) + "|" + juce::String ((int) o.layerMask);
    if (sig != playbackSignature)
    {
        playbackSignature = sig;
        rebuildPlaybackData();
        sendChangeMessage();
    }
    else if (got)
        sendChangeMessage();
}

//==============================================================================
void Sample2MidiProcessor::play (double fromSeconds)
{
    if (sample == nullptr)
        return;
    playFrom = juce::jlimit (0.0, getSampleLengthSeconds(), fromSeconds);
    playPosSec = playFrom.load();
    restartRequested = true;
    playing = true;
}

//==============================================================================
void Sample2MidiProcessor::beginEdit()
{
    undoStack.push_back (notes);
    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void Sample2MidiProcessor::endEdit()
{
    std::sort (notes.begin(), notes.end(), [] (const mex::Note& a, const mex::Note& b)
               { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    userEdited = true;
    ++editVersion;
    playbackSignature.clear();
    timerCallback();
}

void Sample2MidiProcessor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (notes);
    notes = std::move (undoStack.back());
    undoStack.pop_back();
    userEdited = true;
    ++editVersion;
    playbackSignature.clear();
    timerCallback();
}

void Sample2MidiProcessor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (notes);
    notes = std::move (redoStack.back());
    redoStack.pop_back();
    userEdited = true;
    ++editVersion;
    playbackSignature.clear();
    timerCallback();
}

void Sample2MidiProcessor::previewNote (int pitch, int channel, int velocity)
{
    int s1, n1, s2, n2;
    previewFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
        previewEvents[(size_t) s1] = { juce::jlimit (0, 127, pitch), juce::jlimit (1, 16, channel), juce::jlimit (1, 127, velocity) };
    previewFifo.finishedWrite (n1);
}

double Sample2MidiProcessor::getBeatSeconds() const
{
    const double bpm = isConforming() ? getSampleBpm() : getSessionBpm();
    return 60.0 / juce::jlimit (20.0, 400.0, bpm);
}

bool Sample2MidiProcessor::importMidi (const juce::File& f, int targetLayer)
{
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
        return false;
    const int ppq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
    const double secPerBeat = getBeatSeconds();

    std::vector<mex::Note> imported;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* ev = seq.getEventPointer (i);
            if (! ev->message.isNoteOn())
                continue;
            const double on = ev->message.getTimeStamp() / ppq * secPerBeat;
            const double off = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() / ppq * secPerBeat : on + 0.25 * secPerBeat;
            mex::Note n;
            n.pitch = ev->message.getNoteNumber();
            n.start = on;
            n.end = std::max (on + 0.01, off);
            n.velocity = ev->message.getVelocity();
            const int ch = ev->message.getChannel();
            n.track = ch == 10 ? mex::drums : ch == 1 ? mex::melody : ch == 2 ? mex::chords : ch == 3 ? mex::bass
                                                                                                    : juce::jlimit (0, 3, targetLayer < 0 ? 0 : targetLayer);
            imported.push_back (n);
        }
    }
    if (imported.empty())
        return false;
    beginEdit();
    notes.insert (notes.end(), imported.begin(), imported.end());
    endEdit();
    return true;
}

void Sample2MidiProcessor::stop()
{
    if (playing.exchange (false))
        stopRequested = true;
    playPosSec = 0.0;
}

bool Sample2MidiProcessor::exportMidi (const juce::File& dest, int layer) const
{
    return mex::writeMidiFile (notes, exportOptions (layer), dest);
}

juce::Array<juce::File> Sample2MidiProcessor::exportAll (const juce::File& dest) const
{
    juce::Array<juce::File> written;
    if (exportMidi (dest, -1))
        written.add (dest);
    for (int t = 0; t < mex::numTracks; ++t)
    {
        if (! isLayerOn (t) || countNotes (t) == 0)
            continue;
        auto f = dest.getSiblingFile (juce::File::createLegalFileName (dest.getFileNameWithoutExtension() + " - " + mex::trackName (t) + ".mid"));
        if (exportMidi (f, t))
            written.add (f);
    }
    return written;
}

juce::File Sample2MidiProcessor::writeTempMidi (int layer) const
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("oraclee");
    dir.createDirectory();
    const auto base = currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension() : juce::String ("sample");
    const auto suffix = layer >= 0 ? " - " + mex::trackName (layer) : juce::String (" (MIDI)");
    auto f = dir.getChildFile (juce::File::createLegalFileName (base + suffix + ".mid"));
    return exportMidi (f, layer) ? f : juce::File();
}

//==============================================================================
void Sample2MidiProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("file", currentFile.getFullPathName(), nullptr);
    state.removeChild (state.getChildWithName ("NOTES"), nullptr);
    if (userEdited)
    {
        juce::ValueTree nv ("NOTES");
        for (auto& n : notes)
        {
            juce::ValueTree e ("N");
            e.setProperty ("p", n.pitch, nullptr);
            e.setProperty ("s", n.start, nullptr);
            e.setProperty ("e", n.end, nullptr);
            e.setProperty ("v", n.velocity, nullptr);
            e.setProperty ("t", n.track, nullptr);
            nv.appendChild (e, nullptr);
        }
        state.appendChild (nv, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void Sample2MidiProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid())
            return;
        const juce::String path = state.getProperty ("file").toString();
        if (auto nv = state.getChildWithName ("NOTES"); nv.isValid())
        {
            restoredNotes.clear();
            for (auto e : nv)
                restoredNotes.push_back ({ (int) e["p"], (double) e["s"], (double) e["e"], (int) e["v"], (int) e["t"], 0.0f });
            hasRestoredNotes = true;
        }
        apvts.replaceState (state);

        if (path.isNotEmpty())
            juce::MessageManager::callAsync ([alive = aliveFlag, this, path]
            {
                if (! alive->load())
                    return;
                juce::File f (path);
                juce::String err;
                if (f.existsAsFile() && f != currentFile)
                    loadSample (f, err);
            });
    }
}

juce::AudioProcessorEditor* Sample2MidiProcessor::createEditor()
{
    return new Sample2MidiEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Sample2MidiProcessor();
}
