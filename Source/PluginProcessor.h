#pragma once
// VOCAL TUNE STUDIO - PluginProcessor
// 構成: オーディオスレッドは「録音(キャプチャ)」と「編集結果の再生」だけを行う(メモリ確保・ファイルI/Oなし)。
//       解析・レンダリング・読み書きはすべてワーカースレッドで実行し、完成した結果をロックで差し替える。
#include <juce_audio_utils/juce_audio_utils.h>
#include "Core/EditSession.h"
#include "Core/Presets.h"
#include <atomic>
#include <deque>
#include <functional>
#include <memory>

struct RenderedAudio { std::vector<std::vector<float>> ch; };

class VocalTuneProcessor final : public juce::AudioProcessor,
                                 public juce::ChangeBroadcaster,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::AsyncUpdater
{
public:
    enum class Mode : int { Bypass = 0, Capture = 1, Edit = 2 };

    VocalTuneProcessor();
    ~VocalTuneProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "VOCAL TUNE STUDIO"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- UIから使うAPI(メッセージスレッド) ----
    juce::AudioProcessorValueTreeState apvts;

    bool isBusy() const { return busy.load(); }
    juce::String getStatus() const { const juce::ScopedLock l(statusLock); return status; }
    juce::CriticalSection& getSessionLock() { return sessionLock; }
    const vts::EditSession& getSession() const { return session; }
    uint32_t getEditRevision() const { return editRevision.load(); }
    uint32_t getAudioRevision() const { return audioRevision.load(); }

    // 編集操作。解析中は false を返し何もしない。成功すると再レンダリングを要求する。
    bool tryEdit(const std::function<void(vts::EditSession&)>& op);

    void loadAudioFile(const juce::File& file);
    void saveProject(const juce::File& file);
    void loadProject(const juce::File& file);
    void exportWav(const juce::File& file, int bits);
    void requestAnalyse();
    void startCapture();
    void stopCapture();
    bool isCapturing() const { return mode.load() == (int) Mode::Capture; }
    void applyPreset(int index);
    void setAnalysisRange(double minHz, double maxHz);

    // ---- トランスポート(ホスト停止中のプレビュー再生) ----
    void setInternalPlaying(bool p) { internalPlaying.store(p); }
    bool isInternalPlaying() const { return internalPlaying.load(); }
    void setInternalPositionSec(double sec) { internalPos.store((juce::int64) (juce::jmax(0.0, sec) * currentSampleRate)); playPosSec.store(juce::jmax(0.0, sec)); }
    double getPlayPositionSec() const { return playPosSec.load(); }
    void setLoop(bool on, double startSec, double endSec) { loopStart.store((juce::int64) (startSec * currentSampleRate)); loopEnd.store((juce::int64) (endSec * currentSampleRate)); loopOn.store(on); }
    double getHostBpm() const { return hostBpm.load(); }
    double getSampleRate() const { return currentSampleRate; }

private:
    void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;

    void applyParamsToSession();      // sessionLock保持中に呼ぶ
    void pushSessionToParams();       // メッセージスレッドで呼ぶ(プロジェクト読込後)
    void setParam(const juce::String& id, float plainValue);
    void publishOriginal();
    void postTask(std::function<void()> fn);
    void requestRender();
    void doRender();
    void setStatus(const juce::String& s) { { const juce::ScopedLock l(statusLock); status = s; } sendChangeMessage(); }
    void adoptDetectedKey();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Worker : juce::Thread
    {
        explicit Worker(VocalTuneProcessor& o) : juce::Thread("VTS worker"), owner(o) {}
        void run() override;
        VocalTuneProcessor& owner;
    };

    vts::EditSession session;
    juce::CriticalSection sessionLock;
    juce::CriticalSection taskLock, statusLock;
    std::deque<std::function<void()>> tasks;
    juce::String status { "Load an audio file or capture from the DAW" };
    std::atomic<bool> busy { false }, renderQueued { false }, paramsDirty { false };
    std::atomic<uint32_t> editRevision { 0 }, audioRevision { 0 };

    // オーディオスレッドが読む再生用バッファ(ワーカーが差し替える)
    juce::SpinLock audioLock;
    std::unique_ptr<RenderedAudio> originalBuf, editedBuf;
    std::atomic<bool> showEdited { true };

    // 再生状態
    std::atomic<int> mode { (int) Mode::Edit };
    std::atomic<bool> internalPlaying { false }, loopOn { false };
    std::atomic<juce::int64> internalPos { 0 }, loopStart { 0 }, loopEnd { 0 }, regionStart { 0 };
    std::atomic<double> playPosSec { 0.0 }, hostBpm { 0.0 };
    double currentSampleRate = 48000.0;

    // キャプチャ(最大5分。startCaptureでメッセージスレッドが確保する)
    std::vector<std::vector<float>> capBuf;
    juce::int64 capCapacity = 0, capAnchor = 0;
    bool capAnchored = false;
    std::atomic<juce::int64> capLen { 0 };
    std::atomic<bool> inCaptureBlock { false };

    // パラメーター(生値ポインタ)
    std::atomic<float> *pCenter, *pDrift, *pMod, *pTrans, *pFormant, *pVibDepth, *pVibRate, *pVibOnset, *pVibAmount, *pVibSmooth, *pScale, *pRoot, *pAB;
    bool adoptKeyOnNextAnalysis = true;

    Worker worker { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalTuneProcessor)
};
