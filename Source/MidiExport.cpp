#include "MidiExport.h"
#include <cmath>
#include <tuple>

namespace mex
{
double secondsToBeats (double t, const ExportOptions& o)
{
    const double bpm = o.sampleBpm > 0.0 ? o.sampleBpm : o.sessionBpm;
    return t * juce::jlimit (20.0, 400.0, bpm) / 60.0;
}

std::vector<BeatNote> toBeatNotes (const std::vector<Note>& notes, const ExportOptions& o)
{
    const double grid = o.gridDivision > 0 ? 4.0 / o.gridDivision : 0.0;
    std::vector<BeatNote> out;

    for (auto& n : notes)
    {
        if ((o.layerMask & (1u << n.track)) == 0)
            continue;

        double s = secondsToBeats (n.start, o), e = secondsToBeats (n.end, o);
        if (grid > 0.0)
        {
            s = std::round (s / grid) * grid;
            e = n.track == drums ? s + grid : std::max (s + grid, std::round (e / grid) * grid);
        }
        const int vel = juce::jlimit (1, 127, (int) std::round (100.0f + (n.velocity - 100.0f) * o.dynamics));
        out.push_back ({ n.pitch, trackChannel (n.track), vel, n.track, std::max (0.0, s), std::max (s + 1.0e-3, e) });
    }

    // the same pitch on the same channel must not overlap
    std::sort (out.begin(), out.end(), [] (const BeatNote& a, const BeatNote& b)
               { return std::tie (a.channel, a.pitch, a.start) < std::tie (b.channel, b.pitch, b.start); });
    for (size_t i = 0; i + 1 < out.size(); ++i)
        if (out[i].channel == out[i + 1].channel && out[i].pitch == out[i + 1].pitch && out[i].end > out[i + 1].start)
            out[i].end = out[i + 1].start;
    out.erase (std::remove_if (out.begin(), out.end(), [] (const BeatNote& b) { return b.end <= b.start + 1.0e-4; }), out.end());

    std::sort (out.begin(), out.end(), [] (const BeatNote& a, const BeatNote& b) { return a.start < b.start; });
    return out;
}

juce::MidiFile makeMidiFile (const std::vector<Note>& notes, const ExportOptions& o)
{
    constexpr int ppq = 960;
    const double bpm = juce::jlimit (20.0, 400.0, o.sessionBpm);
    const auto beats = toBeatNotes (notes, o);

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);

    juce::MidiMessageSequence tempo;
    auto add = [] (juce::MidiMessageSequence& seq, juce::MidiMessage m, double tick) { m.setTimeStamp (tick); seq.addEvent (m); };
    add (tempo, juce::MidiMessage::textMetaEvent (3, o.name.isNotEmpty() ? o.name : juce::String ("oraclee")), 0);
    add (tempo, juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0);
    add (tempo, juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    add (tempo, juce::MidiMessage::endOfTrack(), 0);
    file.addTrack (tempo);

    for (int t = 0; t < numTracks; ++t)
    {
        juce::MidiMessageSequence seq;
        int lastTick = 0;
        for (auto& b : beats)
        {
            if (b.track != t)
                continue;
            const int on = (int) std::round (b.start * ppq);
            const int off = juce::jmax (on + 1, (int) std::round (b.end * ppq));
            add (seq, juce::MidiMessage::noteOn (b.channel, b.pitch, (juce::uint8) b.velocity), on);
            add (seq, juce::MidiMessage::noteOff (b.channel, b.pitch), off);
            lastTick = juce::jmax (lastTick, off);
        }
        if (seq.getNumEvents() == 0)
            continue;
        add (seq, juce::MidiMessage::textMetaEvent (3, trackName (t).replace (fr ("é"), "e")), 0); // ASCII: DAWs read latin-1
        seq.sort();
        seq.updateMatchedPairs();
        add (seq, juce::MidiMessage::endOfTrack(), lastTick + 1);
        file.addTrack (seq);
    }
    return file;
}

bool writeMidiFile (const std::vector<Note>& notes, const ExportOptions& o, const juce::File& destination)
{
    auto mf = makeMidiFile (notes, o);
    destination.deleteFile();
    juce::FileOutputStream out (destination);
    if (! out.openedOk())
        return false;
    const bool ok = mf.writeTo (out, 1);
    out.flush();
    return ok;
}
} // namespace mex
