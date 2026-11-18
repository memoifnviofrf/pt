#pragma once
// VOCAL TUNE STUDIO - EditSession: 読み込み→解析→編集→レンダリング→書き出し→プロジェクト保存の一連(JUCE非依存)。
// プラグイン/GUI/CLIはすべてこのクラスを通して同じ処理を使う(重複実装を避ける)。
#include "PitchEditor.h"
#include "WavIO.h"
#include <cstdio>
#include <sstream>
#include <fstream>
#include <cstring>

namespace vts {

class EditSession {
public:
    double sr = 48000;
    std::vector<std::vector<float>> original;   // 非破壊: 元音源は変更しない
    std::vector<PitchFrame> frames;
    std::vector<Note> detectedNotes;
    KeyResult key;
    PitchEditor editor;
    float formantShiftSemis = 0;
    double minHz = 70, maxHz = 1000;

    void setAudio(std::vector<std::vector<float>> ch, double rate) { original = std::move(ch); sr = rate; frames.clear(); detectedNotes.clear(); editor.setNotes({}); }
    bool loadWav(const std::string& path, std::string* err = nullptr) { WavData w; if (!WavIO::read(path, w, err)) return false; setAudio(std::move(w.ch), w.sampleRate); return true; }
    bool hasAudio() const { return !original.empty() && !original[0].empty(); }

    std::vector<float> monoMix() const {
        std::vector<float> m(original[0].size(), 0.f);
        for (auto& c : original) for (size_t i = 0; i < m.size() && i < c.size(); ++i) m[i] += c[i] / (float) original.size();
        return m;
    }

    // ピッチ検出(解析は ~22-24kHz に間引いて行う。高サンプルレートでの計算量爆発を避けるため)
    void detect() {
        auto mono = monoMix();
        const int dec = std::max(1, (int) std::lround(sr / 24000.0)); const double asr = sr / dec;
        auto x = decimate(mono, dec);
        PitchDetectorConfig pc; pc.sampleRate = asr; pc.minHz = (float) minHz; pc.maxHz = (float) maxHz;
        int W = 512; while (W < 0.04 * asr) W *= 2; pc.windowSize = W; pc.hopSize = std::max(32, (int) std::lround(0.0053 * asr));
        frames = PitchDetector(pc).analyse(x);
        hopSec = (double) pc.hopSize / asr;
    }
    void segmentNotes() {
        SegmenterConfig sc; sc.hopSec = hopSec; detectedNotes = NoteSegmenter(sc).segment(frames);
        key = ScaleDetector::detectKey(detectedNotes); editor.setNotes(detectedNotes);
    }
    void analyse() { if (!hasAudio()) return; detect(); segmentNotes(); }

    // 編集結果のレンダリング(各チャンネルへ同じピッチ比・時間写像を適用)
    std::vector<std::vector<float>> render() const { return renderStatic(original, sr, frames, editor, formantShiftSemis); }

    // バックグラウンドスレッド用: 編集状態のコピーだけを渡せば、メインスレッドの編集と並行してレンダリングできる
    static std::vector<std::vector<float>> renderStatic(const std::vector<std::vector<float>>& original, double sr,
                                                        const std::vector<PitchFrame>& frames, const PitchEditor& editor, float formantShiftSemis)
    {
        if (frames.size() < 2) return original;
        auto off = editor.computeOffsets(frames);
        auto ratio = PitchEditor::makeRatioFn(frames, off);
        PitchShifter::RatioFn formant = nullptr;
        if (formantShiftSemis != 0.f) { const double r = std::pow(2.0, formantShiftSemis / 12.0); formant = [r](double) { return r; }; }
        auto anchors = editor.buildAnchors(); bool identity = true;
        for (auto& a : anchors) identity &= std::abs(a.inSec - a.outSec) < 1e-9;
        std::vector<std::vector<float>> out;
        for (auto& ch : original)
            out.push_back(identity ? PitchShifter::process(ch, frames, sr, ratio, formant)
                                   : TimeStretchProcessor::process(ch, frames, sr, anchors, ratio, formant));
        return out;
    }
    bool exportWav(const std::string& path, int bits = 24, std::string* err = nullptr) const {
        WavData w; w.sampleRate = sr; w.ch = render(); return WavIO::write(path, w, bits, err);
    }

