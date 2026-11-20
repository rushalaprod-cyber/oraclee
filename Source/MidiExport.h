#pragma once

#include "NoteExtractor.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace mex
{
struct ExportOptions
{
    double sessionBpm = 120.0; // tempo written in the file / used by the DAW
    double sampleBpm = 0.0;    // > 0: conform - one beat of the sample = one beat of the session
                               // = 0: keep real time (seconds) at the session tempo
    int gridDivision = 0;      // 0 = no quantize, else 4 / 8 / 16 / 32
    float dynamics = 1.0f;     // 0 = every velocity 100, 1 = detected velocities
    unsigned layerMask = 0xF;  // bit per Track
    juce::String name;
};

struct BeatNote
{
    int pitch, channel, velocity, track;
    double start, end; // in beats (quarter notes) from the start of the sample
};

double secondsToBeats (double seconds, const ExportOptions&);
std::vector<BeatNote> toBeatNotes (const std::vector<Note>& notes, const ExportOptions&);

// Type-1 file: a tempo track, then one named track per instrument (MIDI channel 1/2/3/10).
juce::MidiFile makeMidiFile (const std::vector<Note>& notes, const ExportOptions&);
bool writeMidiFile (const std::vector<Note>& notes, const ExportOptions&, const juce::File& destination);
} // namespace mex
