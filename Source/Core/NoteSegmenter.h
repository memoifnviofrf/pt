#pragma once
// VOCAL TUNE STUDIO - NoteSegmenter (JUCE非依存コア)
// PitchDetectorのフレーム列を編集可能な音符列へ分離する。
// 方針: 有声区間を作る(短い無声ギャップは子音として橋渡し) →
//       ビブラート周期程度の移動平均でビブラートを除去した曲線で音程変化点を検出 →
//       境界は生ピッチ(メディアン)で半値交差位置へ精密化。
#include "PitchDetector.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace vts {

struct Note {
    double startSec = 0, endSec = 0;
    float  medianMidi = 0;      // 音符の代表音高(連続MIDI値)
    float  meanCentsDev = 0;    // 最寄り半音からのずれ(セント)
    bool   legatoToNext = false;// 次の音符と隙間なく接続
    int    firstFrame = 0, lastFrame = 0; // 解析フレーム範囲(包含)
};

struct SegmenterConfig {
    double hopSec = 256.0 / 48000.0;
    double maxBridgeGapSec = 0.05;   // これ以下の無声ギャップは同一音符内として扱う(子音)
    double vibratoWindowSec = 0.20;  // ビブラート除去用の平滑窓(≥1周期)
    float  changeThresholdSemis = 0.6f;
    double confirmSec = 0.06;        // 変化が継続してから分割確定
    double minNoteSec = 0.06;
};

class NoteSegmenter {
public:
    explicit NoteSegmenter(const SegmenterConfig& c) : cfg(c) {}

    std::vector<Note> segment(const std::vector<PitchFrame>& fr) const
    {
        std::vector<Note> notes;
        const int n = (int) fr.size();
        const int bridge = std::max(1, (int) std::round(cfg.maxBridgeGapSec / cfg.hopSec));
        int i = 0;
        while (i < n) {
            if (!fr[(size_t) i].voiced) { ++i; continue; }
            int a = i, last = i, j = i + 1;
            while (j < n && j - last <= bridge) { if (fr[(size_t) j].voiced) last = j; ++j; }
            segmentRun(fr, a, last, notes);
            i = last + 1;
        }
        // 短すぎる音符は隣接へ吸収せず破棄(ノイズ)。legato判定
        notes.erase(std::remove_if(notes.begin(), notes.end(),
            [&](const Note& x){ return x.endSec - x.startSec < cfg.minNoteSec; }), notes.end());
        for (size_t k = 0; k + 1 < notes.size(); ++k)
            notes[k].legatoToNext = (notes[k+1].startSec - notes[k].endSec) < cfg.hopSec * 1.5;
        return notes;
    }

    // ---- 手動編集用(UIから呼ぶ) ----
    static void mergeWithNext(std::vector<Note>& ns, size_t idx, const std::vector<PitchFrame>& fr)
    {
        if (idx + 1 >= ns.size()) return;
        Note m = ns[idx]; m.endSec = ns[idx+1].endSec; m.lastFrame = ns[idx+1].lastFrame;
        m.legatoToNext = ns[idx+1].legatoToNext;
        fill(m, fr); ns[idx] = m; ns.erase(ns.begin() + (long) idx + 1);
    }
    static void splitAt(std::vector<Note>& ns, size_t idx, double tSec, const std::vector<PitchFrame>& fr)
    {
        if (idx >= ns.size() || tSec <= ns[idx].startSec || tSec >= ns[idx].endSec) return;
        Note a = ns[idx], b = ns[idx];
        a.endSec = b.startSec = tSec; a.legatoToNext = true;
        int fsplit = a.firstFrame;
        while (fsplit < a.lastFrame && fr[(size_t) fsplit].timeSec < tSec) ++fsplit;
        a.lastFrame = std::max(a.firstFrame, fsplit - 1); b.firstFrame = std::min(fsplit, b.lastFrame);
        fill(a, fr); fill(b, fr);
        ns[idx] = a; ns.insert(ns.begin() + (long) idx + 1, b);
    }

private:
    SegmenterConfig cfg;

