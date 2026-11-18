#pragma once
// VOCAL TUNE STUDIO - PitchEditor: 音符ごとの非破壊ピッチ編集データモデルと補正量計算。
// 元の解析結果(PitchFrame / Note)は変更せず、編集は EditNote のみに保持する。
// 補正の分解:  元ピッチ m(t) = 中心 c + ドリフト(ゆっくりした変動) + モジュレーション(ビブラート等の速い変動)
//   出力 = c + Center*(T-c) + (1-Drift)*drift + (1-Modulation)*mod
//   Center/Drift/Modulation はいずれも「補正量」(0%=元のまま, 100%=完全補正)。
#include "ScaleDetector.h"
#include "PitchShifter.h"
#include "VibratoAnalyzer.h"
#include "TimeStretchProcessor.h"
#include <vector>

namespace vts {

struct EditSettings {
    float pitchCenter = 1.0f;      // 0..1  音符中心を目標へ寄せる量
    float pitchDrift = 0.0f;       // 0..1  ゆっくりしたドリフトの除去量
    float pitchModulation = 0.0f;  // 0..1  速い変動(ビブラート含む)の除去量
    float noteTransition = 0.5f;   // 0..1  音符間の補正量の遷移時間(0=約5ms, 1=約120ms)
    VibratoParams vibrato;         // ビブラート編集(既定値=元のまま)
    ScaleType scale = ScaleType::Chromatic;
    int root = 0;
    unsigned customMask = 0xFFF;
};

struct EditNote {
    Note note;
    double targetMidi = 0;      // 補正後の中心音高
    bool manualTarget = false;  // trueなら自動スケール補正で上書きしない
    double newStartSec = -1, newEndSec = -1;  // タイミング編集後の位置(負=未編集)
};

class PitchEditor {
public:
    EditSettings settings;

    void setNotes(const std::vector<Note>& ns) {
        notes.clear(); for (auto& n : ns) { EditNote e; e.note = n; e.targetMidi = n.medianMidi; notes.push_back(e); }
        undoStack.clear(); redoStack.clear(); autoTargets(false);
    }
    const std::vector<EditNote>& getNotes() const { return notes; }
    // プロジェクト復元用: 保存済みの編集状態をそのまま設定(Undo履歴は消える)
    void setEditNotes(const std::vector<EditNote>& ns) { notes = ns; undoStack.clear(); redoStack.clear(); }

    // スケール/クロマチックへ自動量子化(手動指定済みは保持)
    void autoTargets(bool recordUndo = true) {
        if (recordUndo) pushUndo();
        auto mask = ScaleDetector::scaleMask(settings.scale, settings.root, settings.customMask);
        for (auto& e : notes) if (!e.manualTarget) e.targetMidi = ScaleDetector::nearestScaleNote(e.note.medianMidi, mask);
    }
    void setManualTarget(size_t idx, double midi) {
        if (idx >= notes.size()) return;
        pushUndo(); notes[idx].targetMidi = midi; notes[idx].manualTarget = true;
    }
    void resetNote(size_t idx) {
        if (idx >= notes.size()) return;
        pushUndo(); notes[idx].manualTarget = false; autoTargetsNoUndo();
    }
    bool undo() { if (undoStack.empty()) return false; redoStack.push_back(notes); notes = undoStack.back(); undoStack.pop_back(); return true; }
    bool redo() { if (redoStack.empty()) return false; undoStack.push_back(notes); notes = redoStack.back(); redoStack.pop_back(); return true; }

