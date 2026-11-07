#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class LinkGrabberAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit LinkGrabberAudioProcessorEditor(LinkGrabberAudioProcessor&);
    ~LinkGrabberAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    LinkGrabberAudioProcessor& processorRef;

    juce::ComboBox formatBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> formatAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkGrabberAudioProcessorEditor)
};
