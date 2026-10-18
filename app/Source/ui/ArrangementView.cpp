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

        // Shared timeline mapping for the ruler and the lanes: the visible
        // range [start, start + ticks) drawn across `width` pixels from `left`.
        struct Timeline
        {
            double start = 0.0, ticks = 1.0;
            float left = 0.0f, width = 1.0f;

            float x (double tick) const      { return left + (float) ((tick - start) / ticks) * width; }
            double tick (float px) const     { return start + (double) (px - left) / width * ticks; }
            float w (double length) const    { return (float) (length / ticks) * width; }
            double end() const               { return start + ticks; }
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
            return { owner.viewStart, owner.visibleTicks(), 0.0f, (float) getWidth() };
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = owner.session.getProject();
            const auto tl = timeline();
            const auto perBar = p.timeSignature.ticksPerBar(), perBeat = p.timeSignature.ticksPerBeat();

            // Spec 5.2: bg/panel ruler, loop bar across the top, bar numbers in
            // Mono (brighter inside the loop), bar and beat ticks at the bottom.
            g.setColour (theme::bgPanel);
            g.fillRect (getLocalBounds());
            const auto h = (float) getHeight();

            const auto loopArea = juce::Rectangle<float> (tl.x ((double) p.loopStart), 5.0f,
                                                          tl.x ((double) p.loopEnd) - tl.x ((double) p.loopStart), 8.0f);
            g.setColour (theme::purple.withAlpha (p.loopEnabled ? 0.3f : 0.12f));
            g.fillRoundedRectangle (loopArea, 3.0f);
            g.setColour (theme::purple.withAlpha (p.loopEnabled ? 0.9f : 0.35f));
            g.drawRoundedRectangle (loopArea.reduced (0.5f), 3.0f, 1.0f);

            g.setFont (theme::numericFont (10.0f, juce::Font::bold));
            for (int bar = (int) (tl.start / perBar); bar <= (int) (tl.end() / perBar); ++bar)
            {
                const auto barTick = (time::Tick) bar * perBar;
                const auto x = tl.x ((double) barTick);
                g.setColour (theme::borderStrong);
                g.fillRect (juce::Rectangle<float> (std::round (x), h - 16.0f, 1.0f, 16.0f));
                const bool inLoop = barTick >= p.loopStart && barTick < p.loopEnd;
                g.setColour (inLoop ? theme::textSecondary : theme::textMuted);
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 5.0f, h - 26.0f, 36.0f, 12.0f), juce::Justification::centredLeft);

                g.setColour (theme::border);
                for (int beat = 1; beat < p.timeSignature.numerator; ++beat)
                {
                    const auto bx = tl.x ((double) (bar * perBar + beat * perBeat));
                    g.fillRect (juce::Rectangle<float> (std::round (bx), h - 6.0f, 1.0f, 6.0f));
                }
            }

            g.setColour (theme::borderSubtle);
            g.fillRect (0.0f, h - 1.0f, (float) getWidth(), 1.0f);

            // Playhead head: a rounded down-pointing triangle, 14 x 12.
            const auto x = tl.x (owner.engine.getPositionTicks());
            juce::Path head;
            head.addTriangle (x - 7.0f, h - 18.0f, x + 7.0f, h - 18.0f, x, h - 6.0f);
            head = head.createPathWithRoundedCorners (2.0f);
            g.setColour (theme::playhead);
            g.fillPath (head);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            loopDrag = e.position.y < 16.0f;
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

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            // Over the ruler the plain wheel zooms (there is nothing to scroll vertically).
            owner.handleWheel (e.getEventRelativeTo (&owner), wheel, true);
        }

    private:
        // Nearest bar line, so the loop ends where the drag is released.
        int barLineAt (float x) const
        {
            const auto perBar = owner.session.getProject().timeSignature.ticksPerBar();
            return juce::jlimit (0, owner.totalBars(), (int) std::round (timeline().tick (x) / (double) perBar));
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
                b->setColour (juce::TextButton::buttonColourId, theme::bgControl);
                b->setColour (juce::TextButton::textColourOffId, theme::textSecondary);
                b->setColour (juce::TextButton::textColourOnId, theme::bgApp);
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

        // Spec 12.6: a 248 x 64 card with a colour strip, name + type tag, M/S and
        // volume, and the pan knob on the right.
        void paint (juce::Graphics& g) override
        {
            const bool selected = owner.selection.getTrack() == trackId;
            const auto* t = owner.session.findTrack (trackId);
            const auto card = cardBounds().toFloat();

            {
                juce::Graphics::ScopedSaveState clip (g);
                juce::Path shape;
                shape.addRoundedRectangle (card, theme::cardRadius);
                g.reduceClipRegion (shape);
                g.setColour (selected ? theme::bgRaised : theme::bgSurface);
                g.fillRect (card);
                g.setColour (colour);
                g.fillRect (card.withWidth (4.0f));
            }
            g.setColour (selected ? colour : theme::borderSubtle);
            g.drawRoundedRectangle (card.reduced (0.5f), theme::cardRadius, theme::borderWidth);

            if (editor == nullptr && t != nullptr)
            {
                g.setColour (t->muted ? theme::textMuted : theme::textPrimary);
                g.setFont (theme::font (13.0f, juce::Font::bold));
                const auto nameWidth = juce::jmin ((float) nameArea.getWidth() - 50.0f,
                                                   juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 1.0f);
                g.drawText (name, nameArea.toFloat().withWidth (nameWidth), juce::Justification::centredLeft, true);
                g.setColour (theme::textMuted);
                g.setFont (theme::captionFont (9.0f));
                g.drawText (t->type == TrackType::drums ? "DRUMS" : "SYNTH",
                            nameArea.toFloat().withTrimmedLeft (nameWidth + 8.0f), juce::Justification::centredLeft, false);
            }
        }

        void resized() override
        {
            auto area = cardBounds().withTrimmedLeft (4 + 12).withTrimmedRight (12).reduced (0, 9);
            pan.setBounds (area.removeFromRight (28).withSizeKeepingCentre (28, 28));
            area.removeFromRight (10);
            nameArea = area.removeFromTop (20);
            auto controls = area.withSizeKeepingCentre (area.getWidth(), 22).withY (nameArea.getBottom() + 4);
            mute.setBounds (controls.removeFromLeft (24));
            controls.removeFromLeft (6);
            solo.setBounds (controls.removeFromLeft (24));
            controls.removeFromLeft (8);
            volume.setBounds (controls.removeFromLeft (juce::jmin (104, controls.getWidth())));
            if (editor != nullptr)
                editor->setBounds (nameArea.expanded (2, 2));
        }

        juce::Rectangle<int> cardBounds() const { return getLocalBounds().reduced (8, 3); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            owner.selection.selectTrack (trackId);
            draggingTrack = false;
            if (e.mods.isPopupMenu())
                showMenu();
        }

        // Drag the header up or down to move the track.
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || (! draggingTrack && std::abs (e.getDistanceFromDragStartY()) < 6))
                return;
            draggingTrack = true;
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            const auto [target, current] = dropTarget (e);
            owner.setTrackDropRow (target == current ? -1 : target > current ? target + 1 : target);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! draggingTrack)
                return;
            draggingTrack = false;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            owner.setTrackDropRow (-1);
            const auto [target, current] = dropTarget (e);
            if (target != current)
                owner.session.moveTrack (trackId, target);   // may delete this header
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (nameArea.contains (e.getPosition()))
                showEditor();
        }

    private:
        int index() const
        {
            const auto& tracks = owner.session.getProject().tracks;
            for (size_t i = 0; i < tracks.size(); ++i)
                if (tracks[i].id == trackId) return (int) i;
            return -1;
        }

        // {row the track would move to, its row now}
        std::pair<int, int> dropTarget (const juce::MouseEvent& e) const
        {
            const auto rows = (int) owner.session.getProject().tracks.size();
            const auto y = (float) getY() + e.position.y;   // in the lanes, where the headers sit
            return { juce::jlimit (0, rows - 1, (int) std::floor (y / (float) rowHeight)), index() };
        }

        void showMenu()
        {
            const auto i = index();
            const auto rows = (int) owner.session.getProject().tracks.size();
            juce::PopupMenu menu;
            menu.addItem (1, "Rename...");
            menu.addItem (3, "Move up", i > 0);
            menu.addItem (4, "Move down", i < rows - 1);
            menu.addSeparator();
            menu.addItem (2, "Delete track");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                [safe = juce::Component::SafePointer<TrackHeader> (this), i] (int r)
            {
                if (safe == nullptr) return;
                if (r == 1) safe->showEditor();
                if (r == 2) safe->owner.session.removeTrack (safe->trackId);
                if (r == 3) safe->owner.session.moveTrack (safe->trackId, i - 1);
                if (r == 4) safe->owner.session.moveTrack (safe->trackId, i + 1);
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
        bool draggingTrack = false;
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
            return { owner.viewStart, owner.visibleTicks(), (float) headerWidth, (float) (getWidth() - headerWidth) };
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
            return { x0 + 1.0f, (float) (row * rowHeight) + 5.0f, juce::jmax (6.0f, x1 - x0 - 2.0f), (float) rowHeight - 10.0f };
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
            // Covers the whole glow (x - 8 .. x + 8) and its anti-aliased edges;
            // anything narrower leaves a trail of glow behind the moving playhead.
            auto strip = [&] (double t) { return juce::Rectangle<int> ((int) std::floor (tl.x (t)) - 10, 0, 21, getHeight()); };
            repaint (strip (playhead));
            playhead = tick;
            repaint (strip (playhead));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = owner.session.getProject();
            const auto tl = timeline();
            const auto perBar = p.timeSignature.ticksPerBar(), perBeat = p.timeSignature.ticksPerBeat();

            g.fillAll (theme::bgInset);
            g.setColour (theme::bgPanel);
            g.fillRect (getLocalBounds().withWidth (headerWidth));
            g.saveState();
            g.reduceClipRegion (getLocalBounds().withTrimmedLeft (headerWidth));   // scrolled content stays right of the headers

            // Lanes: 64 tall with a 6 px gap, aligned with the track headers.
            for (size_t i = 0; i < p.tracks.size(); ++i)
            {
                const auto lane = juce::Rectangle<float> (tl.left, (float) i * rowHeight + 3.0f, tl.width, (float) rowHeight - 6.0f);
                const bool selected = p.tracks[i].id == owner.selection.getTrack();
                g.setColour (selected ? owner.selection.colourOf (p.tracks[i].id).withAlpha (0.08f) : theme::bgSurface.withAlpha (0.5f));
                g.fillRect (lane);
            }

            // Loop shading and grid.
            if (p.loopEnabled)
            {
                g.setColour (theme::purple.withAlpha (0.04f));
                g.fillRect (juce::Rectangle<float> (tl.x ((double) p.loopStart), 0.0f,
                                                    tl.x ((double) p.loopEnd) - tl.x ((double) p.loopStart), (float) getHeight()));
            }
            for (auto t = (time::Tick) (tl.start / (double) perBeat) * perBeat; (double) t <= tl.end(); t += perBeat)
            {
                g.setColour (t % perBar == 0 ? theme::border.withAlpha (0.9f) : theme::borderSubtle.withAlpha (0.7f));
                g.fillRect (juce::Rectangle<float> (std::round (tl.x ((double) t)), 0.0f, 1.0f, (float) getHeight()));
            }

            // Clips.
            for (auto& t : p.tracks)
            {
                const auto colour = owner.selection.colourOf (t.id);
                for (auto& c : t.clips)
                {
                    if (t.muted) g.beginTransparencyLayer (0.45f);
                    paintClip (g, t, c, colour);
                    if (t.muted) g.endTransparencyLayer();
                }
            }

            // Playhead: a 2 px accent line with a soft glow.
            const auto x = tl.x (playhead);
            juce::ColourGradient glow (theme::playhead.withAlpha (0.0f), x - 8.0f, 0.0f, theme::playhead.withAlpha (0.0f), x + 8.0f, 0.0f, false);
            glow.addColour (0.5, theme::playhead.withAlpha (0.35f));
            g.setGradientFill (glow);
            g.fillRect (x - 8.0f, 0.0f, 16.0f, (float) getHeight());
            g.setColour (theme::playhead);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
            g.restoreState();

            g.setColour (theme::borderSubtle);
            g.fillRect (headerWidth - 1, 0, 1, getHeight());

            // Where a dragged track header will land.
            if (dropRow >= 0)
            {
                g.setColour (theme::accent);
                g.fillRect (0, juce::jlimit (0, getHeight() - 3, dropRow * rowHeight - 1), getWidth(), 3);
            }
        }

        // Spec 5.2: translucent body, solid header strip with the name, and a
        // preview of the content (drum hits in three rows, or notes).
        void paintClip (juce::Graphics& g, const Track& t, const Clip& c, juce::Colour colour)
        {
            const auto r = clipBounds (t, c);
            const bool selected = owner.selection.getClip() == c.id;
            constexpr float radius = 6.0f;

            {
                juce::Graphics::ScopedSaveState state (g);
                juce::Path shape;
                shape.addRoundedRectangle (r, radius);
                g.reduceClipRegion (shape);
                g.setColour (colour.withAlpha (0.16f));
                g.fillRect (r);
                g.setColour (colour.withAlpha (0.9f));
                g.fillRect (r.withHeight (16.0f));
            }

            g.setColour (theme::bgApp);
            g.setFont (theme::font (10.0f, juce::Font::bold));
            const auto perBar = owner.session.getProject().timeSignature.ticksPerBar();
            const bool wholeBars = c.length % perBar == 0;
            const auto bars = (double) c.length / (double) perBar;
            // The label stays readable when the clip starts left of the view.
            auto label = r.withHeight (16.0f);
            label.setLeft (juce::jmax (label.getX(), (float) headerWidth));
            g.drawText ((t.type == TrackType::drums ? "Pattern" : "Clip") + juce::String::fromUTF8 (" \xc2\xb7 ")
                            + juce::String (bars, wholeBars ? 0 : 2) + (c.length == perBar ? " bar" : " bars"),
                        label.withTrimmedLeft (7.0f).withTrimmedRight (4.0f), juce::Justification::centredLeft, true);

            // Content preview.
            g.saveState();
            g.reduceClipRegion (r.reduced (1.0f).withTrimmedTop (16.0f).toNearestInt());
            const auto tl = timeline();
            g.setColour (colour);
            if (t.type == TrackType::drums)
            {
                // Rows: kick, snare + clap, hats (closed + open, dimmer).
                constexpr int rowOfLane[] { 0, 1, 2, 2, 1 };
                const float rowY[] { 22.0f, 34.0f, 46.0f };
                const auto stepTicks = project::patternTicks / project::stepsPerPattern;
                const auto cellW = juce::jmax (1.5f, tl.w ((double) stepTicks) - 2.0f);
                for (time::Tick at = 0, step = 0; at < c.length; at += stepTicks, ++step)
                    for (int lane = 0; lane < audio::DrumMachine::numLanes; ++lane)
                        if (c.pattern[(size_t) lane][(size_t) (step % project::stepsPerPattern)])
                        {
                            const auto row = rowOfLane[lane];
                            g.setColour (colour.withAlpha (row == 2 ? 0.55f : 1.0f));
                            g.fillRoundedRectangle (tl.x ((double) (c.start + at)) + 1.0f, r.getY() + rowY[row] - 2.0f,
                                                    cellW, 7.0f, 1.5f);
                        }
            }
            else if (! c.notes.empty())
            {
                int lo = 127, hi = 0;
                for (auto& n : c.notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
                const float range = (float) std::max (12, hi - lo + 1);
                const float bandTop = r.getY() + 22.0f, bandHeight = juce::jmax (4.0f, r.getHeight() - 26.0f);
                for (auto& n : c.notes)
                {
                    if (n.start >= c.length) continue;
                    const auto y = bandTop + bandHeight - ((float) (n.pitch - lo) + 0.5f) / range * bandHeight;
                    g.fillRoundedRectangle (tl.x ((double) (c.start + n.start)), y - 2.0f,
                                            juce::jmax (3.0f, tl.x ((double) (c.start + std::min (n.end(), c.length))) - tl.x ((double) (c.start + n.start)) - 1.0f),
                                            4.0f, 2.0f);
                }
            }
            g.restoreState();

            if (selected)
            {
                g.setColour (theme::textPrimary);
                g.drawRoundedRectangle (r.reduced (0.75f), radius, 1.5f);
            }
            else
            {
                g.setColour (colour.withAlpha (0.7f));
                g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            }
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

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (! owner.handleWheel (e.getEventRelativeTo (&owner), wheel))
                Component::mouseWheelMove (e, wheel);   // vertical: the viewport scrolls the tracks
        }

        // Track drag: line drawn above row `row` (-1 = none).
        void setDropRow (int row)
        {
            if (row != dropRow)
            {
                dropRow = row;
                repaint();
            }
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
        int dropRow = -1;
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

        hScroll.setAutoHide (false);
        hScroll.addListener (this);
        addAndMakeVisible (hScroll);
        zoomOutButton.setTooltip ("Zoom out (Ctrl+-, or Ctrl+wheel)");
        zoomInButton.setTooltip ("Zoom in (Ctrl+=, or Ctrl+wheel)");
        fitButton.setTooltip ("Show the whole song");
        zoomOutButton.onClick = [this] { zoomOut(); };
        zoomInButton.onClick = [this] { zoomIn(); };
        fitButton.onClick = [this] { zoomToFit(); };
        for (auto* b : { &zoomOutButton, &zoomInButton, &fitButton })
            b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);   // ghost buttons
        for (auto* b : { &zoomOutButton, &zoomInButton, &fitButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }

        sessionListener = session.addListener ([this] (project::Session::Change change)
        {
            if (change == project::Session::Change::metadata || change == project::Session::Change::instrument)
                return;
            rebuild();
            setView (viewStart, viewBars);   // the song may have shrunk
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

    //==============================================================================
    int ArrangementView::totalBars() const
    {
        const auto perBar = (double) session.getProject().timeSignature.ticksPerBar();
        return std::max (barsShown(), (int) std::ceil ((viewStart + visibleTicks()) / perBar));
    }

    double ArrangementView::visibleTicks() const
    {
        return viewBars * (double) session.getProject().timeSignature.ticksPerBar();
    }

    void ArrangementView::setView (double startTick, double bars)
    {
        const auto perBar = (double) session.getProject().timeSignature.ticksPerBar();
        viewBars = juce::jlimit (minBarsVisible, maxBarsVisible, bars);
        const auto maxStart = std::max (0.0, (double) barsShown() * perBar - visibleTicks());
        viewStart = juce::jlimit (0.0, maxStart, startTick);
        updateScrollBar();
        ruler->repaint();
        lanes->repaint();
    }

    void ArrangementView::updateScrollBar()
    {
        const auto perBar = (double) session.getProject().timeSignature.ticksPerBar();
        hScroll.setRangeLimits (0.0, std::max ((double) barsShown() * perBar, viewStart + visibleTicks()), juce::dontSendNotification);
        hScroll.setCurrentRange (viewStart, visibleTicks(), juce::dontSendNotification);
        hScroll.setSingleStepSize (perBar);
    }

    void ArrangementView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
    {
        setView (newRangeStart, viewBars);
    }

    void ArrangementView::zoomAround (double factor, float xInThis)
    {
        const auto laneWidth = (float) std::max (1, getWidth() - headerWidth - viewport.getScrollBarThickness());
        const auto fraction = juce::jlimit (0.0, 1.0, (double) (xInThis - (float) headerWidth) / (double) laneWidth);
        const auto anchor = viewStart + fraction * visibleTicks();
        const auto bars = juce::jlimit (minBarsVisible, maxBarsVisible, viewBars / factor);
        const auto perBar = (double) session.getProject().timeSignature.ticksPerBar();
        setView (anchor - fraction * bars * perBar, bars);
    }

    // Button/keyboard zoom keeps the playhead in place when it is in view, else the centre.
    void ArrangementView::zoomIn()  { zoomAround (1.5, zoomAnchorX()); }
    void ArrangementView::zoomOut() { zoomAround (1.0 / 1.5, zoomAnchorX()); }

    float ArrangementView::zoomAnchorX() const
    {
        const auto laneWidth = (double) std::max (1, getWidth() - headerWidth - viewport.getScrollBarThickness());
        const auto position = engine.getPositionTicks();
        const auto fraction = position >= viewStart && position <= viewStart + visibleTicks() ? (position - viewStart) / visibleTicks() : 0.5;
        return (float) headerWidth + (float) (fraction * laneWidth);
    }

    void ArrangementView::zoomToFit()
    {
        const auto& p = session.getProject();
        time::Tick last = 0;
        for (auto& t : p.tracks)
            for (auto& c : t.clips)
                last = std::max (last, c.end());
        const auto perBar = p.timeSignature.ticksPerBar();
        setView (0.0, std::max (4.0, (double) ((last + perBar - 1) / perBar) + 1.0));
    }

    bool ArrangementView::handleWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel, bool plainWheelZooms)
    {
        const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        if (std::abs (delta) < 1.0e-6f)
            return false;

        if (e.mods.isCommandDown() || (plainWheelZooms && ! e.mods.isShiftDown()))
        {
            zoomAround (std::pow (1.5, (double) wheel.deltaY * 4.0), e.position.x);
            return true;
        }
        if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        {
            setView (viewStart - (double) delta * visibleTicks() * 0.5, viewBars);
            return true;
        }
        return false;
    }

    void ArrangementView::setTrackDropRow (int row)
    {
        lanes->setDropRow (row);
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
            repaint();   // the track count
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
        const auto position = engine.getPositionTicks();

        // When the playhead moves (playing, Stop, seek) page along so it stays in
        // view; a still playhead never overrides scrolling, nor does a drag.
        const bool moved = std::abs (position - lastPosition) > 0.5;
        lastPosition = position;
        if (moved && ! isMouseButtonDown (true)
            && (position < viewStart || position > viewStart + visibleTicks() * 0.97))
        {
            const auto perBar = (double) session.getProject().timeSignature.ticksPerBar();
            setView (std::floor (position / perBar) * perBar, viewBars);
        }

        if (moved)
        {
            lanes->setPlayhead (position);
            ruler->repaint();
        }
    }

    void ArrangementView::duplicateSelectedClip()
    {
        if (const auto copy = session.duplicateClip (selection.getClip()))
            selection.selectClip (copy);
    }

    bool ArrangementView::copySelectedClip()
    {
        const auto* c = session.findClip (selection.getClip());
        if (c == nullptr)
            return false;
        clipboard = *c;
        clipboardType = session.findTrack (session.trackOfClip (c->id))->type;
        if (onMessage) onMessage ("Clip copied - select a track and press Ctrl+V to paste it at the playhead");
        return true;
    }

    void ArrangementView::cutSelectedClip()
    {
        if (copySelectedClip())
            session.deleteClip (selection.getClip());
    }

    bool ArrangementView::pasteClip()
    {
        if (! clipboard)
            return false;
        const auto* t = session.findTrack (selection.getTrack());
        if (t == nullptr || t->type != clipboardType)
        {
            if (onMessage) onMessage (clipboardType == TrackType::drums ? "Select a drum track to paste this pattern"
                                                                        : "Select a synth track to paste this clip");
            return false;
        }
        const auto perBeat = (double) session.getProject().timeSignature.ticksPerBeat();
        const auto at = (time::Tick) (std::round (engine.getPlayStart() / perBeat) * perBeat);
        if (const auto id = session.pasteClip (t->id, *clipboard, clipboardType, at))
        {
            selection.selectClip (id);
            return true;
        }
        return false;
    }

    void ArrangementView::deleteSelectedClip()
    {
        session.deleteClip (selection.getClip());
    }

    void ArrangementView::paint (juce::Graphics& g)
    {
        // The panel: bg/panel, rounded, border/subtle (children are clipped to it
        // by being inset; the timeline well is bg/inset).
        const auto panel = getLocalBounds().toFloat();
        g.setColour (theme::bgPanel);
        g.fillRoundedRectangle (panel, theme::panelRadius);

        // Track-list toolbar: TRACKS + count chip.
        auto corner = getLocalBounds().removeFromTop (rulerHeight).withWidth (headerWidth).reduced (16, 0);
        g.setColour (theme::textMuted);
        g.setFont (theme::captionFont (10.0f));
        const juce::String caption ("TRACKS");
        const auto captionWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), caption) + 2;
        g.drawText (caption, corner.removeFromLeft (captionWidth), juce::Justification::centredLeft, false);
        corner.removeFromLeft (8);
        const auto count = juce::String ((int) session.getProject().tracks.size());
        g.setFont (theme::numericFont (10.0f, juce::Font::bold));
        const auto chipWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), count) + 12;
        const auto chipArea = corner.removeFromLeft (chipWidth).withSizeKeepingCentre (chipWidth, 16);
        g.setColour (theme::bgControl);
        g.fillRoundedRectangle (chipArea.toFloat(), 8.0f);
        g.setColour (theme::textSecondary);
        g.drawText (count, chipArea, juce::Justification::centred, false);

        // ZOOM caption in the footer.
        g.setColour (theme::textMuted);
        g.setFont (theme::captionFont (9.0f));
        g.drawText ("ZOOM", getLocalBounds().removeFromBottom (footerHeight).withWidth (headerWidth).reduced (16, 0),
                    juce::Justification::centredLeft, false);

        // Footer under the timeline, and the column divider.
        g.setColour (theme::bgInset);
        g.fillRect (getLocalBounds().removeFromBottom (footerHeight).withTrimmedLeft (headerWidth).withTrimmedRight (1).withTrimmedBottom (1));
        g.setColour (theme::borderSubtle);
        g.fillRect (headerWidth - 1, 0, 1, getHeight());
    }

    void ArrangementView::paintOverChildren (juce::Graphics& g)
    {
        // Round the panel's corners over the scrolled content, then outline it.
        const auto panel = getLocalBounds().toFloat();
        juce::Path outside;
        outside.addRectangle (panel);
        outside.addRoundedRectangle (panel, theme::panelRadius);
        outside.setUsingNonZeroWinding (false);
        g.setColour (theme::bgApp);
        g.fillPath (outside);
        g.setColour (theme::borderSubtle);
        g.drawRoundedRectangle (panel.reduced (0.5f), theme::panelRadius, theme::borderWidth);
    }

    void ArrangementView::resized()
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (rulerHeight);

        auto footer = area.removeFromBottom (footerHeight);
        auto zoom = footer.removeFromLeft (headerWidth).reduced (8, 7).withTrimmedLeft (48);
        zoomOutButton.setBounds (zoom.removeFromLeft (28));
        zoom.removeFromLeft (6);
        zoomInButton.setBounds (zoom.removeFromLeft (28));
        zoom.removeFromLeft (6);
        fitButton.setBounds (zoom.removeFromLeft (40));
        hScroll.setBounds (footer.withTrimmedRight (viewport.getScrollBarThickness()).reduced (12, (footerHeight - 6) / 2));
        auto corner = top.removeFromLeft (headerWidth).reduced (8, 7);
        addSynthButton.setBounds (corner.removeFromRight (74));
        corner.removeFromRight (6);
        addDrumsButton.setBounds (corner.removeFromRight (74));
        ruler->setBounds (top.withTrimmedRight (viewport.getScrollBarThickness()));
        viewport.setBounds (area);

        const auto contentWidth = area.getWidth() - viewport.getScrollBarThickness();
        lanes->setSize (contentWidth, juce::jmax (area.getHeight(), (int) lanes->headers.size() * rowHeight));
        for (size_t i = 0; i < lanes->headers.size(); ++i)
            lanes->headers[i]->setBounds (0, (int) i * rowHeight, headerWidth, rowHeight);
    }
}
