#include "../Source/Core/NoteSegmenter.h"
#include <cstdio>
#include <random>
using namespace vts;

struct NoteSpec { double midi, dur, vibCents, vibHz; bool silenceAfter; double gapSec; };

int main()
{
    const double sr = 48000;
    std::vector<NoteSpec> spec = {
        {60, 0.5, 0, 0, false, 0}, {62, 0.5, 0, 0, false, 0}, {64, 0.6, 50, 5.5, true, 0.2}, {67, 0.5, 20, 5.0, false, 0}, {66.0 + 0.0, 0.4, 0, 0, false, 0} };
    std::mt19937 rng(7); std::normal_distribution<double> g(0, 1);
    std::vector<float> x; double ph = 0, t0 = 0;
    std::vector<std::pair<double,double>> truth; // start,end
    for (auto& n : spec) {
        size_t N = (size_t) (n.dur * sr);
        for (size_t i = 0; i < N; ++i) {
            double t = i / sr;
            double midi = n.midi + (n.vibCents/100.0) * std::sin(2*M_PI*n.vibHz*t);
            double f = 440.0 * std::pow(2.0, (midi - 69.0)/12.0);
            ph += 2*M_PI*f/sr; double s = 0; for (int h = 1; h <= 10; ++h) s += std::sin(h*ph)/h;
            x.push_back((float) (0.3*s + 0.01*g(rng)));
        }
        truth.push_back({t0, t0 + n.dur}); t0 += n.dur;
        if (n.silenceAfter) { size_t M = (size_t) (n.gapSec*sr); for (size_t i = 0; i < M; ++i) x.push_back(0.f); t0 += n.gapSec; }
    }
    PitchDetectorConfig pc; pc.sampleRate = sr;
    auto fr = PitchDetector(pc).analyse(x);
    SegmenterConfig sc; sc.hopSec = pc.hopSize / sr;
    auto notes = NoteSegmenter(sc).segment(fr);

    int fails = 0;
    printf("detected %zu notes (expected %zu)\n", notes.size(), truth.size());
    for (auto& n : notes) printf("  %.3f - %.3f  midi %.2f  dev %+.1f c  legato=%d\n", n.startSec, n.endSec, n.medianMidi, n.meanCentsDev, (int) n.legatoToNext);
    if (notes.size() != truth.size()) { printf("FAIL: note count\n"); return 1; }
    const double midis[] = {60, 62, 64, 67, 66};
    for (size_t i = 0; i < notes.size(); ++i) {
        double es = std::abs(notes[i].startSec - truth[i].first), ee = std::abs(notes[i].endSec - truth[i].second);
        double pm = std::abs(notes[i].medianMidi - midis[i]) * 100;
        bool ok = es < 0.04 && ee < 0.04 && pm < 15;
        printf("note %zu: start err %.1f ms, end err %.1f ms, pitch err %.1f c  %s\n", i, es*1000, ee*1000, pm, ok ? "PASS" : "FAIL");
        fails += !ok;
    }
    // 手動編集: 分割→結合で元に戻ること
    auto ed = notes; NoteSegmenter::splitAt(ed, 0, 0.25, fr);
    bool s1 = ed.size() == notes.size() + 1 && std::abs(ed[0].endSec - 0.25) < 1e-9;
    NoteSegmenter::mergeWithNext(ed, 0, fr);
    bool s2 = ed.size() == notes.size() && std::abs(ed[0].endSec - notes[0].endSec) < 1e-9;
    printf("split/merge: %s\n", (s1 && s2) ? "PASS" : "FAIL"); fails += !(s1 && s2);
    return fails;
}
