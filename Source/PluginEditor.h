#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// SPIKE (Phase 2.0): remove/productionise in Phase 3.2
// Temporary native drag source used only to test external file drag-out in each host.
class FileHandle : public juce::Component
{
public:
    FileHandle() = default;

    // Supplies the file to drag (message thread). Returns File() if unavailable.
    std::function<juce::File()> getFileToDrag;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    bool dragStarted = false;  // guard: at most one external drag per gesture

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileHandle)
};

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

    // SPIKE (Phase 2.0): remove/productionise in Phase 3.2
    FileHandle fileHandle;
    juce::TextButton wavButton { "WAV" };
    juce::TextButton mp3Button { "MP3" };
    juce::TextButton revealButton { "Show in Finder" };
    bool spikeUseWav = true;
    juce::String spikeStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkGrabberAudioProcessorEditor)
};
