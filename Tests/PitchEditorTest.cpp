#include "../Source/Core/PitchEditor.h"
#include <cstdio>
using namespace vts;

static double env(double f) {
    struct F { double fc, bw, g; } fm[] = { {700, 120, 1.0}, {1250, 150, 0.7}, {2600, 250, 0.4} };
    double a = 0.02; for (auto& q : fm) a += q.g / std::sqrt(1.0 + std::pow((f - q.fc) / q.bw, 2)); return a;
}
static std::vector<float> synth(double sr, double dur, const std::function<double(double)>& midiFn) {
    size_t N = (size_t) (sr * dur); std::vector<float> x(N); double ph = 0;
    for (size_t i = 0; i < N; ++i) {
        double f0 = 440.0 * std::pow(2.0, (midiFn(i / sr) - 69.0) / 12.0); ph += 2 * M_PI * f0 / sr; double s = 0;
        for (int h = 1; h * f0 < 6000; ++h) s += env(h * f0) * std::cos(h * ph);
        x[i] = (float) (0.15 * s);
    }
    return x;
}
static Note mk(double s, double e, double midi) { Note n; n.startSec = s; n.endSec = e; n.medianMidi = (float) midi; return n; }

int main()
{
    int fails = 0; auto check = [&](const char* name, bool ok, const char* detail) { printf("%-44s %s  %s\n", name, ok ? "PASS" : "FAIL", detail); fails += !ok; };

    // ---- キー/スケール検出 (音符データのみ) ----
    {
        std::vector<Note> cmaj, dmin; double t = 0;
        for (double m : {60, 64, 67, 72, 62, 64, 65, 64, 62, 60}) { cmaj.push_back(mk(t, t + 0.5, m)); t += 0.5; }
        t = 0; for (double m : {62, 65, 69, 74, 64, 65, 67, 65, 64, 62}) { dmin.push_back(mk(t, t + 0.5, m)); t += 0.5; }
        auto a = ScaleDetector::detectKey(cmaj), b = ScaleDetector::detectKey(dmin);
        char buf[96]; snprintf(buf, sizeof buf, "got root=%d minor=%d / root=%d minor=%d", a.root, (int) a.minor, b.root, (int) b.minor);
        check("key detection (C major, D minor)", a.root == 0 && !a.minor && b.root == 2 && b.minor, buf);
        auto mk2 = ScaleDetector::scaleMask(ScaleType::Major, 0);
        check("nearestScaleNote 61.4 in C major -> 62", ScaleDetector::nearestScaleNote(61.4, mk2) == 62.0, "");
        auto pent = ScaleDetector::scaleMask(ScaleType::Pentatonic, 0);
        check("nearestScaleNote 65.2 in C pentatonic -> 64", ScaleDetector::nearestScaleNote(65.2, pent) == 64.0, "");
    }

    // ---- 全パイプライン: 検出→音符分離→スケール補正→PSOLA→再検出 ----
    const double sr = 48000;
    // 実際は E4,G4,A4,C5 (C major) をそれぞれ detune/ドリフト/ビブラート付きで歌ったもの
    const double nm[4] = {64, 67, 69, 72}, det[4] = {0.35, -0.30, 0.0, -0.40}, vib[4] = {0, 0, 0.30, 0}, drf[4] = {0, 0, 0, 0.25};
    auto midiFn = [&](double t) { int k = std::min(3, (int) (t / 0.6)); double lt = t - k * 0.6;
        return nm[k] + det[k] + vib[k] * std::sin(2 * M_PI * 5.5 * lt) + drf[k] * (lt / 0.6); };
    auto x = synth(sr, 2.4, midiFn);
    PitchDetectorConfig pc; pc.sampleRate = sr;
    auto fr = PitchDetector(pc).analyse(x);
    SegmenterConfig sc; sc.hopSec = pc.hopSize / sr;
    auto notes = NoteSegmenter(sc).segment(fr);
    { char b[64]; snprintf(b, sizeof b, "%zu notes", notes.size()); check("segmentation of detuned melody", notes.size() == 4, b); if (notes.size() != 4) return 1; }
    auto key = ScaleDetector::detectKey(notes);

    auto run = [&](EditSettings s, std::vector<double>* centerCents, std::vector<double>* resStd) {
        PitchEditor ed; ed.settings = s; ed.setNotes(notes);
        auto off = ed.computeOffsets(fr);
        auto y = PitchShifter::process(x, fr, sr, PitchEditor::makeRatioFn(fr, off));
        auto fo = PitchDetector(pc).analyse(y);
        for (size_t i = 0; i < ed.getNotes().size(); ++i) {
            const auto& e = ed.getNotes()[i]; std::vector<double> v;
            for (auto& f : fo) if (f.voiced && f.timeSec > e.note.startSec + 0.12 && f.timeSec < e.note.endSec - 0.12) v.push_back(f.midi);
            if (v.empty()) { centerCents->push_back(1e6); resStd->push_back(1e6); continue; }
            std::vector<double> sv = v; std::sort(sv.begin(), sv.end()); double med = sv[sv.size() / 2];
            double mean = 0; for (double q : v) mean += q; mean /= v.size(); double var = 0; for (double q : v) var += (q - mean) * (q - mean);
            centerCents->push_back((med - e.targetMidi) * 100.0); resStd->push_back(std::sqrt(var / v.size()) * 100.0);
        }
        return y;
    };
    EditSettings full; full.scale = key.minor ? ScaleType::NaturalMinor : ScaleType::Major; full.root = key.root;
    full.pitchCenter = 1; full.pitchDrift = 1; full.pitchModulation = 1;
    { char b[64]; snprintf(b, sizeof b, "root=%d minor=%d", key.root, (int) key.minor);
      check("detected key of melody = C major", key.root == 0 && !key.minor, b); }

    std::vector<double> cc, rs; auto y = run(full, &cc, &rs);
    printf("  [Center/Drift/Mod 100%%] center err vs target (cents):"); for (double c : cc) printf(" %+.1f", c); printf(" | residual std:"); for (double r : rs) printf(" %.1f", r); printf("\n");
    bool ok = true; for (double c : cc) ok &= std::abs(c) < 8.0; for (double r : rs) ok &= r < 8.0;
    check("full correction: |center err|<8c, std<8c", ok, "");
    bool fin = true; for (float v : y) fin &= std::isfinite(v); check("output finite", fin, "");

    // ビブラート保持: Modulation=0, Drift=0, Center=100% → 3番目(ビブラート音)の揺れが残る
    EditSettings keep = full; keep.pitchDrift = 0; keep.pitchModulation = 0;
    std::vector<double> cc2, rs2; run(keep, &cc2, &rs2);
    printf("  [Mod 0%%, Drift 0%%] center err:"); for (double c : cc2) printf(" %+.1f", c); printf(" | residual std:"); for (double r : rs2) printf(" %.1f", r); printf("\n");
    check("vibrato kept at Modulation=0 (note 3 std>15c)", rs2[2] > 15.0 && std::abs(cc2[2]) < 10.0, "");
    check("vibrato removed at Modulation=100% (note 3 std<8c)", rs[2] < 8.0, "");

    // Pitch Center 50%: 中心は元と目標の中間へ
    EditSettings half = keep; half.pitchCenter = 0.5f; std::vector<double> cc3, rs3; run(half, &cc3, &rs3);
    // note1: 元=+35c, 目標=0c → 約+17.5c
    char b3[64]; snprintf(b3, sizeof b3, "note1 center err %+.1f c (expect ~+17)", cc3[0]);
    check("Pitch Center 50% -> halfway", std::abs(cc3[0] - 17.5) < 8.0, b3);

    // 手動ターゲット + Undo/Redo
    {
        PitchEditor ed; ed.settings = full; ed.setNotes(notes);
        double t0 = ed.getNotes()[0].targetMidi;
        ed.setManualTarget(0, 65.0); bool a = ed.getNotes()[0].targetMidi == 65.0;
        ed.settings.root = 2; ed.autoTargets(); bool keepManual = ed.getNotes()[0].targetMidi == 65.0;  // 手動は自動で上書きされない
        bool u1 = ed.undo(), u2 = ed.undo(); bool back = ed.getNotes()[0].targetMidi == t0;
        bool r1 = ed.redo(); bool redone = ed.getNotes()[0].targetMidi == 65.0;
        check("manual target / undo / redo", a && keepManual && u1 && u2 && back && r1 && redone, "");
    }
    return fails;
}
