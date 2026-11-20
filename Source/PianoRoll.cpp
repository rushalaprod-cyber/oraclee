#include "PianoRoll.h"

using mex::fr;
namespace T = theme;

PianoRoll::PianoRoll (Sample2MidiProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    startTimerHz (30);
}

//==============================================================================
// geometry
juce::Rectangle<float> PianoRoll::rulerArea() const { return { keyW, 0.0f, getWidth() - keyW, rulerH }; }
juce::Rectangle<float> PianoRoll::keysArea() const { return { 0.0f, rulerH, keyW, getHeight() - rulerH - velH - 4.0f }; }
juce::Rectangle<float> PianoRoll::gridArea() const { return { keyW, rulerH, getWidth() - keyW, getHeight() - rulerH - velH - 4.0f }; }
juce::Rectangle<float> PianoRoll::velArea() const { return { keyW, getHeight() - velH, getWidth() - keyW, velH }; }

float PianoRoll::xOf (double sec) const { return gridArea().getX() + (float) ((sec - scrollSec) * pxPerSec); }
double PianoRoll::secAt (float x) const { return scrollSec + (x - gridArea().getX()) / pxPerSec; }
float PianoRoll::yOf (int pitch) const { return gridArea().getY() + (topPitch - (float) pitch - 1.0f) * rowH; }
int PianoRoll::pitchAt (float y) const { return juce::jlimit (0, 127, (int) std::floor (topPitch - (y - gridArea().getY()) / rowH)); }

juce::Rectangle<float> PianoRoll::noteRect (const mex::Note& n) const
{
    const float x0 = xOf (n.start), x1 = xOf (n.end);
    return { x0, yOf (n.pitch) + 1.0f, std::max (3.0f, x1 - x0), std::max (2.0f, rowH - 2.0f) };
}

double PianoRoll::totalSeconds() const
{
    const double beat = proc.getBeatSeconds();
    double t = std::max (proc.getSampleLengthSeconds(), 16.0 * beat);
    for (auto& n : proc.getNotes())
        t = std::max (t, n.end + 8.0 * beat);
    return t;
}

void PianoRoll::clampScroll()
{
    const auto g = gridArea();
    pxPerSec = juce::jlimit (8.0, 3000.0, pxPerSec);
    rowH = juce::jlimit (5.0f, 40.0f, rowH);
    const double view = g.getWidth() / pxPerSec;
    scrollSec = juce::jlimit (0.0, std::max (0.0, totalSeconds() - view * 0.5), scrollSec);
    const float rows = g.getHeight() / rowH;
    topPitch = juce::jlimit (std::min (128.0f, rows), 128.0f, topPitch);
}

void PianoRoll::zoomX (double factor, float anchorX)
{
    const double t = secAt (anchorX);
    pxPerSec *= factor;
    clampScroll();
    scrollSec = t - (anchorX - gridArea().getX()) / pxPerSec;
    clampScroll();
    repaint();
}

void PianoRoll::zoomY (float factor, float anchorY)
{
    const float p = topPitch - (anchorY - gridArea().getY()) / rowH;
    rowH *= factor;
    clampScroll();
    topPitch = p + (anchorY - gridArea().getY()) / rowH;
    clampScroll();
    repaint();
}

void PianoRoll::zoomToFit()
{
    const auto g = gridArea();
    if (g.getWidth() <= 10.0f || g.getHeight() <= 10.0f)
        return;
    const double len = std::max (proc.getSampleLengthSeconds(), 8.0 * proc.getBeatSeconds());
    pxPerSec = g.getWidth() / len;
    scrollSec = 0.0;

    int lo = 127, hi = 0;
    for (auto& n : proc.getNotes())
        if (target < 0 || n.track == target) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
    if (lo > hi) { lo = 48; hi = 72; }
    const int span = juce::jmax (24, hi - lo + 6);
    rowH = juce::jlimit (7.0f, 22.0f, g.getHeight() / (float) span);
    const float rows = g.getHeight() / rowH;
    topPitch = (float) (lo + hi) * 0.5f + rows * 0.5f + 0.5f;
    clampScroll();
    fitted = true;
    repaint();
}

void PianoRoll::setTargetLayer (int layer)
{
    target = layer;
    for (auto& n : proc.getNotesForEdit())
        if (! editable (n)) n.selected = false;
    if (layer == mex::drums)
    {
        rowH = std::max (rowH, 16.0f);
        topPitch = 48.0f;
        clampScroll();
    }
    repaint();
    if (onStateChanged) onStateChanged();
}