    // フレームごとの補正量[半音]。frames.size()と同じ長さ。ノート外は0。
    std::vector<float> computeOffsets(const std::vector<PitchFrame>& fr, double slowWindowSec = 0.20) const
    {
        const int nf = (int) fr.size(); std::vector<float> off((size_t) nf, 0.f);
        if (nf < 2) return off;
        const double hop = fr[1].timeSec - fr[0].timeSec;
        const int W = std::max(3, (int) std::round(slowWindowSec / hop)) | 1;
        std::vector<std::vector<float>> perNote;
        for (auto& e : notes) {
            const int a = e.note.firstFrame, b = e.note.lastFrame, L = b - a + 1;
            std::vector<double> m((size_t) L); double hold = e.note.medianMidi;
            for (int k = 0; k < L; ++k) { const auto& f = fr[(size_t) (a + k)]; if (f.voiced) hold = f.midi; m[(size_t) k] = hold; }
            const double c = e.note.medianMidi, T = e.targetMidi;
            std::vector<float> o((size_t) L), slowv((size_t) L), modv((size_t) L);
            for (int k = 0; k < L; ++k) {
                double acc = 0; int cnt = 0;
                for (int d = -W / 2; d <= W / 2; ++d) { int q = k + d; if (q < 0 || q >= L) continue; acc += m[(size_t) q]; ++cnt; }
                slowv[(size_t) k] = (float) (acc / cnt); modv[(size_t) k] = (float) (m[(size_t) k] - acc / cnt);
            }
            if (settings.vibrato.active()) {   // ビブラート編集: 速い変動成分を解析・再構成
                auto info = VibratoAnalyzer::analyse(modv, hop);
                modv = VibratoAnalyzer::edit(modv, info, settings.vibrato, hop);
                // ここで編集済みのビブラートが「元の変動」として扱われるため、Modulation量は別途適用される
            }
            for (int k = 0; k < L; ++k) {
                double drift = slowv[(size_t) k] - c, mod = modv[(size_t) k];
                double out = c + settings.pitchCenter * (T - c) + (1 - settings.pitchDrift) * drift + (1 - settings.pitchModulation) * mod;
                o[(size_t) k] = (float) (out - m[(size_t) k]);
                off[(size_t) (a + k)] = o[(size_t) k];
            }
            perNote.push_back(std::move(o));
        }
        // 隣接音符の境界で補正量をクロスフェード(補正が階段状にならないように)
        const double twSec = 0.005 + 0.115 * settings.noteTransition;
        const int h = std::max(1, (int) std::round(twSec / hop / 2));
        for (size_t i = 0; i + 1 < notes.size(); ++i) {
            const int la = notes[i].note.lastFrame, fb = notes[i + 1].note.firstFrame;
            if (fb - la > 3) continue;  // 間が空いている=別フレーズ
            const float oA = perNote[i].back(), oB = perNote[i + 1].front();
            const int mid = (la + fb + 1) / 2;
            for (int j = std::max(notes[i].note.firstFrame, mid - h); j < std::min(nf, mid + h); ++j) {
                if (j > notes[i + 1].note.lastFrame) break;
                double w = 0.5 * (1 - std::cos(M_PI * (j - (mid - h) + 0.5) / (2.0 * h)));
                off[(size_t) j] = (float) ((1 - w) * oA + w * oB);
            }
        }
        return off;
    }

    // PitchShifter用の時間→ピッチ比関数
    static PitchShifter::RatioFn makeRatioFn(const std::vector<PitchFrame>& fr, std::vector<float> off) {
        return [&fr, off = std::move(off)](double t) {
            if (fr.size() < 2) return 1.0;
            const double hop = fr[1].timeSec - fr[0].timeSec;
            double u = (t - fr[0].timeSec) / hop; if (u <= 0 || u >= (double) (fr.size() - 1)) return 1.0;
            int k = (int) u; double a = u - k; double s = off[(size_t) k] * (1 - a) + off[(size_t) k + 1] * a;
            return std::pow(2.0, s / 12.0);
        };
    }

