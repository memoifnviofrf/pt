#include "../Source/Core/PitchEditor.h"
#include <cstdio>
#include <random>
using namespace vts;

static double env(double f) {
    struct F { double fc, bw, g; } fm[] = { {700, 120, 1.0}, {1250, 150, 0.7}, {2600, 250, 0.4} };
    double a = 0.02; for (auto& q : fm) a += q.g / std::sqrt(1.0 + std::pow((f - q.fc) / q.bw, 2)); return a;
}
static void addVoiced(std::vector<float>& x, double sr, double dur, const std::function<double(double)>& midiFn, double& ph) {
    size_t N = (size_t) (sr * dur);
    for (size_t i = 0; i < N; ++i) {
        double f0 = 440.0 * std::pow(2.0, (midiFn(i / sr) - 69.0) / 12.0); ph += 2 * M_PI * f0 / sr; double s = 0;
        for (int h = 1; h * f0 < 6000; ++h) s += env(h * f0) * std::cos(h * ph);
        x.push_back((float) (0.15 * s));
    }
}
static double harmDb(const std::vector<float>& y, double sr, double t0, double t1, double f) {
    long a = (long) (t0 * sr), b = (long) (t1 * sr); double re = 0, im = 0, ws = 0;
    for (long n = a; n < b; ++n) { double w = 0.5 * (1 - std::cos(2 * M_PI * (n - a) / (b - a)));
        re += w * y[(size_t) n] * std::cos(2 * M_PI * f * n / sr); im -= w * y[(size_t) n] * std::sin(2 * M_PI * f * n / sr); ws += w; }
    return 20 * std::log10(2 * std::sqrt(re * re + im * im) / ws + 1e-12);
}
static double envErrDb(const std::vector<float>& y, double sr, double f0, double t0, double t1) {
    std::vector<double> m, r;
    for (int h = 1; h * f0 < 3500; ++h) { m.push_back(harmDb(y, sr, t0, t1, h * f0)); r.push_back(20 * std::log10(env(h * f0))); }
    double am = 0, ar = 0; for (size_t i = 0; i < m.size(); ++i) { am += m[i]; ar += r[i]; } am /= m.size(); ar /= m.size();
    double e = 0; for (size_t i = 0; i < m.size(); ++i) { double d = (m[i] - am) - (r[i] - ar); e += d * d; } return std::sqrt(e / m.size());
}

static const double sr = 48000;
static PitchDetectorConfig pc() { PitchDetectorConfig c; c.sampleRate = sr; return c; }

// 単一音符音声のビブラートを測る
static VibratoInfo measureVibrato(const std::vector<float>& y) {
    auto fr = PitchDetector(pc()).analyse(y); const double hop = pc().hopSize / sr;
    std::vector<float> m; double t0 = -1; for (auto& f : fr) if (f.voiced) { if (t0 < 0) t0 = f.timeSec; m.push_back(f.midi); }
    const int L = (int) m.size(), W = (int) std::round(0.2 / hop) | 1; std::vector<float> mod((size_t) L);
    for (int k = 0; k < L; ++k) { double a = 0; int n = 0; for (int d = -W / 2; d <= W / 2; ++d) { int q = k + d; if (q < 0 || q >= L) continue; a += m[(size_t) q]; ++n; } mod[(size_t) k] = (float) (m[(size_t) k] - a / n); }
    auto v = VibratoAnalyzer::analyse(mod, hop); v.onsetSec += t0 - hop * 0; return v;
}

