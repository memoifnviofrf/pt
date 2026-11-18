#pragma once
// VOCAL TUNE STUDIO - PitchDetector (JUCE非依存コア)
// YIN(累積平均正規化差分) + 放物線補間 + Viterbi風オクターブ連続性トラッキング
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>

namespace vts {

struct PitchFrame {
    double timeSec = 0.0;
    float  f0Hz = 0.0f;        // 0 = 無声/検出なし
    float  midi = 0.0f;        // 連続MIDIノート値(セント精度)
    float  confidence = 0.0f;  // 0..1 (1 - YIN最小値)
    bool   voiced = false;
};

struct PitchDetectorConfig {
    double sampleRate = 48000.0;
    float  minHz = 70.0f;      // ユーザー指定の解析音域
    float  maxHz = 1200.0f;
    int    windowSize = 2048;  // 解析窓(minHzの2周期以上)
    int    hopSize = 256;
    float  yinThreshold = 0.15f;
    float  voicedConfidence = 0.45f;
    float  minRms = 1.0e-3f;   // 無音ゲート
};

class PitchDetector {
public:
    explicit PitchDetector(const PitchDetectorConfig& c) : cfg(c) {}

    // オフライン解析(バックグラウンドスレッド用)。オーディオスレッドでは呼ばないこと。
    std::vector<PitchFrame> analyse(const std::vector<float>& x) const
    {
        std::vector<PitchFrame> out;
        const int W = cfg.windowSize, H = cfg.hopSize;
        const int tauMax = std::min(W / 2 - 1, (int) (cfg.sampleRate / cfg.minHz));
        const int tauMin = std::max(2, (int) (cfg.sampleRate / cfg.maxHz));
        std::vector<float> d((size_t) tauMax + 2), cmnd((size_t) tauMax + 2);

        struct Cand { float f0; float conf; };
        std::vector<std::vector<Cand>> cands;

        for (size_t pos = 0; pos + (size_t) W <= x.size(); pos += (size_t) H) {
            PitchFrame f;
            f.timeSec = (pos + W * 0.5) / cfg.sampleRate;
            out.push_back(f);
            std::vector<Cand> cs;
            double e = 0; for (int i = 0; i < W; ++i) e += (double) x[pos+i]*x[pos+i];
            if (std::sqrt(e / W) >= cfg.minRms) {
                const int N = W / 2;
                for (int t = 1; t <= tauMax; ++t) {
                    double s = 0;
                    for (int i = 0; i < N; ++i) { double v = x[pos+i] - x[pos+i+t]; s += v*v; }
                    d[(size_t) t] = (float) s;
                }
                cmnd[0] = 1.f; double run = 0;
                for (int t = 1; t <= tauMax; ++t) {
                    run += d[(size_t) t];
                    cmnd[(size_t) t] = run > 0 ? (float) (d[(size_t) t] * t / run) : 1.f;
                }
                // 局所最小を候補として収集(オクターブ誤り対策のため複数保持)
                for (int t = tauMin; t < tauMax; ++t) {
                    if (cmnd[(size_t) t] < cmnd[(size_t) t-1] && cmnd[(size_t) t] <= cmnd[(size_t) t+1]
                        && cmnd[(size_t) t] < 0.5f) {
                        float a = cmnd[(size_t) t-1], b = cmnd[(size_t) t], c = cmnd[(size_t) t+1];
                        float den = a - 2*b + c;
                        float off = std::abs(den) > 1e-9f ? 0.5f * (a - c) / den : 0.f;
                        float tau = (float) t + std::clamp(off, -1.f, 1.f);
                        cs.push_back({ (float) (cfg.sampleRate / tau), 1.f - b });
                    }
                }
                // サブハーモニクス抑制: 高いf0候補が十分な信頼度を持つ場合、その整数分の1の候補は除去
                std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b){ return a.f0 > b.f0; });
                std::vector<Cand> kept;
                for (auto& c : cs) {
                    bool sub = false;
                    for (auto& k : kept) {
                        float r = k.f0 / c.f0;
                        if (r > 1.8f && std::abs(r - std::round(r)) < 0.06f && k.conf >= c.conf - 0.15f && k.conf > 0.5f) { sub = true; break; }
                    }
                    if (!sub) kept.push_back(c);
                }
                cs = std::move(kept);
            }
            cands.push_back(std::move(cs));
        }
        if (out.empty()) return out;

        // Viterbi: 状態=各候補 + 無声。遷移コスト=半音差(連続性)でオクターブ飛びを抑制
        const size_t T = out.size();
        const float UV = 0.0f;
        struct Node { float cost; int prev; };
        std::vector<std::vector<Node>> dp(T);
        auto obs = [&](const Cand& c) {
            // 低tauほど(高いf0ほど)僅かに不利→サブハーモニクスではなく基本波を選びやすくする弱いバイアスは付けない
            return (1.f - c.conf) * 2.f + (c.conf < cfg.voicedConfidence ? 1.f : 0.f);
        };
        for (size_t t = 0; t < T; ++t) {
            size_t n = cands[t].size() + 1;
            dp[t].assign(n, { 1e9f, -1 });
            for (size_t j = 0; j < n; ++j) {
                float o = (j == cands[t].size()) ? 0.9f + (cands[t].empty() ? 0.f : 0.f) : obs(cands[t][j]);
                if (t == 0) { dp[t][j] = { o, -1 }; continue; }
                for (size_t i = 0; i < dp[t-1].size(); ++i) {
                    float tr;
                    bool iuv = (i == cands[t-1].size()), juv = (j == cands[t].size());
                    if (iuv && juv) tr = 0.f;
                    else if (iuv || juv) tr = 0.3f;
                    else tr = 0.15f * std::abs(12.f * std::log2(cands[t][j].f0 / cands[t-1][i].f0));
                    float c = dp[t-1][i].cost + tr + o;
                    if (c < dp[t][j].cost) dp[t][j] = { c, (int) i };
                }
            }
        }
        int best = 0; float bc = 1e9f;
        for (size_t j = 0; j < dp[T-1].size(); ++j) if (dp[T-1][j].cost < bc) { bc = dp[T-1][j].cost; best = (int) j; }
        for (size_t t = T; t-- > 0;) {
            if ((size_t) best < cands[t].size()) {
                auto& c = cands[t][(size_t) best];
                out[t].f0Hz = c.f0; out[t].confidence = c.conf; out[t].voiced = true;
                out[t].midi = 69.f + 12.f * std::log2(c.f0 / 440.f);
            }
            best = dp[t][(size_t) best].prev;
            if (best < 0 && t > 0) best = 0;
        }
        (void) UV;
        return out;
    }
private:
    PitchDetectorConfig cfg;
};

} // namespace vts
