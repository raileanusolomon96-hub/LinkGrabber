#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include "HistoryStore.h"
#include "DownloadManager.h"


class LinkGrabberAudioProcessor : public juce::AudioProcessor,
                                  private DownloadManager::Listener
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

    // Global download history (message thread only; never touch from processBlock).
    HistoryStore& getHistoryStore() { return historyStore; }

    // Processor-owned download engine (survives the editor closing). Message thread for
    // start/cancel/listeners; getStatus() is thread-safe.
    DownloadManager& getDownloadManager() { return downloadManager; }

    // Message thread. Reads the `format` parameter (0 = MP3 320, 1 = WAV 24-bit), the stored
    // link and output folder, and starts a job. Returns false if busy or the link is invalid.
    bool startDownload();

    juce::AudioProcessorValueTreeState parameters;

private:
    // DownloadManager::Listener (message thread): adds finished downloads to the history.
    void downloadFinished(const DownloadManager::Status& finished) override;

    HistoryStore historyStore;  // default directory; no disk access until reload()/add()
    DownloadManager downloadManager;  // declared after historyStore: destroyed first (joins its worker)

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::ValueTree getOrCreateExtras();
    juce::ValueTree getExtrasConst() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkGrabberAudioProcessor)
};
