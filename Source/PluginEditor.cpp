#include "PluginEditor.h"

LinkGrabberAudioProcessorEditor::LinkGrabberAudioProcessorEditor(LinkGrabberAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    // Placeholder shell: real native UI arrives in Stage 3.
    formatBox.addItemList(processorRef.parameters.getParameter("format")->getAllValueStrings(), 1);
    addAndMakeVisible(formatBox);
    formatAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.parameters, "format", formatBox);

    setSize(480, 360);
    setResizable(false, false);
}

LinkGrabberAudioProcessorEditor::~LinkGrabberAudioProcessorEditor()
{
}

void LinkGrabberAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xffc0c0c0));

    g.setColour(juce::Colours::black);
    g.setFont(20.0f);
    g.drawFittedText("LinkGrabber - Stage 1", getLocalBounds().withTrimmedBottom(60),
                     juce::Justification::centred, 1);
}

void LinkGrabberAudioProcessorEditor::resized()
{
    formatBox.setBounds(295, 71, 90, 26);
}
