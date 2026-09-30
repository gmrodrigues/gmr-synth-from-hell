#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cmath>

namespace
{
class SineSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SineVoice final : public juce::SynthesiserVoice
{
public:
    using juce::SynthesiserVoice::renderNextBlock;

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SineSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int pitchWheelValue) override
    {
        currentNote = midiNoteNumber;
        level = juce::jlimit (0.0, 0.16, static_cast<double> (velocity) * 0.16);
        tailOff = 0.0;
        updatePitch (pitchWheelValue);
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            if (tailOff <= 0.0)
                tailOff = 1.0;
        }
        else
        {
            clearCurrentNote();
            angleDelta = 0.0;
        }
    }

    void pitchWheelMoved (int value) override
    {
        updatePitch (value);
    }

    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample,
                          int numSamples) override
    {
        if (angleDelta <= 0.0)
            return;

        while (--numSamples >= 0)
        {
            auto sample = static_cast<float> (std::sin (currentAngle) * level);

            if (tailOff > 0.0)
            {
                sample *= static_cast<float> (tailOff);
                tailOff *= 0.9995;

                if (tailOff <= 0.005)
                {
                    clearCurrentNote();
                    angleDelta = 0.0;
                    break;
                }
            }

            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                output.addSample (channel, startSample, sample);

            currentAngle += angleDelta;
            ++startSample;
        }
    }

private:
    void updatePitch (int pitchWheelValue)
    {
        const auto bend = (static_cast<double> (pitchWheelValue) - 8192.0) / 8192.0;
        const auto note = static_cast<double> (currentNote) + bend * 2.0;
        const auto frequency = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
        angleDelta = juce::MathConstants<double>::twoPi * frequency / getSampleRate();
    }

    int currentNote = 69;
    double currentAngle = 0.0;
    double angleDelta = 0.0;
    double level = 0.0;
    double tailOff = 0.0;
};

class MainComponent final : public juce::AudioAppComponent,
                            private juce::MidiInputCallback,
                            private juce::Timer
{
public:
    MainComponent()
    {
        title.setText ("GMR Synth — teste rápido do MPK Mini Plus",
                       juce::dontSendNotification);
        title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        title.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (title);

        status.setJustificationType (juce::Justification::topLeft);
        status.setFont (juce::FontOptions (16.0f));
        status.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (status);

        hint.setText ("Toque as teclas. Pitch bend também está ativo.\n"
                      "Saída Bluetooth funciona para este teste, mas terá latência perceptível.",
                      juce::dontSendNotification);
        hint.setJustificationType (juce::Justification::centred);
        hint.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (hint);

        testButton.onClick = [safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safeThis == nullptr)
                return;

            safeThis->lastNote.store (69, std::memory_order_relaxed);
            safeThis->lastVelocity.store (0.8f, std::memory_order_relaxed);
            safeThis->noteIsOn.store (true, std::memory_order_relaxed);
            safeThis->synth.noteOn (1, 69, 0.8f);

            juce::Timer::callAfterDelay (700, [safeThis]
            {
                if (safeThis == nullptr)
                    return;

                safeThis->synth.noteOff (1, 69, 0.0f, true);
                safeThis->noteIsOn.store (false, std::memory_order_relaxed);
            });
        };
        addAndMakeVisible (testButton);

        for (int i = 0; i < 16; ++i)
            synth.addVoice (new SineVoice());
        synth.addSound (new SineSound());

        setSize (680, 350);
        setAudioChannels (0, 2);
        preferJackBackend();
        openMpkMidiInputs();
        startTimerHz (20);
    }

    ~MainComponent() override
    {
        stopTimer();

        for (const auto& identifier : openedMidiIdentifiers)
            deviceManager.removeMidiInputDeviceCallback (identifier, this);

        shutdownAudio();
    }

    void prepareToPlay (int, double sampleRate) override
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        bufferToFill.clearActiveBufferRegion();

        juce::MidiBuffer noMidiEvents;
        synth.renderNextBlock (*bufferToFill.buffer, noMidiEvents,
                               bufferToFill.startSample, bufferToFill.numSamples);

        auto peak = 0.0f;
        for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel)
            peak = juce::jmax (peak, bufferToFill.buffer->getMagnitude (
                                        channel, bufferToFill.startSample, bufferToFill.numSamples));

        outputPeak.store (peak, std::memory_order_relaxed);
        audioBlockCount.fetch_add (1, std::memory_order_relaxed);
    }

    void releaseResources() override {}

    void paint (juce::Graphics& graphics) override
    {
        graphics.fillAll (juce::Colour (0xff141820));
        graphics.setColour (juce::Colour (0xff65d6a6));
        graphics.fillRoundedRectangle (18.0f, 72.0f,
                                       static_cast<float> (getWidth() - 36), 132.0f, 12.0f);
        graphics.setColour (juce::Colour (0xff1d2430));
        graphics.fillRoundedRectangle (22.0f, 76.0f,
                                       static_cast<float> (getWidth() - 44), 124.0f, 9.0f);
    }

    void resized() override
    {
        title.setBounds (20, 18, getWidth() - 40, 40);
        status.setBounds (42, 92, getWidth() - 84, 92);
        testButton.setBounds ((getWidth() - 220) / 2, 218, 220, 42);
        hint.setBounds (30, 275, getWidth() - 60, 54);
    }

