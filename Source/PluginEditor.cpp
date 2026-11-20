#include "PluginEditor.h"
#include "MidiExport.h"

using mex::fr;
namespace T = theme;

//==============================================================================
OracleeLookAndFeel::OracleeLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, T::bg);
    setColour (juce::TextButton::buttonColourId, T::panel2);
    setColour (juce::TextButton::buttonOnColourId, T::accent);
    setColour (juce::TextButton::textColourOffId, T::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, T::panel2);
    setColour (juce::ComboBox::outlineColourId, T::frame);
    setColour (juce::ComboBox::textColourId, T::text);
    setColour (juce::ComboBox::arrowColourId, T::dim);
    setColour (juce::PopupMenu::backgroundColourId, T::panel);
    setColour (juce::PopupMenu::textColourId, T::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, T::accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, T::text);
    setColour (juce::Slider::textBoxTextColourId, T::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, T::panel2);
    setColour (juce::TextEditor::textColourId, T::text);
    setColour (juce::TextEditor::highlightColourId, T::accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, T::accent2);
    setColour (juce::ToggleButton::textColourId, T::text);
    setColour (juce::TooltipWindow::backgroundColourId, T::panel2);
    setColour (juce::TooltipWindow::textColourId, T::text);
    setColour (juce::TooltipWindow::outlineColourId, T::frame);
}

void OracleeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    if (b.getToggleState())
    {
        const auto c = b.getProperties().contains ("colour") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["colour"]) : T::accent;
        juce::ColourGradient gr (c.brighter (0.25f), 0.0f, r.getY(), c.darker (0.25f), 0.0f, r.getBottom(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (c.brighter (0.5f).withAlpha (0.9f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        return;
    }
    g.setColour (down ? T::panel3 : (over ? T::panel2.brighter (0.06f) : T::panel.withAlpha (0.9f)));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (over ? T::frame.brighter (0.4f) : T::frame);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void OracleeLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (! b.isEnabled() ? T::faint : (b.getToggleState() ? juce::Colours::white : T::text));
    g.setFont (T::spaced (juce::jmin (12.5f, b.getHeight() * 0.42f), 0.14f));
    g.drawFittedText (T::upper (b.getButtonText()), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
}

void OracleeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (20.0f).withSizeKeepingCentre (13.0f, 13.0f);
    const bool on = b.getToggleState();
    g.setColour (T::panel2);
    g.fillRoundedRectangle (box, 2.0f);
    g.setColour (over ? T::frame.brighter (0.5f) : T::frame.brighter (0.2f));
    g.drawRoundedRectangle (box, 2.0f, 1.0f);
    if (on)
    {
        g.setColour (T::accent.withAlpha (0.35f));
        g.fillRoundedRectangle (box.expanded (2.0f), 3.0f);
        g.setColour (T::accent2);
        g.fillRoundedRectangle (box.reduced (3.0f), 1.0f);
    }
    r.removeFromLeft (6.0f);
    g.setColour (b.isEnabled() ? (on ? T::text : T::dim) : T::faint);
    g.setFont (T::spaced (11.0f, 0.14f));
    g.drawText (T::upper (b.getButtonText()), r, juce::Justification::centredLeft, true);
}

void OracleeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                           juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = y + h * 0.5f;
    const juce::Rectangle<float> track ((float) x, cy - 1.5f, (float) w, 3.0f);
    g.setColour (T::panel3);
    g.fillRect (track);
    g.setColour (s.isEnabled() ? T::accent : T::faint);
    g.fillRect (track.withRight (pos));
    g.setColour (s.isEnabled() ? T::text : T::dim);
    g.fillRoundedRectangle (pos - 3.0f, cy - 7.0f, 6.0f, 14.0f, 1.5f);
}

void OracleeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float a0, float a1, juce::Slider& s)
{
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float d = std::min (area.getWidth(), area.getHeight()) - 6.0f;
    const auto c = area.getCentre();
    const float r = d * 0.5f;
    const float ang = a0 + pos * (a1 - a0);

    g.setColour (T::faint);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * i / 10.0f;
        g.fillEllipse (c.x + (r + 1.0f) * std::sin (a) - 1.0f, c.y - (r + 1.0f) * std::cos (a) - 1.0f, 2.0f, 2.0f);
    }
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, r - 5.0f, r - 5.0f, 0.0f, a0, a1, true);
    g.setColour (T::line.brighter (0.1f));
    g.strokePath (track, juce::PathStrokeType (3.0f));
    if (pos > 0.001f)
    {
        value.addCentredArc (c.x, c.y, r - 5.0f, r - 5.0f, 0.0f, a0, ang, true);
        g.setColour (T::accent.withAlpha (0.25f));
        g.strokePath (value, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (T::accent2);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float br = r - 11.0f;
    juce::ColourGradient body (juce::Colour (0xff1c2a3f), c.x - br * 0.4f, c.y - br * 0.6f, juce::Colour (0xff05080e), c.x + br * 0.5f, c.y + br, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (juce::Colour (0xff2c3e58));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);
    g.setColour (T::text);
    g.drawLine (c.x + br * 0.55f * std::sin (ang), c.y - br * 0.55f * std::cos (ang),
                c.x + br * 0.88f * std::sin (ang), c.y - br * 0.88f * std::cos (ang), 2.0f);
    g.setFont (T::font (juce::jlimit (10.0f, 13.0f, br * 0.33f), true));
    g.drawText (s.getTextFromValue (s.getValue()), juce::Rectangle<float> (c.x - br, c.y - 8.0f, br * 2.0f, 16.0f),
                juce::Justification::centred, false);
}

void OracleeLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (T::panel.withAlpha (0.95f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.isMouseOver (true) ? T::frame.brighter (0.4f) : T::frame);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 15.0f, ay = h * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (T::dim);
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

void OracleeLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
    l.setFont (getComboBoxFont (box));
}

juce::Font OracleeLookAndFeel::getComboBoxFont (juce::ComboBox&) { return T::font (13.0f, true); }
juce::Font OracleeLookAndFeel::getPopupMenuFont() { return T::font (14.0f); }

juce::Label* OracleeLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (T::font (12.5f, true));
    l->setJustificationType (juce::Justification::centredRight);
    return l;
}

