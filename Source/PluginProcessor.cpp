#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AudioFileManager.h"
#include "ProjectManager.h"
#include "Core/Resampler.h"

namespace
{
const char* kIds[] = { "pitchCenter", "pitchDrift", "pitchMod", "noteTransition", "formantShift",
                       "vibDepth", "vibRate", "vibOnset", "vibAmount", "vibSmooth", "scaleType", "rootNote", "abEdited" };
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalTuneProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto flt = [&](const char* id, const char* name, float lo, float hi, float step, float def, const char* unit)
    {
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name,
              juce::NormalisableRange<float>(lo, hi, step), def, juce::AudioParameterFloatAttributes().withLabel(unit)));
    };
    flt("pitchCenter",    "Pitch Center",     0.f, 100.f, 0.1f, 100.f, "%");
    flt("pitchDrift",     "Pitch Drift",      0.f, 100.f, 0.1f,   0.f, "%");
    flt("pitchMod",       "Pitch Modulation", 0.f, 100.f, 0.1f,   0.f, "%");
    flt("noteTransition", "Note Transition",  0.f, 100.f, 0.1f,  50.f, "%");
    flt("formantShift",   "Formant Shift",  -12.f,  12.f, 0.01f, 0.f, "st");
    flt("vibDepth",       "Vibrato Depth",    0.f, 200.f, 0.1f, 100.f, "%");
    flt("vibRate",        "Vibrato Rate",    50.f, 200.f, 0.1f, 100.f, "%");
    flt("vibOnset",       "Vibrato Onset",   -1.f,   1.f, 0.01f, 0.f, "s");
    flt("vibAmount",      "Vibrato Amount",   0.f, 100.f, 0.1f, 100.f, "%");
    flt("vibSmooth",      "Vibrato Smoothing",0.f, 100.f, 0.1f,   0.f, "%");
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "scaleType", 1 }, "Scale",
          juce::StringArray { "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor", "Pentatonic" }, 0));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "rootNote", 1 }, "Key",
          juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "abEdited", 1 }, "A/B (on = Edited)", true));
    return l;
}

VocalTuneProcessor::VocalTuneProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "VTS", createLayout())
{
    pCenter = apvts.getRawParameterValue("pitchCenter");  pDrift = apvts.getRawParameterValue("pitchDrift");
    pMod = apvts.getRawParameterValue("pitchMod");        pTrans = apvts.getRawParameterValue("noteTransition");
    pFormant = apvts.getRawParameterValue("formantShift"); pVibDepth = apvts.getRawParameterValue("vibDepth");
    pVibRate = apvts.getRawParameterValue("vibRate");      pVibOnset = apvts.getRawParameterValue("vibOnset");
    pVibAmount = apvts.getRawParameterValue("vibAmount");  pVibSmooth = apvts.getRawParameterValue("vibSmooth");
    pScale = apvts.getRawParameterValue("scaleType");      pRoot = apvts.getRawParameterValue("rootNote");
    pAB = apvts.getRawParameterValue("abEdited");
    for (auto* id : kIds) apvts.addParameterListener(id, this);
    worker.startThread();
}

VocalTuneProcessor::~VocalTuneProcessor()
{
    for (auto* id : kIds) apvts.removeParameterListener(id, this);
    cancelPendingUpdate();
    worker.stopThread(30000);
}

void VocalTuneProcessor::Worker::run()
{
    while (! threadShouldExit())
    {
        std::function<void()> t;
        {
            const juce::ScopedLock l(owner.taskLock);
            if (! owner.tasks.empty()) { t = std::move(owner.tasks.front()); owner.tasks.pop_front(); }
        }
        if (t) t(); else wait(-1);
    }
}

void VocalTuneProcessor::postTask(std::function<void()> fn)
{
    { const juce::ScopedLock l(taskLock); tasks.push_back(std::move(fn)); }
    worker.notify();
}

// ===================== オーディオスレッド =====================
bool VocalTuneProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
}

