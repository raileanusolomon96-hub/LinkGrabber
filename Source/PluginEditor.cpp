#include "PluginEditor.h"

namespace
{
    constexpr int kTimerHz = 30;
    constexpr double kWaveMs = 2600.0;
    constexpr double kSweepColMs = 110.0;   // analysing: one column per 110 ms

    juce::String formatLabelFor(const juce::String& fmt, const juce::String& fallback)
    {
        if (fmt.equalsIgnoreCase("wav")) return "WAV 24-bit";
        if (fmt.equalsIgnoreCase("mp3")) return "MP3 320";
        return fallback;
    }

    juce::String formatSize(juce::int64 bytes)
    {
        if (bytes <= 0) return "0 KB";
        const double mb = (double) bytes / (1024.0 * 1024.0);
        return mb >= 1.0 ? juce::String(mb, 1) + " MB" : juce::String(juce::jmax(1, (int) (bytes / 1024))) + " KB";
    }

    // Global (all instances / sessions / projects) rights-notice acknowledgement marker:
    // ~/Library/Application Support/LinkGrabber/notice-accepted-v1 on macOS.
    juce::File noticeMarkerFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("LinkGrabber").getChildFile("notice-accepted-v1");
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
        const auto st = processorRef.getDownloadManager().getStatus();
        if (viewState == DownloadManager::State::done)
            viewResetSerial = st.jobSerial;  // editing the link leaves the result tile
        if (transientText.isNotEmpty())
            transientText.clear();
        applyStatus(st);
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

    historyBar.onClick = [this] { setHistoryExpanded(! historyExpanded); };
    addAndMakeVisible(historyBar);
    historyList.onRemove = [this](const juce::String& id) { removeHistoryRow(id); };
    addChildComponent(historyList);
    addChildComponent(resultTile);

    linkField.onEscapeKey = [this] { handleEscape(); };
    noticePanel.onAccept = [this] { acceptNotice(); };
    addChildComponent(noticePanel);      // last = topmost
    setWantsKeyboardFocus(true);

    // Fixed size: setSize is the only size source, no host-driven resizing.
    setResizable(false, false);
    setSize(480, 360);

    processorRef.getHistoryStore().reload();
    rebuildHistoryRows();

    // Resume from whatever the (processor-owned) manager is doing right now.
    auto& mgr = processorRef.getDownloadManager();
    mgr.addListener(this);
    applyStatus(mgr.getStatus());

    showNotice(! noticeMarkerFile().existsAsFile());

    poller.fn = [this] { pollHistory(); };
    poller.startTimerHz(1);
}

LinkGrabberAudioProcessorEditor::~LinkGrabberAudioProcessorEditor()
{
    stopTimer();
    poller.stopTimer();
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
        analysingStartMs = -1.0;
        if (s.state != State::idle)
            transientText.clear();
    }

    const bool reset = (s.jobSerial != 0 && s.jobSerial == viewResetSerial);
    const State st = reset ? State::idle : s.state;
    viewState = st;
    viewActive = (st == State::lookingUp || st == State::downloading || st == State::converting || st == State::analysing);

    // Percent is monotonic within a job; converting/done hold 100.
    if (reset || st == State::idle || st == State::lookingUp)
        shownPercent = 0;
    else if (st == State::converting || st == State::analysing || st == State::done)
        shownPercent = 100;
    else
        shownPercent = juce::jmax(shownPercent, juce::jlimit(0, 100, s.percent));

    // Result tile: only for a finished job whose file still exists.
    tileShown = false;
    if (st == State::done && s.filePath.isNotEmpty())
    {
        const juce::File f(s.filePath);
        if (f.existsAsFile())
        {
            tileShown = true;
            donePath = s.filePath;
            const auto title = s.title.isNotEmpty() ? s.title : f.getFileNameWithoutExtension();
            const juce::String fmt = s.format.equalsIgnoreCase("wav") || f.hasFileExtension("wav") ? "WAV" : "MP3";
            const auto bytes = s.bytes > 0 ? s.bytes : f.getSize();
            resultTile.setInfo(title, fmt + " | " + formatSize(bytes), s.filePath);
            resultTile.setKeyBpm(s.keyName, s.bpm);
        }
    }

    percentLed.setValue(shownPercent);
    elapsedLed.setValue(reset || st == State::idle ? 0 : (int) s.elapsedSeconds);

    // Smiley
    using Face = lgui::SmileyButton::Face;
    smiley.setFace(st == State::done || st == State::analysing ? Face::cool
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
            const double prog = juce::jmin(1.0, (now - waveStartMs) / kWaveMs);
            board.setView(Mode::converting, shownPercent, (int) std::floor(prog * (lgui::TileBoard::cols + 1)), -1);
            break;
        }
        case State::analysing:
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (analysingStartMs < 0.0) analysingStartMs = now;
            const int col = (int) std::floor((now - analysingStartMs) / kSweepColMs) % (lgui::TileBoard::cols + 1);
            board.setView(Mode::analysing, 100, col, -1);
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
            case State::analysing:   statusText = "Listening for key and tempo..."; break;
            case State::done:
            {
                // "Done. A minor, 128 BPM. Drag ..." / "Done. Key and BPM unknown. Drag ..."
                juce::String found;
                if (s.keyName.isNotEmpty())
                    found = s.keyName.replace(" min", " minor").replace(" maj", " major");
                if (s.bpm > 0)
                    found << (found.isNotEmpty() ? ", " : "") << s.bpm << " BPM";
                const auto lead = found.isNotEmpty() ? "Done. " + found + ". " : juce::String("Done. Key and BPM unknown. ");
                statusText = tileShown ? lead + "Drag the file tile into your DAW."
                                       : "Done. File saved to " + juce::File(s.filePath).getParentDirectory().getFullPathName();
            }
                break;
            case State::error:
                statusText = s.errorMessage.isNotEmpty() ? s.errorMessage : DownloadCommand::userMessage(s.errorKind);
                statusIsError = true;
                break;
        }

    goButton.setButtonText(viewActive ? "Cancel" : "Download");
    historyCount = (int) processorRef.getHistoryStore().getEntries().size();
    historyBar.setButtonText("History (" + juce::String(historyCount) + ")");
    updatePanelVisibility();

    // Timer only while a job is active (also drives the converting sweep and the live elapsed LED).
    if (viewActive)
    {
        if (! isTimerRunning()) startTimerHz(kTimerHz);
    }
    else
        stopTimer();

    repaint();
}

