#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Identifier extrasType { "Extras" };
    const juce::Identifier linkProp { "link" };
    const juce::Identifier outputFolderProp { "outputFolder" };

    juce::String defaultOutputFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                   .getChildFile("LinkGrabber").getFullPathName();
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout LinkGrabberAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // format - Choice, non-automatable (selects output format of the NEXT job)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "format", 1 },
        "Format",
        juce::StringArray { "MP3 320", "WAV 24-bit" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));

    return layout;
}

LinkGrabberAudioProcessor::LinkGrabberAudioProcessor()
    : AudioProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , parameters(*this, nullptr, "Parameters", createParameterLayout())
{
    getOrCreateExtras();
}

LinkGrabberAudioProcessor::~LinkGrabberAudioProcessor()
{
}

void LinkGrabberAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void LinkGrabberAudioProcessor::releaseResources()
{
}

bool LinkGrabberAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

void LinkGrabberAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    // Silent passthrough: audio is already in place. Only clear output channels
    // that have no matching input.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* LinkGrabberAudioProcessor::createEditor()
{
    return new LinkGrabberAudioProcessorEditor(*this);
}

juce::ValueTree LinkGrabberAudioProcessor::getOrCreateExtras()
{
    auto extras = parameters.state.getChildWithName(extrasType);
    if (! extras.isValid())
    {
        extras = juce::ValueTree(extrasType);
        extras.setProperty(linkProp, juce::String(), nullptr);
        extras.setProperty(outputFolderProp, defaultOutputFolder(), nullptr);
        parameters.state.appendChild(extras, nullptr);
    }
    return extras;
}

juce::ValueTree LinkGrabberAudioProcessor::getExtrasConst() const
{
    return parameters.state.getChildWithName(extrasType);
}

juce::String LinkGrabberAudioProcessor::getLink() const
{
    return getExtrasConst().getProperty(linkProp).toString();
}

void LinkGrabberAudioProcessor::setLink(const juce::String& newLink)
{
    getOrCreateExtras().setProperty(linkProp, newLink, nullptr);
}

juce::String LinkGrabberAudioProcessor::getOutputFolder() const
{
    auto path = getExtrasConst().getProperty(outputFolderProp).toString();
    if (path.isEmpty() || ! juce::File::isAbsolutePath(path))
        return defaultOutputFolder();
    return path;
}

void LinkGrabberAudioProcessor::setOutputFolder(const juce::String& newFolderPath)
{
    getOrCreateExtras().setProperty(outputFolderProp, newFolderPath, nullptr);
}

void LinkGrabberAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void LinkGrabberAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
        getOrCreateExtras();  // older/partial state: make sure extras exist with defaults
    }
}

// Factory function
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LinkGrabberAudioProcessor();
}
