// Drives the piano roll with real mouse / key events and checks the result.
#include "../Source/PluginProcessor.h"
#include "../Source/PianoRoll.h"
#include "../Source/MidiExport.h"
#include <iostream>

static int fails = 0;
#define CHECK(c) do { if (c) std::cout << "  ok   " #c "\n"; else { std::cout << "  FAIL " #c "\n"; ++fails; } } while (0)

struct Mouse
{
    juce::Component& c;
    juce::Point<float> down;
    juce::Time t0;
    juce::MouseEvent ev (juce::Point<float> p, juce::ModifierKeys m, bool dragged)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c, juce::Time::getCurrentTime(), down, t0, 1, dragged);
    }
    void press (juce::Point<float> p, juce::ModifierKeys m = juce::ModifierKeys::leftButtonModifier)
    { down = p; t0 = juce::Time::getCurrentTime(); c.mouseDown (ev (p, m, false)); }
    void dragTo (juce::Point<float> p, juce::ModifierKeys m = juce::ModifierKeys::leftButtonModifier) { c.mouseDrag (ev (p, m, true)); }
    void release (juce::Point<float> p, juce::ModifierKeys m = {}) { c.mouseUp (ev (p, m, true)); }
};

int main (int, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    Sample2MidiProcessor proc;
    proc.prepareToPlay (48000, 512);
    juce::String err;
    proc.loadSample (juce::File (argv[1]), err);
    for (int i = 0; i < 200 && proc.isAnalysing(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    PianoRoll roll (proc);
    roll.setBounds (0, 0, 1400, 800);
    roll.notesChangedExternally();
    roll.setSnapBeats (0.25);
    const double beat = proc.getBeatSeconds();
    const size_t n0 = proc.getNotes().size();
    std::cout << "analysed " << n0 << " notes, beat " << beat << " s\n";
    Mouse m { roll };

    // helper: screen position of (seconds, pitch) through the roll's own geometry
    auto at = [&] (double sec, int pitch)
    {
        // reproduce the roll mapping by probing: find x for sec and y for pitch
        float x = 0, y = 0;
        for (int px = 62; px < 1400; ++px) { juce::MouseEvent e = m.ev ({ (float) px, 400.0f }, {}, false); (void) e; }
        // use public behaviour: zoomToFit puts t=0 at grid x=62 (keyW); we compute from note rects instead
        (void) sec; (void) pitch; return juce::Point<float> (x, y);
    };
    (void) at;

    std::cout << "[draw a note on empty space, Tout target]\n";
    roll.setTargetLayer (mex::melody);
    // find an empty spot: high pitch area near the top of the grid, at 1/3 of the width
    const juce::Point<float> p1 (500.0f, 40.0f);
    m.press (p1); m.release (p1);
    CHECK (proc.getNotes().size() == n0 + 1);
    const auto created = [&] { for (auto& n : proc.getNotes()) if (n.selected) return n; return mex::Note(); }();
    CHECK (created.track == mex::melody);
    CHECK (std::abs (std::fmod (created.start / (0.25 * beat) + 1e-6, 1.0)) < 1e-3); // snapped to the 1/16 grid
    CHECK (proc.canUndo());

    std::cout << "[drag the note right and up]\n";
    const auto r0 = p1;
    m.press (r0 + juce::Point<float> (2, 0)); m.dragTo (r0 + juce::Point<float> (120, -30)); m.release (r0 + juce::Point<float> (120, -30));
    const auto moved = [&] { for (auto& n : proc.getNotes()) if (n.selected) return n; return mex::Note(); }();
    CHECK (moved.start > created.start);
    CHECK (moved.pitch > created.pitch);
    CHECK (proc.getNotes().size() == n0 + 1);

    std::cout << "[keyboard: transpose, duplicate, quantize, delete, undo, redo]\n";
    roll.keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    const auto down1 = [&] { for (auto& n : proc.getNotes()) if (n.selected) return n; return mex::Note(); }();
    CHECK (down1.pitch == moved.pitch - 1);
    roll.keyPressed (juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0));
    CHECK (proc.getNotes().size() == n0 + 2);
    roll.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    CHECK (proc.getNotes().size() == n0 + 1);
    roll.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
    CHECK (proc.getNotes().size() == n0 + 2);
    roll.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
    CHECK (proc.getNotes().size() == n0 + 1);

    std::cout << "[select all melody, transpose octave, undo]\n";
    roll.keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));
    int mel = 0; for (auto& n : proc.getNotes()) mel += n.track == mex::melody;
    CHECK (roll.numSelected() == mel);
    std::vector<int> before; for (auto& n : proc.getNotes()) before.push_back (n.pitch);
    roll.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
    bool allUp = true; size_t i = 0;
    for (auto& n : proc.getNotes()) { if (n.track == mex::melody && n.pitch < 127 && n.pitch != before[i] + 12) allUp = false; ++i; }
    CHECK (allUp);
    roll.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
    i = 0; bool restored = true;
    for (auto& n : proc.getNotes()) { if (n.pitch != before[i]) restored = false; ++i; }
    CHECK (restored);

    std::cout << "[quantize everything to 1/16]\n";
    roll.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    roll.keyPressed (juce::KeyPress ('q'));
    bool q = true;
    for (auto& n : proc.getNotes()) if (n.track == mex::melody && std::abs (std::remainder (n.start, 0.25 * beat)) > 1e-6) q = false;
    CHECK (q);
    roll.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));

    std::cout << "[right-click erase]\n";
    roll.setTargetLayer (-1);
    // find a visible note centre by scanning
    juce::Point<float> hit;
    bool found = false;
    roll.setTool (PianoRoll::Tool::select); // a click on empty space does nothing with this tool
    for (int y = 30; y < 680 && ! found; y += 3)
        for (int x = 300; x < 1390 && ! found; x += 3)
        {
            roll.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            m.press ({ (float) x, (float) y }); m.release ({ (float) x, (float) y });
            if (roll.numSelected() == 1) { hit = { (float) x, (float) y }; found = true; }
        }
    roll.setTool (PianoRoll::Tool::draw);
    CHECK (found);
    const size_t nBefore = proc.getNotes().size();
    m.press (hit, juce::ModifierKeys::rightButtonModifier); m.release (hit);
    CHECK (proc.getNotes().size() == nBefore - 1);

    std::cout << "[resize from the right edge]\n";
    roll.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
    roll.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    m.press (hit); m.release (hit); // select it
    auto sel = roll.selectedNotes();
    CHECK (sel.size() == 1);
    // walk right to its edge
    if (sel.empty()) { std::cout << "abort\n"; return 1; }
    roll.setTool (PianoRoll::Tool::select);
    float edge = hit.x;
    while (edge < 1390)
    {
        roll.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        m.press ({ edge + 1.0f, hit.y }); m.release ({ edge + 1.0f, hit.y });
        if (roll.numSelected() == 0) break;
        edge += 1.0f;
    }
    roll.setTool (PianoRoll::Tool::draw);
    roll.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    const double len0 = sel[0].end - sel[0].start;
    m.press ({ edge - 2.0f, hit.y }); m.dragTo ({ edge + 80.0f, hit.y }); m.release ({ edge + 80.0f, hit.y });
    auto after = roll.selectedNotes();
    CHECK (after.size() == 1 && after[0].end - after[0].start > len0 + 0.05);
    CHECK (after.size() == 1 && after[0].start == sel[0].start);

    std::cout << "[velocity lane paint]\n";
    roll.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    const float velY = 800.0f - 70.0f; // near top of the lane -> loud
    m.press ({ 62.0f, velY }); m.dragTo ({ 1399.0f, velY }); m.release ({ 1399.0f, velY });
    int loud = 0, all = 0;
    for (auto& n : proc.getNotes()) { ++all; loud += n.velocity > 110; }
    CHECK (loud > all / 2);
    roll.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));

    std::cout << "[MIDI import]\n";
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("oraclee_test.mid");
    std::vector<mex::Note> two { { 60, 0.0, 0.5, 90, mex::melody }, { 36, 0.0, 0.1, 120, mex::drums } };
    mex::ExportOptions o; o.sessionBpm = 60.0 / beat; o.sampleBpm = 60.0 / beat;
    mex::writeMidiFile (two, o, tmp);
    const size_t nb = proc.getNotes().size();
    CHECK (proc.importMidi (tmp, -1));
    CHECK (proc.getNotes().size() == nb + 2);
    int drumsAt0 = 0; for (auto& n : proc.getNotes()) drumsAt0 += (n.track == mex::drums && n.pitch == 36 && n.velocity == 120 && n.start < 0.01);
    CHECK (drumsAt0 >= 1);

    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: ") << (fails ? juce::String (fails) : juce::String()) << "\n";
    return fails;
}