//==============================================================================
void DragOut::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = ! isOn || isOn();
    const bool ready = on && (! isReady || isReady());
    const bool over = isMouseOver();
    const auto sub = subtitle ? subtitle() : juce::String();

    if (primary)
    {
        juce::ColourGradient gr (juce::Colour (0xff4b98ff).withAlpha (ready ? 1.0f : 0.35f), 0.0f, r.getY(),
                                 juce::Colour (0xff2a5fd6).withAlpha (ready ? 1.0f : 0.35f), 0.0f, r.getBottom(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0xff9cc8ff).withAlpha (over && ready ? 1.0f : 0.6f));
        g.drawRoundedRectangle (r, 3.0f, over && ready ? 1.6f : 1.0f);
        g.setColour (juce::Colours::white.withAlpha (ready ? 1.0f : 0.6f));
        g.setFont (T::spaced (17.0f, 0.18f));
        auto tr = r.reduced (12.0f, 6.0f);
        g.drawText (title, tr.removeFromTop (tr.getHeight() * 0.6f), juce::Justification::centredBottom);
        g.setFont (T::font (11.5f));
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawText (sub, tr, juce::Justification::centredTop);
        return;
    }

    g.setColour (ready ? T::panel2.brighter (over ? 0.08f : 0.0f) : T::panel.withAlpha (0.8f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (over && ready ? colour.withAlpha (0.9f) : T::frame);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    g.setColour (ready ? colour : T::faint);
    g.fillRect (r.withWidth (3.0f).reduced (0.0f, 4.0f));

    auto content = r.reduced (12.0f, 2.0f);
    if (onDotClick)
    {
        auto dot = content.removeFromLeft (14.0f).withSizeKeepingCentre (10.0f, 10.0f);
        g.setColour (on ? colour : T::dim);
        if (on) g.fillEllipse (dot);
        else g.drawEllipse (dot, 1.4f);
        content.removeFromLeft (8.0f);
    }
    auto grip = content.removeFromRight (12.0f);
    g.setColour (ready ? colour.withAlpha (0.8f) : T::faint);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (grip.getCentreX() - 3.5f + j * 5.0f, grip.getCentreY() - 6.0f + i * 5.0f, 2.2f, 2.2f);

    g.setColour (on ? T::text : T::dim);
    g.setFont (T::spaced (12.0f, 0.16f));
    g.drawText (T::upper (title), content.removeFromTop (content.getHeight() * 0.55f), juce::Justification::bottomLeft, true);
    g.setColour (T::dim);
    g.setFont (T::font (11.0f));
    g.drawText (sub, content, juce::Justification::topLeft, true);
}

void DragOut::mouseDown (const juce::MouseEvent& e)
{
    pressedDot = onDotClick && e.x < 32;
    if (pressedDot) { onDotClick(); repaint(); }
}

void DragOut::mouseDrag (const juce::MouseEvent& e)
{
    if (pressedDot || dragging || e.getDistanceFromDragStart() < 4 || (isOn && ! isOn()) || (isReady && ! isReady()) || ! makeFile)
        return;
    const auto f = makeFile();
    if (! f.existsAsFile())
        return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this,
                                                                [safe = juce::Component::SafePointer<DragOut> (this)]
                                                                {
                                                                    if (safe != nullptr) safe->dragging = false;
                                                                });
}

