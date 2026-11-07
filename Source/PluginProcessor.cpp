#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include "StateSanitiser.h"

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
    , historyStore()
{
    getOrCreateExtras();

    downloadManager.addListener(this);
}

LinkGrabberAudioProcessor::~LinkGrabberAudioProcessor()
{
    downloadManager.removeListener(this);
}

bool LinkGrabberAudioProcessor::startDownload()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // `format` is read here, on the message thread, at start time. Changing it later does not
    // affect the running job (the manager copies it).
    int formatIndex = 0;
    if (auto* raw = parameters.getRawParameterValue("format"))
        formatIndex = juce::roundToInt(raw->load());

    return downloadManager.start(LinkGrabberState::sanitiseLink(getLink()), formatIndex, getOutputFolder());
}

void LinkGrabberAudioProcessor::downloadFinished(const DownloadManager::Status& finished)
{
    // Message thread (AsyncUpdater). The worker never touches the history.
    if (finished.state != DownloadManager::State::done)
        return;

    const juce::File file(finished.filePath);
    if (! file.existsAsFile())
        return;

    HistoryEntry entry;
    entry.id = finished.videoId.isNotEmpty()
                   ? finished.videoId
                   : "lg-" + juce::String::toHexString(juce::Time::currentTimeMillis());
    entry.title = finished.title.isNotEmpty() ? finished.title : file.getFileNameWithoutExtension();
    entry.path = file.getFullPathName();
    entry.format = finished.format;
    entry.timestamp = juce::Time::getCurrentTime().toISO8601(true);
    entry.bytes = file.getSize();
    historyStore.add(entry);
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
        auto extras = getOrCreateExtras();  // older/partial state: make sure extras exist with defaults

        // A restored session is untrusted input: sanitise before anything uses it.
        extras.setProperty(outputFolderProp,
                           LinkGrabberState::sanitiseOutputFolder(extras.getProperty(outputFolderProp).toString(),
                                                                  defaultOutputFolder()),
                           nullptr);
        extras.setProperty(linkProp,
                           LinkGrabberState::sanitiseLink(extras.getProperty(linkProp).toString()),
                           nullptr);
    }
}

// Factory function
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LinkGrabberAudioProcessor();
}
