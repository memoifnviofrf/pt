#pragma once
// VOCAL TUNE STUDIO - TimeStretchProcessor (JUCE非依存コア, オフライン用)
// 有声区間: PitchShifter(TD-PSOLA)の時間写像モード → ピッチ・フォルマントを保ったまま伸縮
// その他(子音/ブレス/無音): WSOLA
// アンカー(入力時間→出力時間)の区分線形写像でノート単位のタイミング編集を行う。
#include "PitchShifter.h"
#include <vector>
#include <utility>

namespace vts {

class TimeStretchProcessor {
public:
    struct Anchor { double inSec, outSec; };

    // anchors: 単調増加であること(違反するものは無視)。先頭(0,0)と末尾は自動補完。
    // pitchRatio/formantRatio は入力時間基準(省略可)。
    static std::vector<float> process(const std::vector<float>& x, const std::vector<PitchFrame>& fr, double sr,
                                      std::vector<Anchor> anchors,
                                      PitchShifter::RatioFn pitchRatio = nullptr, PitchShifter::RatioFn formantRatio = nullptr)
    {
        const double Lin = (double) x.size() / sr;
        std::vector<Anchor> a; a.push_back({0, 0});
        std::sort(anchors.begin(), anchors.end(), [](const Anchor& p, const Anchor& q) { return p.inSec < q.inSec; });
        for (auto& q : anchors)
            if (q.inSec > a.back().inSec + 1e-4 && q.outSec > a.back().outSec + 1e-4 && q.inSec < Lin) a.push_back(q);
        // 末尾: 最後のアンカー以降は傾き1で続く
        a.push_back({ Lin, a.back().outSec + (Lin - a.back().inSec) });
        const long outLen = std::max(1L, (long) std::llround(a.back().outSec * sr));

        auto fwd = [a, sr](double s) { double t = s / sr; return segMap(a, t, true) * sr; };
        auto inv = [a, sr](double s) { double t = s / sr; return segMap(a, t, false) * sr; };

        std::vector<float> base = wsola(x, sr, outLen, inv);
        if (!pitchRatio) pitchRatio = [](double) { return 1.0; };
        PitchShifter::TimeMap tm; tm.fwd = fwd; tm.inv = inv; tm.outLen = outLen;
        return PitchShifter::process(x, fr, sr, pitchRatio, formantRatio, &tm, &base);
    }

private:
    static double segMap(const std::vector<Anchor>& a, double t, bool forward) {
        auto in = [&](size_t i) { return forward ? a[i].inSec : a[i].outSec; };
        auto out = [&](size_t i) { return forward ? a[i].outSec : a[i].inSec; };
        if (t <= in(0)) return out(0) + (t - in(0));
        size_t k = 1; while (k + 1 < a.size() && t > in(k)) ++k;
        if (t >= in(a.size() - 1)) return out(a.size() - 1) + (t - in(a.size() - 1));
        double u = (t - in(k - 1)) / (in(k) - in(k - 1)); return out(k - 1) + u * (out(k) - out(k - 1));
    }

    // WSOLA: 出力フレームごとに、写像された入力位置付近で直前フレームの「自然な続き」と最も相関する位置を選ぶ
    static std::vector<float> wsola(const std::vector<float>& x, double sr, long outLen, const std::function<double(double)>& inv)
    {
        const long N = (long) x.size();
        const int G = (int) (std::round(0.025 * sr) / 2) * 2;   // グレイン長 ~25ms
        const int Hs = G / 2, delta = (int) (0.010 * sr);       // 合成ホップ, 探索幅 ±10ms
        std::vector<double> out((size_t) outLen + (size_t) G, 0.0);
        std::vector<double> win((size_t) G); for (int i = 0; i < G; ++i) win[(size_t) i] = 0.5 * (1 - std::cos(2 * M_PI * (i + 0.5) / G));
        auto at = [&](long i) { return (i < 0 || i >= N) ? 0.0 : (double) x[(size_t) i]; };
        long prevPos = -1;
        for (long m = 0; m * Hs < outLen + Hs; ++m) {
            long outStart = m * Hs - Hs;   // グレインは中心がm*Hsになるよう配置
            double center = inv((double) (outStart + G / 2));
            long target = (long) std::llround(center) - G / 2, pos = target;
            if (prevPos >= 0) {
                double best = -1e30; const long ref = prevPos + Hs;   // 直前グレインの続き
                for (long c = target - delta; c <= target + delta; c += 2) {
                    double num = 0, e1 = 0, e2 = 0;
                    for (int i = 0; i < Hs; i += 2) { double p = at(ref + i), q = at(c + i); num += p * q; e1 += p * p; e2 += q * q; }
                    double sc = num / std::sqrt(e1 * e2 + 1e-12);
                    if (sc > best) { best = sc; pos = c; }
                }
            }
            for (int i = 0; i < G; ++i) { long oi = outStart + i; if (oi < 0 || oi >= outLen) continue; out[(size_t) oi] += win[(size_t) i] * at(pos + i); }
            prevPos = pos;
        }
        std::vector<float> y((size_t) outLen); for (long i = 0; i < outLen; ++i) y[(size_t) i] = (float) out[(size_t) i];
        return y;
    }
};

} // namespace vts