//==============================================================================
Sample2MidiEditor::Sample2MidiEditor (Sample2MidiProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      standalone (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone || p.wrapperType == juce::AudioProcessor::wrapperType_Undefined),
      roll (p)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    //== header
    makeButton (loadButton, "Charger", fr ("Charger un sample audio ou un .mid (ou glisse-le dans la fenêtre)")).onClick = [this] { chooseFile(); };
    makeButton (exportButton, "Exporter", fr ("Écrit un .mid multipiste + un .mid par instrument")).onClick = [this] { exportFile(); };
    makeButton (undoButton, "Undo", "Annuler (Cmd/Ctrl+Z)").onClick = [this] { proc.undo(); roll.repaint(); updateToolbar(); };
    makeButton (redoButton, "Redo", fr ("Rétablir (Cmd/Ctrl+Maj+Z)")).onClick = [this] { proc.redo(); roll.repaint(); updateToolbar(); };
    makeButton (listenOrig, "Original", fr ("Écouter le sample d'origine")).onClick = [this] { setListen (true, false); };
    makeButton (listenRec, fr ("Recréé"), fr ("Écouter le MIDI joué par le synthé adaptatif")).onClick = [this] { setListen (false, true); };

    //== left: source
    for (auto* l : { &fileLabel, &infoLabel, &soundLabel })
    {
        l->setFont (l == &fileLabel ? T::font (14.0f, true) : T::font (12.0f));
        l->setColour (juce::Label::textColourId, l == &fileLabel ? T::text : T::dim);
        l->setMinimumHorizontalScale (0.85f);
        if (l == &soundLabel) l->setFont (T::font (11.0f));
        addAndMakeVisible (*l);
    }

    //== left: analysis
    modeL = &makeLabel ("MODE");
    setupCombo (modeBox, "mode", modeAtt);
    modeBox.setTooltip (fr ("Multi-instruments : sépare batterie, basse, accords et mélodie.\nMélodie seule : une note à la fois.\nAccords : tout sur une piste."));
    sensL = &makeLabel (fr ("SENSIBILITÉ"));
    setupSlider (sensKnob, "sensitivity", sensAtt, true);
    sensKnob.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100)) + " %"; };
    sensKnob.setTooltip (fr ("Plus haut = détecte plus de notes, y compris les plus faibles."));
    minNoteL = &makeLabel (fr ("DURÉE MIN"));
    setupSlider (minNoteKnob, "minNote", minNoteAtt, true);
    minNoteKnob.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " ms"; };

    makeButton (advancedButton, fr ("Avancé  ▸"), fr ("Afficher les réglages fins de l'analyse")).onClick = [this]
    {
        showAdvanced = ! showAdvanced;
        advancedButton.setButtonText (showAdvanced ? fr ("Avancé  ▾") : fr ("Avancé  ▸"));
        resized();
        repaint();
    };
    lowL = &makeLabel ("NOTE GRAVE");
    setupSlider (lowSlider, "lowNote", lowAtt, false);
    highL = &makeLabel (fr ("NOTE AIGUË"));
    setupSlider (highSlider, "highNote", highAtt, false);
    splitL = &makeLabel ("LIMITE BASSE");
    setupSlider (splitSlider, "bassSplit", splitAtt, false);
    splitSlider.setTooltip (fr ("Les notes jusqu'à cette hauteur vont sur la piste Basse."));
    for (auto* s : { &lowSlider, &highSlider, &splitSlider })
    {
        s->textFromValueFunction = [] (double v) { return mex::noteName ((int) v); };
        s->valueFromTextFunction = [] (const juce::String& t)
        {
            for (int n = 0; n < 128; ++n)
                if (mex::noteName (n).equalsIgnoreCase (t.trim())) return (double) n;
            return t.getDoubleValue();
        };
    }
    polyL = &makeLabel ("POLYPHONIE");
    setupSlider (polySlider, "polyphony", polyAtt, false);
    setupToggle (drumsToggle, "Batterie", "drums", drumsAtt, fr ("Détecter kick, snare et hi-hat (mode Multi)."));
    makeButton (reanalyseButton, fr ("Réanalyser"), fr ("Relance l'analyse. Tes retouches restent récupérables avec Undo.")).onClick = [this] { proc.reanalyse(); };

    //== left: tempo
    sampleBpmL = &makeLabel ("BPM DU SAMPLE");
    setupSlider (sampleBpmSlider, "sampleBpm", sampleBpmAtt, false);
    sampleBpmSlider.setNumDecimalPlacesToDisplay (2);
    sampleBpmSlider.onDragStart = [this] { if (auto* prm = proc.apvts.getParameter ("sampleBpmAuto")) prm->setValueNotifyingHost (0.0f); };
    setupToggle (sampleAutoToggle, "Auto", "sampleBpmAuto", sampleAutoAtt, fr ("Détection automatique du tempo du sample."));
    sessionBpmL = &makeLabel ("BPM SESSION");
    setupSlider (sessionBpmSlider, "bpm", sessionBpmAtt, false);
    sessionBpmSlider.setNumDecimalPlacesToDisplay (2);
    setupToggle (syncHostToggle, "DAW", "syncHost", syncHostAtt, fr ("Suivre le tempo du projet DAW."));
    sessionInfo.setFont (T::font (12.5f, true));
    sessionInfo.setColour (juce::Label::textColourId, T::accent2);
    addAndMakeVisible (sessionInfo);
    for (auto* c : std::initializer_list<juce::Component*> { sessionBpmL, &sessionBpmSlider, &syncHostToggle })
        c->setVisible (standalone);
    timingL = &makeLabel ("CALAGE");
    setupCombo (timingBox, "timing", timingAtt);
    timingBox.setTooltip (fr ("Caler : chaque temps du sample tombe sur un temps de ta session.\nTemps réel : garde les durées en secondes."));

    //== centre: piano roll toolbar
    makeButton (drawTool, fr ("✎ Crayon"), fr ("Crayon (P) : clic = créer / déplacer, bord droit = durée, clic droit = effacer, Maj+glisser = dupliquer"))
        .onClick = [this] { roll.setTool (PianoRoll::Tool::draw); };
    makeButton (selectTool, fr ("Sélection"), fr ("Sélection (E) : glisser pour sélectionner"))
        .onClick = [this] { roll.setTool (PianoRoll::Tool::select); };
    makeButton (quantizeButton, "Quantize", fr ("Aligne la sélection (ou la piste) sur la grille (Q)"))
        .onClick = [this] { roll.grabKeyboardFocus(); roll.keyPressed (juce::KeyPress ('q')); };
    makeButton (fitButton, "Ajuster", fr ("Zoom pour tout voir")).onClick = [this] { roll.zoomToFit(); };
    snapBox.addItemList ({ "Grille off", "Grille 1/32", "Grille 1/16", "Grille 1/8", "Grille 1/4", "Grille 1/2", "Grille 1 mesure" }, 1);
    snapBox.setSelectedId (3, juce::dontSendNotification);
    snapBox.onChange = [this]
    {
        static const double beats[] = { 0.0, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
        roll.setSnapBeats (beats[juce::jlimit (0, 6, snapBox.getSelectedId() - 1)]);
    };
    addAndMakeVisible (snapBox);
    for (int t = -1; t < mex::numTracks; ++t)
    {
        auto* b = targetButtons.add (new juce::TextButton (t < 0 ? juce::String ("Tout") : mex::trackName (t)));
        b->getProperties().set ("colour", (juce::int64) (t < 0 ? T::accent : T::layer (t).darker (0.25f)).getARGB());
        b->setTooltip (t < 0 ? fr ("Éditer toutes les pistes") : fr ("Éditer seulement : ") + mex::trackName (t));
        b->onClick = [this, t] { roll.setTargetLayer (t); updateToolbar(); };
        addAndMakeVisible (b);
    }
    ghostToggle.setButtonText (fr ("Fantômes"));
    ghostToggle.setToggleState (true, juce::dontSendNotification);
    ghostToggle.onClick = [this] { roll.setShowGhosts (ghostToggle.getToggleState()); };
    waveToggle.setButtonText ("Onde");
    waveToggle.setToggleState (true, juce::dontSendNotification);
    waveToggle.onClick = [this] { roll.setShowWave (waveToggle.getToggleState()); };
    for (auto* t : { &ghostToggle, &waveToggle }) addAndMakeVisible (*t);
    roll.onStateChanged = [this] { updateToolbar(); };
    addAndMakeVisible (roll);

    //== right: tracks
    for (int t = 0; t <= mex::numTracks; ++t)
    {
        const int layer = t < mex::numTracks ? t : -1;
        auto* c = chips.add (new DragOut());
        c->title = layer < 0 ? juce::String ("Toutes les pistes") : mex::trackName (layer);
        c->colour = layer < 0 ? T::accent2 : T::layer (layer);
        c->subtitle = [this, layer]
        {
            const int n = proc.countNotes (layer);
            return juce::String (n) + (n > 1 ? " notes" : " note") + fr (" · canal ") + (layer < 0 ? juce::String ("1/2/3/10") : juce::String (mex::trackChannel (layer)));
        };
        c->isReady = [this, layer] { return proc.countNotes (layer) > 0 && ! proc.isAnalysing(); };
        c->makeFile = [this, layer] { return proc.writeTempMidi (layer); };
        if (layer >= 0)
        {
            c->isOn = [this, layer] { return proc.isLayerOn (layer); };
            c->onDotClick = [this, layer]
            {
                if (auto* prm = proc.apvts.getParameter ("layer" + juce::String (layer)))
                    prm->setValueNotifyingHost (proc.isLayerOn (layer) ? 0.0f : 1.0f);
            };
            c->setTooltip (fr ("Glisse vers une piste de ton DAW. Le point active / coupe la piste."));
        }
        else
            c->setTooltip (fr ("Glisse toutes les pistes actives (fichier multipiste)."));
        addAndMakeVisible (c);
    }
    mainDrag.primary = true;
    mainDrag.title = "GLISSER LE MIDI";
    mainDrag.subtitle = [this]
    {
        const int sel = roll.numSelected();
        return sel > 0 ? juce::String (sel) + fr (" notes sélectionnées → ton DAW") : fr ("toutes les pistes actives → ton DAW");
    };
    mainDrag.isReady = [this] { return proc.countNotes (-1) > 0 && ! proc.isAnalysing(); };
    mainDrag.makeFile = [this]
    {
        auto sel = roll.selectedNotes();
        return sel.empty() ? proc.writeTempMidi (-1) : tempMidiFor (sel, fr (" - sélection"));
    };
    mainDrag.setTooltip (fr ("Attrape ce bouton et dépose-le sur une piste (ou la playlist) de ton DAW"));
    addAndMakeVisible (mainDrag);

    //== right: playback
    makeButton (playButton, fr ("▶"), "Lecture / stop (Espace)").onClick = [this] { roll.togglePlay(); timerCallback(); };
    positionLabel.setFont (T::spaced (18.0f, 0.1f));
    positionLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (positionLabel);
    setupToggle (adaptiveToggle, fr ("Synthé adaptatif"), "adaptive", adaptiveAtt,
                 fr ("Rejoue le MIDI avec des sons appris du sample (notes échantillonnées, timbre modélisé, vrais coups de batterie)."));
    setupToggle (followToggle, "Jouer avec le DAW", "followHost", followAtt,
                 fr ("Pendant la lecture du DAW, oraclee envoie son MIDI calé sur la session.\nCanaux : Mélodie 1, Accords 2, Basse 3, Batterie 10."));
    setupToggle (loopToggle, "Boucler", "loop", loopAtt, fr ("Répéter la boucle (lecture interne et DAW)."));

    //== right: export
    quantL = &makeLabel ("QUANTIZE EXPORT");
    setupCombo (quantBox, "quantize", quantAtt);
    dynL = &makeLabel (fr ("VÉLOCITÉ"));
    setupSlider (dynKnob, "dynamics", dynAtt, true);
    dynKnob.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100)) + " %"; };
    dynKnob.setTooltip (fr ("100 % = vélocités détectées / retouchées, 0 % = toutes à 100."));

    statusLabel.setFont (T::font (12.0f));
    statusLabel.setColour (juce::Label::textColourId, T::dim);
    addAndMakeVisible (statusLabel);

    for (auto* s : { &sensKnob, &minNoteKnob, &dynKnob, &lowSlider, &highSlider, &splitSlider })
        s->updateText();

    proc.addChangeListener (this);
    roll.notesChangedExternally();
    updateStatus();
    updateToolbar();
    startTimerHz (30);

    setResizable (true, true);
    setResizeLimits (1200, 760, 2800, 1800);
    setSize (1440, 900);
}

