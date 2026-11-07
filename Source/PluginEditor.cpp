#include "PluginEditor.h"

namespace
{
    constexpr int kTimerHz = 30;
    constexpr double kWaveMs = 2600.0;

    juce::String formatLabelFor(const juce::String& fmt, const juce::String& fallback)
    {
        if (fmt.equalsIgnoreCase("wav")) return "WAV 24-bit";
        if (fmt.equalsIgnoreCase("mp3")) return "MP3 320";
        return fallback;
    }
}

//==============================================================================
LinkGrabberAudioProcessorEditor::LinkGrabberAudioProcessorEditor(LinkGrabberAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&laf);

    addAndMakeVisible(percentLed);
    addAndMakeVisible(elapsedLed);

    smiley.onClick = [this] { cancelAndReset(); };
    addAndMakeVisible(smiley);

    linkField.setMultiLine(false);
    linkField.setReturnKeyStartsNewLine(false);
    linkField.setScrollbarsShown(false);
    linkField.setFont(lgui::makeFont(12.0f));
    linkField.setIndents(3, 0);
    linkField.setBorder({});
    linkField.setTextToShowWhenEmpty("Paste YouTube link", juce::Colour(0xff808080));
    linkField.setSelectAllWhenFocused(false);
    linkField.setText(processorRef.getLink(), juce::dontSendNotification);
    linkField.onTextChange = [this]
    {
        processorRef.setLink(linkField.getText());
        if (transientText.isNotEmpty())
        {
            transientText.clear();
            applyStatus(processorRef.getDownloadManager().getStatus());
        }
    };
    linkField.onReturnKey = [this] { startOrCancel(); };
    addAndMakeVisible(linkField);

    pasteButton.onClick = [this]
    {
        const auto text = juce::SystemClipboard::getTextFromClipboard().trim();
        if (text.isNotEmpty())
            linkField.setText(text, juce::sendNotificationSync);  // fires onTextChange -> setLink
    };
    addAndMakeVisible(pasteButton);

    formatBox.addItemList(processorRef.parameters.getParameter("format")->getAllValueStrings(), 1);
    addAndMakeVisible(formatBox);
    formatAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.parameters, "format", formatBox);

    goButton.onClick = [this] { startOrCancel(); };
    addAndMakeVisible(goButton);

    addAndMakeVisible(board);

    finderButton.onClick = [this] { showInFinder(); };
    addAndMakeVisible(finderButton);

    setSize(480, 360);
    setResizable(false, false);

    processorRef.getHistoryStore().reload();
    historyCount = (int) processorRef.getHistoryStore().getEntries().size();

    // Resume from whatever the (processor-owned) manager is doing right now.
    auto& mgr = processorRef.getDownloadManager();
    mgr.addListener(this);
    applyStatus(mgr.getStatus());

    // TEMP SNAPSHOT (Phase 3.1): remove after phase
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone
        && juce::SystemStats::getEnvironmentVariable("LINKGRABBER_SNAPSHOT_DIR", {}).isNotEmpty())  // TEMP SNAPSHOT (Phase 3.1): remove after phase
    {
        snapshotMode = true;  // TEMP SNAPSHOT (Phase 3.1): remove after phase
        juce::Timer::callAfterDelay(1500, [safe = juce::Component::SafePointer<juce::Component>(this)]  // TEMP SNAPSHOT (Phase 3.1): remove after phase
        {
            if (auto* self = dynamic_cast<LinkGrabberAudioProcessorEditor*>(safe.getComponent()))  // TEMP SNAPSHOT (Phase 3.1): remove after phase
                self->runSnapshots();  // TEMP SNAPSHOT (Phase 3.1): remove after phase
        });
    }
}

LinkGrabberAudioProcessorEditor::~LinkGrabberAudioProcessorEditor()
{
    stopTimer();
    processorRef.getDownloadManager().removeListener(this);  // the job keeps running without us
    formatAttachment.reset();
    setLookAndFeel(nullptr);
}

