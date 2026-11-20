#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

// FL Studio-style piano roll:
//  Draw tool  : click empty = new note (then drag to place) - click note = select & move - drag the
//               right edge = resize - Shift+drag = duplicate - right click / right drag = delete
//  Select tool: drag = rectangle selection (Cmd/Ctrl+drag in the draw tool too)
//  Wheel = scroll pitch, Shift+wheel = scroll time, Cmd/Ctrl+wheel = zoom time, Alt+wheel = zoom pitch
//  Keys: Suppr, Cmd+A, Cmd+Z / Cmd+Shift+Z, Cmd+C / Cmd+V, Cmd+D, flèches, Q (quantize), P / E, Espace
class PianoRoll : public juce::Component, private juce::Timer
{
public:
    enum class Tool { draw, select };

    explicit PianoRoll (Sample2MidiProcessor&);

    void setTool (Tool t) { tool = t; updateCursor ({}); if (onStateChanged) onStateChanged(); }
    Tool getTool() const { return tool; }
    void setTargetLayer (int layer);   // -1 = every layer editable
    int getTargetLayer() const { return target; }
    void setSnapBeats (double b) { snapBeats = b; repaint(); }
    double getSnapBeats() const { return snapBeats; }
    void setShowGhosts (bool b) { showGhosts = b; repaint(); }
    void setShowWave (bool b) { showWave = b; repaint(); }
    void zoomToFit();
    void notesChangedExternally();     // new analysis / undo / import
    std::vector<mex::Note> selectedNotes() const;
    int numSelected() const;
    double getCursorSeconds() const { return cursorSec; }
    void togglePlay();

    std::function<void()> onStateChanged;
    bool dragHover = false;

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverNote = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag { none, move, resize, rubber, erase, velocity, ruler, keys, create };

    // geometry
    static constexpr float keyW = 62.0f, rulerH = 26.0f, velH = 78.0f, scrollH = 0.0f;
    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> rulerArea() const;
    juce::Rectangle<float> keysArea() const;
    juce::Rectangle<float> velArea() const;
    float xOf (double sec) const;
    double secAt (float x) const;
    float yOf (int pitch) const;
    int pitchAt (float y) const;
    juce::Rectangle<float> noteRect (const mex::Note&) const;
    double totalSeconds() const;
    void clampScroll();
    void zoomX (double factor, float anchorX);
    void zoomY (float factor, float anchorY);

    // editing helpers
    bool editable (const mex::Note& n) const { return target < 0 || n.track == target; }
    int noteAt (juce::Point<float>) const;
    double stepSeconds() const;
    double snapRound (double t) const;
    double snapFloor (double t) const;
    void selectNone();
    void selectAllEditable();
    void deleteSelected();
    void transposeSelected (int semis);
    void moveSelected (double seconds);
    void quantizeSelected();
    void duplicateSelected();
    void copySelected();
    void paste();
    void audition (const mex::Note&);
    void begin();
    void commit();
    void updateCursor (juce::Point<float>);
    int channelFor (int track) const { return mex::trackChannel (track); }
    void timerCallback() override;
    void rebuildPeaks();

    Sample2MidiProcessor& proc;
    Tool tool = Tool::draw;
    int target = -1;
    double snapBeats = 0.25;
    bool showGhosts = true, showWave = true;

    double pxPerSec = 120.0, scrollSec = 0.0;
    float rowH = 14.0f, topPitch = 84.0f; // fractional pitch at the top edge
    double cursorSec = 0.0;
    bool fitted = false;

    Drag drag = Drag::none;
    bool editOpen = false;
    juce::Point<float> downPos;
    double downSec = 0.0;
    int downPitch = 60, anchor = -1, lastAuditionPitch = -1, hoverNote = -1;
    std::vector<mex::Note> dragOrig;
    juce::Rectangle<float> rubber;
    double lastLength = 0.0;
    int lastVelocity = 100;
    float lastVelX = 0.0f;
    std::vector<mex::Note> clipboard;

    std::shared_ptr<const juce::AudioBuffer<float>> peaksFor;
    std::vector<float> peaks;
};
