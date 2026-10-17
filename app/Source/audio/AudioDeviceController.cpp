#include "audio/AudioDeviceController.h"

namespace beatforge
{
    AudioDeviceController::AudioDeviceController (audio::AudioEngine& e, juce::PropertiesFile& s)
        : engine (e), settings (s)
    {
    }

    AudioDeviceController::~AudioDeviceController()
    {
        deviceManager.removeChangeListener (this);
        deviceManager.removeAudioCallback (this);
        deviceManager.closeAudioDevice();
    }

    void AudioDeviceController::open()
    {
        const auto saved = settings.getXmlValue (settingsKey);
        const auto error = deviceManager.initialise (0, 2, saved.get(), true);

        {
            std::scoped_lock lock (errorMutex);
            lastError = error;
        }

        deviceManager.addAudioCallback (this);
        deviceManager.addChangeListener (this);
        sendChangeMessage();
    }

    AudioDeviceController::Status AudioDeviceController::getStatus() const
    {
        Status status;
        status.typeName = deviceManager.getCurrentAudioDeviceType();

        if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && device->isOpen())
        {
            status.deviceOpen = true;
            status.deviceName = device->getName();
            status.sampleRate = device->getCurrentSampleRate();
            status.bufferSize = device->getCurrentBufferSizeSamples();
            status.outputLatencySamples = device->getOutputLatencyInSamples();
            status.xruns = deviceManager.getXRunCount();
        }

        {
            std::scoped_lock lock (errorMutex);
            status.error = lastError;
        }

        if (! status.deviceOpen && status.error.isEmpty())
        {
            auto* type = deviceManager.getCurrentDeviceTypeObject();
            const auto typeName = status.typeName.isNotEmpty() ? status.typeName : juce::String ("audio");

            if (type == nullptr || type->getDeviceNames (false).isEmpty())
                status.error = "no " + typeName + " output devices were found";
            else
                status.error = "no output device is selected";
        }

        return status;
    }

    void AudioDeviceController::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                                  float* const* outputs, int numOutputs,
                                                                  int numSamples,
                                                                  const juce::AudioIODeviceCallbackContext&)
    {
        engine.process (outputs, numOutputs, numSamples);
    }

    void AudioDeviceController::audioDeviceAboutToStart (juce::AudioIODevice* device)
    {
        engine.prepare (device->getCurrentSampleRate());
    }

    void AudioDeviceController::audioDeviceStopped()
    {
        engine.reset();
    }

    void AudioDeviceController::audioDeviceError (const juce::String& message)
    {
        // May arrive on any thread.
        {
            std::scoped_lock lock (errorMutex);
            lastError = message;
        }
        triggerAsyncUpdate();
    }

    void AudioDeviceController::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // Device selection/settings changed (from the settings dialog or the system).
        if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && device->isOpen())
        {
            std::scoped_lock lock (errorMutex);
            lastError.clear();
        }

        if (auto xml = deviceManager.createStateXml())
            settings.setValue (settingsKey, xml.get());
        else
            settings.removeValue (settingsKey);   // using the system default

        settings.saveIfNeeded();
        sendChangeMessage();
    }

    void AudioDeviceController::handleAsyncUpdate()
    {
        sendChangeMessage();
    }
}