//==============================================================================
void LinkGrabberAudioProcessorEditor::applyStatus(const DownloadManager::Status& s)
{
    JUCE_ASSERT_MESSAGE_THREAD
    using State = DownloadManager::State;

    if (s.jobSerial != lastSerial)
    {
        lastSerial = s.jobSerial;
        shownPercent = 0;
        waveStartMs = -1.0;
        if (s.state != State::idle)
            transientText.clear();
    }

    const bool reset = (s.jobSerial != 0 && s.jobSerial == viewResetSerial);
    const State st = reset ? State::idle : s.state;
    viewState = st;
    viewActive = (st == State::lookingUp || st == State::downloading || st == State::converting);

    // Percent is monotonic within a job; converting/done hold 100.
    if (reset || st == State::idle || st == State::lookingUp)
        shownPercent = 0;
    else if (st == State::converting || st == State::done)
        shownPercent = 100;
    else
        shownPercent = juce::jmax(shownPercent, juce::jlimit(0, 100, s.percent));

    percentLed.setValue(shownPercent);
    elapsedLed.setValue(reset || st == State::idle ? 0 : (int) s.elapsedSeconds);

    // Smiley
    using Face = lgui::SmileyButton::Face;
    smiley.setFace(st == State::done ? Face::cool
                   : st == State::error ? Face::dead
                   : viewActive && st != State::idle ? Face::oh : Face::smile);

    // Board
    using Mode = lgui::TileBoard::Mode;
    switch (st)
    {
        case State::idle:
        case State::lookingUp:   board.setView(Mode::hidden, 0, -1, -1); break;
        case State::downloading: board.setView(Mode::downloading, shownPercent, -1, -1); break;
        case State::converting:
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (waveStartMs < 0.0) waveStartMs = now;
            const double prog = waveProgressOverride >= 0.0 ? waveProgressOverride  // TEMP SNAPSHOT (Phase 3.1): remove after phase
                                                            : juce::jmin(1.0, (now - waveStartMs) / kWaveMs);
            board.setView(Mode::converting, shownPercent, (int) std::floor(prog * (lgui::TileBoard::cols + 1)), -1);
            break;
        }
        case State::done:        board.setView(Mode::done, 100, -1, -1); break;
        case State::error:       board.setView(Mode::downloading, shownPercent, -1, board.mineTileFor(shownPercent)); break;
    }

    // Status line
    statusIsError = false;
    if (transientText.isNotEmpty() && st == State::idle)
    {
        statusText = transientText;
        statusIsError = transientIsError;
    }
    else
        switch (st)
        {
            case State::idle:
                statusText = (! reset && s.errorMessage.isNotEmpty()) ? s.errorMessage
                                                                      : juce::String("Paste a YouTube link, pick a format, then press Download.");
                break;
            case State::lookingUp:   statusText = "Looking up video..."; break;
            case State::downloading: statusText = "Downloading... " + juce::String(shownPercent) + "%"; break;
            case State::converting:  statusText = "Converting to " + formatLabelFor(s.format, formatBox.getText()) + "..."; break;
            case State::done:
                statusText = "Done. File saved to " + juce::File(s.filePath).getParentDirectory().getFullPathName();
                break;
            case State::error:
                statusText = s.errorMessage.isNotEmpty() ? s.errorMessage : DownloadCommand::userMessage(s.errorKind);
                statusIsError = true;
                break;
        }

    goButton.setButtonText(viewActive ? "Cancel" : "Download");
    historyCount = (int) processorRef.getHistoryStore().getEntries().size();

    // Timer only while a job is active (also drives the converting sweep and the live elapsed LED).
    if (viewActive && ! snapshotMode)
    {
        if (! isTimerRunning()) startTimerHz(kTimerHz);
    }
    else
        stopTimer();

    repaint();
}

void LinkGrabberAudioProcessorEditor::timerCallback()
{
    applyStatus(processorRef.getDownloadManager().getStatus());
}

void LinkGrabberAudioProcessorEditor::setTransientMessage(const juce::String& text, bool isError)
{
    transientText = text;
    transientIsError = isError;
    applyStatus(processorRef.getDownloadManager().getStatus());
}

void LinkGrabberAudioProcessorEditor::cancelAndReset()
{
    auto& mgr = processorRef.getDownloadManager();
    const auto s = mgr.getStatus();
    if (s.isActive())
        mgr.cancel();
    viewResetSerial = s.jobSerial;
    transientText.clear();
    applyStatus(s);
}

void LinkGrabberAudioProcessorEditor::startOrCancel()
{
    if (viewActive)
    {
        cancelAndReset();
        return;
    }

    if (linkField.getText().trim().isEmpty())
    {
        setTransientMessage("Paste a YouTube link first.", true);
        return;
    }

    processorRef.setLink(linkField.getText());
    auto& mgr = processorRef.getDownloadManager();
    const bool started = processorRef.startDownload();

    if (! started && mgr.isBusy())
    {
        setTransientMessage("Still cancelling the previous download. Try again in a moment.", false);
        return;
    }

    viewResetSerial = 0;  // show whatever the new job (or the immediate link error) publishes
    transientText.clear();
    applyStatus(mgr.getStatus());
}

void LinkGrabberAudioProcessorEditor::showInFinder()
{
    const auto& entries = processorRef.getHistoryStore().getEntries();
    if (! entries.empty())
    {
        const juce::File f(entries.front().path);
        if (f.existsAsFile())
        {
            f.revealToUser();
            return;
        }
    }
    const juce::File folder(processorRef.getOutputFolder());
    if (folder.isDirectory())
        folder.revealToUser();
    else
        setTransientMessage("Nothing to show yet.", false);
}

