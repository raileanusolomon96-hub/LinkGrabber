#include "PluginEditor.h"

//==============================================================================
// SPIKE (Phase 2.0): remove/productionise in Phase 3.2
void FileHandle::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xffc0c0c0));
    g.fillRect(r);

    // Raised bevel
    g.setColour(juce::Colours::white);
    g.drawLine(r.getX(), r.getY(), r.getRight(), r.getY(), 2.0f);
    g.drawLine(r.getX(), r.getY(), r.getX(), r.getBottom(), 2.0f);
    g.setColour(juce::Colour(0xff808080));
    g.drawLine(r.getX(), r.getBottom(), r.getRight(), r.getBottom(), 2.0f);
    g.drawLine(r.getRight(), r.getY(), r.getRight(), r.getBottom(), 2.0f);

    g.setColour(juce::Colours::black);
    g.setFont(16.0f);
    g.drawFittedText("Drag test file", getLocalBounds(), juce::Justification::centred, 1);
}

void FileHandle::mouseDown(const juce::MouseEvent&)
{
    dragStarted = false;  // new gesture
}

void FileHandle::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 4)
        return;

    if (! getFileToDrag)
        return;

    const auto file = getFileToDrag();
    if (! file.existsAsFile())
        return;

    dragStarted = true;  // set BEFORE the call so nothing can start a second drag

    // Must be called synchronously from the live mouseDrag event (macOS uses the
    // window's currentEvent). Static function: no DragAndDropContainer is required
    // for external file drags. canMoveFiles=false => receiver copies.
    juce::Component::SafePointer<FileHandle> safeThis(this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles(
        juce::StringArray { file.getFullPathName() }, false, this,
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->dragStarted = false;
        });
}

void FileHandle::mouseUp(const juce::MouseEvent&)
{
    dragStarted = false;
}

//==============================================================================
LinkGrabberAudioProcessorEditor::LinkGrabberAudioProcessorEditor(LinkGrabberAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    // Placeholder shell: real native UI arrives in Stage 3.
    formatBox.addItemList(processorRef.parameters.getParameter("format")->getAllValueStrings(), 1);
    addAndMakeVisible(formatBox);
    formatAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.parameters, "format", formatBox);

    // SPIKE (Phase 2.0): remove/productionise in Phase 3.2
    fileHandle.getFileToDrag = [this]
    {
        const auto f = processorRef.getSpikeDummyFile(spikeUseWav);
        spikeStatus = f.existsAsFile() ? f.getFileName() : juce::String("Could not create dummy file");
        repaint();
        return f;
    };
    addAndMakeVisible(fileHandle);
    // Pre-generate both dummies now so mouseDrag does no file creation on first drag.
    processorRef.getSpikeDummyFile(true);
    processorRef.getSpikeDummyFile(false);

    auto setupToggle = [this](juce::TextButton& b, bool wav)
    {
        b.setClickingTogglesState(true);
        b.setRadioGroupId(1001);
        b.setToggleState(wav == spikeUseWav, juce::dontSendNotification);
        b.onClick = [this, wav] { spikeUseWav = wav; };
        addAndMakeVisible(b);
    };
    setupToggle(wavButton, true);
    setupToggle(mp3Button, false);

    revealButton.onClick = [this]
    {
        const auto f = processorRef.getSpikeDummyFile(spikeUseWav);
        if (f.existsAsFile())
            f.revealToUser();
    };
    addAndMakeVisible(revealButton);

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
    g.drawFittedText("LinkGrabber - Stage 2.0 spike", getLocalBounds().withTrimmedBottom(60),
                     juce::Justification::centred, 1);

    // SPIKE (Phase 2.0): remove/productionise in Phase 3.2
    g.setFont(12.0f);
    g.drawText(spikeStatus, 30, 330, 420, 20, juce::Justification::centredLeft);
}

void LinkGrabberAudioProcessorEditor::resized()
{
    formatBox.setBounds(295, 71, 90, 26);

    // SPIKE (Phase 2.0): remove/productionise in Phase 3.2
    fileHandle.setBounds(30, 200, 200, 60);
    wavButton.setBounds(250, 200, 60, 26);
    mp3Button.setBounds(315, 200, 60, 26);
    revealButton.setBounds(250, 234, 125, 26);
}
