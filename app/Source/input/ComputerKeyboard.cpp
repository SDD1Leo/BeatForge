#include "input/ComputerKeyboard.h"

namespace beatforge
{
    int ComputerKeyboard::semitoneFor (int keyCode) noexcept
    {
        constexpr const char* keys = "q2w3er5t6y7ui9o0p[=]";   // semitones 0..19
        const auto c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) keyCode);
        for (int i = 0; keys[i] != 0; ++i)
            if ((juce::juce_wchar) keys[i] == c)
                return i;
        return -1;
    }

    void ComputerKeyboard::setEnabled (bool shouldBeEnabled)
    {
        if (enabled == shouldBeEnabled)
            return;
        if (! shouldBeEnabled)
            releaseAll();
        enabled = shouldBeEnabled;
        if (onChange) onChange();
    }

    void ComputerKeyboard::send (int pitch, int vel)
    {
        engine.screenNote ({ audio::AudioEngine::now(), pitch, vel });
    }

    bool ComputerKeyboard::keyPressed (const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        if (! enabled || mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
            return false;

        const auto code = key.getKeyCode();
        const auto lower = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);

        if (lower == 'z' || lower == 'x' || lower == 'c' || lower == 'v')
        {
            // Changing the octave ends held notes (their keys now mean other pitches).
            if (lower == 'z' || lower == 'x')
            {
                releaseAll();
                baseNote = juce::jlimit (12, 108 - numKeys + 1, baseNote + (lower == 'x' ? 12 : -12));
            }
            else
            {
                velocity = juce::jlimit (1, 127, velocity + (lower == 'v' ? 20 : -20));
            }
            if (onChange) onChange();
            return true;
        }

        const auto semitone = semitoneFor (code);
        if (semitone < 0)
            return false;

        const auto pitch = baseNote + semitone;
        if (heldKey[(size_t) pitch] == 0)   // key auto-repeat: already sounding
        {
            heldKey[(size_t) pitch] = code;
            send (pitch, velocity);
        }
        return true;
    }

    void ComputerKeyboard::keyStateChanged()
    {
        for (int pitch = 0; pitch < 128; ++pitch)
            if (const auto code = heldKey[(size_t) pitch]; code != 0 && ! juce::KeyPress::isKeyCurrentlyDown (code))
            {
                heldKey[(size_t) pitch] = 0;
                send (pitch, 0);
            }
    }

    void ComputerKeyboard::releaseAll()
    {
        for (int pitch = 0; pitch < 128; ++pitch)
            if (heldKey[(size_t) pitch] != 0)
            {
                heldKey[(size_t) pitch] = 0;
                send (pitch, 0);
            }
    }
}
