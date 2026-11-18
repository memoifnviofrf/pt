#include "PluginEditor.h"

namespace { const juce::Colour kBar(0xff1b1f27), kAccent(0xfff0a030); }

VocalTuneEditor::VocalTuneEditor(VocalTuneProcessor& p) : juce::AudioProcessorEditor(&p), processor(p), view(p)
{
    laf.setColourScheme(juce::LookAndFeel_V4::getMidnightColourScheme());
    laf.setColour(juce::Slider::thumbColourId, kAccent);
    setLookAndFeel(&laf);

    addAndMakeVisible(view);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold))); titleLabel.setColour(juce::Label::textColourId, kAccent);
    statusLabel.setJustificationType(juce::Justification::centredRight); statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &statusLabel, &openBtn, &openProjBtn, &saveBtn, &exportBtn, &undoBtn, &redoBtn,
                                                           &captureBtn, &settingsBtn, &presetBox, &abBtn, &scaleLbl, &rootLbl, &pitchHdr, &vibHdr, &fmtHdr,
                                                           &scaleBox, &rootBox, &playBtn, &stopBtn, &loopBtn, &pitchTool, &timeTool, &splitTool, &mergeTool,
                                                           &snapBtn, &quantizeBtn, &hPlus, &hMinus, &vPlus, &vMinus, &fitBtn, &timeLabel })
        addAndMakeVisible(c);
    for (auto* h : { &pitchHdr, &vibHdr, &fmtHdr }) { h->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold))); h->setColour(juce::Label::textColourId, kAccent); }

    // パラメーター
    addKnob(knobs[0], "pitchCenter", "Pitch Center");   addKnob(knobs[1], "pitchDrift", "Pitch Drift");
    addKnob(knobs[2], "pitchMod", "Pitch Modulation");  addKnob(knobs[3], "noteTransition", "Note Transition");
    addKnob(knobs[4], "formantShift", "Formant Shift");
    addKnob(knobs[5], "vibDepth", "Depth");  addKnob(knobs[6], "vibRate", "Rate");  addKnob(knobs[7], "vibOnset", "Onset");
    addKnob(knobs[8], "vibAmount", "Amount"); addKnob(knobs[9], "vibSmooth", "Smoothing");
    if (auto* sc = dynamic_cast<juce::AudioParameterChoice*>(processor.apvts.getParameter("scaleType"))) scaleBox.addItemList(sc->choices, 1);
    if (auto* rc = dynamic_cast<juce::AudioParameterChoice*>(processor.apvts.getParameter("rootNote"))) rootBox.addItemList(rc->choices, 1);
    scaleAtt = std::make_unique<ComboAtt>(processor.apvts, "scaleType", scaleBox);
    rootAtt = std::make_unique<ComboAtt>(processor.apvts, "rootNote", rootBox);
    abAtt = std::make_unique<ButtonAtt>(processor.apvts, "abEdited", abBtn);

    // プリセット
    presetBox.setTextWhenNothingSelected("Preset");
    { int id = 1; for (auto& pr : vts::presets()) presetBox.addItem(pr.name, id++); }
    presetBox.onChange = [this] { processor.applyPreset(presetBox.getSelectedItemIndex()); };

    // ボタン
    openBtn.onClick = [this] { chooseAudio(); };          openProjBtn.onClick = [this] { chooseOpenProject(); };
    saveBtn.onClick = [this] { chooseSaveProject(); };    exportBtn.onClick = [this] { showExportMenu(); };
    undoBtn.onClick = [this] { doUndo(); };               redoBtn.onClick = [this] { doRedo(); };
    settingsBtn.onClick = [this] { showSettings(); };
    captureBtn.setClickingTogglesState(false);
    captureBtn.onClick = [this] { if (processor.isCapturing()) processor.stopCapture(); else processor.startCapture(); };
    playBtn.onClick = [this] { togglePlay(); };
    stopBtn.onClick = [this] { if (! processor.isInternalPlaying()) processor.setInternalPositionSec(0.0); processor.setInternalPlaying(false); };
    loopBtn.setClickingTogglesState(true);
    loopBtn.onClick = [this] { const double total = processor.getSession().hasAudio() ? (double) processor.getSession().original[0].size() / processor.getSession().sr : 0.0;
                               processor.setLoop(loopBtn.getToggleState(), 0.0, total); };
    for (auto* t : { &pitchTool, &timeTool, &splitTool, &mergeTool }) { t->setClickingTogglesState(true); t->setRadioGroupId(101); }
    pitchTool.setToggleState(true, juce::dontSendNotification);
    pitchTool.onClick = [this] { selectTool(PitchEditorView::Tool::Pitch); };  timeTool.onClick = [this] { selectTool(PitchEditorView::Tool::Time); };
    splitTool.onClick = [this] { selectTool(PitchEditorView::Tool::Split); };  mergeTool.onClick = [this] { selectTool(PitchEditorView::Tool::Merge); };
    snapBtn.setClickingTogglesState(true); snapBtn.setToggleState(true, juce::dontSendNotification);
    snapBtn.onClick = [this] { view.setSnap(snapBtn.getToggleState()); };
    quantizeBtn.onClick = [this] { showQuantizeMenu(); };
    hPlus.onClick = [this] { view.zoomTime(1.4); };  hMinus.onClick = [this] { view.zoomTime(1.0 / 1.4); };
    vPlus.onClick = [this] { view.zoomPitch(1.25); }; vMinus.onClick = [this] { view.zoomPitch(0.8); };
    fitBtn.onClick = [this] { view.fitAll(); };
    timeLabel.setJustificationType(juce::Justification::centredLeft);

    processor.addChangeListener(this);
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(960, 560, 3400, 2000);
    setSize(1200, 720);
    startTimerHz(10);
    changeListenerCallback(nullptr);
}