void LinkGrabberAudioProcessorEditor::downloadFinished(const DownloadManager::Status& s)
{
    applyStatus(s);
    // The processor's own listener (which adds the history row) may run after this one. Re-read
    // the history on the next message-loop turn, when every listener has been called.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<LinkGrabberAudioProcessorEditor>(this)]
    {
        if (safe != nullptr)
        {
            safe->rebuildHistoryRows();
            safe->applyStatus(safe->processorRef.getDownloadManager().getStatus());
        }
    });
}

void LinkGrabberAudioProcessorEditor::updatePanelVisibility()
{
    historyBar.setExpanded(historyExpanded);
    historyList.setVisible(historyExpanded);
    resultTile.setVisible(tileShown && ! historyExpanded);
    board.setVisible(! historyExpanded && ! tileShown);
}

void LinkGrabberAudioProcessorEditor::setHistoryExpanded(bool e)
{
    historyExpanded = e;
    if (e)
        rebuildHistoryRows();
    updatePanelVisibility();
    repaint();
}

void LinkGrabberAudioProcessorEditor::rebuildHistoryRows()
{
    const auto& entries = processorRef.getHistoryStore().getEntries();
    std::vector<lgui::HistoryList::Row> rows;
    rows.reserve(entries.size());
    for (const auto& e : entries)
        rows.push_back({ e.id, e.title.isNotEmpty() ? e.title : juce::File(e.path).getFileNameWithoutExtension(),
                         e.path, e.format, HistoryStore::isMissingOnDisk(e), e.analysed, e.key, e.bpm });
    historyList.setRows(std::move(rows));
    historyCount = (int) entries.size();
    historyBar.setButtonText("History (" + juce::String(historyCount) + ")");
}

void LinkGrabberAudioProcessorEditor::pollHistory()
{
    const bool changed = processorRef.getHistoryStore().refreshIfChanged();
    if (changed || historyExpanded)
        rebuildHistoryRows();
    else if ((int) processorRef.getHistoryStore().getEntries().size() != historyCount)
        rebuildHistoryRows();

    // Result tile disappears if its file was moved or deleted.
    if (tileShown && ! juce::File(donePath).existsAsFile())
        applyStatus(processorRef.getDownloadManager().getStatus());
}

void LinkGrabberAudioProcessorEditor::removeHistoryRow(const juce::String& id)
{
    processorRef.getHistoryStore().remove(id);  // the audio file is never touched
    rebuildHistoryRows();
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

void LinkGrabberAudioProcessorEditor::showNotice(bool show)
{
    noticeShowing = show;
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &linkField, &pasteButton, &formatBox, &goButton, &finderButton, &historyBar, &smiley, &historyList })
        c->setEnabled(! show);
    noticePanel.setVisible(show);
    if (show)
    {
        noticePanel.toFront(false);
        noticePanel.grabKeyboardFocus();
    }
    repaint();
}

void LinkGrabberAudioProcessorEditor::acceptNotice()
{
    const auto marker = noticeMarkerFile();
    if (marker.getParentDirectory().createDirectory().wasOk())
        marker.replaceWithText("accepted\n");   // failure ignored: the notice just shows again next time
    showNotice(false);
    grabKeyboardFocus();
}

bool LinkGrabberAudioProcessorEditor::handleEscape()
{
    if (noticeShowing)
        return false;
    if (viewActive)
    {
        cancelAndReset();
        return true;
    }
    if (historyExpanded)
    {
        setHistoryExpanded(false);
        return true;
    }
    return false;
}

bool LinkGrabberAudioProcessorEditor::keyPressed(const juce::KeyPress& k)
{
    // Only reached when focus is inside this editor, so host shortcuts are untouched otherwise.
    if (noticeShowing)
        return false;
    if (k == juce::KeyPress::escapeKey)
        return handleEscape();
    if (k.getModifiers().isCommandDown() && ! k.getModifiers().isAltDown()
        && (k.getKeyCode() == 'V' || k.getKeyCode() == 'v'))
    {
        const auto text = juce::SystemClipboard::getTextFromClipboard().trim();
        if (text.isNotEmpty())
            linkField.setText(text, juce::sendNotificationSync);  // fires onTextChange -> setLink
        return true;
    }
    return false;
}

void LinkGrabberAudioProcessorEditor::startOrCancel()
{
    if (noticeShowing)
        return;
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
    if (historyExpanded)
    {
        const juce::File sel(historyList.getSelectedPath());
        if (sel.existsAsFile())
        {
            sel.revealToUser();
            return;
        }
    }
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
    historyBar.setBounds(15, 289, 340, 22);
    historyList.setBounds(15, 107, 450, 174);
    resultTile.setBounds(30, 119, 420, 150);
    noticePanel.setBounds(getLocalBounds());
}