void PianoRoll::rebuildPeaks()
{
    peaksFor = proc.getSample();
    peaks.clear();
    if (peaksFor == nullptr)
        return;
    constexpr int buckets = 4096;
    const int n = peaksFor->getNumSamples();
    peaks.assign (buckets, 0.0f);
    float mx = 1.0e-6f;
    for (int b = 0; b < buckets; ++b)
    {
        const int a = (int) ((juce::int64) n * b / buckets), e = (int) ((juce::int64) n * (b + 1) / buckets);
        float m = 0.0f;
        for (int c = 0; c < peaksFor->getNumChannels(); ++c)
            if (e > a) m = std::max (m, peaksFor->getMagnitude (c, a, e - a));
        peaks[(size_t) b] = m;
        mx = std::max (mx, m);
    }
    for (auto& v : peaks) v /= mx;
}

void PianoRoll::notesChangedExternally()
{
    if (proc.getSample() != peaksFor)
    {
        rebuildPeaks();
        fitted = false;
    }
    if (! fitted && (! proc.getNotes().empty() || proc.getSample() != nullptr) && ! proc.isAnalysing())
        zoomToFit();
    repaint();
}

//==============================================================================
// editing helpers
double PianoRoll::stepSeconds() const { return snapBeats > 0.0 ? snapBeats * proc.getBeatSeconds() : 0.0; }
double PianoRoll::snapRound (double t) const { const double s = stepSeconds(); return s > 0.0 ? std::round (t / s) * s : t; }
double PianoRoll::snapFloor (double t) const { const double s = stepSeconds(); return s > 0.0 ? std::floor (t / s) * s : t; }

int PianoRoll::noteAt (juce::Point<float> pos) const
{
    const auto& notes = proc.getNotes();
    for (int i = (int) notes.size(); --i >= 0;)
        if (editable (notes[(size_t) i]) && noteRect (notes[(size_t) i]).expanded (0.0f, 0.5f).contains (pos))
            return i;
    return -1;
}

void PianoRoll::begin()
{
    if (! editOpen)
    {
        proc.beginEdit();
        editOpen = true;
    }
}

void PianoRoll::commit()
{
    if (editOpen)
    {
        editOpen = false;
        proc.endEdit();
    }
    repaint();
    if (onStateChanged) onStateChanged();
}

void PianoRoll::audition (const mex::Note& n)
{
    lastAuditionPitch = n.pitch;
    proc.previewNote (n.pitch, channelFor (n.track), n.velocity);
}

std::vector<mex::Note> PianoRoll::selectedNotes() const
{
    std::vector<mex::Note> v;
    for (auto& n : proc.getNotes())
        if (n.selected) v.push_back (n);
    return v;
}

int PianoRoll::numSelected() const
{
    int c = 0;
    for (auto& n : proc.getNotes()) c += n.selected ? 1 : 0;
    return c;
}

void PianoRoll::selectNone()
{
    for (auto& n : proc.getNotesForEdit()) n.selected = false;
    repaint();
    if (onStateChanged) onStateChanged();
}

void PianoRoll::selectAllEditable()
{
    for (auto& n : proc.getNotesForEdit()) n.selected = editable (n);
    repaint();
    if (onStateChanged) onStateChanged();
}

void PianoRoll::deleteSelected()
{
    if (numSelected() == 0) return;
    begin();
    auto& v = proc.getNotesForEdit();
    v.erase (std::remove_if (v.begin(), v.end(), [] (const mex::Note& n) { return n.selected; }), v.end());
    commit();
}

void PianoRoll::transposeSelected (int semis)
{
    if (numSelected() == 0) return;
    begin();
    const mex::Note* first = nullptr;
    for (auto& n : proc.getNotesForEdit())
        if (n.selected) { n.pitch = juce::jlimit (0, 127, n.pitch + semis); if (first == nullptr) first = &n; }
    if (first != nullptr) audition (*first);
    commit();
}

void PianoRoll::moveSelected (double seconds)
{
    if (numSelected() == 0) return;
    double minStart = 1.0e9;
    for (auto& n : proc.getNotes()) if (n.selected) minStart = std::min (minStart, n.start);
    seconds = std::max (seconds, -minStart);
    begin();
    for (auto& n : proc.getNotesForEdit())
        if (n.selected) { n.start += seconds; n.end += seconds; }
    commit();
}

