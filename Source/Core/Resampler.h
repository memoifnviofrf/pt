#pragma once
// VOCAL TUNE STUDIO - Resampler: 窓付きsinc補間によるサンプルレート変換(JUCE非依存, オフライン用)
#include <vector>
#include <cmath>
#include <algorithm>

namespace vts {

inline std::vector<float> resample(const std::vector<float>& x, double srcSr, double dstSr)
{
    if (x.empty() || srcSr <= 0 || dstSr <= 0 || std::abs(srcSr - dstSr) < 1e-6) return x;
    const double ratio = dstSr / srcSr;                  // 出力/入力
    const double cut = std::min(1.0, ratio) * 0.97;      // 入力ナイキスト比のカットオフ(ダウンサンプル時は折り返し防止)
    const int zc = 24;                                   // 片側ゼロ交差数
    const double W = zc / cut;                           // 入力サンプル単位の片側幅
    const size_t M = (size_t) std::floor((double) x.size() * ratio);
    std::vector<float> y(M);
    for (size_t n = 0; n < M; ++n) {
        const double p = (double) n / ratio;
        const long i0 = (long) std::floor(p) - (long) std::ceil(W) + 1, i1 = (long) std::floor(p) + (long) std::ceil(W);
        double acc = 0, ws = 0;
        for (long i = i0; i <= i1; ++i) {
            const double d = (double) i - p; if (std::abs(d) >= W) continue;
            const double a = M_PI * d * cut, s = std::abs(a) < 1e-12 ? 1.0 : std::sin(a) / a;
            const double h = cut * s * 0.5 * (1.0 + std::cos(M_PI * d / W));
            ws += h; if (i >= 0 && i < (long) x.size()) acc += h * (double) x[(size_t) i];
        }
        y[n] = (float) (ws > 0 ? acc / ws : 0.0);
    }
    return y;
}

} // namespace vts
