#pragma once

#include <beatforge/audio/AudioEngine.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <mutex>

namespace beatforge
{
    // Owns the native audio device and connects it to the core AudioEngine.
    // Lives on the message thread; only the AudioIODeviceCallback methods run
    // on the audio thread, and those touch nothing but the engine.
    //
    // Broadcasts a change message whenever the device or its status changes.
    class AudioDeviceController final : public juce::ChangeBroadcaster,
                                        private juce::AudioIODeviceCallback,
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
            juce::String error;   // empty when everything is fine
        };

        AudioDeviceController (audio::AudioEngine&, juce::PropertiesFile& settings);
        ~AudioDeviceController() override;

        // Opens the saved device, falling back to the system default.
        void open();

        Status getStatus() const;
        juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    private:
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override;
        void audioDeviceError (const juce::String& message) override;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void handleAsyncUpdate() override;

        audio::AudioEngine& engine;
        juce::PropertiesFile& settings;
        juce::AudioDeviceManager deviceManager;

        mutable std::mutex errorMutex;   // never taken on the audio callback path
        juce::String lastError;

        static constexpr const char* settingsKey = "audioDeviceState";

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceController)
    };
}