void PianoRoll::quantizeSelected()
{
    const double s = stepSeconds() > 0.0 ? stepSeconds() : 0.25 * proc.getBeatSeconds();
    const bool any = numSelected() > 0;
    begin();
    for (auto& n : proc.getNotesForEdit())
    {
        if (! (any ? n.selected : editable (n)))
            continue;
        const double len = n.end - n.start;
        n.start = std::max (0.0, std::round (n.start / s) * s);
        n.end = n.track == mex::drums ? n.start + std::min (len, s) : std::max (n.start + s, std::round (n.end / s) * s);
    }
    commit();
}

void PianoRoll::duplicateSelected()
{
    auto sel = selectedNotes();
    if (sel.empty()) return;
    double a = 1.0e9, b = 0.0;
    for (auto& n : sel) { a = std::min (a, n.start); b = std::max (b, n.end); }
    const double s = stepSeconds() > 0.0 ? stepSeconds() : proc.getBeatSeconds();
    const double offset = std::ceil ((b - a) / s - 1.0e-6) * s;
    begin();
    auto& v = proc.getNotesForEdit();
    for (auto& n : v) n.selected = false;
    for (auto n : sel) { n.start += offset; n.end += offset; n.selected = true; v.push_back (n); }
    commit();
}

void PianoRoll::copySelected() { clipboard = selectedNotes(); }

void PianoRoll::paste()
{
    if (clipboard.empty()) return;
    double a = 1.0e9;
    for (auto& n : clipboard) a = std::min (a, n.start);
    const double offset = snapRound (cursorSec) - a;
    begin();
    auto& v = proc.getNotesForEdit();
    for (auto& n : v) n.selected = false;
    for (auto n : clipboard)
    {
        n.start += offset; n.end += offset; n.selected = true;
        if (target >= 0) n.track = target;
        v.push_back (n);
    }
    commit();
}

void PianoRoll::togglePlay()
{
    if (proc.isPlaying()) proc.stop();
    else proc.play (cursorSec);
    repaint();
}