    static float median(std::vector<float> v) {
        if (v.empty()) return 0;
        size_t h = v.size()/2;
        std::nth_element(v.begin(), v.begin() + (long) h, v.end()); return v[h];
    }
    static void fill(Note& nt, const std::vector<PitchFrame>& fr) {
        std::vector<float> v;
        for (int k = nt.firstFrame; k <= nt.lastFrame; ++k) if (fr[(size_t) k].voiced) v.push_back(fr[(size_t) k].midi);
        nt.medianMidi = median(v);
        double s = 0; for (float x : v) s += (x - std::round(nt.medianMidi)) * 100.0;
        nt.meanCentsDev = v.empty() ? 0.f : (float) (s / v.size());
    }
    void emit(const std::vector<PitchFrame>& fr, int a, int b, std::vector<Note>& out) const {
        Note nt; nt.firstFrame = a; nt.lastFrame = b;
        nt.startSec = fr[(size_t) a].timeSec - cfg.hopSec * 0.5;
        nt.endSec   = fr[(size_t) b].timeSec + cfg.hopSec * 0.5;
        fill(nt, fr); out.push_back(nt);
    }

    void segmentRun(const std::vector<PitchFrame>& fr, int a, int b, std::vector<Note>& out) const
    {
        const int L = b - a + 1;
        // 無声ギャップは直前の有声値で埋めた作業用曲線
        std::vector<float> raw((size_t) L), voicedMask((size_t) L);
        float prev = 0; bool have = false;
        for (int k = 0; k < L; ++k) {
            const auto& f = fr[(size_t) (a + k)];
            if (f.voiced) { prev = f.midi; have = true; }
            raw[(size_t) k] = have ? prev : f.midi;
            voicedMask[(size_t) k] = f.voiced ? 1.f : 0.f;
        }
        // 5点メディアン(スパイク除去)
        std::vector<float> m((size_t) L);
        for (int k = 0; k < L; ++k) {
            std::vector<float> w;
            for (int d = -2; d <= 2; ++d) w.push_back(raw[(size_t) std::clamp(k + d, 0, L - 1)]);
            m[(size_t) k] = median(w);
        }
        // ビブラート除去の移動平均
        const int W = std::max(3, (int) std::round(cfg.vibratoWindowSec / cfg.hopSec)) | 1;
        std::vector<float> s((size_t) L);
        for (int k = 0; k < L; ++k) {
            double acc = 0; int cnt = 0;
            for (int d = -W/2; d <= W/2; ++d) { int q = k + d; if (q < 0 || q >= L) continue; acc += m[(size_t) q]; ++cnt; }
            s[(size_t) k] = (float) (acc / cnt);
        }
        const int K = std::max(2, (int) std::round(cfg.confirmSec / cfg.hopSec));
        int segStart = 0, streak = 0, streakStart = -1;
        for (int k = 0; k < L; ++k) {
            // 現在の代表音高: セグメント内メディアン(境界直後は生曲線のみ使用)
            std::vector<float> w(m.begin() + segStart, m.begin() + k + 1);
            float center = median(w);
            // 平滑曲線(ビブラート除去済み)と生曲線の両方が基準から外れたときのみ変化候補とする
            if (std::abs(s[(size_t) k] - center) > cfg.changeThresholdSemis
                && std::abs(m[(size_t) k] - center) > cfg.changeThresholdSemis && k - segStart > K) {
                if (streak == 0) streakStart = k;
                ++streak;
            } else { streak = 0; streakStart = -1; }
            if (streak >= K) {
                std::vector<float> tw(m.begin() + streakStart, m.begin() + k + 1);
                float target = median(tw);
                int lo = std::max(segStart + 1, streakStart - W / 2 - K);
                int bnd = streakStart;
                for (int q = lo; q <= std::min(k, streakStart + K); ++q)
                    if (std::abs(m[(size_t) q] - target) < std::abs(m[(size_t) q] - center)) { bnd = q; break; }
                emit(fr, a + segStart, a + bnd - 1, out);
                segStart = bnd; streak = 0; streakStart = -1;
            }
        }
        emit(fr, a + segStart, b, out);
    }
};

} // namespace vts