int main()
{
    int fails = 0; auto check = [&](const char* name, bool ok, const char* detail) { printf("%-46s %s  %s\n", name, ok ? "PASS" : "FAIL", detail); fails += !ok; };
    char b[160];

    // ===== ビブラート =====
    double ph = 0; std::vector<float> xv;
    addVoiced(xv, sr, 2.0, [](double t) { return 62.0 + 0.40 * std::clamp((t - 0.4) / 0.3, 0.0, 1.0) * std::sin(2 * M_PI * 5.5 * t); }, ph);
    auto o = measureVibrato(xv);
    snprintf(b, sizeof b, "rate %.2f Hz, depth %.1f c, onset %.2f s, regularity %.2f", o.rateHz, o.depthSemis * 100, o.onsetSec, o.regularity);
    check("vibrato analysis (true 5.5Hz, 40c, ramp 0.4-0.7s)", o.detected && std::abs(o.rateHz - 5.5) < 0.3 && std::abs(o.depthSemis * 100 - 40) < 12, b);
    { double p2 = 0; std::vector<float> xs; addVoiced(xs, sr, 2.0, [](double) { return 62.0; }, p2);
      auto s = measureVibrato(xs); snprintf(b, sizeof b, "depth %.1f c, regularity %.2f", s.depthSemis * 100, s.regularity);
      check("steady note -> no vibrato detected", !s.detected, b); }

    auto fr = PitchDetector(pc()).analyse(xv);
    SegmenterConfig sc; sc.hopSec = pc().hopSize / sr; auto notes = NoteSegmenter(sc).segment(fr);
    snprintf(b, sizeof b, "%zu notes", notes.size()); check("vibrato note stays one note", notes.size() == 1, b);
    auto apply = [&](VibratoParams vp) {
        PitchEditor ed; ed.settings.pitchCenter = 0; ed.settings.pitchDrift = 0; ed.settings.pitchModulation = 0; ed.settings.vibrato = vp; ed.setNotes(notes);
        auto off = ed.computeOffsets(fr);
        return PitchShifter::process(xv, fr, sr, PitchEditor::makeRatioFn(fr, off));
    };
    { VibratoParams vp; vp.depth = 0.5f; auto v = measureVibrato(apply(vp));
      snprintf(b, sizeof b, "depth %.1f c (orig %.1f), rate %.2f", v.depthSemis * 100, o.depthSemis * 100, v.rateHz);
      check("Vibrato Depth x0.5", std::abs(v.depthSemis / o.depthSemis - 0.5) < 0.15 && std::abs(v.rateHz - o.rateHz) < 0.3, b); }
    { VibratoParams vp; vp.rate = 1.2f; auto v = measureVibrato(apply(vp));
      snprintf(b, sizeof b, "rate %.2f Hz (orig %.2f, expect %.2f), depth %.1f c", v.rateHz, o.rateHz, o.rateHz * 1.2, v.depthSemis * 100);
      check("Vibrato Rate x1.2", std::abs(v.rateHz - o.rateHz * 1.2) < 0.35 && std::abs(v.depthSemis / o.depthSemis - 1.0) < 0.25, b); }
    { VibratoParams vp; vp.onsetShiftSec = 0.3f; auto v = measureVibrato(apply(vp));
      snprintf(b, sizeof b, "onset %.2f s (orig %.2f, expect +0.30)", v.onsetSec, o.onsetSec);
      check("Vibrato Onset +0.3s", std::abs((v.onsetSec - o.onsetSec) - 0.3) < 0.12, b); }
    { VibratoParams vp; vp.amount = 0.f; auto v = measureVibrato(apply(vp));
      snprintf(b, sizeof b, "depth %.1f c, detected=%d", v.depthSemis * 100, (int) v.detected);
      check("Vibrato Amount 0% removes vibrato", !v.detected && v.depthSemis * 100 < 10, b); }

    // ===== タイミング / 時間伸縮 =====
    { // 定常音 1.0s を 1.5倍に伸ばす / 0.7倍に縮める
        for (double k : { 1.5, 0.7 }) {
            double p2 = 0; std::vector<float> x; addVoiced(x, sr, 1.0, [](double) { return 57.0; }, p2);  // A3=220Hz
            auto f = PitchDetector(pc()).analyse(x);
            auto y = TimeStretchProcessor::process(x, f, sr, { {1.0 * 0.5, 0.5 * k} });  // 途中アンカー: 0.5s→0.5k
            auto fo = PitchDetector(pc()).analyse(y); double se = 0; int c = 0; bool fin = true; for (float v : y) fin &= std::isfinite(v);
            for (auto& q : fo) if (q.timeSec > 0.15 && q.timeSec < y.size() / sr - 0.15) { double cents = q.voiced ? 1200 * std::log2(q.f0Hz / 220.0) : 1e3; se += cents * cents; ++c; }
            double rms = std::sqrt(se / c), ee = envErrDb(y, sr, 220.0, 0.2, y.size() / sr - 0.2);
            double expLen = 0.5 * k + 0.5;   // 0.5sは k倍, 残り0.5sは等倍
            snprintf(b, sizeof b, "len %.3f s (expect %.3f), pitch RMS %.2f c, env err %.2f dB", y.size() / sr, expLen, rms, ee);
            check(k > 1 ? "time stretch: first half x1.5 (steady note)" : "time stretch: first half x0.7 (steady note)", std::abs(y.size() / sr - expLen) < 0.01 && rms < 5 && ee < 3.0 && fin, b);
        }
    }
    { // フレーズ: 2音目を0.2s遅らせる(間のブレスは0.2→0.4sに伸びる)
        std::mt19937 rng(5); std::normal_distribution<double> g(0, 1);
        double p2 = 0; std::vector<float> x;
        addVoiced(x, sr, 0.6, [](double) { return 57.0; }, p2);                                  // A3 220Hz
        for (int i = 0; i < (int) (0.2 * sr); ++i) x.push_back((float) (0.05 * g(rng)));          // ブレス/ノイズ
        addVoiced(x, sr, 0.6, [](double) { return 64.0; }, p2);                                  // E4 329.6Hz
        auto f = PitchDetector(pc()).analyse(x); auto ns = NoteSegmenter(sc).segment(f);
        snprintf(b, sizeof b, "%zu notes", ns.size()); check("phrase: 2 notes before edit", ns.size() == 2, b);
        PitchEditor ed; ed.setNotes(ns); ed.setNoteTiming(1, ns[1].startSec + 0.2, ns[1].endSec + 0.2);
        auto y = TimeStretchProcessor::process(x, f, sr, ed.buildAnchors());
        auto fo = PitchDetector(pc()).analyse(y); auto ns2 = NoteSegmenter(sc).segment(fo);
        bool fin = true; for (float v : y) fin &= std::isfinite(v);
        snprintf(b, sizeof b, "len %.3f s (expect 1.600), notes %zu", y.size() / sr, ns2.size());
        check("note timing edit: output length", std::abs(y.size() / sr - 1.6) < 0.01 && fin, b);
        if (ns2.size() == 2) {
            double e1 = std::abs(ns2[1].startSec - (ns[1].startSec + 0.2)), e0 = std::abs(ns2[0].startSec - ns[0].startSec);
            double e0e = std::abs(ns2[0].endSec - ns[0].endSec);
            snprintf(b, sizeof b, "note2 start err %.1f ms, note1 start/end err %.1f/%.1f ms, pitches %.2f / %.2f (expect 57/64)", e1 * 1000, e0 * 1000, e0e * 1000, ns2[0].medianMidi, ns2[1].medianMidi);
            check("note timing edit: onset position & pitch kept", e1 < 0.04 && e0 < 0.04 && e0e < 0.04 && std::abs(ns2[0].medianMidi - 57) < 0.05 && std::abs(ns2[1].medianMidi - 64) < 0.05, b);
        } else check("note timing edit: onset position & pitch kept", false, "note count changed");
        double r = 0; long n0 = (long) (0.68 * sr), n1 = (long) (0.92 * sr); for (long i = n0; i < n1; ++i) r += (double) y[(size_t) i] * y[(size_t) i];
        double rmsOut = std::sqrt(r / (n1 - n0)), rmsIn = 0.05; double db = 20 * std::log10(rmsOut / rmsIn);
        snprintf(b, sizeof b, "stretched noise level %+.2f dB vs original", db); check("WSOLA noise level within +-3 dB", std::abs(db) < 3.0, b);
    }
    { // ピッチ+時間の同時処理
        double p2 = 0; std::vector<float> x; addVoiced(x, sr, 1.2, [](double) { return 57.0; }, p2);
        auto f = PitchDetector(pc()).analyse(x); const double r = std::pow(2.0, 4.0 / 12.0);
        auto y = TimeStretchProcessor::process(x, f, sr, { {0.6, 0.9} }, [&](double) { return r; });
        auto fo = PitchDetector(pc()).analyse(y); double se = 0; int c = 0;
        for (auto& q : fo) if (q.timeSec > 0.15 && q.timeSec < y.size() / sr - 0.15) { double cents = q.voiced ? 1200 * std::log2(q.f0Hz / (220.0 * r)) : 1e3; se += cents * cents; ++c; }
        snprintf(b, sizeof b, "pitch RMS %.2f c, len %.3f s", std::sqrt(se / c), y.size() / sr);
        check("pitch +4 semis with time stretch combined", std::sqrt(se / c) < 6.0, b);
    }
    return fails;
}