//==============================================================================
// mouse
void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto pos = e.position;
    downPos = pos;
    downSec = secAt (pos.x);
    downPitch = pitchAt (pos.y);
    drag = Drag::none;
    anchor = -1;

    if (rulerArea().contains (pos))
    {
        drag = Drag::ruler;
        cursorSec = juce::jlimit (0.0, totalSeconds(), snapRound (downSec));
        repaint();
        return;
    }
    if (keysArea().contains (pos))
    {
        drag = Drag::keys;
        lastAuditionPitch = downPitch;
        proc.previewNote (downPitch, target == mex::drums ? 10 : (target >= 0 ? mex::trackChannel (target) : 1), 100);
        return;
    }
    if (velArea().contains (pos))
    {
        drag = Drag::velocity;
        lastVelX = pos.x;
        mouseDrag (e);
        return;
    }
    if (! gridArea().contains (pos))
        return;

    auto& notes = proc.getNotesForEdit();
    if (e.mods.isPopupMenu()) // FL: right click deletes
    {
        drag = Drag::erase;
        mouseDrag (e);
        return;
    }

    const int hit = noteAt (pos);
    const bool rubberWanted = tool == Tool::select ? hit < 0 : (e.mods.isCommandDown() || e.mods.isCtrlDown());
    if (rubberWanted)
    {
        drag = Drag::rubber;
        if (! e.mods.isShiftDown()) for (auto& n : notes) n.selected = false;
        dragOrig = notes;
        rubber = { pos, pos };
        repaint();
        return;
    }

    if (hit >= 0)
    {
        auto& n = notes[(size_t) hit];
        if (tool == Tool::select && e.mods.isShiftDown() && n.selected)
        {
            n.selected = false; // toggle off
            repaint();
            if (onStateChanged) onStateChanged();
            return;
        }
        if (! n.selected)
        {
            if (! e.mods.isShiftDown() || tool == Tool::draw) for (auto& x : notes) x.selected = false;
            n.selected = true;
        }
        lastLength = n.end - n.start;
        lastVelocity = n.velocity;
        anchor = hit;
        audition (n);

        const auto r = noteRect (n);
        drag = pos.x > r.getRight() - std::min (9.0f, r.getWidth() * 0.35f) ? Drag::resize : Drag::move;

        if (drag == Drag::move && tool == Tool::draw && e.mods.isShiftDown()) // Shift+drag = duplicate
        {
            begin();
            const size_t count = notes.size();
            for (size_t i = 0; i < count; ++i)
                if (notes[i].selected)
                {
                    auto c = notes[i];
                    notes[i].selected = false;
                    notes.push_back (c);
                    if ((int) i == hit) anchor = (int) notes.size() - 1;
                }
        }
        dragOrig = notes;
        repaint();
        if (onStateChanged) onStateChanged();
        return;
    }

    if (tool == Tool::draw) // new note, then it follows the mouse
    {
        begin();
        for (auto& x : notes) x.selected = false;
        mex::Note n;
        const double step = stepSeconds();
        n.start = std::max (0.0, snapFloor (downSec));
        const double len = lastLength > 0.0 ? lastLength : (step > 0.0 ? step : 0.25 * proc.getBeatSeconds());
        n.end = n.start + len;
        n.pitch = downPitch;
        n.velocity = lastVelocity;
        n.track = target >= 0 ? target : (downPitch <= (int) proc.apvts.getRawParameterValue ("bassSplit")->load() ? mex::bass : mex::melody);
        n.selected = true;
        notes.push_back (n);
        anchor = (int) notes.size() - 1;
        audition (n);
        drag = Drag::move;
        dragOrig = notes;
        downSec = secAt (pos.x);
        repaint();
    }
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    auto& notes = proc.getNotesForEdit();

    switch (drag)
    {
        case Drag::ruler:
            cursorSec = juce::jlimit (0.0, totalSeconds(), snapRound (secAt (pos.x)));
            repaint();
            break;

        case Drag::keys:
        {
            const int p = pitchAt (pos.y);
            if (p != lastAuditionPitch)
            {
                lastAuditionPitch = p;
                proc.previewNote (p, target == mex::drums ? 10 : 1, 100);
            }
            break;
        }

        case Drag::velocity:
        {
            const auto va = velArea();
            const int v = juce::jlimit (1, 127, (int) std::round ((va.getBottom() - 3.0f - pos.y) / (va.getHeight() - 8.0f) * 127.0f));
            const float x0 = std::min (lastVelX, pos.x) - 4.0f, x1 = std::max (lastVelX, pos.x) + 4.0f;
            const bool onlySel = numSelected() > 0;
            bool changed = false;
            for (auto& n : notes)
            {
                if (! editable (n) || (onlySel && ! n.selected)) continue;
                const float x = xOf (n.start);
                if (x >= x0 && x <= x1 && n.velocity != v)
                {
                    begin();
                    n.velocity = v;
                    changed = true;
                }
            }
            lastVelX = pos.x;
            if (changed) { lastVelocity = v; repaint(); }
            break;
        }

        case Drag::erase:
        {
            const int hit = noteAt (pos);
            if (hit >= 0)
            {
                begin();
                notes.erase (notes.begin() + hit);
                repaint();
            }
            break;
        }

        case Drag::rubber:
        {
            if (notes.size() != dragOrig.size()) { drag = Drag::none; break; }
            rubber = juce::Rectangle<float> (downPos, pos);
            for (size_t i = 0; i < notes.size(); ++i)
                notes[i].selected = dragOrig[i].selected || (editable (notes[i]) && noteRect (notes[i]).intersects (rubber));
            repaint();
            break;
        }

        case Drag::move:
        {
            if (anchor < 0 || notes.size() != dragOrig.size()) { drag = Drag::none; break; }
            if (! editOpen && e.getDistanceFromDragStart() < 3) break;
            begin();
            const auto& a = dragOrig[(size_t) anchor];
            const double dt = secAt (pos.x) - downSec;
            double delta = (stepSeconds() > 0.0 ? snapRound (a.start + dt) : a.start + dt) - a.start;
            int dp = pitchAt (pos.y) - downPitch;
            double minStart = 1.0e9;
            int minP = 127, maxP = 0;
            for (auto& n : dragOrig)
                if (n.selected) { minStart = std::min (minStart, n.start); minP = std::min (minP, n.pitch); maxP = std::max (maxP, n.pitch); }
            delta = std::max (delta, -minStart);
            dp = juce::jlimit (-minP, 127 - maxP, dp);
            for (size_t i = 0; i < notes.size(); ++i)
                if (dragOrig[i].selected)
                {
                    notes[i] = dragOrig[i];
                    notes[i].start += delta;
                    notes[i].end += delta;
                    notes[i].pitch += dp;
                }
            if (notes[(size_t) anchor].pitch != lastAuditionPitch)
                audition (notes[(size_t) anchor]);
            repaint();
            break;
        }

        case Drag::resize:
        {
            if (anchor < 0 || notes.size() != dragOrig.size()) { drag = Drag::none; break; }
            begin();
            const auto& a = dragOrig[(size_t) anchor];
            const double dt = secAt (pos.x) - downSec;
            const double newEnd = stepSeconds() > 0.0 ? snapRound (a.end + dt) : a.end + dt;
            const double dEnd = newEnd - a.end;
            const double minLen = stepSeconds() > 0.0 ? stepSeconds() * 0.5 : 0.02;
            for (size_t i = 0; i < notes.size(); ++i)
                if (dragOrig[i].selected)
                {
                    notes[i] = dragOrig[i];
                    notes[i].end = std::max (notes[i].start + minLen, dragOrig[i].end + dEnd);
                }
            lastLength = notes[(size_t) anchor].end - notes[(size_t) anchor].start;
            repaint();
            break;
        }

        case Drag::none:
        case Drag::create:
            break;
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::ruler && proc.isPlaying())
        proc.play (cursorSec);
    if (drag == Drag::rubber)
    {
        rubber = {};
        if (onStateChanged) onStateChanged();
    }
    drag = Drag::none;
    anchor = -1;
    commit();
}