Sample2MidiEditor::~Sample2MidiEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

juce::Label& Sample2MidiEditor::makeLabel (const juce::String& text, float size)
{
    auto* l = labels.add (new juce::Label ({}, text));
    l->setFont (T::spaced (size, 0.22f));
    l->setColour (juce::Label::textColourId, T::dim);
    l->setBorderSize ({});
    addAndMakeVisible (l);
    return *l;
}

juce::TextButton& Sample2MidiEditor::makeButton (juce::TextButton& b, const juce::String& text, const juce::String& tooltip)
{
    b.setButtonText (text);
    b.setTooltip (tooltip);
    addAndMakeVisible (b);
    return b;
}

void Sample2MidiEditor::setupSlider (juce::Slider& s, const juce::String& paramId, std::unique_ptr<SliderAtt>& att, bool knob)
{
    if (knob)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    }
    else
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    }
    addAndMakeVisible (s);
    att = std::make_unique<SliderAtt> (proc.apvts, paramId, s);
}

void Sample2MidiEditor::setupCombo (juce::ComboBox& b, const juce::String& paramId, std::unique_ptr<ComboAtt>& att)
{
    b.addItemList (proc.apvts.getParameter (paramId)->getAllValueStrings(), 1);
    addAndMakeVisible (b);
    att = std::make_unique<ComboAtt> (proc.apvts, paramId, b);
}

