#pragma once

#include <beatforge/audio/AudioEngine.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace beatforge
{
    // Plays notes from the computer keyboard, like a small MIDI keyboard
    // (tracker layout): the QWERTY row holds the white keys and the number row
    // the black keys, from Q = C up to ] = G an octave and a half higher.
    //
    //    2 3   5 6 7   9 0   =         (black keys)
    //   Q W E R T Y U I O P [ ]        (white keys)
    //
    // Z / X: octave down / up. C / V: velocity down / up.
    // Notes go through the engine's live input, so they play the selected synth
    // track and are recorded like a MIDI keyboard. Message thread only.
    class ComputerKeyboard
    {
    public:
        explicit ComputerKeyboard (audio::AudioEngine& e) : engine (e) {}
        ~ComputerKeyboard() { releaseAll(); }

        void setEnabled (bool);
        bool isEnabled() const noexcept { return enabled; }

        // True if the key belongs to the computer keyboard (only while enabled,
        // and only without Ctrl/Alt), so it must not trigger anything else.
        bool keyPressed (const juce::KeyPress&);
        // Call on every key state change (and regularly): ends notes whose key is up.
        void keyStateChanged();
        void releaseAll();

        int getLowestNote() const noexcept { return baseNote; }
        int getHighestNote() const noexcept { return baseNote + numKeys - 1; }
        int getVelocity() const noexcept { return velocity; }

        std::function<void()> onChange;   // enabled, octave or velocity changed

        static constexpr int numKeys = 20;

    private:
        static int semitoneFor (int keyCode) noexcept;   // -1 if not a note key
        void send (int pitch, int vel);

        audio::AudioEngine& engine;
        bool enabled = false;
        int baseNote = 60, velocity = 100;
        std::array<int, 128> heldKey {};   // per pitch: key code holding it, 0 = none
    };
}
