#pragma once
// VOCAL TUNE STUDIO - PluginEditor: 上部(ファイル/Undo/プリセット)・中央(ピアノロール)・右(パラメーター)・下部(再生/ツール/ズーム)
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UI/PitchEditorView.h"
#include <array>

class VocalTuneEditor final : public juce::AudioProcessorEditor,
                              private juce::ChangeListener,
                              private juce::Timer
{
public:
    explicit VocalTuneEditor(VocalTuneProcessor&);
    ~VocalTuneEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<SliderAtt> att;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void addKnob(Knob&, const juce::String& paramId, const juce::String& name);
    void chooseAudio(), chooseOpenProject(), chooseSaveProject(), chooseExport(int bits);
    void showSettings(), showQuantizeMenu(), showExportMenu();
    void doUndo(), doRedo(), togglePlay();
    void selectTool(PitchEditorView::Tool t);

    VocalTuneProcessor& processor;
    juce::LookAndFeel_V4 laf;
    PitchEditorView view;
    std::unique_ptr<juce::FileChooser> chooser;

    // 上部
    juce::Label titleLabel { {}, "VOCAL TUNE STUDIO" }, statusLabel;
    juce::TextButton openBtn { "Open Audio" }, openProjBtn { "Open" }, saveBtn { "Save" }, exportBtn { "Export WAV" },
                     undoBtn { "Undo" }, redoBtn { "Redo" }, captureBtn { "Capture" }, settingsBtn { "Settings" };
    juce::ComboBox presetBox;
    juce::ToggleButton abBtn { "B: Edited  (off = A: Original)" };
    std::unique_ptr<ButtonAtt> abAtt;

    // 右パネル
    std::array<Knob, 10> knobs;
    juce::Label scaleLbl { {}, "Scale" }, rootLbl { {}, "Key" }, pitchHdr { {}, "PITCH" }, vibHdr { {}, "VIBRATO" }, fmtHdr { {}, "FORMANT" };
    juce::ComboBox scaleBox, rootBox;
    std::unique_ptr<ComboAtt> scaleAtt, rootAtt;

    // 下部
    juce::TextButton playBtn { "Play" }, stopBtn { "Stop" }, loopBtn { "Loop" },
                     pitchTool { "Pitch" }, timeTool { "Time" }, splitTool { "Split" }, mergeTool { "Merge" },
                     snapBtn { "Snap" }, quantizeBtn { "Quantize" }, hPlus { "H+" }, hMinus { "H-" }, vPlus { "V+" }, vMinus { "V-" }, fitBtn { "Fit" };
    juce::Label timeLabel;
    int exportBits = 24;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalTuneEditor)
};