void Sample2MidiEditor::setupToggle (juce::ToggleButton& b, const juce::String& text, const juce::String& paramId,
                                     std::unique_ptr<ButtonAtt>& att, const juce::String& tooltip)
{
    b.setButtonText (text);
    addAndMakeVisible (b);
    att = std::make_unique<ButtonAtt> (proc.apvts, paramId, b);
    b.setTooltip (tooltip);
}

void Sample2MidiEditor::setListen (bool original, bool recreated)
{
    if (auto* a = proc.apvts.getParameter ("playOriginal")) a->setValueNotifyingHost (original ? 1.0f : 0.0f);
    if (auto* b = proc.apvts.getParameter ("playSynth")) b->setValueNotifyingHost (recreated ? 1.0f : 0.0f);
    if (! proc.isPlaying()) proc.play (roll.getCursorSeconds());
    timerCallback();
}

juce::File Sample2MidiEditor::tempMidiFor (const std::vector<mex::Note>& notes, const juce::String& suffix)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("oraclee");
    dir.createDirectory();
    auto o = proc.exportOptions (-1);
    o.layerMask = 0xF;
    o.name += suffix;
    auto f = dir.getChildFile (juce::File::createLegalFileName (o.name + ".mid"));
    return mex::writeMidiFile (notes, o, f) ? f : juce::File();
}

//==============================================================================
void Sample2MidiEditor::rebuildBackground()
{
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    background = juce::Image (juce::Image::RGB, w, h, true);
    juce::Graphics g (background);
    juce::ColourGradient base (juce::Colour (0xff0b1220), 0.0f, 0.0f, juce::Colour (0xff04070c), 0.0f, (float) h, false);
    g.setGradientFill (base);
    g.fillAll();
    juce::Random rng (7);
    for (int i = 0; i < 6; ++i) // blue mist
    {
        const float cx = w * (0.15f + 0.7f * rng.nextFloat()), cy = h * (0.1f + 0.8f * rng.nextFloat());
        const float rad = w * (0.18f + 0.2f * rng.nextFloat());
        juce::ColourGradient mist (juce::Colour (0xff2a4a7a).withAlpha (0.10f), cx, cy, juce::Colours::transparentBlack, cx + rad, cy, true);
        g.setGradientFill (mist);
        g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
    }
    for (int y = 0; y < h; ++y) // brushed steel streaks
    {
        g.setColour (juce::Colours::white.withAlpha (rng.nextFloat() * 0.035f));
        const float x0 = rng.nextFloat() * w * 0.3f;
        g.drawHorizontalLine (y, x0, x0 + w * (0.4f + 0.6f * rng.nextFloat()));
    }
}

