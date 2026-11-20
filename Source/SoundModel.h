#pragma once

#include "NoteExtractor.h"
#include <array>
#include <memory>

// "Adaptive" preview instrument: learns how each layer of the sample sounds so the
// extracted MIDI can be played back with something close to the original.
//  - a clean, isolated note of the layer  -> sampled and re-pitched (best quality)
//  - otherwise                             -> additive model: measured harmonic profile,
//                                             attack / decay / release, high-harmonic damping
//  - drums                                 -> the real kick / snare / hat hits, sliced
namespace mex
{
struct LayerTimbre
{
    bool valid = false;
    std::array<float, 32> harm {};      // harmonic amplitudes, harm[0] (fundamental) = 1
    float attack = 0.005f;              // seconds
    float decayDb = 6.0f;               // dB per second while held
    float release = 0.15f;              // seconds to -60 dB after note-off
    float brightDecay = 3.0f;           // extra dB/s per harmonic step (upper partials die faster)
    float fundAmp = 0.1f;               // typical fundamental amplitude in the original (level matching)
    float tableGain = 1.0f;             // peak of the additive waveform relative to its fundamental
    float sliceGain = 1.0f;             // restores the slice to its original level

    bool hasSlice = false;              // sampled note
    juce::AudioBuffer<float> slice;     // mono
    double sliceRate = 44100.0;
    int rootPitch = 60;
    int loopStart = 0, loopEnd = 0;     // sustain loop (samples), 0 = none

    // wavetables for the additive model, per octave: [octave][0 = bright, 1 = dark]
    std::vector<std::array<std::vector<float>, 2>> tables;
};

struct DrumHits
{
    std::array<juce::AudioBuffer<float>, 3> hit; // kick, snare, hat (mono)
    std::array<bool, 3> has {};
    std::array<float, 3> gain { 1.0f, 1.0f, 1.0f };
    double rate = 44100.0;
};

struct SoundKit
{
    std::array<LayerTimbre, 3> layers; // melody, chords, bass
    DrumHits drums;
    double tablesForRate = 0.0;
    juce::String describe() const;     // e.g. "Mélodie : échantillon · Accords : modélisé …"
};

SoundKit buildSoundKit (const juce::AudioBuffer<float>& audio, double sampleRate,
                        const std::vector<Note>& notes, double tuning);

// (re)builds the wavetables for a given output sample rate (message thread)
void prepareTables (SoundKit&, double outputSampleRate);

constexpr int kTableSize = 2048;
} // namespace mex