void VocalTuneProcessor::prepareToPlay(double sr, int)
{
    currentSampleRate = sr;
    const juce::ScopedTryLock tl(sessionLock);
    if (tl.isLocked() && session.hasAudio() && std::abs(session.sr - sr) > 0.5 && ! busy.load())
    {
        busy = true;
        postTask([this, sr]
        {
            { const juce::ScopedLock l(sessionLock);
              setStatus("Sample rate changed - re-analysing (manual edits are discarded)");
              std::vector<std::vector<float>> ch;
              for (auto& c : session.original) ch.push_back(vts::resample(c, session.sr, sr));
              session.setAudio(std::move(ch), sr); session.analyse(); applyParamsToSession(); publishOriginal(); }
            ++audioRevision; busy = false; doRender(); setStatus("Ready");
        });
    }
}

void VocalTuneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    bool hostPlaying = false; juce::int64 hostPos = 0;
    if (auto* ph = getPlayHead())
        if (auto p = ph->getPosition())
        {
            hostPlaying = p->getIsPlaying();
            if (auto t = p->getTimeInSamples()) hostPos = *t;
            if (auto b = p->getBpm()) hostBpm.store(*b);
        }

    const auto m = (Mode) mode.load();
    if (m == Mode::Bypass) return;

    if (m == Mode::Capture)                      // 入力はそのまま通しつつ録音
    {
        if (hostPlaying && ! capBuf.empty())
        {
            inCaptureBlock.store(true);
            if (! capAnchored) { capAnchor = hostPos; capAnchored = true; regionStart.store(hostPos); }
            const int cc = (int) capBuf.size();
            for (int i = 0; i < n; ++i)
            {
                const juce::int64 pos = hostPos - capAnchor + i;
                if (pos < 0 || pos >= capCapacity) continue;
                for (int c = 0; c < cc; ++c) capBuf[(size_t) c][(size_t) pos] = buffer.getReadPointer(juce::jmin(c, nch - 1))[i];
                if (pos + 1 > capLen.load()) capLen.store(pos + 1);
            }
            inCaptureBlock.store(false);
        }
        return;
    }

    // ---- Edit: 編集結果(または元音源)を再生。ホストが動いていればホストに同期 ----
    bool useInternal = false;
    juce::int64 readPos = 0;
    if (hostPlaying) readPos = hostPos - regionStart.load();
    else if (internalPlaying.load()) { readPos = internalPos.load(); useInternal = true; }

    const juce::SpinLock::ScopedTryLockType lock(audioLock);
    const RenderedAudio* src = nullptr;
    if (lock.isLocked()) src = (showEdited.load() ? editedBuf.get() : originalBuf.get());
    if (src == nullptr || src->ch.empty()) return;      // 未解析(またはロック失敗): 入力素通し

    if (! hostPlaying && ! useInternal) { buffer.clear(); return; }

    const juce::int64 len = (juce::int64) src->ch[0].size();
    const bool loop = useInternal && loopOn.load() && loopEnd.load() > loopStart.load();
    const juce::int64 ls = loopStart.load(), le = loopEnd.load();
    juce::int64 pos = readPos;
    for (int i = 0; i < n; ++i)
    {
        if (loop && pos >= le) pos = ls + (pos - ls) % (le - ls);
        for (int c = 0; c < nch; ++c)
        {
            const auto& s = src->ch[(size_t) juce::jmin(c, (int) src->ch.size() - 1)];
            buffer.getWritePointer(c)[i] = (pos >= 0 && pos < len) ? s[(size_t) pos] : 0.f;
        }
        ++pos;
    }
    if (useInternal)
    {
        if (pos >= len + (juce::int64) currentSampleRate && ! loop) internalPlaying.store(false);
        internalPos.store(pos);
    }
    playPosSec.store((double) pos / currentSampleRate);
}

// ===================== パラメーター → セッション =====================
void VocalTuneProcessor::handleAsyncUpdate()
{
    showEdited.store(pAB->load() > 0.5f);
    if (busy.load()) { paramsDirty = true; return; }
    {
        const juce::ScopedLock l(sessionLock);
        if (! session.hasAudio()) return;
        applyParamsToSession();
    }
    ++editRevision;
    requestRender();
}

