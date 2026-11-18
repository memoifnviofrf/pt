#pragma once
// VOCAL TUNE STUDIO - PitchEditorView: ピアノロール上の音符ブロブ編集ビュー(波形・元/編集後ピッチ曲線・信頼度表示付き)
#include <juce_audio_utils/juce_audio_utils.h>
#include "../PluginProcessor.h"
#include <set>

class PitchEditorView final : public juce::Component,
                              private juce::ScrollBar::Listener,
                              private juce::Timer,
                              private juce::ChangeListener
{
public:
    enum class Tool { Pitch, Time, Split, Merge };

    explicit PitchEditorView(VocalTuneProcessor&);
    ~PitchEditorView() override;

    void setTool(Tool t) { tool = t; repaint(); }
    void setSnap(bool on) { snap = on; }
    void zoomTime(double factor) { zoomTimeAt(factor, getPlotArea().getCentreX()); }
    void zoomPitch(double factor) { zoomPitchAt(factor, (float) getPlotArea().getCentreY()); }
    void fitAll();
    juce::Array<size_t> getSelection() const { juce::Array<size_t> a; for (auto i : selected) a.add(i); return a; }
    void deleteSelectionTargets();     // 選択音符を自動(スケール)ターゲットへ戻す
    void quantizeTiming(double gridSec);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr int keyboardW = 52, rulerH = 22, waveH = 64, barSize = 12;

    struct Drag
    {
        enum class Kind { None, Move, ResizeL, ResizeR, Rubber } kind = Kind::None;
        juce::Point<float> origin;
        size_t anchor = 0;
        double deltaSemis = 0, deltaSec = 0, newStart = 0, newEnd = 0;
        double anchorTarget = 0, anchorStart = 0, anchorEnd = 0;
        juce::Rectangle<float> rubber;
    } drag;

    juce::Rectangle<int> getPlotArea() const;
    float timeToX(double t) const { return (float) (keyboardW + (t - viewStart) * pxPerSec); }
    double xToTime(float x) const { return viewStart + (x - keyboardW) / pxPerSec; }
    float midiToY(double m) const { return (float) (rulerH + (viewTopMidi - m) * pxPerSemi); }
    double yToMidi(float y) const { return viewTopMidi - (y - rulerH) / pxPerSemi; }

    void drawKeyboardAndGrid(juce::Graphics&, juce::Rectangle<int> plot, const vts::EditSession&);
    void drawRuler(juce::Graphics&, juce::Rectangle<int> plot);
    void drawWaveform(juce::Graphics&, juce::Rectangle<int> plot, const vts::EditSession&);
    void drawCurves(juce::Graphics&, juce::Rectangle<int> plot, const vts::EditSession&);
    void drawNotes(juce::Graphics&, juce::Rectangle<int> plot, const vts::EditSession&);

    // ノートの表示区間(編集済みなら編集後)
    static void displaySpan(const vts::EditNote& e, double& s, double& t) { s = e.newStartSec >= 0 ? e.newStartSec : e.note.startSec; t = e.newEndSec >= 0 ? e.newEndSec : e.note.endSec; }
    int hitNote(const vts::EditSession&, juce::Point<float> p) const;
    double toInputTime(const vts::EditNote&, double outTime) const;
    double snapTime(double t) const;
    double mapTime(double inSec) const;      // 入力時間→編集後の表示時間
    void refreshCaches(const vts::EditSession&);
    void updateScrollBars();
    void zoomTimeAt(double factor, float anchorX);
    void zoomPitchAt(double factor, float anchorY);
    void commitDrag();

    void scrollBarMoved(juce::ScrollBar*, double newStart) override;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }

    VocalTuneProcessor& proc;
    juce::ScrollBar hBar { false }, vBar { true };
    Tool tool = Tool::Pitch;
    bool snap = true;
    double viewStart = 0.0, pxPerSec = 120.0, viewTopMidi = 84.0, pxPerSemi = 16.0;
    std::set<size_t> selected;

    // 描画キャッシュ(編集/音源リビジョンが変わったときだけ再計算)
    uint32_t cachedEdit = ~0u, cachedAudio = ~0u;
    std::vector<float> offsets;                 // 編集による補正量[半音]/フレーム
    std::vector<float> peaks;                   // 波形の(min,max)ペア / 128サンプル
    std::vector<vts::TimeStretchProcessor::Anchor> anchors;
    size_t cachedNoteCount = 0;
    bool wasBusy = false;
    double totalSec = 10.0, lastPos = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchEditorView)
};
