#include "ui/PianoRollPanel.h"
#include "ui/Theme.h"

#include <cmath>

namespace beatforge
{
    namespace
    {
        constexpr int numRows = PianoRollPanel::highestPitch - PianoRollPanel::lowestPitch + 1;
        constexpr int velocityLaneHeight = 76;

        bool isBlackKey (int pitch)
        {
            const int pc = pitch % 12;
            return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        }

        juce::String pitchName (int pitch)
        {
            static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return juce::String (names[pitch % 12]) + juce::String (pitch / 12 - 1);   // C4 = 60
        }

        const struct { const char* name; time::Tick ticks; } grids[] {
            { "1/4", midi::quarter }, { "1/8", midi::eighth }, { "1/16", midi::sixteenth }, { "1/32", midi::thirtySecond }
        };
    }

    //==============================================================================
    // Keyboard + note grid, inside the vertical viewport.
    class PianoRollPanel::Editor final : public juce::Component,
                                         public juce::SettableTooltipClient
    {
    public:
        explicit Editor (PianoRollPanel& p) : owner (p)
        {
            setWantsKeyboardFocus (true);
            setSize (400, numRows * rowHeight);
        }

        // --- geometry -------------------------------------------------------------
        time::Tick clipLength() const
        {
            const auto* clip = owner.clipPtr();
            return clip ? clip->length : 3840;
        }

        float gridLeft() const     { return (float) keyboardWidth; }
        float gridWidth() const    { return (float) getWidth() - gridLeft(); }
        float tickToX (double t) const { return gridLeft() + (float) (t / (double) clipLength()) * gridWidth(); }
        double xToTick (float x) const { return (double) (x - gridLeft()) / gridWidth() * (double) clipLength(); }
        int yToPitch (float y) const   { return juce::jlimit (lowestPitch, highestPitch, highestPitch - (int) (y / (float) rowHeight)); }
        float pitchToY (int pitch) const { return (float) ((highestPitch - pitch) * rowHeight); }

        juce::Rectangle<float> noteBounds (const midi::Note& n) const
        {
            const auto x0 = tickToX ((double) n.start), x1 = tickToX ((double) n.end());
            return { x0, pitchToY (n.pitch), juce::jmax (3.0f, x1 - x0), (float) rowHeight };
        }

        const midi::Note* noteAt (juce::Point<float> p) const
        {
            const auto* clip = owner.clipPtr();
            if (! clip) return nullptr;
            // Topmost = last drawn = last in the list.
            for (auto it = clip->notes.rbegin(); it != clip->notes.rend(); ++it)
                if (it->start < clip->length && noteBounds (*it).contains (p))
                    return &*it;
            return nullptr;
        }

        void setPlayhead (double clipTick)
        {
            auto strip = [this] (double t) { return juce::Rectangle<int> ((int) tickToX (t) - 2, 0, 5, getHeight()); };
            if (playhead >= 0.0) repaint (strip (playhead));
            playhead = clipTick;
            if (playhead >= 0.0) repaint (strip (playhead));
        }

        // --- painting -------------------------------------------------------------
        void paint (juce::Graphics& g) override
        {
            const auto* clip = owner.clipPtr();
            const auto clipLen = clipLength();
            const auto grid = owner.gridTicks();
            const auto perBeat = owner.session.getProject().timeSignature.ticksPerBeat();
            const auto perBar = owner.session.getProject().timeSignature.ticksPerBar();
            const auto clipBounds = g.getClipBounds();

            // Pitch rows: black-key rows darker, a line at every C.
            for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
            {
                const auto y = pitchToY (pitch);
                if (y > (float) clipBounds.getBottom() || y + rowHeight < (float) clipBounds.getY())
                    continue;
                g.setColour (isBlackKey (pitch) ? theme::background : theme::panel);
                g.fillRect (gridLeft(), y, gridWidth(), (float) rowHeight);
                if (pitch % 12 == 0)
                {
                    g.setColour (theme::border);
                    g.drawHorizontalLine ((int) (y + rowHeight) - 1, gridLeft(), (float) getWidth());
                }
            }

            // Time lines: bars strong, beats medium, grid faint.
            for (time::Tick t = 0; t <= clipLen; t += grid)
            {
                const auto x = (int) tickToX ((double) t);
                g.setColour (t % perBar == 0 ? theme::borderStrong : (t % perBeat == 0 ? theme::border : theme::border.withAlpha (0.35f)));
                g.drawVerticalLine (x, (float) clipBounds.getY(), (float) clipBounds.getBottom());
            }

            // Notes: track colour, brightness follows velocity, selection outlined.
            if (clip)
            {
                for (auto& n : clip->notes)
                {
                    if (n.start >= clip->length)
                        continue;   // beyond a shortened clip: kept, not shown
                    const auto r = noteBounds (n).reduced (0.5f, 1.0f);
                    if (! r.toNearestInt().intersects (clipBounds))
                        continue;

                    const bool selected = owner.selection.count (n.id) > 0;
                    const auto colour = theme::track::synth;
                    g.setColour (colour.withAlpha (0.45f + 0.55f * (float) n.velocity / 127.0f));
                    g.fillRoundedRectangle (r, 2.5f);
                    g.setColour (selected ? theme::textPrimary : colour.brighter (0.4f));
                    g.drawRoundedRectangle (r, 2.5f, selected ? 1.6f : 1.0f);
                }
            }

            // Marquee.
            if (mode == Mode::marquee)
            {
                g.setColour (theme::violet.withAlpha (0.15f));
                g.fillRect (marquee);
                g.setColour (theme::violet);
                g.drawRect (marquee, 1.0f);
            }

            // Playhead.
            if (playhead >= 0.0)
            {
                const auto x = tickToX (playhead);
                g.setColour (theme::playhead);
                g.fillRect (x - 1.0f, (float) clipBounds.getY(), 2.0f, (float) clipBounds.getHeight());
            }

            paintKeyboard (g, clipBounds);
        }

        void paintKeyboard (juce::Graphics& g, juce::Rectangle<int> visible)
        {
            const float keysWidth = (float) keyboardWidth - 2.0f;
            const float blackWidth = keysWidth * 0.62f;

            // One continuous ivory column; black keys and divisions on top.
            g.setColour (theme::textPrimary.withAlpha (0.9f));
            g.fillRect (0.0f, (float) visible.getY(), keysWidth, (float) visible.getHeight());

            for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
            {
                const auto y = pitchToY (pitch);
                if (y > (float) visible.getBottom() || y + rowHeight < (float) visible.getY())
                    continue;

                const bool black = isBlackKey (pitch);
                const bool held = pitch == heldPitch;

                if (black)
                {
                    // White-key division runs through the middle of each black key.
                    g.setColour (theme::border.withAlpha (0.6f));
                    g.drawHorizontalLine ((int) (y + rowHeight * 0.5f), blackWidth, keysWidth);

                    g.setColour (held ? theme::track::synth : theme::background);
                    g.fillRoundedRectangle (juce::Rectangle<float> (-3.0f, y + 1.0f, blackWidth + 3.0f, (float) rowHeight - 2.0f), 2.0f);
                }
                else
                {
                    if (held)
                    {
                        g.setColour (theme::track::synth);
                        g.fillRect (0.0f, y, keysWidth, (float) rowHeight);
                    }

                    // E-F and B-C have no black key between them: draw the edge.
                    if (! isBlackKey (pitch + 1))
                    {
                        g.setColour (theme::border.withAlpha (0.6f));
                        g.drawHorizontalLine ((int) y, 0.0f, keysWidth);
                    }

                    if (pitch % 12 == 0)
                    {
                        g.setColour (held ? theme::textPrimary : theme::background);
                        g.setFont (theme::font (9.5f, juce::Font::bold));
                        g.drawText (pitchName (pitch), juce::Rectangle<float> (0.0f, y, keysWidth - 4.0f, (float) rowHeight),
                                    juce::Justification::centredRight);
                    }
                }
            }

            g.setColour (theme::border);
            g.drawVerticalLine (keyboardWidth - 1, (float) visible.getY(), (float) visible.getBottom());
        }

        // --- mouse ----------------------------------------------------------------
        void mouseDown (const juce::MouseEvent& e) override
        {
            grabKeyboardFocus();
            const auto pos = e.position;

            if (pos.x < gridLeft())   // keyboard: audition
            {
                heldPitch = yToPitch (pos.y);
                owner.engine.previewNoteOn (owner.slot(), heldPitch, 100);
                repaint (0, 0, keyboardWidth, getHeight());
                return;
            }

            if (owner.clipPtr() == nullptr)
                return;

            const auto* hit = noteAt (pos);
            const bool additive = e.mods.isShiftDown() || e.mods.isCtrlDown();

            if (hit != nullptr && e.mods.isPopupMenu())
            {
                owner.selection.erase (hit->id);
                owner.session.removeNotes (owner.clipId(), { hit->id });
                return;
            }

            dragStart = pos;

            if (hit != nullptr)
            {
                if (additive)
                {
                    if (! owner.selection.erase (hit->id))
                        owner.selection.insert (hit->id);
                }
                else if (owner.selection.count (hit->id) == 0)
                {
                    owner.selection = { hit->id };
                }

                anchorId = hit->id;
                originals.clear();
                for (auto id : owner.selection)
                    if (auto* n = owner.session.findNote (owner.clipId(), id))
                        originals.push_back (*n);

                mode = noteBounds (*hit).getRight() - pos.x <= 6.0f ? Mode::resize : Mode::move;
                owner.session.beginGesture();   // the whole drag is one undo step
                auditionPitch = hit->pitch;
                owner.engine.previewNoteOn (owner.slot(), auditionPitch, hit->velocity);
                owner.repaint();
                return;
            }

            if (! additive)
                owner.selection.clear();
            mode = Mode::pendingCreate;
            owner.repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto pos = e.position;

            if (heldPitch >= 0)
            {
                const auto pitch = yToPitch (pos.y);
                if (pitch != heldPitch)
                {
                    owner.engine.previewNoteOff (owner.slot(), heldPitch);
                    heldPitch = pitch;
                    owner.engine.previewNoteOn (owner.slot(), heldPitch, 100);
                    repaint (0, 0, keyboardWidth, getHeight());
                }
                return;
            }

            const auto grid = owner.gridTicks();
            const auto rawTicks = xToTick (pos.x) - xToTick (dragStart.x);
            const auto deltaTicks = (time::Tick) std::round (rawTicks / (double) grid) * grid;
            const auto deltaPitch = -(int) std::round ((pos.y - dragStart.y) / (float) rowHeight);

            switch (mode)
            {
                case Mode::pendingCreate:
                    if (pos.getDistanceFrom (dragStart) > 4.0f)
                        mode = Mode::marquee;
                    [[fallthrough]];
                case Mode::marquee:
                    if (mode == Mode::marquee)
                    {
                        marquee = juce::Rectangle<float> (dragStart, pos).withLeft (juce::jmax (gridLeft(), juce::jmin (dragStart.x, pos.x)));
                        selectInMarquee (e.mods.isShiftDown() || e.mods.isCtrlDown());
                        repaint();
                    }
                    break;

                case Mode::move:
                {
                    std::vector<midi::Note> moved;
                    for (auto n : originals)
                    {
                        n.start = juce::jlimit<time::Tick> (0, clipLength() - 1, n.start + deltaTicks);
                        n.pitch = juce::jlimit (midi::minPitch, midi::maxPitch, n.pitch + deltaPitch);
                        moved.push_back (n);
                    }
                    owner.session.updateNotes (owner.clipId(), moved);

                    if (auto* anchor = owner.session.findNote (owner.clipId(), anchorId); anchor && anchor->pitch != auditionPitch)
                    {
                        owner.engine.previewNoteOff (owner.slot(), auditionPitch);
                        auditionPitch = anchor->pitch;
                        owner.engine.previewNoteOn (owner.slot(), auditionPitch, anchor->velocity);
                    }
                    break;
                }

                case Mode::resize:
                {
                    std::vector<midi::Note> resized;
                    for (auto n : originals)
                    {
                        n.length = juce::jmax<time::Tick> (grid, n.length + deltaTicks);
                        resized.push_back (n);
                    }
                    owner.session.updateNotes (owner.clipId(), resized);
                    if (auto* anchor = owner.session.findNote (owner.clipId(), anchorId))
                        owner.newNoteLength = anchor->length;
                    break;
                }

                case Mode::none:
                    break;
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (heldPitch >= 0)
            {
                owner.engine.previewNoteOff (owner.slot(), heldPitch);
                heldPitch = -1;
                repaint (0, 0, keyboardWidth, getHeight());
                return;
            }

            if (mode == Mode::pendingCreate)
            {
                const auto grid = owner.gridTicks();
                const auto start = (time::Tick) std::floor (xToTick (e.position.x) / (double) grid) * grid;
                const auto pitch = yToPitch (e.position.y);
                const auto id = owner.session.addNote (owner.clipId(), { 0, pitch, 100, start, owner.newNoteLength });
                if (id != 0)
                {
                    owner.selection = { id };
                    owner.engine.previewNoteOn (owner.slot(), pitch, 100);
                    juce::Timer::callAfterDelay (180, [this, pitch, safe = juce::Component::SafePointer<Editor> (this)]
                    {
                        if (safe != nullptr)
                            owner.engine.previewNoteOff (owner.slot(), pitch);
                    });
                }
            }

            if (mode == Mode::move || mode == Mode::resize)
            {
                owner.engine.previewNoteOff (owner.slot(), auditionPitch);
                owner.session.endGesture();
            }

            mode = Mode::none;
            owner.repaint();
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto* hit = e.position.x >= gridLeft() ? noteAt (e.position) : nullptr;
            const bool edge = hit != nullptr && noteBounds (*hit).getRight() - e.position.x <= 6.0f;
            setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor
                                 : hit != nullptr ? juce::MouseCursor::DraggingHandCursor
                                 : e.position.x < gridLeft() ? juce::MouseCursor::PointingHandCursor
                                 : juce::MouseCursor::NormalCursor);
        }

        // --- keys -----------------------------------------------------------------
        bool keyPressed (const juce::KeyPress& key) override
        {
            auto& session = owner.session;
            const auto* clip = owner.clipPtr();
            if (! clip)
                return false;

            const auto selectedIds = std::vector<midi::NoteId> (owner.selection.begin(), owner.selection.end());

            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            {
                session.removeNotes (owner.clipId(), selectedIds);
                owner.selection.clear();
                return true;
            }

            if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
            {
                owner.selection.clear();
                for (auto& n : clip->notes)
                    owner.selection.insert (n.id);
                owner.repaint();
                return true;
            }

            if (key == juce::KeyPress::escapeKey)
            {
                owner.selection.clear();
                owner.repaint();
                return true;
            }

            // Plain Q only: Ctrl+Q must still reach the window (quit).
            if (key == juce::KeyPress ('q') || key == juce::KeyPress ('Q'))
            {
                owner.quantizeSelectionOrAll();
                return true;
            }

            const bool up = key.isKeyCode (juce::KeyPress::upKey), down = key.isKeyCode (juce::KeyPress::downKey);
            const bool left = key.isKeyCode (juce::KeyPress::leftKey), right = key.isKeyCode (juce::KeyPress::rightKey);

            if ((up || down || left || right) && ! selectedIds.empty())
            {
                const int semis = (up ? 1 : down ? -1 : 0) * (key.getModifiers().isShiftDown() ? 12 : 1);
                const auto ticks = (right ? 1 : left ? -1 : 0) * owner.gridTicks();
                std::vector<midi::Note> changed;
                for (auto id : selectedIds)
                    if (auto* n = session.findNote (owner.clipId(), id))
                    {
                        auto m = *n;
                        m.pitch = juce::jlimit (midi::minPitch, midi::maxPitch, m.pitch + semis);
                        m.start = juce::jlimit<time::Tick> (0, clip->length - 1, m.start + ticks);
                        changed.push_back (m);
                    }
                session.updateNotes (owner.clipId(), changed);
                return true;
            }

            return false;   // Space, Ctrl+S, ... go to the main window
        }

    private:
        enum class Mode { none, pendingCreate, marquee, move, resize };

        void selectInMarquee (bool additive)
        {
            if (! additive)
                owner.selection.clear();
            for (auto& n : owner.clipPtr()->notes)
                if (n.start < clipLength() && noteBounds (n).intersects (marquee))
                    owner.selection.insert (n.id);
            owner.repaint();
        }

        PianoRollPanel& owner;
        Mode mode = Mode::none;
        juce::Point<float> dragStart;
        juce::Rectangle<float> marquee;
        std::vector<midi::Note> originals;
        midi::NoteId anchorId = 0;
        int auditionPitch = -1, heldPitch = -1;
        double playhead = -1.0;
    };