void VocalTuneProcessor::applyParamsToSession()
{
    auto& s = session.editor.settings;
    s.pitchCenter = pCenter->load() / 100.f;   s.pitchDrift = pDrift->load() / 100.f;
    s.pitchModulation = pMod->load() / 100.f;  s.noteTransition = pTrans->load() / 100.f;
    s.scale = (vts::ScaleType) juce::jlimit(0, 5, (int) std::lround(pScale->load()));
    s.root = juce::jlimit(0, 11, (int) std::lround(pRoot->load()));
    s.vibrato.depth = pVibDepth->load() / 100.f;  s.vibrato.rate = pVibRate->load() / 100.f;
    s.vibrato.onsetShiftSec = pVibOnset->load();  s.vibrato.amount = pVibAmount->load() / 100.f;
    s.vibrato.smoothing = pVibSmooth->load() / 100.f;
    session.formantShiftSemis = pFormant->load();
    session.editor.autoTargets(false);          // 手動指定していない音符だけスケールへ再量子化
}

void VocalTuneProcessor::setParam(const juce::String& id, float plain)
{
    if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain));
}

void VocalTuneProcessor::pushSessionToParams()
{
    const juce::ScopedLock l(sessionLock);
    const auto& s = session.editor.settings;
    setParam("pitchCenter", s.pitchCenter * 100.f);  setParam("pitchDrift", s.pitchDrift * 100.f);
    setParam("pitchMod", s.pitchModulation * 100.f); setParam("noteTransition", s.noteTransition * 100.f);
    setParam("formantShift", session.formantShiftSemis);
    setParam("vibDepth", s.vibrato.depth * 100.f);   setParam("vibRate", s.vibrato.rate * 100.f);
    setParam("vibOnset", s.vibrato.onsetShiftSec);   setParam("vibAmount", s.vibrato.amount * 100.f);
    setParam("vibSmooth", s.vibrato.smoothing * 100.f);
    setParam("scaleType", (float) juce::jlimit(0, 5, (int) s.scale)); setParam("rootNote", (float) s.root);
}

void VocalTuneProcessor::applyPreset(int index)
{
    const auto& ps = vts::presets();
    if (index < 0 || index >= (int) ps.size()) return;
    const auto& p = ps[(size_t) index];
    setParam("pitchCenter", p.center);  setParam("pitchDrift", p.drift);  setParam("pitchMod", p.modulation);
    setParam("noteTransition", p.transition);  setParam("formantShift", p.formantSemis);
    setParam("vibDepth", p.vibDepthPct);  setParam("vibRate", p.vibRatePct);  setParam("vibOnset", p.vibOnsetSec);
    setParam("vibAmount", p.vibAmountPct);  setParam("vibSmooth", p.vibSmoothPct);
}

void VocalTuneProcessor::adoptDetectedKey()
{
    const int root = session.key.root; const bool minor = session.key.minor;
    juce::MessageManager::callAsync([this, root, minor] { setParam("rootNote", (float) root); setParam("scaleType", minor ? 2.f : 1.f); });
}

// ===================== バッファ公開・レンダリング =====================
void VocalTuneProcessor::publishOriginal()           // sessionLock保持中
{
    auto b = std::make_unique<RenderedAudio>();
    b->ch = session.original;
    const juce::SpinLock::ScopedLockType l(audioLock);
    originalBuf.swap(b);                              // 古いバッファは b のデストラクタで(ロック外ではないが)解放: 短時間
}

void VocalTuneProcessor::requestRender()
{
    if (renderQueued.exchange(true)) return;          // 連続編集は1回にまとめる
    postTask([this] { renderQueued = false; doRender(); });
}

void VocalTuneProcessor::doRender()
{
    vts::PitchEditor ed; float formant = 0; double sr = 48000;
    {
        const juce::ScopedLock l(sessionLock);
        if (! session.hasAudio()) return;
        if (session.frames.size() < 2) { auto b = std::make_unique<RenderedAudio>(); b->ch = session.original;
                                         const juce::SpinLock::ScopedLockType al(audioLock); editedBuf.swap(b); return; }
        ed = session.editor; formant = session.formantShiftSemis; sr = session.sr;
    }
    // original / frames を変更するのはワーカースレッドだけなので、ここでは安全に参照できる
    auto out = vts::EditSession::renderStatic(session.original, sr, session.frames, ed, formant);
    auto b = std::make_unique<RenderedAudio>(); b->ch = std::move(out);
    { const juce::SpinLock::ScopedLockType al(audioLock); editedBuf.swap(b); }
    ++editRevision;
    setStatus("Ready");
}