    // ---- 音符操作(UI用。いずれもUndo 1ステップ) ----
    // 複数音符を半音単位で移動(手動ターゲットになる)
    void transposeNotes(const std::vector<size_t>& idx, double deltaSemis) {
        if (idx.empty()) return;
        pushUndo();
        for (size_t i : idx) if (i < notes.size()) { notes[i].targetMidi += deltaSemis; notes[i].manualTarget = true; }
    }
    // 複数音符を時間方向へ移動
    void moveNotes(const std::vector<size_t>& idx, double deltaSec) {
        if (idx.empty()) return;
        pushUndo();
        for (size_t i : idx) if (i < notes.size()) {
            auto& e = notes[i]; double s0 = e.newStartSec >= 0 ? e.newStartSec : e.note.startSec, e1 = e.newEndSec >= 0 ? e.newEndSec : e.note.endSec;
            e.newStartSec = std::max(0.0, s0 + deltaSec); e.newEndSec = e.newStartSec + (e1 - s0);
        }
    }
    // 音符の分割。tSecは入力(元音源)時間。成功でtrue
    bool splitNote(size_t idx, double tSec, const std::vector<PitchFrame>& fr) {
        if (idx >= notes.size()) return false;
        std::vector<Note> tmp(1, notes[idx].note);
        NoteSegmenter::splitAt(tmp, 0, tSec, fr); if (tmp.size() != 2) return false;
        pushUndo(); EditNote a = notes[idx], b = a; a.note = tmp[0]; b.note = tmp[1];
        if (a.newStartSec >= 0) {   // 編集済みなら分割点の出力位置を線形に決める
            double u = (tSec - notes[idx].note.startSec) / (notes[idx].note.endSec - notes[idx].note.startSec);
            double mid = a.newStartSec + u * (a.newEndSec - a.newStartSec); a.newEndSec = mid; b.newStartSec = mid;
        }
        notes[idx] = a; notes.insert(notes.begin() + (long) idx + 1, b); return true;
    }
    // idx と idx+1 を結合。結合後の目標音高は自動(手動指定なし)に戻る
    bool mergeNotes(size_t idx, const std::vector<PitchFrame>& fr) {
        if (idx + 1 >= notes.size()) return false;
        std::vector<Note> tmp; tmp.reserve(2); tmp.push_back(notes[idx].note); tmp.push_back(notes[idx + 1].note);
        NoteSegmenter::mergeWithNext(tmp, 0, fr); if (tmp.size() != 1) return false;
        pushUndo(); EditNote m = notes[idx]; const EditNote& n = notes[idx + 1]; m.note = tmp[0]; m.manualTarget = false;
        if (m.newStartSec >= 0 || n.newStartSec >= 0) {
            m.newStartSec = m.newStartSec >= 0 ? m.newStartSec : notes[idx].note.startSec;
            m.newEndSec = n.newEndSec >= 0 ? n.newEndSec : n.note.endSec;
        }
        notes[idx] = m; notes.erase(notes.begin() + (long) idx + 1); autoTargetsNoUndo(); return true;
    }

    // ---- タイミング編集 ----
    void setNoteTiming(size_t idx, double newStart, double newEnd) {
        if (idx >= notes.size() || newEnd <= newStart) return;
        pushUndo(); notes[idx].newStartSec = newStart; notes[idx].newEndSec = newEnd;
    }
    // 開始位置をグリッドへ寄せる(長さは維持)。amount: 0..1
    void quantizeTiming(double gridSec, float amount) {
        if (gridSec <= 0) return;
        pushUndo();
        for (auto& e : notes) {
            double s0 = e.newStartSec >= 0 ? e.newStartSec : e.note.startSec, e1 = e.newEndSec >= 0 ? e.newEndSec : e.note.endSec;
            double snapped = std::round(s0 / gridSec) * gridSec, ns = s0 + amount * (snapped - s0);
            e.newStartSec = ns; e.newEndSec = ns + (e1 - s0);
        }
    }
    // TimeStretchProcessor用アンカー(入力時間→出力時間)。未編集の音符も恒等アンカーを置き、
    // 編集した音符の前後(音符間のブレス等)だけが伸縮するようにする。
    std::vector<TimeStretchProcessor::Anchor> buildAnchors() const {
        std::vector<TimeStretchProcessor::Anchor> a;
        for (auto& e : notes) {
            const bool ed = e.newStartSec >= 0;
            a.push_back({ e.note.startSec, ed ? e.newStartSec : e.note.startSec });
            a.push_back({ e.note.endSec, ed ? e.newEndSec : e.note.endSec });
        }
        return a;
    }

private:
    std::vector<EditNote> notes;
    std::vector<std::vector<EditNote>> undoStack, redoStack;
    void pushUndo() { undoStack.push_back(notes); redoStack.clear(); }
    void autoTargetsNoUndo() {
        auto mask = ScaleDetector::scaleMask(settings.scale, settings.root, settings.customMask);
        for (auto& e : notes) if (!e.manualTarget) e.targetMidi = ScaleDetector::nearestScaleNote(e.note.medianMidi, mask);
    }
};

} // namespace vts