//==============================================================================
void LinkGrabberAudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace lgui;

    g.fillAll(kFace);
    bevelRect(g, getLocalBounds(), 2, kLight, kDark);  // outer window bevel

    drawSunken(g, { 15, 15, 450, 48 }, 2, kFace);     // top bar
    drawSunken(g, { 15, 71, 200, 26 }, 2, kLight);    // link field frame (TextEditor sits inside)
    drawSunken(g, { 15, 107, 450, 174 }, 3, kFace);   // board frame

    // History header (expander + list are Phase 3.2): non-functional label for now
    drawRaisedButton(g, { 15, 289, 340, 22 }, false);
    {
        juce::Path arrow;
        arrow.addTriangle(24.0f, 295.0f, 24.0f, 305.0f, 29.0f, 300.0f);  // right-pointing (collapsed)
        g.setColour(kBlack);
        g.fillPath(arrow);
        g.setFont(makeFont(12.0f));
        g.drawText("History (" + juce::String(historyCount) + ")", 36, 289, 300, 22, juce::Justification::centredLeft, false);
    }

    // Status bar
    drawSunken(g, { 15, 321, 450, 24 }, 1, kFace);
    g.setColour(statusIsError ? kErrorText : kBlack);
    g.setFont(makeFont(12.0f));
    g.drawText(statusText, 15 + 1 + 6, 321, 450 - 2 - 12, 24, juce::Justification::centredLeft, true);
}

void LinkGrabberAudioProcessorEditor::resized()
{
    percentLed.setBounds(22, 22, 53, 34);
    smiley.setBounds(223, 22, 34, 34);
    elapsedLed.setBounds(405, 22, 53, 34);

    linkField.setBounds(17, 73, 196, 22);
    pasteButton.setBounds(225, 71, 60, 26);
    formatBox.setBounds(295, 71, 90, 26);
    goButton.setBounds(395, 71, 70, 26);

    board.setBounds(24, 113, 432, 162);
    finderButton.setBounds(365, 289, 100, 22);
}

//==============================================================================
// TEMP SNAPSHOT (Phase 3.1): remove after phase (whole function)
void LinkGrabberAudioProcessorEditor::runSnapshots()
{
    using State = DownloadManager::State;
    const juce::File dir(juce::SystemStats::getEnvironmentVariable("LINKGRABBER_SNAPSHOT_DIR", {}));
    dir.createDirectory();

    struct Case { const char* name; DownloadManager::Status st; double wave; bool link; };
    juce::uint32 serial = 1000;
    auto mk = [&serial](State s, int pct, double el)
    {
        DownloadManager::Status st;
        st.state = s; st.percent = pct; st.elapsedSeconds = el; st.jobSerial = ++serial; st.format = "mp3";
        return st;
    };

    std::vector<Case> cases;
    cases.push_back({ "idle", mk(State::idle, 0, 0), -1.0, false });
    cases.push_back({ "lookingup", mk(State::lookingUp, 0, 1), -1.0, true });
    cases.push_back({ "downloading-0", mk(State::downloading, 0, 2), -1.0, true });
    cases.push_back({ "downloading-40", mk(State::downloading, 40, 4), -1.0, true });
    cases.push_back({ "downloading-72", mk(State::downloading, 72, 7), -1.0, true });
    cases.push_back({ "converting", mk(State::converting, 100, 10), 0.5, true });
    {
        auto st = mk(State::done, 100, 12);
        st.filePath = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                          .getChildFile("LinkGrabber").getChildFile("Example Song.mp3").getFullPathName();
        cases.push_back({ "done", st, -1.0, true });
    }
    {
        auto st = mk(State::error, 35, 5);
        st.errorKind = DownloadCommand::FailureKind::unavailable;
        st.errorMessage = DownloadCommand::userMessage(st.errorKind);
        cases.push_back({ "error-unavailable", st, -1.0, true });
        auto st2 = mk(State::error, 0, 0);
        st2.errorKind = DownloadCommand::FailureKind::toolMissing;
        st2.errorMessage = DownloadCommand::userMessage(st2.errorKind);
        cases.push_back({ "error-toolmissing", st2, -1.0, true });
    }
    cases.push_back({ "link-filled", mk(State::idle, 0, 0), -1.0, true });

    const juce::String link = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
    for (double scale : { 2.0, 1.0 })
    {
        for (auto& c : cases)
        {
            linkField.setText(std::string_view(c.name) == "idle" ? juce::String() : link, juce::dontSendNotification);
            waveProgressOverride = c.wave;
            lastSerial = 0;
            applyStatus(c.st);
            auto img = createComponentSnapshot(getLocalBounds(), true, (float) scale);
            const auto file = dir.getChildFile(juce::String("snap-") + c.name + (scale == 2.0 ? "" : "-1x") + ".png");
            if (auto out = file.createOutputStream())
            {
                out->setPosition(0);
                out->truncate();
                juce::PNGImageFormat().writeImageToStream(img, *out);
            }
        }
    }
    juce::JUCEApplicationBase::quit();
}
