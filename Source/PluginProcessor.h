#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

class LinkGrabberAudioProcessor : public juce::AudioProcessor
{
public:
    LinkGrabberAudioProcessor();
    ~LinkGrabberAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LinkGrabber"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Non-parameter session state (message thread only). Stored as properties on a
    // child ValueTree of the APVTS state so it persists with the host session.
    juce::String getLink() const;
    void setLink(const juce::String& newLink);
    juce::String getOutputFolder() const;
    void setOutputFolder(const juce::String& newFolderPath);

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::ValueTree getOrCreateExtras();
    juce::ValueTree getExtrasConst() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkGrabberAudioProcessor)
};