    //==============================================================================
    class PianoRollPanel::VelocityLane final : public juce::Component,
                                               public juce::SettableTooltipClient
    {
    public:
        VelocityLane (PianoRollPanel& p, Editor& e) : owner (p), editor (e)
        {
            setTooltip ("Velocity: drag a bar up or down (moves all selected notes together)");
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::panel);
            g.fillRect (getLocalBounds());
            g.setColour (theme::border);
            g.drawHorizontalLine (0, 0.0f, (float) getWidth());

            g.setColour (theme::textSecondary);
            g.setFont (theme::captionFont (9.5f));
            g.drawText ("VELOCITY", juce::Rectangle<int> (0, 6, keyboardWidth - 6, 14), juce::Justification::centredRight);

            const auto* clip = owner.clipPtr();
            if (! clip)
                return;

            const auto area = barArea();
            g.setColour (theme::border.withAlpha (0.5f));
            g.drawHorizontalLine ((int) area.getY(), area.getX(), area.getRight());

            for (auto& n : clip->notes)
            {
                if (n.start >= clip->length)
                    continue;
                const bool selected = owner.selection.count (n.id) > 0;
                const auto x = editor.tickToX ((double) n.start);
                const auto h = area.getHeight() * (float) n.velocity / 127.0f;
                g.setColour (selected ? theme::textPrimary : theme::track::synth);
                g.fillRect (x, area.getBottom() - h, 3.0f, h);
                g.fillEllipse (x - 2.0f, area.getBottom() - h - 3.5f, 7.0f, 7.0f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto* clip = owner.clipPtr();
            if (! clip)
                return;

            // Nearest bar within 8 px, preferring selected notes.
            const midi::Note* best = nullptr;
            float bestDistance = 8.0f;
            for (auto& n : clip->notes)
            {
                if (n.start >= clip->length)
                    continue;
                const auto d = std::abs (editor.tickToX ((double) n.start) + 1.5f - e.position.x)
                             - (owner.selection.count (n.id) ? 0.5f : 0.0f);
                if (d < bestDistance) { bestDistance = d; best = &n; }
            }
            if (best == nullptr)
                return;

            if (owner.selection.count (best->id) == 0)
                owner.selection = { best->id };

            originals.clear();
            for (auto id : owner.selection)
                if (auto* n = owner.session.findNote (owner.clipId(), id))
                    originals.push_back (*n);
            anchorVelocity = best->velocity;
            owner.session.beginGesture();
            setVelocityFromY (e.position.y);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! originals.empty())
                setVelocityFromY (e.position.y);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (! originals.empty())
                owner.session.endGesture();
            originals.clear();
        }

    private:
        juce::Rectangle<float> barArea() const
        {
            return juce::Rectangle<float> (editor.gridLeft(), 10.0f, editor.gridWidth(), (float) getHeight() - 16.0f);
        }

        void setVelocityFromY (float y)
        {
            const auto area = barArea();
            const auto target = juce::jlimit (1, 127, (int) std::round ((area.getBottom() - y) / area.getHeight() * 127.0f));
            const auto delta = target - anchorVelocity;

            std::vector<midi::Note> changed;
            for (auto n : originals)
            {
                n.velocity = juce::jlimit (1, 127, n.velocity + delta);
                changed.push_back (n);
            }
            owner.session.updateNotes (owner.clipId(), changed);
        }

        PianoRollPanel& owner;
        Editor& editor;
        std::vector<midi::Note> originals;
        int anchorVelocity = 100;
    };