void Sample2MidiEditor::paint (juce::Graphics& g)
{
    if (background.isValid() && background.getWidth() == getWidth() && background.getHeight() == getHeight())
        g.drawImageAt (background, 0, 0);
    else
        g.fillAll (T::bg);

    // header: orb mark + wordmark + version
    {
        const juce::Rectangle<float> orb (22.0f, 14.0f, 44.0f, 44.0f);
        juce::ColourGradient og (T::accent2, orb.getX(), orb.getY(), T::accent.darker (0.4f), orb.getRight(), orb.getBottom(), false);
        g.setGradientFill (og);
        g.drawEllipse (orb, 2.0f);
        g.drawEllipse (orb.reduced (8.0f), 1.0f);
        g.fillEllipse (orb.withSizeKeepingCentre (12.0f, 12.0f));
        g.setColour (T::accent2.withAlpha (0.25f));
        for (int i = 0; i < 12; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * i / 12.0f;
            g.drawLine (orb.getCentreX() + 26.0f * std::cos (a), orb.getCentreY() + 26.0f * std::sin (a),
                        orb.getCentreX() + 30.0f * std::cos (a), orb.getCentreY() + 30.0f * std::sin (a), 1.0f);
        }
        g.setColour (T::text);
        g.setFont (T::spaced (34.0f, 0.12f, true));
        g.drawText ("oraclee", 80, 6, 260, 42, juce::Justification::centredLeft);
        g.setColour (T::dim);
        g.setFont (T::spaced (10.5f, 0.55f, false));
        g.drawText (fr ("AUDIO → MIDI   ") + juce::String (ORACLEE_VERSION), 82, 44, 300, 16, juce::Justification::centredLeft);
        g.setColour (T::line);
        g.drawHorizontalLine (71, 0.0f, (float) getWidth());
    }

    for (auto& p : panels)
        T::drawPanel (g, p.r.toFloat(), p.title);

    if (showAdvanced && lowL != nullptr)
    {
        g.setColour (T::line);
        g.drawHorizontalLine (lowL->getY() - 8, (float) lowL->getX(), (float) polySlider.getRight());
    }
}

void Sample2MidiEditor::resized()
{
    rebuildBackground();
    panels.clear();
    auto r = getLocalBounds();

    //== header
    auto header = r.removeFromTop (72).reduced (16, 18);
    header.removeFromLeft (360);
    auto right = header.removeFromRight (520);
    exportButton.setBounds (right.removeFromRight (110));
    right.removeFromRight (8);
    loadButton.setBounds (right.removeFromRight (110));
    right.removeFromRight (20);
    listenRec.setBounds (right.removeFromRight (100));
    right.removeFromRight (4);
    listenOrig.setBounds (right.removeFromRight (100));
    undoButton.setBounds (header.removeFromLeft (70));
    header.removeFromLeft (6);
    redoButton.setBounds (header.removeFromLeft (70));

    statusLabel.setBounds (r.removeFromBottom (26).reduced (20, 2));
    r.reduce (14, 12);

    //== columns
    auto left = r.removeFromLeft (310);
    r.removeFromLeft (12);
    auto rightCol = r.removeFromRight (300);
    r.removeFromRight (12);
    auto centre = r;

    auto panel = [this] (juce::Rectangle<int>& col, int h, const juce::String& title)
    {
        auto p = col.removeFromTop (h);
        col.removeFromTop (12);
        panels.push_back ({ p, title });
        return p.withTrimmedTop (40).reduced (18, 12);
    };
    auto labelled = [] (juce::Rectangle<int>& a, juce::Label* l, juce::Component& c, int h, juce::Component* extra = nullptr, int extraW = 64)
    {
        auto row = a.removeFromTop (h);
        if (l != nullptr) l->setBounds (row.removeFromTop (14));
        if (extra != nullptr) extra->setBounds (row.removeFromRight (extraW).withSizeKeepingCentre (extraW, 24));
        c.setBounds (row.withSizeKeepingCentre (row.getWidth(), 26));
        a.removeFromTop (6);
    };

    // SOURCE
    {
        auto a = panel (left, 124, "SOURCE");
        fileLabel.setBounds (a.removeFromTop (22));
        infoLabel.setBounds (a.removeFromTop (20));
        soundLabel.setBounds (a.removeFromTop (36));
    }
    // ANALYSE
    {
        const int h = showAdvanced ? 486 : 300;
        auto a = panel (left, h, "ANALYSE");
        labelled (a, modeL, modeBox, 44);
        auto knobs = a.removeFromTop (126);
        auto k1 = knobs.removeFromLeft (knobs.getWidth() / 2);
        sensL->setBounds (k1.removeFromBottom (16));
        sensL->setJustificationType (juce::Justification::centred);
        sensKnob.setBounds (k1.withSizeKeepingCentre (104, 104));
        minNoteL->setBounds (knobs.removeFromBottom (16));
        minNoteL->setJustificationType (juce::Justification::centred);
        minNoteKnob.setBounds (knobs.withSizeKeepingCentre (104, 104));
        a.removeFromTop (8);
        auto adv = a.removeFromTop (28);
        advancedButton.setBounds (adv.removeFromLeft (adv.getWidth() / 2 - 4));
        reanalyseButton.setBounds (adv.removeFromRight (adv.getWidth() - 8));
        for (auto* c : std::initializer_list<juce::Component*> { lowL, &lowSlider, highL, &highSlider, splitL, &splitSlider, polyL, &polySlider, &drumsToggle })
            c->setVisible (showAdvanced);
        if (showAdvanced)
        {
            a.removeFromTop (16);
            labelled (a, lowL, lowSlider, 38);
            labelled (a, highL, highSlider, 38);
            labelled (a, splitL, splitSlider, 38);
            labelled (a, polyL, polySlider, 38);
            drumsToggle.setBounds (a.removeFromTop (24));
        }
    }
    // TEMPO
    {
        auto a = panel (left, standalone ? 236 : 196, "TEMPO");
        labelled (a, sampleBpmL, sampleBpmSlider, 42, &sampleAutoToggle);
        if (standalone)
            labelled (a, sessionBpmL, sessionBpmSlider, 42, &syncHostToggle);
        sessionInfo.setBounds (a.removeFromTop (20));
        a.removeFromTop (4);
        labelled (a, timingL, timingBox, 44);
    }

    //== centre: piano roll
    {
        panels.push_back ({ centre, "PIANO ROLL" });
        auto a = centre.withTrimmedTop (40).reduced (14, 10);
        auto tb = a.removeFromTop (28);
        a.removeFromTop (8);
        auto tb2 = a.removeFromTop (26);
        a.removeFromTop (8);
        roll.setBounds (a);

        drawTool.setBounds (tb.removeFromLeft (104));
        tb.removeFromLeft (4);
        selectTool.setBounds (tb.removeFromLeft (104));
        tb.removeFromLeft (10);
        snapBox.setBounds (tb.removeFromLeft (140));
        fitButton.setBounds (tb.removeFromRight (84));
        tb.removeFromRight (6);
        quantizeButton.setBounds (tb.removeFromRight (100));
        tb.removeFromRight (12);
        waveToggle.setBounds (tb.removeFromRight (74));
        ghostToggle.setBounds (tb.removeFromRight (104));

        const int bw = juce::jmin (112, tb2.getWidth() / targetButtons.size());
        for (auto* b : targetButtons)
            b->setBounds (tb2.removeFromLeft (bw).reduced (2, 0));
    }

    //== right: tracks / playback / export
    {
        auto a = panel (rightCol, 5 * 45 + 6 + 68 + 40 + 26, "PISTES MIDI");
        for (auto* c : chips)
        {
            c->setBounds (a.removeFromTop (40));
            a.removeFromTop (5);
        }
        a.removeFromTop (6);
        mainDrag.setBounds (a.removeFromTop (68));
    }
    {
        auto a = panel (rightCol, 168, "LECTURE");
        auto row = a.removeFromTop (36);
        playButton.setBounds (row.removeFromLeft (64));
        positionLabel.setBounds (row);
        a.removeFromTop (4);
        adaptiveToggle.setBounds (a.removeFromTop (24));
        followToggle.setBounds (a.removeFromTop (24));
        loopToggle.setBounds (a.removeFromTop (24));
    }
    {
        auto a = panel (rightCol, rightCol.getHeight(), "EXPORT");
        labelled (a, quantL, quantBox, 44);
        dynL->setBounds (a.removeFromBottom (16));
        dynL->setJustificationType (juce::Justification::centred);
        const int ks = juce::jmax (40, juce::jmin (100, a.getHeight()));
        dynKnob.setBounds (a.withSizeKeepingCentre (ks, ks));
    }
}

