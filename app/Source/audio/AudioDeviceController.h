#pragma once

#include <beatforge/audio/AudioEngine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <mutex>

namespace beatforge
{
    // Owns the native audio device and the MIDI inputs and connects them to the
    // core AudioEngine. Lives on the message thread; the AudioIODeviceCallback
    // methods run on the audio thread and the MIDI callback on a MIDI thread,
    // and those touch nothing but the engine and an atomic counter.
    //
    // MIDI inputs: every input is enabled the first time it is seen (also when
    // plugged in later); after that the choice in Audio Settings is kept.
    //
    // Broadcasts a change message whenever the device or its status changes.
    class AudioDeviceController final : public juce::ChangeBroadcaster,
                                        private juce::AudioIODeviceCallback,
                                        private juce::MidiInputCallback,
                                        private juce::ChangeListener,
                                        private juce::AsyncUpdater
    {
    public:
        struct Status
        {
            bool deviceOpen = false;
            juce::String typeName, deviceName;
            double sampleRate = 0.0;
            int bufferSize = 0;
            int outputLatencySamples = 0;
            int xruns = 0;
            double cpuLoad = 0.0;           // share of the audio callback's time budget in use (0..1)
            juce::StringArray midiInputs;   // enabled MIDI inputs
            juce::String error;   // empty when everything is fine
        };

        AudioDeviceController (audio::AudioEngine&, juce::PropertiesFile& settings);
        ~AudioDeviceController() override;

        // Opens the saved device, falling back to the system default.
        void open();

        Status getStatus() const;
        juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

        // Grows with every MIDI message received (for an activity light).
        std::uint32_t getMidiEventCount() const noexcept { return midiEventCount.load (std::memory_order_relaxed); }

    private:
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override;
        void audioDeviceError (const juce::String& message) override;

        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
        void enableNewMidiInputs();
        void saveDeviceState();

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void handleAsyncUpdate() override;

        audio::AudioEngine& engine;
        juce::PropertiesFile& settings;
        juce::AudioDeviceManager deviceManager;

        mutable std::mutex errorMutex;   // never taken on the audio callback path
        juce::String lastError;

        std::atomic<std::uint32_t> midiEventCount { 0 };
        juce::MidiDeviceListConnection midiDevicesChanged;

        static constexpr const char* settingsKey = "audioDeviceState";
        static constexpr const char* knownMidiInputsKey = "knownMidiInputs";

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceController)
    };
}