private:
    void preferJackBackend()
    {
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type->getTypeName().containsIgnoreCase ("JACK"))
            {
                deviceManager.setCurrentAudioDeviceType (type->getTypeName(), true);
                return;
            }
        }
    }

    void openMpkMidiInputs()
    {
        for (const auto& input : juce::MidiInput::getAvailableDevices())
        {
            if (! input.name.containsIgnoreCase ("MPK mini Plus"))
                continue;

            deviceManager.setMidiInputDeviceEnabled (input.identifier, true);
            deviceManager.addMidiInputDeviceCallback (input.identifier, this);
            openedMidiIdentifiers.add (input.identifier);
            openedMidiNames.add (input.name);
        }
    }

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override
    {
        if (message.isNoteOn())
        {
            lastNote.store (message.getNoteNumber(), std::memory_order_relaxed);
            lastVelocity.store (message.getVelocity(), std::memory_order_relaxed);
            noteIsOn.store (true, std::memory_order_relaxed);
            midiMessageCount.fetch_add (1, std::memory_order_relaxed);
            synth.noteOn (message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
        }
        else if (message.isNoteOff())
        {
            lastNote.store (message.getNoteNumber(), std::memory_order_relaxed);
            noteIsOn.store (false, std::memory_order_relaxed);
            midiMessageCount.fetch_add (1, std::memory_order_relaxed);
            synth.noteOff (message.getChannel(), message.getNoteNumber(),
                           message.getFloatVelocity(), true);
        }
        else if (message.isPitchWheel())
        {
            midiMessageCount.fetch_add (1, std::memory_order_relaxed);
            synth.handlePitchWheel (message.getChannel(), message.getPitchWheelValue());
        }
        else if (message.isController())
        {
            synth.handleController (message.getChannel(), message.getControllerNumber(),
                                    message.getControllerValue());
        }
    }

    void timerCallback() override
    {
        juce::String text;

        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            text << "Áudio: " << device->getTypeName() << " / " << device->getName()
                 << "\n" << juce::String (device->getCurrentSampleRate(), 0) << " Hz, bloco "
                 << device->getCurrentBufferSizeSamples() << " amostras";
        }
        else
        {
            text << "Áudio indisponível";
        }

        if (audioError.isNotEmpty())
            text << " — " << audioError;

        text << "\nMIDI: "
             << (openedMidiNames.isEmpty() ? "MPK Mini Plus não encontrado"
                                           : openedMidiNames.joinIntoString (", "));

        const auto count = midiMessageCount.load (std::memory_order_relaxed);
        if (count > 0)
        {
            const auto note = lastNote.load (std::memory_order_relaxed);
            text << "\nÚltima tecla: " << juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                 << " (" << note << ") — "
                 << (noteIsOn.load (std::memory_order_relaxed) ? "pressionada" : "solta")
                 << ", velocity " << juce::String (lastVelocity.load (std::memory_order_relaxed), 2)
                 << ", eventos " << count;
        }
        else
        {
            text << "\nAguardando uma tecla…";
        }

        const auto peak = outputPeak.load (std::memory_order_relaxed);
        text << "\nDSP: " << audioBlockCount.load (std::memory_order_relaxed) << " blocos, pico "
             << juce::String (juce::Decibels::gainToDecibels (peak, -100.0f), 1) << " dBFS";

        status.setText (text, juce::dontSendNotification);
    }

    juce::Label title;
    juce::Label status;
    juce::Label hint;
    juce::TextButton testButton { "TESTAR SOM (A4)" };

    juce::Synthesiser synth;
    juce::StringArray openedMidiIdentifiers;
    juce::StringArray openedMidiNames;
    juce::String audioError;

    std::atomic<int> lastNote { -1 };
    std::atomic<float> lastVelocity { 0.0f };
    std::atomic<bool> noteIsOn { false };
    std::atomic<unsigned long long> midiMessageCount { 0 };
    std::atomic<unsigned long long> audioBlockCount { 0 };
    std::atomic<float> outputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (juce::String name)
        : DocumentWindow (std::move (name), juce::Colour (0xff141820),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "GMR Synth Quick Test"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override           { return false; }

    void initialise (const juce::String&) override
    {
        window = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        window.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> window;
};
} // namespace

START_JUCE_APPLICATION (Application)
