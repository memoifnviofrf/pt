#pragma once
// VOCAL TUNE STUDIO - VibratoAnalyzer: ビブラートの解析と編集(JUCE非依存コア)
// 入力は「速い変動成分」mod[半音]、フレーム間隔hop[秒]。
#include <vector>
#include <cmath>
#include <algorithm>

namespace vts {

struct VibratoInfo {
    bool   detected = false;
    double rateHz = 0, depthSemis = 0;   // depth = 振幅(ピーク)
    double onsetSec = 0;                 // 音符先頭からのビブラート立ち上がり(包絡が深さの50%に達する時刻)
    double regularity = 0;               // 自己相関ピーク(0..1)
    double phaseAtMid = 0;               // 中央時刻での位相[rad], mod ≈ A cos(phase)
    std::vector<float> env;              // フレームごとの振幅包絡[半音]
};

struct VibratoParams {
    float depth = 1.0f;       // 深さ倍率(1=元)
    float rate = 1.0f;        // 速さ倍率(1=元)
    float onsetShiftSec = 0;  // 立ち上がりの時間シフト(+で遅らせる, -で早める)
    float amount = 1.0f;      // ビブラート全体の残す量(0=除去)
    float smoothing = 0.0f;   // 0..1 波形のなだらかさ
    bool  active() const { return depth != 1.f || rate != 1.f || onsetShiftSec != 0.f || amount != 1.f || smoothing != 0.f; }
};

class VibratoAnalyzer {
public:
    static VibratoInfo analyse(const std::vector<float>& mod, double hop)
    {
        VibratoInfo v; const int L = (int) mod.size(); v.env.assign((size_t) L, 0.f);
        if (L * hop < 0.35) return v;
        // 3..9Hz の範囲で正規化自己相関ピークを探す
        const int lagLo = std::max(2, (int) std::floor(1.0 / (9.0 * hop))), lagHi = std::min(L / 2, (int) std::ceil(1.0 / (3.0 * hop)));
        if (lagHi <= lagLo + 1) return v;
        double e0 = 0; for (float q : mod) e0 += (double) q * q; if (e0 <= 1e-12) return v;
        std::vector<double> ac((size_t) lagHi + 2, 0.0);
        for (int lag = lagLo - 1; lag <= lagHi + 1; ++lag) {
            double num = 0, e1 = 0, e2 = 0;
            for (int i = 0; i + lag < L; ++i) { num += (double) mod[(size_t) i] * mod[(size_t) (i + lag)]; e1 += (double) mod[(size_t) i] * mod[(size_t) i]; e2 += (double) mod[(size_t) (i + lag)] * mod[(size_t) (i + lag)]; }
            ac[(size_t) lag] = num / std::sqrt(e1 * e2 + 1e-12);
        }
        int bl = lagLo; for (int lag = lagLo; lag <= lagHi; ++lag) if (ac[(size_t) lag] > ac[(size_t) bl]) bl = lag;
        double a = ac[(size_t) bl - 1], b = ac[(size_t) bl], c = ac[(size_t) bl + 1], den = a - 2 * b + c;
        double off = std::abs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
        double lag = bl + std::max(-1.0, std::min(1.0, off));
        v.rateHz = 1.0 / (lag * hop); v.regularity = b;
        // 振幅包絡: 1周期窓のRMS*sqrt2(中心窓)
        const int P = std::max(3, (int) std::round(lag)) | 1;
        for (int k = 0; k < L; ++k) {
            double s = 0; int n = 0; for (int d = -P / 2; d <= P / 2; ++d) { int q = k + d; if (q < 0 || q >= L) continue; s += (double) mod[(size_t) q] * mod[(size_t) q]; ++n; }
            v.env[(size_t) k] = (float) (std::sqrt(s / n) * std::sqrt(2.0));
        }
        std::vector<float> sorted = v.env; std::sort(sorted.begin(), sorted.end());
        v.depthSemis = sorted[(size_t) ((double) (L - 1) * 0.8)];
        for (int k = 0; k < L; ++k) if (v.env[(size_t) k] >= 0.5 * v.depthSemis) { v.onsetSec = k * hop; break; }
        // 位相: 推定レートでの最小二乗フィット(定常部=包絡が深さの70%以上のフレーム)
        double cs = 0, sn = 0; const double w = 2 * M_PI * v.rateHz * hop; const int mid = L / 2;
        for (int k = 0; k < L; ++k) if (v.env[(size_t) k] >= 0.7 * v.depthSemis) { cs += mod[(size_t) k] * std::cos(w * (k - mid)); sn += mod[(size_t) k] * std::sin(w * (k - mid)); }
        v.phaseAtMid = std::atan2(-sn, cs);
        v.detected = v.regularity > 0.5 && v.depthSemis > 0.15 && v.rateHz >= 3.5 && v.rateHz <= 9.0;
        return v;
    }

    // 編集後のモジュレーション成分を返す(検出されなかった場合は元のまま)。
    static std::vector<float> edit(const std::vector<float>& mod, const VibratoInfo& v, const VibratoParams& p, double hop)
    {
        if (!v.detected || !p.active()) return mod;
        const int L = (int) mod.size(); std::vector<float> out((size_t) L);
        const bool resynth = p.rate != 1.f || p.onsetShiftSec != 0.f;
        const double g0 = (double) p.depth * p.amount;
        if (!resynth) {
            for (int k = 0; k < L; ++k) out[(size_t) k] = (float) (g0 * mod[(size_t) k]);
        } else {
            // 解析した包絡(時間シフト)×正弦で再合成。位相は中央で元と一致させる
            const double r2 = v.rateHz * p.rate, w = 2 * M_PI * r2 * hop; const int mid = L / 2;
            const double sh = p.onsetShiftSec / hop;
            for (int k = 0; k < L; ++k) {
                double q = k - sh; int q0 = (int) std::floor(q); double fr = q - q0;
                auto E = [&](int i) { return (double) v.env[(size_t) std::max(0, std::min(L - 1, i))]; };
                double env = (q < 0) ? 0.0 : E(q0) * (1 - fr) + E(q0 + 1) * fr;
                out[(size_t) k] = (float) (g0 * env * std::cos(v.phaseAtMid + w * (k - mid)));
            }
        }
        if (p.smoothing > 0.f) {
            const int Wd = std::max(1, (int) std::round(p.smoothing * 0.5 / (v.rateHz * hop) * 0.5)) | 1;
            std::vector<float> t = out;
            for (int k = 0; k < L; ++k) { double s = 0; int n = 0; for (int d = -Wd / 2; d <= Wd / 2; ++d) { int q = k + d; if (q < 0 || q >= L) continue; s += t[(size_t) q]; ++n; } out[(size_t) k] = (float) (s / n); }
        }
        return out;
    }
};

} // namespace vts