bool VocalTuneProcessor::tryEdit(const std::function<void(vts::EditSession&)>& op)
{
    if (busy.load()) return false;
    { const juce::ScopedLock l(sessionLock); if (! session.hasAudio()) return false; op(session); }
    ++editRevision;
    requestRender();
    sendChangeMessage();
    return true;
}

// ===================== ファイル/解析ジョブ =====================
void VocalTuneProcessor::loadAudioFile(const juce::File& file)
{
    if (busy.exchange(true)) return;
    setStatus("Loading " + file.getFileName() + " ...");
    postTask([this, file]
    {
        std::vector<std::vector<float>> ch; juce::String err;
        if (! AudioFileManager::load(file, currentSampleRate, ch, err)) { busy = false; setStatus("Error: " + err); return; }
        { const juce::ScopedLock l(sessionLock);
          setStatus("Analysing ...");
          session.setAudio(std::move(ch), currentSampleRate); session.analyse(); applyParamsToSession(); publishOriginal(); }
        regionStart.store(0); ++audioRevision;
        busy = false; adoptDetectedKey(); doRender();
        if (paramsDirty.exchange(false)) triggerAsyncUpdate();
    });
}

void VocalTuneProcessor::requestAnalyse()
{
    if (busy.exchange(true)) return;
    setStatus("Analysing ...");
    postTask([this]
    {
        { const juce::ScopedLock l(sessionLock);
          if (session.hasAudio()) { session.analyse(); applyParamsToSession(); publishOriginal(); } }
        ++audioRevision; busy = false; doRender();
        if (paramsDirty.exchange(false)) triggerAsyncUpdate();
    });
}

void VocalTuneProcessor::setAnalysisRange(double minHz, double maxHz)
{
    { const juce::ScopedLock l(sessionLock); session.minHz = juce::jlimit(30.0, 500.0, minHz); session.maxHz = juce::jlimit(session.minHz * 1.5, 2000.0, maxHz); }
    requestAnalyse();
}

void VocalTuneProcessor::saveProject(const juce::File& file)
{
    postTask([this, file]
    {
        juce::String err; bool ok;
        { const juce::ScopedLock l(sessionLock); ok = ProjectManager::save(session, file, err); }
        setStatus(ok ? "Project saved: " + file.getFileName() : "Save failed: " + err);
    });
}

void VocalTuneProcessor::loadProject(const juce::File& file)
{
    if (busy.exchange(true)) return;
    setStatus("Loading project ...");
    postTask([this, file]
    {
        juce::String err; bool ok;
        { const juce::ScopedLock l(sessionLock);
          ok = ProjectManager::load(session, file, err);
          if (ok && std::abs(session.sr - currentSampleRate) > 0.5)
          {
              std::vector<std::vector<float>> ch; for (auto& c : session.original) ch.push_back(vts::resample(c, session.sr, currentSampleRate));
              auto saved = session.editor.settings; session.setAudio(std::move(ch), currentSampleRate); session.analyse(); session.editor.settings = saved;
              err = "Sample rate differs from host: re-analysed";
          }
          if (ok) publishOriginal(); }
        if (ok) { regionStart.store(0); ++audioRevision; juce::MessageManager::callAsync([this] { pushSessionToParams(); }); }
        busy = false; if (ok) doRender();
        setStatus(ok ? (err.isEmpty() ? "Project loaded" : err) : "Load failed: " + err);
    });
}