VocalTuneEditor::~VocalTuneEditor() { processor.removeChangeListener(this); stopTimer(); setLookAndFeel(nullptr); }

void VocalTuneEditor::addKnob(Knob& k, const juce::String& id, const juce::String& name)
{
    k.label.setText(name, juce::dontSendNotification); k.label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(k.label); addAndMakeVisible(k.slider);
    k.att = std::make_unique<SliderAtt>(processor.apvts, id, k.slider);
}

void VocalTuneEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff14171d));
    g.setColour(kBar);
    g.fillRect(0, 0, getWidth(), 44);
    g.fillRect(0, getHeight() - 48, getWidth(), 48);
    g.fillRect(getWidth() - 280, 44, 280, getHeight() - 92);
}

void VocalTuneEditor::resized()
{
    auto r = getLocalBounds();
    // 上部
    auto top = r.removeFromTop(44).reduced(6, 6);
    titleLabel.setBounds(top.removeFromLeft(190));
    for (auto* b : { &openBtn, &openProjBtn, &saveBtn, &exportBtn }) { b->setBounds(top.removeFromLeft(b == &openBtn || b == &exportBtn ? 96 : 56).reduced(1)); }
    top.removeFromLeft(10);
    undoBtn.setBounds(top.removeFromLeft(54).reduced(1)); redoBtn.setBounds(top.removeFromLeft(54).reduced(1));
    top.removeFromLeft(10);
    presetBox.setBounds(top.removeFromLeft(190).reduced(1));
    captureBtn.setBounds(top.removeFromLeft(80).reduced(1)); settingsBtn.setBounds(top.removeFromLeft(76).reduced(1));
    statusLabel.setBounds(top);
    // 下部
    auto bottom = r.removeFromBottom(48).reduced(6, 8);
    for (auto* b : { &playBtn, &stopBtn, &loopBtn }) b->setBounds(bottom.removeFromLeft(60).reduced(1));
    bottom.removeFromLeft(8); timeLabel.setBounds(bottom.removeFromLeft(110)); bottom.removeFromLeft(8);
    for (auto* b : { &pitchTool, &timeTool, &splitTool, &mergeTool }) b->setBounds(bottom.removeFromLeft(62).reduced(1));
    bottom.removeFromLeft(8); snapBtn.setBounds(bottom.removeFromLeft(56).reduced(1)); quantizeBtn.setBounds(bottom.removeFromLeft(80).reduced(1));
    bottom.removeFromLeft(8);
    for (auto* b : { &hMinus, &hPlus, &vMinus, &vPlus, &fitBtn }) b->setBounds(bottom.removeFromLeft(46).reduced(1));
    // 右パネル
    auto panel = r.removeFromRight(280).reduced(10, 8);
    auto row = [&](juce::Component& label, juce::Component& comp) { auto rr = panel.removeFromTop(26); label.setBounds(rr.removeFromLeft(112)); comp.setBounds(rr); panel.removeFromTop(2); };
    pitchHdr.setBounds(panel.removeFromTop(20));
    row(scaleLbl, scaleBox); row(rootLbl, rootBox);
    for (int i = 0; i < 4; ++i) row(knobs[(size_t) i].label, knobs[(size_t) i].slider);
    panel.removeFromTop(6); fmtHdr.setBounds(panel.removeFromTop(20)); row(knobs[4].label, knobs[4].slider);
    panel.removeFromTop(6); vibHdr.setBounds(panel.removeFromTop(20));
    for (int i = 5; i < 10; ++i) row(knobs[(size_t) i].label, knobs[(size_t) i].slider);
    panel.removeFromTop(10); abBtn.setBounds(panel.removeFromTop(28));
    view.setBounds(r);
}

void VocalTuneEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    statusLabel.setText(processor.getStatus(), juce::dontSendNotification);
    view.repaint();
}

void VocalTuneEditor::timerCallback()
{
    const bool busy = processor.isBusy();
    for (auto* b : { &openBtn, &openProjBtn, &exportBtn, &undoBtn, &redoBtn, &settingsBtn }) b->setEnabled(! busy);
    captureBtn.setButtonText(processor.isCapturing() ? "Stop Capture" : "Capture");
    captureBtn.setColour(juce::TextButton::buttonColourId, processor.isCapturing() ? juce::Colour(0xffb03030) : laf.findColour(juce::TextButton::buttonColourId));
    playBtn.setToggleState(processor.isInternalPlaying(), juce::dontSendNotification);
    const double t = processor.getPlayPositionSec();
    timeLabel.setText(juce::String::formatted("%d:%06.3f", (int) (t / 60), std::fmod(t, 60.0)), juce::dontSendNotification);
    statusLabel.setText(processor.getStatus(), juce::dontSendNotification);
}

bool VocalTuneEditor::keyPressed(const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (m.isCommandDown() && k.getKeyCode() == 'Z') { if (m.isShiftDown()) doRedo(); else doUndo(); return true; }
    if (m.isCommandDown() && k.getKeyCode() == 'Y') { doRedo(); return true; }
    if (k.getKeyCode() == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey) { view.deleteSelectionTargets(); return true; }
    return false;
}

void VocalTuneEditor::selectTool(PitchEditorView::Tool t) { view.setTool(t); }
void VocalTuneEditor::doUndo() { processor.tryEdit([](vts::EditSession& s) { s.editor.undo(); }); }
void VocalTuneEditor::doRedo() { processor.tryEdit([](vts::EditSession& s) { s.editor.redo(); }); }
void VocalTuneEditor::togglePlay() { processor.setInternalPlaying(! processor.isInternalPlaying()); }

void VocalTuneEditor::chooseAudio()
{
    chooser = std::make_unique<juce::FileChooser>("Select an audio file", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.existsAsFile()) processor.loadAudioFile(f); });
}

void VocalTuneEditor::chooseOpenProject()
{
    chooser = std::make_unique<juce::FileChooser>("Open project", juce::File(), "*.vtsproj");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.existsAsFile()) processor.loadProject(f); });
}

void VocalTuneEditor::chooseSaveProject()
{
    chooser = std::make_unique<juce::FileChooser>("Save project", juce::File(), "*.vtsproj");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) processor.saveProject(f.withFileExtension("vtsproj")); });
}

void VocalTuneEditor::showExportMenu()
{
    juce::PopupMenu m; m.addItem(1, "16-bit PCM"); m.addItem(2, "24-bit PCM"); m.addItem(3, "32-bit float");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportBtn), [this](int r) { if (r > 0) chooseExport(r == 1 ? 16 : r == 2 ? 24 : 32); });
}

void VocalTuneEditor::chooseExport(int bits)
{
    exportBits = bits;
    chooser = std::make_unique<juce::FileChooser>("Export edited audio", juce::File(), "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) processor.exportWav(f.withFileExtension("wav"), exportBits); });
}

void VocalTuneEditor::showSettings()
{
    auto* w = new juce::AlertWindow("Analysis settings", "Vocal range used for pitch detection (Hz). Changing it re-analyses the audio and discards manual note edits.",
                                    juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("min", juce::String(processor.getSession().minHz, 0), "Lowest note (Hz)");
    w->addTextEditor("max", juce::String(processor.getSession().maxHz, 0), "Highest note (Hz)");
    w->addButton("Re-analyse", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r)
    {
        if (r == 1) processor.setAnalysisRange(w->getTextEditorContents("min").getDoubleValue(), w->getTextEditorContents("max").getDoubleValue());
    }), true);
}

void VocalTuneEditor::showQuantizeMenu()
{
    const double bpm = processor.getHostBpm() > 20.0 ? processor.getHostBpm() : 120.0, beat = 60.0 / bpm;
    juce::PopupMenu m;
    m.addSectionHeader(juce::String::formatted("Quantize note starts (%.0f BPM%s)", bpm, processor.getHostBpm() > 20.0 ? "" : ", assumed"));
    m.addItem(1, "1/4 note"); m.addItem(2, "1/8 note"); m.addItem(3, "1/16 note"); m.addItem(4, "1/32 note");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&quantizeBtn), [this, beat](int r)
    {
        if (r > 0) view.quantizeTiming(beat / (r == 1 ? 1.0 : r == 2 ? 2.0 : r == 3 ? 4.0 : 8.0));
    });
}
