#include "../Source/Core/PitchShifter.h"
#include <cstdio>
#include <random>
using namespace vts;

// 既知の包絡(3フォルマント)を持つ調波信号。包絡は基本周波数と独立。
static double env(double f) {
    struct F { double fc, bw, g; } fm[] = { {700, 120, 1.0}, {1250, 150, 0.7}, {2600, 250, 0.4} };
    double a = 0.02; for (auto& q : fm) a += q.g / std::sqrt(1.0 + std::pow((f - q.fc) / q.bw, 2));
    return a;
}
static std::vector<float> synth(double sr, double dur, std::function<double(double)> f0fn) {
    size_t N = (size_t) (sr * dur); std::vector<float> x(N); double ph = 0;
    for (size_t i = 0; i < N; ++i) {
        double f0 = f0fn(i / sr); ph += 2 * M_PI * f0 / sr; double s = 0;
        for (int h = 1; h * f0 < 6000; ++h) s += env(h * f0) * std::cos(h * ph);
        x[i] = (float) (0.15 * s);
    }
    return x;
}
// 定常区間の第h倍音振幅[dB]
static double harmDb(const std::vector<float>& y, double sr, double t0, double t1, double f) {
    long a = (long) (t0 * sr), b = (long) (t1 * sr); double re = 0, im = 0, ws = 0;
    for (long n = a; n < b; ++n) { double w = 0.5 * (1 - std::cos(2 * M_PI * (n - a) / (b - a)));
        re += w * y[(size_t) n] * std::cos(2 * M_PI * f * n / sr); im -= w * y[(size_t) n] * std::sin(2 * M_PI * f * n / sr); ws += w; }
    return 20 * std::log10(2 * std::sqrt(re * re + im * im) / ws + 1e-12);
}
static double envErrDb(const std::vector<float>& y, double sr, double f0out, double refRatio) {
    std::vector<double> m, r;
    for (int h = 1; h * f0out < 3500; ++h) { m.push_back(harmDb(y, sr, 0.4, 1.4, h * f0out)); r.push_back(20 * std::log10(env(h * f0out / refRatio))); }
    double am = 0, ar = 0; for (size_t i = 0; i < m.size(); ++i) { am += m[i]; ar += r[i]; } am /= m.size(); ar /= m.size();
    double e = 0; for (size_t i = 0; i < m.size(); ++i) { double d = (m[i] - am) - (r[i] - ar); e += d * d; }
    return std::sqrt(e / m.size());
}

int main()
{
    const double sr = 48000; int fails = 0;
    PitchDetectorConfig pc; pc.sampleRate = sr;

    // 1) 定常音 220Hz を +3半音 / -4半音 : ピッチ精度とフォルマント保持
    for (double semis : { 3.0, -4.0 }) {
        auto x = synth(sr, 1.8, [](double) { return 220.0; });
        auto fr = PitchDetector(pc).analyse(x);
        double r = std::pow(2.0, semis / 12.0);
        auto y = PitchShifter::process(x, fr, sr, [&](double) { return r; });
        auto fo = PitchDetector(pc).analyse(y);
        double se = 0; int c = 0;
        for (auto& f : fo) if (f.timeSec > 0.2 && f.timeSec < 1.6) { if (!f.voiced) { se += 1e6; ++c; continue; }
            double cents = 1200 * std::log2(f.f0Hz / (220.0 * r)); se += cents * cents; ++c; }
        double rms = std::sqrt(se / c);
        double ePres = envErrDb(y, sr, 220.0 * r, 1.0), eMove = envErrDb(y, sr, 220.0 * r, r);
        bool ok = rms < 5.0 && ePres < 3.0 && eMove > ePres * 1.5;
        printf("shift %+.0f semis: pitch RMS err %.2f c | envelope err (preserved) %.2f dB, (if moved) %.2f dB  %s\n", semis, rms, ePres, eMove, ok ? "PASS" : "FAIL");
        fails += !ok;
    }
    // 2) 独立フォルマント操作: ピッチ不変, フォルマント+20%
    {
        auto x = synth(sr, 1.8, [](double) { return 220.0; });
        auto fr = PitchDetector(pc).analyse(x);
        auto y = PitchShifter::process(x, fr, sr, [](double) { return 1.0; }, [](double) { return 1.2; });
        double eRef = envErrDb(y, sr, 220.0, 1.2), eNo = envErrDb(y, sr, 220.0, 1.0);
        bool ok = eRef < 3.0 && eNo > eRef * 1.5;
        printf("formant x1.2 (pitch fixed): err vs shifted env %.2f dB, vs original env %.2f dB  %s\n", eRef, eNo, ok ? "PASS" : "FAIL"); fails += !ok;
    }
    // 3) グライド補正: 比率が0→+400centへ連続変化
    {
        auto x = synth(sr, 1.8, [](double) { return 220.0; });
        auto fr = PitchDetector(pc).analyse(x);
        auto rf = [](double t) { return std::pow(2.0, (400.0 * std::clamp(t / 1.8, 0.0, 1.0)) / 1200.0); };
        auto y = PitchShifter::process(x, fr, sr, rf);
        auto fo = PitchDetector(pc).analyse(y); double se = 0; int c = 0;
        for (auto& f : fo) if (f.timeSec > 0.2 && f.timeSec < 1.6 && f.voiced) { double cents = 1200 * std::log2(f.f0Hz / (220.0 * rf(f.timeSec))); se += cents * cents; ++c; }
        double rms = std::sqrt(se / std::max(1, c)); bool ok = c > 100 && rms < 10.0;
        printf("glide 0->+400c: pitch RMS err %.2f c (%d frames)  %s\n", rms, c, ok ? "PASS" : "FAIL"); fails += !ok;
    }
    // 4) 無声(ノイズ)区間の素通し + 有限値チェック
    {
        std::mt19937 rng(3); std::normal_distribution<double> g(0, 1);
        auto a = synth(sr, 0.6, [](double) { return 200.0; });
        std::vector<float> x = a; for (int i = 0; i < (int) (0.3 * sr); ++i) x.push_back((float) (0.05 * g(rng)));
        auto b = synth(sr, 0.6, [](double) { return 200.0; }); x.insert(x.end(), b.begin(), b.end());
        auto fr = PitchDetector(pc).analyse(x);
        auto y = PitchShifter::process(x, fr, sr, [](double) { return 1.26; });
        bool finite = true; for (float v : y) finite &= std::isfinite(v);
        double md = 0; for (long n = (long) (0.65 * sr); n < (long) (0.85 * sr); ++n) md = std::max(md, (double) std::abs(y[(size_t) n] - x[(size_t) n]));
        bool ok = finite && md < 1e-6 && y.size() == x.size();
        printf("noise passthrough max diff %.2e, finite=%d, length kept=%d  %s\n", md, (int) finite, (int) (y.size() == x.size()), ok ? "PASS" : "FAIL"); fails += !ok;
    }
    return fails;
}