void PianoRoll::updateCursor (juce::Point<float> pos)
{
    auto c = juce::MouseCursor::NormalCursor;
    if (gridArea().contains (pos))
    {
        const int hit = noteAt (pos);
        if (hit >= 0)
        {
            const auto r = noteRect (proc.getNotes()[(size_t) hit]);
            c = pos.x > r.getRight() - std::min (9.0f, r.getWidth() * 0.35f) ? juce::MouseCursor::LeftRightResizeCursor
                                                                              : juce::MouseCursor::DraggingHandCursor;
        }
        else if (tool == Tool::draw)
            c = juce::MouseCursor::CrosshairCursor;
    }
    else if (keysArea().contains (pos) || rulerArea().contains (pos))
        c = juce::MouseCursor::PointingHandCursor;
    else if (velArea().contains (pos))
        c = juce::MouseCursor::UpDownResizeCursor;
    setMouseCursor (c);
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    updateCursor (e.position);
    const int h = gridArea().contains (e.position) ? noteAt (e.position) : -1;
    if (h != hoverNote) { hoverNote = h; repaint(); }
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        zoomX (std::pow (1.0015, (w.deltaY + w.deltaX) * 300.0), e.position.x);
    else if (e.mods.isAltDown())
        zoomY (std::pow (1.0015f, w.deltaY * 300.0f), e.position.y);
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        scrollSec -= d * 400.0 / pxPerSec;
        clampScroll();
        repaint();
    }
    else
    {
        topPitch += w.deltaY * 30.0f;
        clampScroll();
        repaint();
    }
}

void PianoRoll::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    zoomX (scale, e.position.x);
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    const int code = k.getKeyCode();
    const bool cmd = m.isCommandDown() || m.isCtrlDown();

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (code == juce::KeyPress::escapeKey) { selectNone(); return true; }
    if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (code == juce::KeyPress::upKey) { transposeSelected (m.isShiftDown() ? 12 : 1); return true; }
    if (code == juce::KeyPress::downKey) { transposeSelected (m.isShiftDown() ? -12 : -1); return true; }
    const double nudge = stepSeconds() > 0.0 ? stepSeconds() : 0.25 * proc.getBeatSeconds();
    if (code == juce::KeyPress::leftKey) { moveSelected (-nudge); return true; }
    if (code == juce::KeyPress::rightKey) { moveSelected (nudge); return true; }

    const int ch = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);
    if (cmd)
    {
        if (ch == 'A') { selectAllEditable(); return true; }
        if (ch == 'Z') { m.isShiftDown() ? proc.redo() : proc.undo(); repaint(); if (onStateChanged) onStateChanged(); return true; }
        if (ch == 'Y') { proc.redo(); repaint(); if (onStateChanged) onStateChanged(); return true; }
        if (ch == 'C') { copySelected(); return true; }
        if (ch == 'V') { paste(); return true; }
        if (ch == 'D' || ch == 'B') { duplicateSelected(); return true; }
        return false;
    }
    if (ch == 'Q') { quantizeSelected(); return true; }
    if (ch == 'P' || ch == 'B') { setTool (Tool::draw); return true; }
    if (ch == 'E') { setTool (Tool::select); return true; }
    return false;
}