bool Sample2MidiEditor::keyPressed (const juce::KeyPress& k)
{
    return roll.keyPressed (k);
}

//==============================================================================
bool Sample2MidiEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
            return true;
        for (auto& e : Sample2MidiProcessor::supportedExtensions())
            if (f.endsWithIgnoreCase (e))
                return true;
    }
    return false;
}

void Sample2MidiEditor::fileDragEnter (const juce::StringArray&, int, int) { roll.dragHover = true; roll.repaint(); }
void Sample2MidiEditor::fileDragExit (const juce::StringArray&) { roll.dragHover = false; roll.repaint(); }

void Sample2MidiEditor::filesDropped (const juce::StringArray& files, int, int)
{
    roll.dragHover = false;
    roll.repaint();
    for (auto& f : files)
    {
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
        {
            lastError = proc.importMidi (juce::File (f), roll.getTargetLayer()) ? juce::String()
                                                                                  : fr ("Fichier MIDI illisible : ") + juce::File (f).getFileName();
            roll.notesChangedExternally();
            updateStatus();
            return;
        }
        for (auto& e : Sample2MidiProcessor::supportedExtensions())
            if (f.endsWithIgnoreCase (e))
            {
                loadFile (juce::File (f));
                return;
            }
    }
}

void Sample2MidiEditor::loadFile (const juce::File& f)
{
    lastError.clear();
    if (! proc.loadSample (f, lastError))
        updateStatus();
    roll.notesChangedExternally();
}

void Sample2MidiEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choisir un sample audio", juce::File(),
                                                   "*" + Sample2MidiProcessor::supportedExtensions().joinIntoString (";*") + ";*.mid;*.midi");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  filesDropped ({ f.getFullPathName() }, 0, 0);
                          });
}