void VocalTuneProcessor::exportWav(const juce::File& file, int bits)
{
    postTask([this, file, bits]
    {
        vts::PitchEditor ed; float formant = 0; double sr = 48000;
        { const juce::ScopedLock l(sessionLock); if (! session.hasAudio()) { setStatus("Nothing to export"); return; }
          ed = session.editor; formant = session.formantShiftSemis; sr = session.sr; }
        setStatus("Exporting ...");
        auto out = vts::EditSession::renderStatic(session.original, sr, session.frames, ed, formant);
        juce::String err; const bool ok = AudioFileManager::exportWav(file, out, sr, bits, err);
        setStatus(ok ? "Exported: " + file.getFileName() : "Export failed: " + err);
    });
}

// ===================== キャプチャ =====================
void VocalTuneProcessor::startCapture()
{
    if (isCapturing() || busy.load()) return;
    const int cc = juce::jlimit(1, 2, getTotalNumInputChannels());
    capCapacity = (juce::int64) (5.0 * 60.0 * currentSampleRate);
    capBuf.assign((size_t) cc, std::vector<float>((size_t) capCapacity, 0.f));
    capLen.store(0); capAnchored = false; internalPlaying.store(false);
    mode.store((int) Mode::Capture);
    setStatus("Capturing: start host playback over the part to edit (max 5 min), then press Capture again");
}

void VocalTuneProcessor::stopCapture()
{
    if (! isCapturing()) return;
    mode.store((int) Mode::Bypass);
    for (int i = 0; i < 500 && inCaptureBlock.load(); ++i) juce::Thread::sleep(1);   // 進行中のブロックを待つ
    const auto n = capLen.load();
    std::vector<std::vector<float>> ch;
    for (auto& c : capBuf) ch.emplace_back(c.begin(), c.begin() + (std::ptrdiff_t) n);
    std::vector<std::vector<float>>().swap(capBuf);
    mode.store((int) Mode::Edit);
    if (n < (juce::int64) (0.3 * currentSampleRate)) { setStatus("Nothing captured (host must be playing)"); return; }
    if (busy.exchange(true)) return;
    setStatus("Analysing capture ...");
    postTask([this, ch = std::move(ch)]() mutable
    {
        { const juce::ScopedLock l(sessionLock);
          session.setAudio(std::move(ch), currentSampleRate); session.analyse(); applyParamsToSession(); publishOriginal(); }
        ++audioRevision; busy = false; adoptDetectedKey(); doRender();
        if (paramsDirty.exchange(false)) triggerAsyncUpdate();
    });
}

// ===================== DAW内の状態保存 =====================
void VocalTuneProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto vt = apvts.copyState();
    vt.setProperty("regionStart", (juce::int64) regionStart.load(), nullptr);
    const juce::ScopedTryLock tl(sessionLock);
    ProjectManager::packState(dest, vt, (tl.isLocked() && ! busy.load()) ? &session : nullptr);
}

void VocalTuneProcessor::setStateInformation(const void* data, int size)
{
    juce::String xml; std::vector<uint8_t> blob;
    if (! ProjectManager::unpackState(data, size, xml, blob)) return;
    if (auto parsed = juce::parseXML(xml))
    {
        auto vt = juce::ValueTree::fromXml(*parsed);
        regionStart.store((juce::int64) vt.getProperty("regionStart", 0));
        apvts.replaceState(vt);
    }
    if (blob.empty() || busy.exchange(true)) return;
    postTask([this, blob = std::move(blob)]
    {
        std::string e; bool ok;
        { const juce::ScopedLock l(sessionLock);
          ok = session.fromBlob(blob.data(), blob.size(), &e);
          if (ok && std::abs(session.sr - currentSampleRate) > 0.5)
          {
              std::vector<std::vector<float>> ch; for (auto& c : session.original) ch.push_back(vts::resample(c, session.sr, currentSampleRate));
              auto saved = session.editor.settings; session.setAudio(std::move(ch), currentSampleRate); session.analyse(); session.editor.settings = saved;
          }
          if (ok) publishOriginal(); }
        if (ok) { ++audioRevision; juce::MessageManager::callAsync([this] { pushSessionToParams(); }); }
        busy = false; if (ok) doRender();
        setStatus(ok ? "Session restored" : "Session restore failed: " + juce::String(e));
    });
}

juce::AudioProcessorEditor* VocalTuneProcessor::createEditor() { return new VocalTuneEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VocalTuneProcessor(); }
