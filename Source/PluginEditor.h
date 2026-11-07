#pragma once
#include "PluginProcessor.h"
#include "ui/LinkGrabberUI.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Native "Minesweeper" editor (Stage 3, Phase 3.1: layout and board). The editor never owns the
// DownloadManager: it listens (message thread) and renders from DownloadManager::Status via
// applyStatus(). Result tile / history list / drag-out arrive in Phase 3.2.
class LinkGrabberAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private DownloadManager::Listener,
                                        private juce::Timer
{
public:
    explicit LinkGrabberAudioProcessorEditor(LinkGrabberAudioProcessor&);
    ~LinkGrabberAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // DownloadManager::Listener (message thread)
    void downloadStatusChanged(const DownloadManager::Status& s) override { applyStatus(s); }
    void downloadFinished(const DownloadManager::Status& s) override { applyStatus(s); }

    void timerCallback() override;

    // Single view-model update used by the real listener, the timer and the snapshot code.
    void applyStatus(const DownloadManager::Status&);
    void startOrCancel();
    void cancelAndReset();
    void showInFinder();
    void setTransientMessage(const juce::String& text, bool isError);

    LinkGrabberAudioProcessor& processorRef;

    lgui::LinkGrabberLookAndFeel laf;  // declared first: destroyed last

    lgui::LedCounter percentLed, elapsedLed;
    lgui::SmileyButton smiley;
    juce::TextEditor linkField;
    lgui::BevelButton pasteButton { "Paste" };
    juce::ComboBox formatBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> formatAttachment;
    lgui::BevelButton goButton { "Download", true };
    lgui::TileBoard board;
    lgui::BevelButton finderButton { "Show in Finder" };

    // View model
    DownloadManager::State viewState = DownloadManager::State::idle;
    juce::uint32 lastSerial = 0;
    juce::uint32 viewResetSerial = 0;   // statuses of this job are displayed as idle (smiley / cancel)
    int shownPercent = 0;               // monotonic within a job
    double waveStartMs = -1.0;
    juce::String statusText;
    bool statusIsError = false;
    juce::String transientText;         // local message (empty link, still cancelling)
    bool transientIsError = false;
    bool viewActive = false;
    int historyCount = 0;

    // TEMP SNAPSHOT (Phase 3.1): remove after phase
    void runSnapshots();                                          // TEMP SNAPSHOT (Phase 3.1): remove after phase
    bool snapshotMode = false;                                    // TEMP SNAPSHOT (Phase 3.1): remove after phase
    double waveProgressOverride = -1.0;                           // TEMP SNAPSHOT (Phase 3.1): remove after phase

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkGrabberAudioProcessorEditor)
};
