#pragma once

#include "PluginProcessor.h"
#include "PianoRoll.h"

//==============================================================================
class OracleeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OracleeLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

//==============================================================================
// Something you drag out of the plugin into the DAW (a .mid file).
class DragOut : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<juce::File()> makeFile;
    std::function<bool()> isReady;
    std::function<juce::String()> subtitle;
    std::function<void()> onDotClick; // optional mute dot
    std::function<bool()> isOn;
    juce::String title;
    juce::Colour colour = theme::accent;
    bool primary = false;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    bool dragging = false, pressedDot = false;
};

//==============================================================================
class Sample2MidiEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit Sample2MidiEditor (Sample2MidiProcessor&);
    ~Sample2MidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void loadFile (const juce::File&);
    void chooseFile();
    void exportFile();
    void updateStatus();
    void updateToolbar();
    void rebuildBackground();
    juce::Label& makeLabel (const juce::String&, float size = 11.0f);
    void setupSlider (juce::Slider&, const juce::String& paramId, std::unique_ptr<SliderAtt>&, bool knob);
    void setupCombo (juce::ComboBox&, const juce::String& paramId, std::unique_ptr<ComboAtt>&);
    void setupToggle (juce::ToggleButton&, const juce::String& text, const juce::String& paramId,
                      std::unique_ptr<ButtonAtt>&, const juce::String& tooltip);
    juce::TextButton& makeButton (juce::TextButton&, const juce::String& text, const juce::String& tooltip);
    juce::File tempMidiFor (const std::vector<mex::Note>&, const juce::String& suffix);
    void setListen (bool original, bool recreated);

    Sample2MidiProcessor& proc;
    OracleeLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 450 };
    juce::OwnedArray<juce::Label> labels;
    juce::Image background;
    bool showAdvanced = false;
    const bool standalone;

    struct PanelRect { juce::Rectangle<int> r; juce::String title; };
    std::vector<PanelRect> panels;

    // header
    juce::TextButton loadButton, exportButton, undoButton, redoButton, listenOrig, listenRec;

    // left: source / analysis / tempo
    juce::Label fileLabel, infoLabel, soundLabel;
    juce::ComboBox modeBox, timingBox, quantBox, snapBox;
    std::unique_ptr<ComboAtt> modeAtt, timingAtt, quantAtt;
    juce::Slider sensKnob, minNoteKnob, dynKnob, lowSlider, highSlider, splitSlider, polySlider, sampleBpmSlider, sessionBpmSlider;
    std::unique_ptr<SliderAtt> sensAtt, minNoteAtt, dynAtt, lowAtt, highAtt, splitAtt, polyAtt, sampleBpmAtt, sessionBpmAtt;
    juce::Label *modeL {}, *sensL {}, *minNoteL {}, *dynL {}, *lowL {}, *highL {}, *splitL {}, *polyL {}, *sampleBpmL {}, *sessionBpmL {}, *timingL {}, *quantL {};
    juce::ToggleButton drumsToggle, sampleAutoToggle, syncHostToggle;
    std::unique_ptr<ButtonAtt> drumsAtt, sampleAutoAtt, syncHostAtt;
    juce::TextButton advancedButton, reanalyseButton;
    juce::Label sessionInfo;

    // centre: piano roll
    PianoRoll roll;
    juce::TextButton drawTool, selectTool, fitButton, quantizeButton;
    juce::OwnedArray<juce::TextButton> targetButtons;
    juce::ToggleButton ghostToggle, waveToggle;

    // right: tracks / playback / export
    juce::OwnedArray<DragOut> chips;
    DragOut mainDrag;
    juce::TextButton playButton;
    juce::Label positionLabel;
    juce::ToggleButton followToggle, loopToggle, adaptiveToggle;
    std::unique_ptr<ButtonAtt> followAtt, loopAtt, adaptiveAtt;

    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sample2MidiEditor)
};
