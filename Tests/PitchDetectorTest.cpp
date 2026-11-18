#include "../Source/Core/PitchDetector.h"
#include <cstdio>
#include <random>
using namespace vts;

static std::vector<float> synth(double sr, double dur, double f0, double vibHz, double vibCents, double breath, unsigned seed)
{
    std::mt19937 rng(seed); std::normal_distribution<double> n(0, 1);
    size_t N = (size_t) (sr * dur); std::vector<float> x(N); double ph = 0;
    for (size_t i = 0; i < N; ++i) {
        double t = i / sr;
        double f = f0 * std::pow(2.0, (vibCents * std::sin(2*M_PI*vibHz*t)) / 1200.0);
        ph += 2*M_PI*f/sr; double s = 0;
        for (int h = 1; h <= 12; ++h) s += std::sin(h*ph) / h;   // 倍音豊富(基本波が弱いケースも後で追加)
        x[i] = (float) (0.3*s + breath*n(rng));
    }
    return x;
}

int main()
{
    const double sr = 48000; int fails = 0;
    struct Case { const char* name; double f0, vibHz, vibCents, breath; };
    Case cases[] = { {"sustained A3", 220, 0, 0, 0.0}, {"vibrato C5 ±40c 5.5Hz", 523.25, 5.5, 40, 0.0},
                     {"low E2", 82.41, 0, 0, 0.0}, {"high A5 breathy", 880, 0, 0, 0.05} };
    for (auto& c : cases) {
        auto x = synth(sr, 1.5, c.f0, c.vibHz, c.vibCents, c.breath, 1);
        PitchDetectorConfig cfg; cfg.sampleRate = sr;
        auto fr = PitchDetector(cfg).analyse(x);
        int voiced = 0, oct = 0; double se = 0; int cnt = 0;
        for (auto& f : fr) {
            if (f.timeSec < 0.1 || f.timeSec > 1.4) continue;
            ++cnt;
            if (!f.voiced) continue;
            ++voiced;
            double tt = f.timeSec;
            double truth = c.f0 * std::pow(2.0, (c.vibCents*std::sin(2*M_PI*c.vibHz*tt))/1200.0);
            double cents = 1200.0 * std::log2(f.f0Hz / truth);
            if (std::abs(cents) > 600) ++oct; else se += cents*cents;
        }
        double rms = std::sqrt(se / std::max(1, voiced - oct));
        bool ok = voiced > 0.95*cnt && oct == 0 && rms < 25.0;
        printf("%-26s voiced %d/%d  octave errors %d  RMS %.2f cents  %s\n", c.name, voiced, cnt, oct, rms, ok ? "PASS" : "FAIL");
        fails += !ok;
    }
    // 無音は無声になること
    std::vector<float> sil(48000, 0.f); PitchDetectorConfig cfg; auto fr = PitchDetector(cfg).analyse(sil);
    int v = 0; for (auto& f : fr) v += f.voiced; printf("silence voiced frames: %d %s\n", v, v==0?"PASS":"FAIL"); fails += v!=0;
    return fails;
}
