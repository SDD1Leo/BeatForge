#include "ui/ArrangementView.h"
#include "ui/Theme.h"

#include <cmath>

namespace beatforge
{
    using project::Clip;
    using project::Track;
    using project::TrackType;

    namespace
    {
        juce::String toJuce (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

        // Shared timeline mapping for the ruler and the lanes.
        struct Timeline
        {
            double ticksTotal = 1.0;
            float left = 0.0f, width = 1.0f;

            float x (double tick) const      { return left + (float) (tick / ticksTotal) * width; }
            double tick (float px) const     { return (double) (px - left) / width * ticksTotal; }
        };
    }

    //==============================================================================
    class ArrangementView::Ruler final : public juce::Component,
                                         public juce::SettableTooltipClient
    {
    public:
        explicit Ruler (ArrangementView& o) : owner (o)
        {
            setTooltip ("Drag in the top strip to set the loop; click below it to move the playhead");
        }

        Timeline timeline() const
        {
            const auto perBar = owner.session.getProject().timeSignature.ticksPerBar();
            return { (double) owner.barsShown() * (double) perBar, 0.0f, (float) getWidth() };
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = owner.session.getProject();
            const auto tl = timeline();
            const auto perBar = p.timeSignature.ticksPerBar(), perBeat = p.timeSignature.ticksPerBeat();

            g.setColour (theme::panelRaised);
            g.fillRect (getLocalBounds());

            // Loop band.
            const auto loopArea = juce::Rectangle<float> (tl.x ((double) p.loopStart), 2.0f,
                                                          tl.x ((double) p.loopEnd) - tl.x ((double) p.loopStart), 8.0f);
            g.setColour (p.loopEnabled ? theme::violet : theme::textMuted.withAlpha (0.5f));
            g.fillRoundedRectangle (loopArea, 2.0f);

            for (int bar = 0; bar < owner.barsShown(); ++bar)
            {
                const auto x = tl.x ((double) bar * perBar);
                g.setColour (theme::border);
                g.drawVerticalLine ((int) x, 12.0f, (float) getHeight());
                g.setColour (theme::textSecondary);
                g.setFont (theme::font (11.0f));
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, 12.0f, 36.0f, 16.0f), juce::Justification::centredLeft);

                for (int beat = 1; beat < p.timeSignature.numerator; ++beat)
                {
                    g.setColour (theme::border.withAlpha (0.6f));
                    const auto bx = tl.x ((double) (bar * perBar + beat * perBeat));
                    g.drawVerticalLine ((int) bx, (float) getHeight() - 6.0f, (float) getHeight());
                }
            }

            g.setColour (theme::border);
            g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

            // Playhead marker.
            const auto x = tl.x (owner.engine.getPositionTicks());
            juce::Path head;
            head.addTriangle (x - 6.0f, 12.0f, x + 6.0f, 12.0f, x, 21.0f);
            g.setColour (theme::playhead);
            g.fillPath (head);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            loopDrag = e.position.y < 12.0f;
            if (loopDrag)
            {
                owner.session.beginGesture();
                anchorBar = barLineAt (e.position.x);
                setLoopTo (e.position.x);
            }
            else
            {
                seekTo (e.position.x);
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (loopDrag) setLoopTo (e.position.x);
            else          seekTo (e.position.x);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (loopDrag)
                owner.session.endGesture();
            loopDrag = false;
        }

    private:
        // Nearest bar line, so the loop ends where the drag is released.
        int barLineAt (float x) const
        {
            const auto perBar = owner.session.getProject().timeSignature.ticksPerBar();
            return juce::jlimit (0, owner.barsShown(), (int) std::round (timeline().tick (x) / (double) perBar));
        }