void Sample2MidiEditor::exportFile()
{
    if (proc.countNotes (-1) == 0)
        return;
    const auto src = proc.getCurrentFile();
    const auto start = src.existsAsFile() ? src.getSiblingFile (src.getFileNameWithoutExtension() + " (oraclee).mid")
                                          : juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("oraclee.mid");
    chooser = std::make_unique<juce::FileChooser> ("Exporter le MIDI", start, "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              if (! f.hasFileExtension ("mid;midi"))
                                  f = f.withFileExtension ("mid");
                              const auto written = proc.exportAll (f);
                              lastError = written.isEmpty() ? fr ("Impossible d'écrire ") + f.getFullPathName() : juce::String();
                              if (lastError.isEmpty())
                                  statusLabel.setText (fr ("Exporté : ") + juce::String (written.size()) + " fichiers dans "
                                                           + f.getParentDirectory().getFullPathName(), juce::dontSendNotification);
                              else
                                  updateStatus();
                          });
}

void Sample2MidiEditor::updateToolbar()
{
    drawTool.setToggleState (roll.getTool() == PianoRoll::Tool::draw, juce::dontSendNotification);
    selectTool.setToggleState (roll.getTool() == PianoRoll::Tool::select, juce::dontSendNotification);
    for (int i = 0; i < targetButtons.size(); ++i)
        targetButtons[i]->setToggleState (roll.getTargetLayer() == i - 1, juce::dontSendNotification);
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    mainDrag.repaint();
    for (auto* c : chips) c->repaint();
}

void Sample2MidiEditor::updateStatus()
{
    const auto f = proc.getCurrentFile();
    fileLabel.setText (f.existsAsFile() ? f.getFileName() : fr ("Aucun sample — glisse un fichier ici"), juce::dontSendNotification);
    soundLabel.setText (proc.describeSound(), juce::dontSendNotification);

    juce::String s;
    if (lastError.isNotEmpty())
        s = lastError;
    else if (proc.getSample() == nullptr && proc.getNotes().empty())
        s = fr ("Glisse un sample audio (ou un .mid) dans la fenêtre pour commencer.");
    else if (proc.isAnalysing())
        s = fr ("Analyse… ") + juce::String ((int) (proc.getProgress() * 100.0f)) + " %";
    else
    {
        juce::StringArray parts;
        for (int t = 0; t < mex::numTracks; ++t)
            if (const int c = proc.countNotes (t); c > 0)
                parts.add (mex::trackName (t) + " " + juce::String (c));
        s = parts.joinIntoString (fr ("  ·  "));
        if (proc.hasUserEdits())
            s += fr ("   —   retouché");
        s += fr ("   —   Espace lecture · Suppr effacer · ↑↓ transposer · Cmd/Ctrl+Z annuler · Q quantize");
    }
    statusLabel.setText (s, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, lastError.isNotEmpty() ? juce::Colours::orange : T::dim);
    exportButton.setEnabled (proc.countNotes (-1) > 0 && ! proc.isAnalysing());
    reanalyseButton.setEnabled (proc.getSample() != nullptr);
    updateToolbar();
}

void Sample2MidiEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    roll.notesChangedExternally();
    updateStatus();
}

void Sample2MidiEditor::timerCallback()
{
    playButton.setButtonText (proc.isPlaying() ? fr ("■") : fr ("▶"));
    playButton.setToggleState (proc.isPlaying(), juce::dontSendNotification);
    const bool orig = proc.apvts.getRawParameterValue ("playOriginal")->load() > 0.5f;
    const bool rec = proc.apvts.getRawParameterValue ("playSynth")->load() > 0.5f;
    listenOrig.setToggleState (orig, juce::dontSendNotification);
    listenRec.setToggleState (rec, juce::dontSendNotification);

    const double beat = proc.getBeatSeconds();
    const double t = proc.isPlaying() ? proc.getPlayPositionSeconds() : roll.getCursorSeconds();
    const int beats = (int) std::floor (t / beat + 1.0e-6);
    positionLabel.setText (juce::String (beats / 4 + 1) + " . " + juce::String (beats % 4 + 1), juce::dontSendNotification);

    const bool hostKnown = proc.getHostBpm() > 0.0;
    const bool sync = proc.apvts.getRawParameterValue ("syncHost")->load() > 0.5f;
    sessionBpmSlider.setEnabled (! (sync && hostKnown));
    if (sync && hostKnown && std::abs (sessionBpmSlider.getValue() - proc.getHostBpm()) > 0.005)
        sessionBpmSlider.setValue (proc.getHostBpm(), juce::sendNotificationSync);

    const auto session = juce::String (proc.getSessionBpm(), 2);
    const auto si = (sync && hostKnown ? fr ("Session DAW : ") : fr ("Session : ")) + session + " BPM"
                  + (proc.isHostPlaying() && proc.apvts.getRawParameterValue ("followHost")->load() > 0.5f ? fr ("   ▶") : juce::String());
    if (sessionInfo.getText() != si) sessionInfo.setText (si, juce::dontSendNotification);

    juce::String info;
    if (proc.getSample() != nullptr)
    {
        const int cents = (int) std::round (proc.getTuning() * 100.0);
        info = juce::String (proc.getSampleLengthSeconds(), 2) + fr (" s  ·  ") + juce::String (proc.getSampleBpm(), 1) + fr (" BPM  ·  ")
             + (cents == 0 ? fr ("accordé A440") : fr ("accordage ") + (cents > 0 ? "+" : "") + juce::String (cents) + " ct");
    }
    if (infoLabel.getText() != info) infoLabel.setText (info, juce::dontSendNotification);

    if (proc.isAnalysing())
        updateStatus();
}
