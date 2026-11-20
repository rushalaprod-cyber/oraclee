// sample2midi <audio> <out.mid> [multi|mono|poly] [session bpm=120] [sensitivity=0.5] [quantize=0]
// Writes <out.mid> (all instruments, one track each) conformed to the session tempo.
#include "NoteExtractor.h"
#include "MidiExport.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "usage: sample2midi <audio> <out.mid> [multi|mono|poly] [session bpm=120] [sensitivity=0.5] [quantize=0]\n";
        return 1;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const juce::File in = cwd.getChildFile (argv[1]), out = cwd.getChildFile (argv[2]);

    mex::Settings s;
    if (argc > 3)
    {
        const juce::String m (argv[3]);
        s.mode = m == "mono" ? mex::Mode::mono : m == "poly" ? mex::Mode::poly : mex::Mode::multi;
    }
    const double session = argc > 4 ? juce::String (argv[4]).getDoubleValue() : 120.0;
    if (argc > 5) s.sensitivity = juce::String (argv[5]).getFloatValue();
    const int quant = argc > 6 ? juce::String (argv[6]).getIntValue() : 0;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in));
    if (reader == nullptr)
    {
        std::cerr << "cannot read " << in.getFullPathName() << "\n";
        return 1;
    }
    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto r = mex::analyse (buf, reader->sampleRate, s);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    mex::ExportOptions o;
    o.sessionBpm = session;
    o.sampleBpm = mex::matchTempoOctave (r.bpm, session);
    o.gridDivision = quant;
    o.name = in.getFileNameWithoutExtension();

    std::cout << "BPM detecte: " << r.bpm << " accordage " << (int) std::round (r.tuning * 100) << " cents" << "  -> cale a " << o.sampleBpm << " (session " << session << ")\n";
    for (auto& n : r.notes)
        std::cout << juce::String (n.start, 3) << "\t" << juce::String (n.end, 3) << "\t"
                  << mex::trackName (n.track).paddedRight (' ', 9) << "\t"
                  << (n.track == mex::drums ? mex::drumName (n.pitch) : mex::noteName (n.pitch)) << "\tvel " << n.velocity
                  << "\tbeat " << juce::String (mex::secondsToBeats (n.start, o), 2) << "\n";
    int count[4] {};
    for (auto& n : r.notes) ++count[n.track];
    std::cout << r.notes.size() << " notes (melodie " << count[0] << ", accords " << count[1] << ", basse " << count[2]
              << ", batterie " << count[3] << ") en " << (int) ms << " ms\n";

    if (! mex::writeMidiFile (r.notes, o, out))
    {
        std::cerr << "cannot write " << out.getFullPathName() << "\n";
        return 1;
    }
    return 0;
}
