#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

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

// SPIKE (Phase 2.0): remove/productionise in Phase 3.2
juce::File LinkGrabberAudioProcessor::getSpikeDummyFile(bool wav)
{
    const juce::File folder(getOutputFolder());
    if (! folder.createDirectory().wasOk())
        return {};

    if (wav)
    {
        const auto file = folder.getChildFile("LinkGrabber drag test.wav");
        if (file.existsAsFile() && file.getSize() > 44)
            return file;

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        if (stream == nullptr)
            return {};

        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(stream,
            juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
                                            .withNumChannels(2)
                                            .withBitsPerSample(24));
        if (writer == nullptr)
            return {};

        constexpr int numSamples = 44100;
        juce::AudioBuffer<float> buffer(2, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto v = 0.25f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 440.0
                                                               * static_cast<double>(i) / 44100.0));
            buffer.setSample(0, i, v);
            buffer.setSample(1, i, v);
        }
        writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
        writer.reset();  // flush and close
        return file;
    }

    const auto file = folder.getChildFile("LinkGrabber drag test.mp3");
    if (file.existsAsFile() && file.getSize() > 417)
        return file;

    // MPEG-1 Layer III, 128 kbps, 44.1 kHz, stereo, no padding, no CRC: frame = 417 bytes.
    // Header FF FB 90 00 followed by an all-zero payload (zero side info/main data = silence).
    // 39 frames * 26.12 ms = ~1.02 s. Placeholder only; NOT produced by an encoder.
    file.deleteFile();
    juce::MemoryBlock data;
    constexpr size_t frameSize = 417;
    constexpr size_t numFrames = 39;
    data.setSize(frameSize * numFrames, true);  // zero-filled
    for (size_t f = 0; f < numFrames; ++f)
    {
        const unsigned char header[4] = { 0xFF, 0xFB, 0x90, 0x00 };
        data.copyFrom(header, static_cast<int>(f * frameSize), 4);
    }
    if (! file.replaceWithData(data.getData(), data.getSize()))
        return {};
    return file;
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