void PianoRoll::timerCallback()
{
    if (proc.isPlaying())
    {
        const double t = proc.getPlayPositionSeconds();
        const double view = gridArea().getWidth() / pxPerSec;
        if (drag == Drag::none && (t > scrollSec + view * 0.95 || t < scrollSec))
        {
            scrollSec = t - view * 0.05;
            clampScroll();
        }
        repaint();
    }
    else if (proc.isAnalysing())
        repaint();
}

//==============================================================================
// painting
void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (T::bg2);
    const auto grid = gridArea();
    const auto& notes = proc.getNotes();
    const double beat = proc.getBeatSeconds();
    const double step = stepSeconds();

    //== rows
    const int pTop = pitchAt (grid.getY() + 0.5f), pBot = pitchAt (grid.getBottom() - 0.5f);
    for (int p = pBot; p <= pTop; ++p)
    {
        const float y = yOf (p);
        if (juce::MidiMessage::isMidiNoteBlack (p))
        {
            g.setColour (T::bg.withAlpha (0.65f));
            g.fillRect (grid.getX(), y, grid.getWidth(), rowH);
        }
        if (p % 12 == 0)
        {
            g.setColour (T::line);
            g.drawHorizontalLine ((int) (y + rowH), grid.getX(), grid.getRight());
        }
        else if (p % 12 == 5)
        {
            g.setColour (T::line.withAlpha (0.4f));
            g.drawHorizontalLine ((int) (y + rowH), grid.getX(), grid.getRight());
        }
    }

    g.saveState();
    g.reduceClipRegion (grid.toNearestInt());

    //== waveform ghost
    if (showWave && ! peaks.empty() && proc.getSampleLengthSeconds() > 0.0)
    {
        const double len = proc.getSampleLengthSeconds();
        g.setColour (T::accent.withAlpha (0.07f));
        const float mid = grid.getCentreY(), half = grid.getHeight() * 0.42f;
        for (int x = (int) grid.getX(); x < (int) grid.getRight(); ++x)
        {
            const double t0 = secAt ((float) x), t1 = secAt ((float) x + 1.0f);
            if (t1 < 0.0 || t0 > len) continue;
            const auto a = (size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) (t0 / len * peaks.size()));
            const auto b = (size_t) juce::jlimit ((int) a + 1, (int) peaks.size(), (int) (t1 / len * peaks.size()) + 1);
            float m = 0.0f;
            for (size_t i = a; i < b; ++i) m = std::max (m, peaks[i]);
            g.drawVerticalLine (x, mid - m * half, mid + m * half);
        }
    }

    //== vertical grid: steps, beats, bars
    auto drawLines = [&] (double unit, juce::Colour c)
    {
        if (unit <= 0.0 || unit * pxPerSec < 5.0) return;
        g.setColour (c);
        const double t1 = secAt (grid.getRight());
        for (double t = std::floor (scrollSec / unit) * unit; t <= t1; t += unit)
            g.drawVerticalLine ((int) std::round (xOf (t)), grid.getY(), grid.getBottom());
    };
    drawLines (step, T::line.withAlpha (0.35f));
    drawLines (beat, T::line.withAlpha (0.8f));
    drawLines (beat * 4.0, T::faint.withAlpha (0.9f));

    if (proc.getSampleLengthSeconds() > 0.0) // end of the sample
    {
        g.setColour (T::accent.withAlpha (0.35f));
        const float x = xOf (proc.getSampleLengthSeconds());
        g.drawLine (x, grid.getY(), x, grid.getBottom(), 1.0f);
    }

    //== notes
    auto drawNote = [&] (const mex::Note& n, bool ghost, bool hovered)
    {
        const auto r = noteRect (n);
        if (r.getRight() < grid.getX() || r.getX() > grid.getRight() || r.getBottom() < grid.getY() || r.getY() > grid.getBottom())
            return;
        const auto c = T::layer (n.track);
        const float rad = std::min (3.0f, r.getHeight() * 0.3f);
        if (ghost)
        {
            g.setColour (c.withAlpha (0.13f));
            g.fillRoundedRectangle (r, rad);
            g.setColour (c.withAlpha (0.28f));
            g.drawRoundedRectangle (r, rad, 0.8f);
            return;
        }
        const float a = 0.5f + 0.5f * n.velocity / 127.0f;
        g.setColour (n.selected ? c.brighter (0.35f) : c.withAlpha (a));
        g.fillRoundedRectangle (r, rad);
        // velocity bar inside the note (like FL's colour by velocity)
        g.setColour (T::bg.withAlpha (0.25f));
        g.fillRect (r.withTrimmedLeft (r.getWidth() * (float) n.velocity / 127.0f).reduced (0.0f, r.getHeight() * 0.38f));
        g.setColour (n.selected ? juce::Colours::white : c.brighter (0.6f).withAlpha (hovered ? 1.0f : 0.7f));
        g.drawRoundedRectangle (r, rad, n.selected ? 1.6f : (hovered ? 1.3f : 0.8f));
        if (r.getWidth() > 24.0f && r.getHeight() >= 9.0f)
        {
            g.setColour (T::bg);
            g.setFont (T::font (std::min (11.0f, r.getHeight() - 1.0f), true));
            g.drawText (n.track == mex::drums ? mex::drumName (n.pitch) : mex::noteName (n.pitch),
                        r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    };
    if (showGhosts)
        for (auto& n : notes)
            if (! editable (n)) drawNote (n, true, false);
    for (size_t i = 0; i < notes.size(); ++i)
        if (editable (notes[i])) drawNote (notes[i], false, (int) i == hoverNote);

    if (! rubber.isEmpty())
    {
        g.setColour (T::accent.withAlpha (0.12f));
        g.fillRect (rubber);
        g.setColour (T::accent);
        g.drawRect (rubber, 1.0f);
    }

    // cursor & playhead
    g.setColour (T::accent2.withAlpha (0.5f));
    g.drawVerticalLine ((int) xOf (cursorSec), grid.getY(), grid.getBottom());
    if (proc.isPlaying())
    {
        g.setColour (juce::Colours::white);
        const float x = xOf (proc.getPlayPositionSeconds());
        g.drawLine (x, grid.getY(), x, grid.getBottom(), 1.5f);
    }

    if (notes.empty() && ! proc.isAnalysing())
    {
        g.setColour (T::dim);
        g.setFont (T::font (15.0f, true));
        g.drawText (proc.getSample() == nullptr ? fr ("Glisse un sample audio ou un fichier .mid ici")
                                                : fr ("Aucune note — augmente la sensibilité, ou dessine au crayon (P)"),
                    grid.withTrimmedBottom (20.0f), juce::Justification::centred);
        g.setColour (T::faint);
        g.setFont (T::font (12.5f));
        g.drawText (fr ("Crayon : clic = note · clic droit = effacer · Ctrl/Cmd + glisser = sélection · molette = défiler, Cmd + molette = zoom"),
                    grid.withTrimmedTop (30.0f), juce::Justification::centred);
    }
    g.restoreState();

    //== ruler
    const auto ru = rulerArea();
    g.setColour (T::panel);
    g.fillRect (ru);
    g.setColour (T::line);
    g.drawHorizontalLine ((int) ru.getBottom() - 1, ru.getX(), ru.getRight());
    {
        g.saveState();
        g.reduceClipRegion (ru.toNearestInt());
        const double bar = beat * 4.0;
        const double t1 = secAt (ru.getRight());
        const long barsPerLabel = std::max (1L, (long) std::ceil (40.0 / std::max (1.0, bar * pxPerSec)));
        g.setFont (T::font (11.0f, true));
        for (double t = std::floor (scrollSec / beat) * beat; t <= t1; t += beat)
        {
            const float x = xOf (t);
            const long bi = std::lround (t / beat);
            const bool isBar = bi % 4 == 0;
            g.setColour (isBar ? T::dim : T::faint);
            g.drawVerticalLine ((int) x, isBar ? ru.getY() + 6.0f : ru.getBottom() - 7.0f, ru.getBottom());
            if (isBar && (bi / 4) % barsPerLabel == 0)
                g.drawText (juce::String (bi / 4 + 1), (int) x + 4, (int) ru.getY() + 2, 40, (int) ru.getHeight() - 4, juce::Justification::centredLeft);
        }
        // cursor marker
        juce::Path tri;
        const float cx = xOf (cursorSec);
        tri.addTriangle (cx - 6.0f, ru.getY() + 2.0f, cx + 6.0f, ru.getY() + 2.0f, cx, ru.getY() + 11.0f);
        g.setColour (T::accent2);
        g.fillPath (tri);
        g.restoreState();
    }
    g.setColour (T::panel);
    g.fillRect (0.0f, 0.0f, keyW, rulerH);

    //== keyboard
    const auto ka = keysArea();
    g.saveState();
    g.reduceClipRegion (ka.toNearestInt());
    g.setColour (T::panel);
    g.fillRect (ka);
    bool anyDrums = target == mex::drums;
    for (auto& n : notes) anyDrums = anyDrums || (n.track == mex::drums && (target < 0 || target == mex::drums));
    for (int p = pBot; p <= pTop; ++p)
    {
        const float y = yOf (p);
        const bool black = juce::MidiMessage::isMidiNoteBlack (p);
        const bool pressed = drag == Drag::keys && p == lastAuditionPitch;
        auto keyR = juce::Rectangle<float> (ka.getX(), y, ka.getWidth() - 2.0f, rowH).reduced (0.0f, 0.5f);
        if (black)
        {
            g.setColour (pressed ? T::accent : juce::Colour (0xff1a1724));
            g.fillRect (keyR.withWidth (keyR.getWidth() * 0.62f));
            g.setColour (juce::Colour (0xffcfcbe0).withAlpha (0.9f));
            g.fillRect (keyR.withTrimmedLeft (keyR.getWidth() * 0.62f));
        }
        else
        {
            g.setColour (pressed ? T::accent : juce::Colour (0xffdcd8ea));
            g.fillRect (keyR);
        }
        const bool drumRow = anyDrums && (p == mex::kKick || p == mex::kSnare || p == mex::kHat);
        if ((p % 12 == 0 || drumRow) && rowH >= 7.0f)
        {
            g.setColour (drumRow ? T::layer (mex::drums).darker (0.4f) : juce::Colour (0xff3a3550));
            g.setFont (T::font (juce::jlimit (8.0f, 11.0f, rowH * 0.85f), true));
            g.drawText (drumRow ? mex::drumName (p) : mex::noteName (p), keyR.reduced (4.0f, 0.0f), juce::Justification::centredRight);
        }
    }
    g.restoreState();
    g.setColour (T::line);
    g.drawVerticalLine ((int) ka.getRight() - 1, ka.getY(), ka.getBottom());

    //== velocity lane
    const auto va = velArea();
    g.setColour (T::panel);
    g.fillRect (va.withX (0.0f).withWidth ((float) getWidth()));
    g.setColour (T::line);
    g.drawHorizontalLine ((int) va.getY(), 0.0f, (float) getWidth());
    g.setColour (T::dim);
    g.setFont (T::font (11.0f, true));
    g.drawFittedText (fr ("Vélocité"), juce::Rectangle<int> (4, (int) va.getY() + 4, (int) keyW - 10, 30), juce::Justification::topRight, 2);
    g.saveState();
    g.reduceClipRegion (va.toNearestInt());
    for (int q = 1; q < 4; ++q)
    {
        g.setColour (T::line.withAlpha (0.5f));
        g.drawHorizontalLine ((int) (va.getBottom() - 3.0f - (va.getHeight() - 8.0f) * q / 4.0f), va.getX(), va.getRight());
    }
    const bool anySel = numSelected() > 0;
    for (auto& n : notes)
    {
        const float x = xOf (n.start);
        if (x < va.getX() - 4.0f || x > va.getRight() + 4.0f) continue;
        const bool ed = editable (n);
        if (! ed && ! showGhosts) continue;
        const float h = (va.getHeight() - 8.0f) * n.velocity / 127.0f;
        const auto c = T::layer (n.track);
        const float alpha = ! ed ? 0.15f : (anySel && ! n.selected ? 0.35f : 0.95f);
        g.setColour ((n.selected ? c.brighter (0.4f) : c).withAlpha (alpha));
        g.drawLine (x, va.getBottom() - 3.0f, x, va.getBottom() - 3.0f - h, 2.0f);
        g.fillEllipse (x - 3.0f, va.getBottom() - 6.0f - h, 6.0f, 6.0f);
    }
    g.restoreState();

    //== overlays
    if (proc.isAnalysing())
    {
        g.setColour (T::bg.withAlpha (0.6f));
        g.fillRect (grid);
        auto bar = grid.withSizeKeepingCentre (300.0f, 6.0f);
        g.setColour (T::panel3);
        g.fillRoundedRectangle (bar, 3.0f);
        juce::ColourGradient gr (T::accent, bar.getX(), 0.0f, T::accent2, bar.getRight(), 0.0f, false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.02f, 1.0f, proc.getProgress())), 3.0f);
        g.setColour (T::text);
        g.setFont (T::font (14.0f, true));
        g.drawText (fr ("L'oracle écoute…"), bar.translated (0.0f, -28.0f).withHeight (20.0f), juce::Justification::centred);
    }
    if (dragHover)
    {
        g.setColour (T::accent2);
        g.drawRect (getLocalBounds().toFloat().reduced (1.0f), 2.0f);
    }
}