        // Anchor to the bar line under the pointer; a click without a drag loops one bar.
        void setLoopTo (float x)
        {
            const auto perBar = owner.session.getProject().timeSignature.ticksPerBar();
            const auto line = barLineAt (x);
            const auto start = line == anchorBar ? anchorBar : std::min (anchorBar, line);
            const auto end = line == anchorBar ? anchorBar + 1 : std::max (anchorBar, line);
            owner.session.setLoop (true, (time::Tick) start * perBar, (time::Tick) end * perBar);
        }

        void seekTo (float x)
        {
            const auto perBeat = (double) owner.session.getProject().timeSignature.ticksPerBeat();
            const auto tick = std::max (0.0, std::round (timeline().tick (x) / perBeat) * perBeat);
            owner.engine.seek (tick);
        }

        ArrangementView& owner;
        bool loopDrag = false;
        int anchorBar = 0;
    };

    //==============================================================================
    class ArrangementView::TrackHeader final : public juce::Component,
                                               public juce::SettableTooltipClient
    {
    public:
        TrackHeader (ArrangementView& o, project::TrackId t) : owner (o), trackId (t)
        {
            for (auto* b : { &mute, &solo })
            {
                b->setClickingTogglesState (true);
                b->setWantsKeyboardFocus (false);
                b->setColour (juce::TextButton::textColourOnId, theme::background);
                addAndMakeVisible (*b);
            }
            mute.setColour (juce::TextButton::buttonOnColourId, theme::muteOn);
            solo.setColour (juce::TextButton::buttonOnColourId, theme::soloOn);
            mute.onClick = [this] { owner.session.setTrackMuted (trackId, mute.getToggleState()); };
            solo.onClick = [this] { owner.session.setTrackSoloed (trackId, solo.getToggleState()); };

            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (project::Project::minVolumeDb, project::Project::maxVolumeDb, 0.1);
            volume.setSkewFactorFromMidPoint (-12.0);
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.setPopupDisplayEnabled (true, true, nullptr);
            volume.setTextValueSuffix (" dB");
            volume.setNumDecimalPlacesToDisplay (1);
            volume.setWantsKeyboardFocus (false);
            volume.onDragStart = [this] { owner.session.beginGesture(); };
            volume.onDragEnd = [this] { owner.session.endGesture(); };
            volume.onValueChange = [this] { owner.session.setTrackVolume (trackId, (float) volume.getValue()); };
            addAndMakeVisible (volume);

            pan.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            pan.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.setPopupDisplayEnabled (true, true, nullptr);
            pan.textFromValueFunction = [] (double v)
            {
                const auto pct = (int) std::round (std::abs (v) * 100.0);
                return pct == 0 ? juce::String ("C") : juce::String (pct) + (v < 0 ? " L" : " R");
            };
            pan.setWantsKeyboardFocus (false);
            pan.onDragStart = [this] { owner.session.beginGesture(); };
            pan.onDragEnd = [this] { owner.session.endGesture(); };
            pan.onValueChange = [this] { owner.session.setTrackPan (trackId, (float) pan.getValue()); };
            addAndMakeVisible (pan);

            refresh();
        }

        project::TrackId getTrackId() const noexcept { return trackId; }

        void refresh()
        {
            const auto* t = owner.session.findTrack (trackId);
            if (t == nullptr)
                return;
            name = toJuce (t->name);
            colour = owner.selection.colourOf (trackId);
            mute.setToggleState (t->muted, juce::dontSendNotification);
            solo.setToggleState (t->soloed, juce::dontSendNotification);
            volume.setValue (t->volumeDb, juce::dontSendNotification);
            pan.setValue (t->pan, juce::dontSendNotification);
            pan.setColour (juce::Slider::rotarySliderFillColourId, colour);
            volume.setColour (juce::Slider::trackColourId, colour);
            mute.setTooltip ("Mute " + name);
            solo.setTooltip ("Solo " + name);
            volume.setTooltip (name + " volume (double-click: 0 dB)");
            pan.setTooltip (name + " pan (double-click: centre)");
            setTooltip (name + (t->type == TrackType::drums ? " (drums)" : " (synth)")
                        + ": double-click the name to rename, right-click for more");
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const bool selected = owner.selection.getTrack() == trackId;
            auto area = getLocalBounds().toFloat().reduced (4.0f, 3.0f);

            g.setColour (selected ? theme::panelRaised.brighter (0.06f) : theme::panelRaised);
            g.fillRoundedRectangle (area, theme::controlRadius);
            g.setColour (selected ? colour.withAlpha (0.8f) : theme::border);
            g.drawRoundedRectangle (area, theme::controlRadius, theme::borderWidth);
            g.setColour (colour);
            g.fillRoundedRectangle (area.withWidth (5.0f), 2.0f);

            if (editor == nullptr)
            {
                const auto* t = owner.session.findTrack (trackId);
                g.setColour (theme::textPrimary);
                g.setFont (theme::font (13.5f, juce::Font::bold));
                g.drawText (name, nameArea, juce::Justification::centredLeft, true);
                g.setColour (theme::textMuted);
                g.setFont (theme::captionFont (9.0f));
                g.drawText (t != nullptr && t->type == TrackType::drums ? "DRUMS" : "SYNTH", nameArea.translated (0, 15),
                            juce::Justification::centredLeft);
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (14, 7);
            auto top = area.removeFromTop (22);
            pan.setBounds (top.removeFromRight (26).withSizeKeepingCentre (26, 26));
            top.removeFromRight (6);
            solo.setBounds (top.removeFromRight (24).reduced (0, 1));
            top.removeFromRight (4);
            mute.setBounds (top.removeFromRight (24).reduced (0, 1));
            nameArea = top.withTrimmedRight (6);
            area.removeFromTop (6);
            volume.setBounds (area.withTrimmedLeft (90).withHeight (18));
            if (editor != nullptr)
                editor->setBounds (nameArea.expanded (2, 2));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            owner.selection.selectTrack (trackId);
            if (e.mods.isPopupMenu())
                showMenu();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (nameArea.contains (e.getPosition()))
                showEditor();
        }

    private:
        void showMenu()
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Rename...");
            menu.addItem (2, "Delete track");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                [safe = juce::Component::SafePointer<TrackHeader> (this)] (int r)
            {
                if (safe == nullptr) return;
                if (r == 1) safe->showEditor();
                if (r == 2) safe->owner.session.removeTrack (safe->trackId);
            });
        }