    //==============================================================================
    PianoRollPanel::PianoRollPanel (audio::AudioEngine& e, project::Session& s, Selection& sel)
        : engine (e), session (s), target (sel)
    {
        editor = std::make_unique<Editor> (*this);
        velocityLane = std::make_unique<VelocityLane> (*this, *editor);

        viewport.setViewedComponent (editor.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
        addAndMakeVisible (*velocityLane);

        for (int bars : { 1, 2, 4, 8 })
            clipLengthBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
        clipLengthBox.setTooltip ("Clip length (the clip loops from bar 1)");
        clipLengthBox.onChange = [this]
        {
            session.resizeClip (clipId(), (time::Tick) clipLengthBox.getSelectedId() * session.getProject().timeSignature.ticksPerBar());
        };

        for (int i = 0; i < (int) std::size (grids); ++i)
            gridBox.addItem (grids[i].name, i + 1);
        gridBox.setSelectedId (3, juce::dontSendNotification);   // 1/16
        gridBox.setTooltip ("Grid: snap for drawing and moving, and the quantize value");
        gridBox.onChange = [this]
        {
            newNoteLength = gridTicks();
            editor->repaint();
        };

        quantizeButton.setTooltip ("Quantize the selected notes, or all notes if none are selected (Q)");
        quantizeButton.onClick = [this] { quantizeSelectionOrAll(); };

        createButton.setTooltip ("Create a 4-bar clip on this track, after its last clip");
        createButton.setColour (juce::TextButton::buttonColourId, theme::violet);
        createButton.onClick = [this]
        {
            const auto* t = session.findTrack (trackId());
            if (t == nullptr)
                return;
            time::Tick start = 0;
            for (auto& c : t->clips)
                start = std::max (start, c.end());
            if (const auto id = session.createClip (t->id, start, 4 * session.getProject().timeSignature.ticksPerBar()))
                target.selectClip (id);
        };

        for (auto* c : std::initializer_list<juce::Component*> { &clipLengthBox, &gridBox, &quantizeButton, &createButton })
        {
            c->setWantsKeyboardFocus (false);
            addAndMakeVisible (c);
        }

        listenerId = session.addListener ([this] (project::Session::Change change)
        {
            using Change = project::Session::Change;
            if (change == Change::replaced || change == Change::clips || change == Change::transport || change == Change::tracks)
                refreshFromSession();
        });
        targetListener = target.addListener ([this] { refreshFromSession(); });

        refreshFromSession();
        startTimerHz (60);
    }

    PianoRollPanel::~PianoRollPanel()
    {
        session.removeListener (listenerId);
        target.removeListener (targetListener);
    }

    project::TrackId PianoRollPanel::trackId() const
    {
        const auto* t = session.findTrack (target.getTrack());
        return t != nullptr && t->type == project::TrackType::instrument ? t->id : 0;
    }

    project::ClipId PianoRollPanel::clipId() const          { return trackId() != 0 ? target.getClip() : 0; }
    const project::Clip* PianoRollPanel::clipPtr() const    { return session.findClip (clipId()); }
    int PianoRollPanel::slot() const                         { return std::max (0, session.slotOf (trackId())); }

    time::Tick PianoRollPanel::gridTicks() const
    {
        const auto index = juce::jlimit (1, (int) std::size (grids), gridBox.getSelectedId()) - 1;
        return grids[index].ticks;
    }

    void PianoRollPanel::quantizeSelectionOrAll()
    {
        session.quantizeNotes (clipId(), std::vector<midi::NoteId> (selection.begin(), selection.end()), gridTicks());
    }

    void PianoRollPanel::refreshFromSession()
    {
        const bool hasClip = clipPtr() != nullptr;

        // A different clip: start with no notes selected.
        if (clipId() != shownClip)
        {
            shownClip = clipId();
            selection.clear();
        }

        // Drop selected ids that no longer exist (deleted, or another project).
        std::erase_if (selection, [this] (midi::NoteId id) { return session.findNote (clipId(), id) == nullptr; });

        if (const auto* c = clipPtr())
        {
            const auto perBar = session.getProject().timeSignature.ticksPerBar();
            const auto bars = (int) (c->length / perBar);
            const bool standard = c->length % perBar == 0 && (bars == 1 || bars == 2 || bars == 4 || bars == 8);
            clipLengthBox.setSelectedId (standard ? bars : 0, juce::dontSendNotification);
            if (! standard)
                clipLengthBox.setText (juce::String ((double) c->length / (double) perBar, 2) + " bars", juce::dontSendNotification);
        }
        for (auto* c : std::initializer_list<juce::Component*> { &clipLengthBox, &gridBox, &quantizeButton, &viewport, velocityLane.get() })
            c->setVisible (hasClip);
        createButton.setVisible (! hasClip && trackId() != 0);

        editor->repaint();
        velocityLane->repaint();
        repaint (headerArea);
    }

    void PianoRollPanel::timerCallback()
    {
        // Playhead inside the edited clip only.
        const auto* clip = clipPtr();
        const bool playing = engine.getState() == audio::TransportState::playing;
        const auto pos = engine.getPositionTicks();
        const double position = clip != nullptr && playing && pos >= (double) clip->start && pos < (double) clip->end()
                                    ? pos - (double) clip->start : -1.0;

        if (! juce::exactlyEqual (position, lastPlayhead))
        {
            lastPlayhead = position;
            editor->setPlayhead (position);
        }

    }

    void PianoRollPanel::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, theme::cornerRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::cornerRadius, theme::borderWidth);

