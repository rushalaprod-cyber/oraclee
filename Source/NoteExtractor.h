#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <vector>

namespace mex
{
inline juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

// Instrument layers ("pistes")
enum Track { melody = 0, chords = 1, bass = 2, drums = 3, numTracks = 4 };

juce::String trackName (int track);       // "Mélodie", "Accords", "Basse", "Batterie"
int trackChannel (int track);             // 1, 2, 3, 10
constexpr int kKick = 36, kSnare = 38, kHat = 42; // GM drum notes

struct Note
{
    int pitch = 60;      // MIDI note number
    double start = 0.0;  // seconds (in the sample)
    double end = 0.0;    // seconds
    int velocity = 100;  // 1..127
    int track = melody;
    float strength = 0.0f; // raw attack loudness, used to derive velocity
    bool selected = false; // editor state (piano roll)
};

enum class Mode
{
    multi = 0, // separate drums / bass / chords / melody
    mono = 1,  // a single melody line (voice, bass, lead) - YIN pitch tracking
    poly = 2   // chords / piano on one track
};

struct Settings
{
    Mode mode = Mode::multi;
    float sensitivity = 0.5f; // 0 = only obvious notes, 1 = catch everything
    float minNoteMs = 70.0f;
    int minPitch = 28;  // E1
    int maxPitch = 100; // E7
    int maxPolyphony = 6;
    int bassSplit = 50; // notes <= this pitch go to the bass layer (multi mode)
    bool drums = true;  // detect drums (multi mode)
};

struct Result
{
    std::vector<Note> notes;
    double bpm = 0.0;       // detected tempo of the sample, 0 = no clear tempo
    double tuning = 0.0;    // detected detune in semitones (+0.35 = 35 cents sharp)
    double lengthSec = 0.0;
};

// Return false from the callback to cancel the analysis.
using ProgressFn = std::function<bool (float)>;

Result analyse (const juce::AudioBuffer<float>& audio, double sampleRate, const Settings& settings,
                const ProgressFn& progress = {});

// Choose bpm, bpm/2 or bpm*2 - whichever is closest to the session tempo.
double matchTempoOctave (double detectedBpm, double sessionBpm);

juce::String noteName (int midiNote);
juce::String drumName (int midiNote);
} // namespace mex