        void showEditor()
        {
            editor = std::make_unique<juce::TextEditor>();
            editor->setFont (theme::font (13.5f, juce::Font::bold));
            editor->setInputRestrictions (project::Project::maxNameLength);
            editor->setText (name, false);
            editor->selectAll();
            editor->onReturnKey = [this] { closeEditor (true); };
            editor->onFocusLost = [this] { closeEditor (true); };
            editor->onEscapeKey = [this] { closeEditor (false); };
            addAndMakeVisible (*editor);
            resized();
            editor->grabKeyboardFocus();
            repaint();
        }

        void closeEditor (bool commit)
        {
            if (editor == nullptr || ! editor->isVisible())
                return;
            const auto text = editor->getText().trim();
            editor->setVisible (false);
            if (commit && text.isNotEmpty())
                owner.session.renameTrack (trackId, text.toStdString());

            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TrackHeader> (this)]
            {
                if (safe == nullptr) return;
                safe->editor = nullptr;
                safe->refresh();
                if (safe->owner.onTextEditFinished) safe->owner.onTextEditFinished();
            });
        }

        ArrangementView& owner;
        project::TrackId trackId;
        juce::String name;
        juce::Colour colour;
        juce::TextButton mute { "M" }, solo { "S" };
        juce::Slider volume, pan;
        juce::Rectangle<int> nameArea;
        std::unique_ptr<juce::TextEditor> editor;
    };

    //==============================================================================
    class ArrangementView::Lanes final : public juce::Component,
                                         public juce::SettableTooltipClient
    {
    public:
        explicit Lanes (ArrangementView& o) : owner (o)
        {
            setWantsKeyboardFocus (true);
            setTooltip ("Double-click to create a clip; drag clips to move, their right edge to resize; double-click a clip to edit");
        }

        std::vector<std::unique_ptr<TrackHeader>> headers;

        Timeline timeline() const
        {
            const auto perBar = owner.session.getProject().timeSignature.ticksPerBar();
            return { (double) owner.barsShown() * (double) perBar, (float) headerWidth, (float) (getWidth() - headerWidth) };
        }

        int rowOf (project::TrackId id) const
        {
            const auto& tracks = owner.session.getProject().tracks;
            for (size_t i = 0; i < tracks.size(); ++i)
                if (tracks[i].id == id) return (int) i;
            return -1;
        }

        const Track* trackAtY (float y) const
        {
            const auto& tracks = owner.session.getProject().tracks;
            const auto row = (int) (y / rowHeight);
            return row >= 0 && row < (int) tracks.size() ? &tracks[(size_t) row] : nullptr;
        }

        juce::Rectangle<float> clipBounds (const Track& t, const Clip& c) const
        {
            const auto tl = timeline();
            const auto row = rowOf (t.id);
            const auto x0 = tl.x ((double) c.start), x1 = tl.x ((double) c.end());
            return { x0, (float) (row * rowHeight) + 5.0f, juce::jmax (6.0f, x1 - x0), (float) rowHeight - 10.0f };
        }

        std::pair<const Track*, const Clip*> clipAt (juce::Point<float> p) const
        {
            const auto* t = trackAtY (p.y);
            if (t == nullptr) return { nullptr, nullptr };
            for (auto it = t->clips.rbegin(); it != t->clips.rend(); ++it)
                if (clipBounds (*t, *it).contains (p))
                    return { t, &*it };
            return { t, nullptr };
        }

        void setPlayhead (double tick)
        {
            const auto tl = timeline();
            auto strip = [&] (double t) { return juce::Rectangle<int> ((int) tl.x (t) - 2, 0, 5, getHeight()); };
            repaint (strip (playhead));
            playhead = tick;
            repaint (strip (playhead));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = owner.session.getProject();
            const auto tl = timeline();
            const auto perBar = p.timeSignature.ticksPerBar(), perBeat = p.timeSignature.ticksPerBeat();

            g.fillAll (theme::background);

            // Rows.
            for (size_t i = 0; i < p.tracks.size(); ++i)
            {
                const auto row = juce::Rectangle<float> (tl.left, (float) i * rowHeight, tl.width, (float) rowHeight);
                g.setColour (p.tracks[i].id == owner.selection.getTrack() ? theme::panel.brighter (0.04f) : theme::panel);
                g.fillRect (row.reduced (0.0f, 1.0f));
            }

            // Loop shading and grid.
            if (p.loopEnabled)
            {
                g.setColour (theme::violet.withAlpha (0.06f));
                g.fillRect (juce::Rectangle<float> (tl.x ((double) p.loopStart), 0.0f,
                                                    tl.x ((double) p.loopEnd) - tl.x ((double) p.loopStart), (float) getHeight()));
            }
            for (time::Tick t = 0; t <= (time::Tick) tl.ticksTotal; t += perBeat)
            {
                g.setColour (t % perBar == 0 ? theme::border : theme::border.withAlpha (0.35f));
                g.drawVerticalLine ((int) tl.x ((double) t), 0.0f, (float) getHeight());
            }

            // Clips.
            for (auto& t : p.tracks)
            {
                const auto colour = owner.selection.colourOf (t.id);
                for (auto& c : t.clips)
                    paintClip (g, t, c, colour);
            }

            // Playhead.
            const auto x = tl.x (playhead);
            g.setColour (theme::playhead.withAlpha (0.25f));
            g.fillRect (x - 3.0f, 0.0f, 6.0f, (float) getHeight());
            g.setColour (theme::playhead);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());

            g.setColour (theme::border);
            g.drawVerticalLine (headerWidth - 1, 0.0f, (float) getHeight());
        }

        void paintClip (juce::Graphics& g, const Track& t, const Clip& c, juce::Colour colour)
        {
            const auto r = clipBounds (t, c);
            const bool selected = owner.selection.getClip() == c.id;

            g.setColour (colour.withAlpha (0.28f));
            g.fillRoundedRectangle (r, theme::controlRadius);
            g.setColour (colour.withAlpha (0.85f));
            g.fillRoundedRectangle (r.withHeight (14.0f), theme::controlRadius);

            g.setColour (theme::background);
            g.setFont (theme::font (10.5f, juce::Font::bold));
            const auto perBar = owner.session.getProject().timeSignature.ticksPerBar();
            const bool wholeBars = c.length % perBar == 0;
            const auto bars = (double) c.length / (double) perBar;
            g.drawText ((t.type == TrackType::drums ? "Pattern  " : "Clip  ") + juce::String (bars, wholeBars ? 0 : 2)
                            + (c.length == perBar ? " bar" : " bars"),
                        r.withHeight (14.0f).reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);

            // Content preview.
            auto body = r.withTrimmedTop (16.0f).reduced (4.0f, 3.0f);
            g.saveState();
            g.reduceClipRegion (body.toNearestInt());
            const auto tl = timeline();
            g.setColour (colour);
            if (t.type == TrackType::drums)
            {
                const float laneH = body.getHeight() / (float) audio::DrumMachine::numLanes;
                const auto stepTicks = project::patternTicks / project::stepsPerPattern;
                for (time::Tick at = 0, step = 0; at < c.length; at += stepTicks, ++step)
                    for (int lane = 0; lane < audio::DrumMachine::numLanes; ++lane)
                        if (c.pattern[(size_t) lane][(size_t) (step % project::stepsPerPattern)])
                        {
                            const auto x0 = tl.x ((double) (c.start + at));
                            g.fillRect (x0, body.getY() + (float) lane * laneH, juce::jmax (1.5f, tl.x ((double) stepTicks) - tl.left - 1.0f), laneH - 1.0f);
                        }
            }
            else if (! c.notes.empty())
            {
                int lo = 127, hi = 0;
                for (auto& n : c.notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
                const float range = (float) std::max (12, hi - lo + 1);
                for (auto& n : c.notes)
                {
                    if (n.start >= c.length) continue;
                    const auto y = body.getBottom() - ((float) (n.pitch - lo) + 0.5f) / range * body.getHeight();
                    g.fillRect (tl.x ((double) (c.start + n.start)), y - 1.0f,
                                juce::jmax (2.0f, tl.x ((double) (c.start + std::min (n.end(), c.length))) - tl.x ((double) (c.start + n.start))), 2.5f);
                }
            }
            g.restoreState();

            g.setColour (selected ? theme::textPrimary : colour);
            g.drawRoundedRectangle (r, theme::controlRadius, selected ? 2.0f : 1.0f);
        }

        // --- mouse ------------------------------------------------------------------
        void mouseDown (const juce::MouseEvent& e) override
        {
            grabKeyboardFocus();
            if (e.position.x < (float) headerWidth)
                return;

            const auto [t, c] = clipAt (e.position);
            if (t != nullptr)
                owner.selection.selectTrack (t->id);
            if (c == nullptr)
                return;

            owner.selection.selectClip (c->id);
            if (e.mods.isPopupMenu())
                return showClipMenu (c->id);

            dragClip = c->id;
            original = *c;
            originalTrack = t->id;
            mode = clipBounds (*t, *c).getRight() - e.position.x <= 7.0f ? Mode::resize : Mode::move;
            dragStart = e.position;
            owner.session.beginGesture();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (mode == Mode::none || owner.session.findClip (dragClip) == nullptr)
                return;

            const auto tl = timeline();
            const auto beat = (double) owner.session.getProject().timeSignature.ticksPerBeat();
            const auto delta = (time::Tick) (std::round ((tl.tick (e.position.x) - tl.tick (dragStart.x)) / beat) * beat);

            if (mode == Mode::move)
            {
                // Vertical: onto another track of the same kind, if the pointer is over one.
                auto target = originalTrack;
                if (const auto* over = trackAtY (e.position.y); over != nullptr
                    && over->type == owner.session.findTrack (originalTrack)->type)
                    target = over->id;
                owner.session.moveClip (dragClip, std::max<time::Tick> (0, original.start + delta), target);
            }
            else
            {
                owner.session.resizeClip (dragClip, std::max<time::Tick> ((time::Tick) beat, original.length + delta));
            }
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (mode != Mode::none)
                owner.session.endGesture();
            mode = Mode::none;
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.position.x < (float) headerWidth)
                return;

            const auto [t, c] = clipAt (e.position);
            if (c != nullptr)
            {
                owner.selection.selectClip (c->id);
                if (owner.onOpenEditor) owner.onOpenEditor();
                return;
            }
            if (t == nullptr)
                return;

            // New clip at the bar under the pointer: 1 bar of drums, 4 bars for synths.
            const auto perBar = owner.session.getProject().timeSignature.ticksPerBar();
            const auto bar = (time::Tick) std::max (0.0, std::floor (timeline().tick (e.position.x) / (double) perBar));
            const auto length = t->type == TrackType::drums ? project::patternTicks : 4 * perBar;
            if (const auto id = owner.session.createClip (t->id, bar * perBar, length); id != 0)
            {
                owner.selection.selectClip (id);
                if (owner.onOpenEditor) owner.onOpenEditor();
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto [t, c] = e.position.x >= (float) headerWidth ? clipAt (e.position) : std::pair<const Track*, const Clip*> {};
            const bool edge = c != nullptr && clipBounds (*t, *c).getRight() - e.position.x <= 7.0f;
            setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor
                                 : c != nullptr ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            {
                owner.deleteSelectedClip();
                return true;
            }
            return false;   // Ctrl+D, Space, ... handled by the main window
        }

    private:
        enum class Mode { none, move, resize };

        void showClipMenu (project::ClipId id)
        {
            juce::PopupMenu menu;
            juce::PopupMenu::Item dup ("Duplicate");
            dup.itemID = 1;
            dup.shortcutKeyDescription = "Ctrl+D";
            menu.addItem (dup);
            juce::PopupMenu::Item del ("Delete");
            del.itemID = 2;
            del.shortcutKeyDescription = "Delete";
            menu.addItem (del);
            menu.showMenuAsync (juce::PopupMenu::Options(), [this, id, safe = juce::Component::SafePointer<Lanes> (this)] (int r)
            {
                if (safe == nullptr) return;
                if (r == 1) { if (auto copy = owner.session.duplicateClip (id)) owner.selection.selectClip (copy); }
                if (r == 2) owner.session.deleteClip (id);
            });
        }

        ArrangementView& owner;
        Mode mode = Mode::none;
        project::ClipId dragClip = 0;
        project::TrackId originalTrack = 0;
        Clip original;
        juce::Point<float> dragStart;
        double playhead = 0.0;
    };

    //==============================================================================
    ArrangementView::ArrangementView (audio::AudioEngine& e, project::Session& s, Selection& sel)
        : engine (e), session (s), selection (sel)
    {
        ruler = std::make_unique<Ruler> (*this);
        lanes = std::make_unique<Lanes> (*this);
        addAndMakeVisible (*ruler);
        viewport.setViewedComponent (lanes.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);

        addDrumsButton.setTooltip ("Add a drum track");
        addSynthButton.setTooltip ("Add a synth track (Nebula Poly or Nebula Bass)");
        for (auto* b : { &addDrumsButton, &addSynthButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
        addDrumsButton.onClick = [this] { if (auto id = session.addTrack (TrackType::drums)) selection.selectTrack (id); };
        addSynthButton.onClick = [this] { if (auto id = session.addTrack (TrackType::instrument)) selection.selectTrack (id); };

        sessionListener = session.addListener ([this] (project::Session::Change change)
        {
            if (change == project::Session::Change::metadata || change == project::Session::Change::instrument)
                return;
            rebuild();
        });
        selectionListener = selection.addListener ([this] { refresh(); });

        rebuild();
        startTimerHz (60);
    }

    ArrangementView::~ArrangementView()
    {
        session.removeListener (sessionListener);
        selection.removeListener (selectionListener);
    }

    int ArrangementView::barsShown() const
    {
        const auto& p = session.getProject();
        const auto perBar = p.timeSignature.ticksPerBar();
        time::Tick last = std::max (p.loopEnd, (time::Tick) 0);
        for (auto& t : p.tracks)
            for (auto& c : t.clips)
                last = std::max (last, c.end());
        return std::max (16, (int) ((last + perBar - 1) / perBar) + 4);
    }

    void ArrangementView::rebuild()
    {
        std::vector<project::TrackId> ids;
        for (auto& t : session.getProject().tracks)
            ids.push_back (t.id);

        if (ids != shownTracks)
        {
            shownTracks = ids;
            lanes->headers.clear();
            for (auto id : ids)
            {
                lanes->headers.push_back (std::make_unique<TrackHeader> (*this, id));
                lanes->addAndMakeVisible (*lanes->headers.back());
            }
            resized();
        }
        refresh();
    }

    void ArrangementView::refresh()
    {
        addDrumsButton.setEnabled ((int) session.getProject().tracks.size() < project::Project::maxTracks);
        addSynthButton.setEnabled (addDrumsButton.isEnabled());
        for (auto& h : lanes->headers)
            h->refresh();
        lanes->repaint();
        ruler->repaint();
    }

    void ArrangementView::timerCallback()
    {
        lanes->setPlayhead (engine.getPositionTicks());
        ruler->repaint();
    }

    void ArrangementView::duplicateSelectedClip()
    {
        if (const auto copy = session.duplicateClip (selection.getClip()))
            selection.selectClip (copy);
    }

    void ArrangementView::deleteSelectedClip()
    {
        session.deleteClip (selection.getClip());
    }

    void ArrangementView::paint (juce::Graphics& g)
    {
        g.setColour (theme::panelRaised);
        g.fillRect (getLocalBounds().removeFromTop (rulerHeight).withWidth (headerWidth));
        g.setColour (theme::border);
        g.drawHorizontalLine (rulerHeight - 1, 0.0f, (float) headerWidth);
        g.drawRect (getLocalBounds().toFloat(), 1.0f);
    }

    void ArrangementView::resized()
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (rulerHeight);
        auto corner = top.removeFromLeft (headerWidth).reduced (8, 4);
        addSynthButton.setBounds (corner.removeFromRight (80));
        corner.removeFromRight (6);
        addDrumsButton.setBounds (corner.removeFromRight (80));
        ruler->setBounds (top.withTrimmedRight (viewport.getScrollBarThickness()));
        viewport.setBounds (area);

        const auto contentWidth = area.getWidth() - viewport.getScrollBarThickness();
        lanes->setSize (contentWidth, juce::jmax (area.getHeight(), (int) lanes->headers.size() * rowHeight));
        for (size_t i = 0; i < lanes->headers.size(); ++i)
            lanes->headers[i]->setBounds (0, (int) i * rowHeight, headerWidth, rowHeight);
    }
}