        // Header: track-colour stripe, caption, track name, note count.
        auto header = headerArea;
        const auto* track = session.findTrack (trackId());
        g.setColour (track != nullptr ? target.colourOf (track->id) : theme::textMuted);
        g.fillRoundedRectangle (header.removeFromLeft (4).toFloat().reduced (0.0f, 6.0f), 2.0f);
        header.removeFromLeft (10);
        g.setColour (theme::textSecondary);
        g.setFont (theme::captionFont());
        g.drawText ("PIANO ROLL", header.removeFromLeft (88), juce::Justification::centredLeft);
        g.setColour (theme::textPrimary);
        g.setFont (theme::font (13.5f, juce::Font::bold));
        g.drawText (track != nullptr ? juce::String::fromUTF8 (track->name.c_str()) : juce::String(), header.removeFromLeft (120),
                    juce::Justification::centredLeft);

        if (const auto* clip = clipPtr())
        {
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (12.5f));
            const auto count = (int) std::count_if (clip->notes.begin(), clip->notes.end(),
                                                    [&] (const midi::Note& n) { return n.start < clip->length; });
            g.drawText (juce::String (count) + (count == 1 ? " note" : " notes")
                            + (selection.empty() ? juce::String() : ", " + juce::String ((int) selection.size()) + " selected"),
                        header.removeFromLeft (200), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (14.0f));
            g.drawFittedText (track == nullptr ? "Select a synth track or one of its clips in the arrangement."
                                               : "This synth track has no clip selected. Double-click its lane in the arrangement, or:",
                              emptyArea.withTrimmedBottom (70).reduced (24, 0), juce::Justification::centred, 2);
        }
    }

    void PianoRollPanel::resized()
    {
        auto area = getLocalBounds().reduced (10, 6);
        headerArea = area.removeFromTop (36);

        auto controls = headerArea;
        quantizeButton.setBounds (controls.removeFromRight (92).reduced (0, 5));
        controls.removeFromRight (8);
        gridBox.setBounds (controls.removeFromRight (80).reduced (0, 5));
        controls.removeFromRight (8);
        clipLengthBox.setBounds (controls.removeFromRight (92).reduced (0, 5));

        area.removeFromTop (4);
        emptyArea = area;
        createButton.setBounds (area.withSizeKeepingCentre (230, 36));

        velocityLane->setBounds (area.removeFromBottom (velocityLaneHeight));
        viewport.setBounds (area);
        editor->setSize (area.getWidth() - viewport.getScrollBarThickness(), numRows * rowHeight);

        if (! scrolledToMiddle && area.getHeight() > 0)
        {
            // Start with C4-C5 in the middle of the view.
            scrolledToMiddle = true;
            viewport.setViewPosition (0, (int) editor->pitchToY (72) - area.getHeight() / 2);
        }
    }
}
