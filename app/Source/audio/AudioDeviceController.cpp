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
        deviceManager.removeMidiInputDeviceCallback ({}, this);
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

        // Listen first: JUCE only queues change messages while someone listens.
        deviceManager.addChangeListener (this);
        deviceManager.addAudioCallback (this);
        deviceManager.addMidiInputDeviceCallback ({}, this);   // all enabled inputs
        enableNewMidiInputs();
        midiDevicesChanged = juce::MidiDeviceListConnection::make ([this]
        {
            enableNewMidiInputs();
            sendChangeMessage();
        });
        sendChangeMessage();
    }

    void AudioDeviceController::saveDeviceState()
    {
        if (auto xml = deviceManager.createStateXml())
            settings.setValue (settingsKey, xml.get());
        else
            settings.removeValue (settingsKey);   // using the system default

        settings.saveIfNeeded();
    }

    void AudioDeviceController::enableNewMidiInputs()
    {
        auto known = juce::StringArray::fromLines (settings.getValue (knownMidiInputsKey));
        known.removeEmptyStrings();
        bool changed = false;

        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (! known.contains (device.identifier))
            {
                deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
                known.add (device.identifier);
                changed = true;
            }

        if (changed)
        {
            settings.setValue (knownMidiInputsKey, known.joinIntoString ("\n"));
            saveDeviceState();   // the enabled inputs are part of the device state
        }
    }

    void AudioDeviceController::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
    {
        // MIDI thread (JUCE serialises the callbacks of all inputs). Any channel.
        const auto now = audio::AudioEngine::now();
        if (m.isNoteOn())
            engine.deviceNote ({ now, m.getNoteNumber(), m.getVelocity() });
        else if (m.isNoteOff())
            engine.deviceNote ({ now, m.getNoteNumber(), 0 });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            engine.deviceNote ({ now, -1, 0 });
        midiEventCount.fetch_add (1, std::memory_order_relaxed);
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
            status.cpuLoad = deviceManager.getCpuUsage();
        }

        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
                status.midiInputs.add (device.name);

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

        // What is rendered now is heard one buffer plus the device latency later;
        // recording places notes where the player heard the beat.
        engine.setOutputLatency (device->getCurrentBufferSizeSamples() + device->getOutputLatencyInSamples());
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

        saveDeviceState();
        sendChangeMessage();
    }

    void AudioDeviceController::handleAsyncUpdate()
    {
        sendChangeMessage();
    }
}