    // ---- プロジェクト保存・復元 ----
    // テキスト部(解析設定・編集状態)。元音源は含まない。
    std::string projectText() const {
        std::string o = "VTSPROJ 1\n"; char b[512]; const auto& s = editor.settings;
        snprintf(b, sizeof b, "range %.17g %.17g\nsettings %.17g %.17g %.17g %.17g %d %d %u %.17g\nvibrato %.17g %.17g %.17g %.17g %.17g\n", minHz, maxHz,
                 s.pitchCenter, s.pitchDrift, s.pitchModulation, s.noteTransition, (int) s.scale, s.root, s.customMask, formantShiftSemis,
                 s.vibrato.depth, s.vibrato.rate, s.vibrato.onsetShiftSec, s.vibrato.amount, s.vibrato.smoothing); o += b;
        for (auto& e : editor.getNotes()) {
            snprintf(b, sizeof b, "note %.17g %.17g %.17g %d %d %.17g %d %.17g %.17g\n", e.note.startSec, e.note.endSec, (double) e.note.medianMidi,
                     e.note.firstFrame, e.note.lastFrame, e.targetMidi, (int) e.manualTarget, e.newStartSec, e.newEndSec); o += b;
        }
        return o;
    }
    bool restoreFromText(const std::string& text, WavData&& audio, std::string* err = nullptr) {
        std::istringstream f(text); std::string line; std::getline(f, line);
        if (line != "VTSPROJ 1") { if (err) *err = "bad project header"; return false; }
        setAudio(std::move(audio.ch), audio.sampleRate);
        EditSettings s; std::vector<EditNote> ns;
        // 先に設定(解析音域)を読む
        { std::string l2; std::istringstream g(text); std::getline(g, l2);
          while (std::getline(g, l2)) { std::istringstream is(l2); std::string tag; is >> tag; if (tag == "range") is >> minHz >> maxHz; } }
        detect();                                   // 解析フレームは決定的に再計算
        while (std::getline(f, line)) {
            std::istringstream is(line); std::string tag; is >> tag;
            if (tag == "settings") { int sc; is >> s.pitchCenter >> s.pitchDrift >> s.pitchModulation >> s.noteTransition >> sc >> s.root >> s.customMask >> formantShiftSemis; s.scale = (ScaleType) sc; }
            else if (tag == "vibrato") is >> s.vibrato.depth >> s.vibrato.rate >> s.vibrato.onsetShiftSec >> s.vibrato.amount >> s.vibrato.smoothing;
            else if (tag == "note") { EditNote e; double md; int man; is >> e.note.startSec >> e.note.endSec >> md >> e.note.firstFrame >> e.note.lastFrame >> e.targetMidi >> man >> e.newStartSec >> e.newEndSec;
                e.note.medianMidi = (float) md; e.manualTarget = man != 0; ns.push_back(e); }
        }
        for (auto& e : ns) if (e.note.lastFrame >= (int) frames.size() || e.note.firstFrame < 0) { if (err) *err = "project/analysis mismatch"; return false; }
        editor.settings = s; editor.setEditNotes(ns);
        std::vector<Note> dn; for (auto& e : ns) dn.push_back(e.note); detectedNotes = dn; key = ScaleDetector::detectKey(dn);
        return true;
    }
    // ファイル: <path>(テキスト) + <path>.audio.wav(元音源float32)
    bool saveProject(const std::string& path, std::string* err = nullptr) const {
        WavData w; w.sampleRate = sr; w.ch = original; if (!WavIO::write(path + ".audio.wav", w, 32, err)) return false;
        std::ofstream f(path); if (!f) { if (err) *err = "cannot write project"; return false; }
        f << projectText(); return (bool) f;
    }
    bool loadProject(const std::string& path, std::string* err = nullptr) {
        std::ifstream f(path); if (!f) { if (err) *err = "cannot open project"; return false; }
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        WavData w; if (!WavIO::read(path + ".audio.wav", w, err)) return false;
        return restoreFromText(text, std::move(w), err);
    }
    // DAWプロジェクト内保存用(プラグイン状態): 1つのバイト列にまとめる
    std::vector<uint8_t> toBlob() const {
        std::string t = projectText(); WavData w; w.sampleRate = sr; w.ch = original; std::vector<uint8_t> wb; WavIO::writeBytes(w, 32, wb);
        std::vector<uint8_t> o; const char* magic = "VTSSTATE1"; o.insert(o.end(), magic, magic + 9);
        auto p64 = [&](uint64_t v) { for (int i = 0; i < 8; ++i) o.push_back((uint8_t) (v >> (8 * i))); };
        p64(t.size()); o.insert(o.end(), t.begin(), t.end()); p64(wb.size()); o.insert(o.end(), wb.begin(), wb.end()); return o;
    }
    bool fromBlob(const uint8_t* d, size_t n, std::string* err = nullptr) {
        auto g64 = [&](size_t p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= (uint64_t) d[p + (size_t) i] << (8 * i); return v; };
        if (n < 25 || std::memcmp(d, "VTSSTATE1", 9)) { if (err) *err = "bad state header"; return false; }
        uint64_t tl = g64(9); if (17 + tl + 8 > n) { if (err) *err = "truncated state"; return false; }
        std::string t((const char*) d + 17, (size_t) tl); uint64_t wl = g64(17 + (size_t) tl); size_t wp = 25 + (size_t) tl;
        if (wp + wl > n) { if (err) *err = "truncated audio"; return false; }
        std::vector<uint8_t> wb(d + wp, d + wp + wl); WavData w; if (!WavIO::readBytes(wb, w, err)) return false;
        return restoreFromText(t, std::move(w), err);
    }

private:
    double hopSec = 128.0 / 24000.0;
    static std::vector<float> decimate(const std::vector<float>& x, int f) {
        if (f <= 1) return x;
        const int T = 8 * f; std::vector<double> h((size_t) (2 * T + 1)); double sum = 0; const double fc = 0.45 / f;
        for (int i = -T; i <= T; ++i) { double s = i == 0 ? 2 * fc : std::sin(2 * M_PI * fc * i) / (M_PI * i);
            double w = 0.5 * (1 + std::cos(M_PI * i / (T + 1))); h[(size_t) (i + T)] = s * w; sum += s * w; }
        for (auto& v : h) v /= sum;
        std::vector<float> y(x.size() / (size_t) f);
        for (size_t n = 0; n < y.size(); ++n) { double a = 0; long c = (long) (n * (size_t) f);
            for (int i = -T; i <= T; ++i) { long q = c + i; if (q >= 0 && q < (long) x.size()) a += h[(size_t) (i + T)] * x[(size_t) q]; } y[n] = (float) a; }
        return y;
    }
};

} // namespace vts
