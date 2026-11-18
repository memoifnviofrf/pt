#pragma once
// VOCAL TUNE STUDIO - PitchShifter (JUCE非依存コア, オフライン用)
// TD-PSOLA。ピッチマーク位置の波形(グレイン)をそのまま再配置するため、
// ピッチ変更時にフォルマント(スペクトル包絡)が原理的に保持される。
// 独立フォルマント操作: グレイン読み出しを formantRatio 倍速で再サンプルする。
// 無声区間は原音をそのまま通す。オーディオスレッドでは呼ばないこと(メモリ確保あり)。
#include "PitchDetector.h"
#include <functional>
#include <vector>
#include <cmath>
#include <algorithm>

namespace vts {

class PitchShifter {
public:
    using RatioFn = std::function<double(double /*timeSec*/)>;

    // 時間写像(サンプル単位)。fwd: 入力位置→出力位置, inv: 出力位置→入力位置。outLen: 出力長。
    struct TimeMap { std::function<double(double)> fwd, inv; long outLen = 0; };

    // pitchRatio(t) / formantRatio(t): tは「入力」時間[秒]。pitchRatio: 出力F0/入力F0 (1.0=変更なし)。formantRatio: 1.0=フォルマント保持。
    // tm指定時は有声区間を時間写像に従って再配置する(ピッチ・フォルマントは保持)。
    // base: 無声区間に使う出力長の信号(省略時は入力そのもの。時間伸縮時はWSOLA等で作った信号を渡す)。
    static std::vector<float> process(const std::vector<float>& x, const std::vector<PitchFrame>& fr,
                                      double sr, RatioFn pitchRatio, RatioFn formantRatio = nullptr,
                                      const TimeMap* tm = nullptr, const std::vector<float>* base = nullptr)
    {
        const long N = (long) x.size();
        const long NO = tm ? tm->outLen : N;
        const std::vector<float>& bs = base ? *base : x;
        if (fr.size() < 2 || N == 0 || (long) bs.size() != NO) return bs.size() == (size_t) NO ? bs : x;
        std::vector<double> acc((size_t) NO, 0.0), mask((size_t) NO, 0.0);
        const double hop = fr[1].timeSec - fr[0].timeSec;
        const int nf = (int) fr.size();
        const int maxGap = 8;

        int i = 0;
        while (i < nf) {
            if (!fr[(size_t) i].voiced) { ++i; continue; }
            int last = i, j = i + 1;
            while (j < nf && j - last <= maxGap) { if (fr[(size_t) j].voiced) last = j; ++j; }
            processRun(x, fr, i, last, hop, sr, pitchRatio, formantRatio, tm, acc, mask);
            i = last + 1;
        }
        std::vector<float> out((size_t) NO);
        for (long n = 0; n < NO; ++n) {
            double m = std::min(1.0, mask[(size_t) n]);
            double v = m * acc[(size_t) n] + (1.0 - m) * bs[(size_t) n];
            out[(size_t) n] = std::isfinite(v) ? (float) v : 0.f;
        }
        return out;
    }

private:
    static double clampd(double v, double a, double b) { return std::min(b, std::max(a, v)); }

    static double readLin(const std::vector<float>& x, double pos) {
        long i0 = (long) std::floor(pos); double fr = pos - (double) i0;
        auto g = [&](long k) { return (k < 0 || k >= (long) x.size()) ? 0.0 : (double) x[(size_t) k]; };
        return g(i0) * (1.0 - fr) + g(i0 + 1) * fr;
    }

    static void processRun(const std::vector<float>& x, const std::vector<PitchFrame>& fr, int fa, int fb,
                           double hop, double sr, const RatioFn& pr, const RatioFn& frm,
                           const TimeMap* tm, std::vector<double>& acc, std::vector<double>& mask)
    {
        const long N = (long) x.size();
        const long NO = (long) acc.size();
        auto fwd = [&](double v) { return tm ? tm->fwd(v) : v; };
        auto inv = [&](double v) { return tm ? tm->inv(v) : v; };
        const int L = fb - fa + 1;
        // 区間内の周期[サンプル]。無声ギャップは最近傍の有声値で保持
        std::vector<double> T((size_t) L, 0.0);
        for (int k = 0; k < L; ++k) if (fr[(size_t) (fa + k)].voiced) T[(size_t) k] = sr / fr[(size_t) (fa + k)].f0Hz;
        double hold = 0; for (int k = 0; k < L; ++k) { if (T[(size_t) k] > 0) hold = T[(size_t) k]; else if (hold > 0) T[(size_t) k] = hold; }
        hold = 0; for (int k = L - 1; k >= 0; --k) { if (T[(size_t) k] > 0) hold = T[(size_t) k]; else T[(size_t) k] = hold; }
        const double t0 = fr[(size_t) fa].timeSec;
        auto periodAt = [&](double s) { // s: サンプル位置
            double u = (s / sr - t0) / hop; u = clampd(u, 0.0, (double) (L - 1));
            int k0 = (int) std::floor(u); int k1 = std::min(L - 1, k0 + 1); double a = u - k0;
            return T[(size_t) k0] * (1 - a) + T[(size_t) k1] * a;
        };
        const double sa = std::max(0.0, (t0 - hop * 0.5) * sr);
        const double sb = std::min((double) N, (fr[(size_t) fb].timeSec + hop * 0.5) * sr);
        if (sb - sa < 2 * periodAt(sa)) return;

        // 極性: 大きい側のピークを追跡
        float mx = 0, mn = 0;
        for (long n = (long) sa; n < (long) sb; ++n) { mx = std::max(mx, x[(size_t) n]); mn = std::min(mn, x[(size_t) n]); }
        const double pol = (-mn > mx) ? -1.0 : 1.0;
        auto xv = [&](long n) { return (n < 0 || n >= N) ? 0.0 : pol * (double) x[(size_t) n]; };
        auto refine = [&](double center, double Tl) {
            long lo = (long) std::floor(center - Tl * 0.25), hi = (long) std::ceil(center + Tl * 0.25);
            long best = (long) std::round(center); double bv = -1e30;
            for (long n = lo; n <= hi; ++n) if (xv(n) > bv) { bv = xv(n); best = n; }
            return (double) best;
        };
        // 解析ピッチマーク(前後へ伸ばす)
        std::vector<double> marks;
        {
            long lo = (long) sa, hi = std::min(N, lo + (long) periodAt(sa) + 1);
            long best = lo; double bv = -1e30;
            for (long n = lo; n < hi; ++n) if (xv(n) > bv) { bv = xv(n); best = n; }
            double m = (double) best; marks.push_back(m);
            while (m < sb + periodAt(sb)) { double Tl = periodAt(m); m = refine(m + Tl, Tl); marks.push_back(m); }
            m = marks.front();
            std::vector<double> back;
            while (m > sa - periodAt(sa)) { double Tl = periodAt(m); m = refine(m - Tl, Tl); back.push_back(m); }
            std::reverse(back.begin(), back.end()); marks.insert(marks.begin(), back.begin(), back.end());
        }
        // 合成: 出力時間軸で進め、解析位置は時間写像の逆写像で決める
        const double oa = fwd(sa), ob = fwd(sb);
        double ts = oa;
        { double r0 = clampd(pr(sa / sr), 0.25, 4.0); ts = oa - periodAt(sa) / r0; }
        while (ts < ob + periodAt(sb)) {
            double ta = clampd(inv(ts), sa, sb);
            double r = clampd(pr(ta / sr), 0.25, 4.0);
            double rf = frm ? clampd(frm(ta / sr), 0.5, 2.0) : 1.0;
            double Tin = periodAt(ta);
            // 最近傍の解析マーク
            auto it = std::lower_bound(marks.begin(), marks.end(), ta);
            double c = (it == marks.end()) ? marks.back() : *it;
            if (it != marks.begin()) { double p = *(it - 1); if (it == marks.end() || std::abs(p - ta) < std::abs(c - ta)) c = p; }
            const double Tc = periodAt(c);
            const int Lg = std::max(2, (int) std::round(Tc));
            const long base = (long) std::floor(ts); const double d = ts - (double) base;
            for (int n = -Lg - 1; n <= Lg + 1; ++n) {
                double u = (double) n - d;                       // 出力サンプル(base+n)のグレイン相対位置
                if (std::abs(u) >= Lg) continue;
                double w = 0.5 * (1.0 + std::cos(M_PI * u / Lg));
                long oi = base + n; if (oi < 0 || oi >= NO) continue;
                acc[(size_t) oi] += w * readLin(x, c + u * rf) / r;
            }
            ts += std::max(2.0, Tin / r);
        }
        // マスク: 区間内=1、端は5msの余弦ランプ
        const long ramp = std::max(8L, (long) (0.005 * sr));
        for (long n = std::max(0L, (long) oa); n < std::min(NO, (long) ob); ++n) {
            double w = 1.0;
            long da = n - (long) oa, db = (long) ob - n;
            if (da < ramp) w = std::min(w, 0.5 * (1 - std::cos(M_PI * da / ramp)));
            if (db < ramp) w = std::min(w, 0.5 * (1 - std::cos(M_PI * db / ramp)));
            mask[(size_t) n] = std::max(mask[(size_t) n], w);
        }
    }
};

} // namespace vts
