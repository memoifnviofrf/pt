#include "../Source/Core/EditSession.h"
#include <cstdio>
#include <chrono>
using namespace vts;

static double env(double f) {
    struct F { double fc, bw, g; } fm[] = { {700, 120, 1.0}, {1250, 150, 0.7}, {2600, 250, 0.4} };
    double a = 0.02; for (auto& q : fm) a += q.g / std::sqrt(1.0 + std::pow((f - q.fc) / q.bw, 2)); return a;
}
static std::vector<float> melody(double sr) {   // E4(+35c) 0.6s, G4(-30c, ビブラート) 0.6s  レガート
    std::vector<float> x; double ph = 0; const double nm[2] = {64.35, 66.70};
    for (int k = 0; k < 2; ++k) { size_t N = (size_t) (0.6 * sr);
        for (size_t i = 0; i < N; ++i) { double t = i / sr, m = nm[k] + (k == 1 ? 0.2 * std::sin(2 * M_PI * 5.5 * t) : 0);
            double f0 = 440.0 * std::pow(2.0, (m - 69) / 12.0); ph += 2 * M_PI * f0 / sr; double s = 0;
            for (int h = 1; h * f0 < 6000; ++h) s += env(h * f0) * std::cos(h * ph);
            x.push_back((float) (0.15 * s)); } }
    return x;
}

int main()
{
    int fails = 0; char b[200];
    auto check = [&](const char* name, bool ok, const char* d) { printf("%-44s %s  %s\n", name, ok ? "PASS" : "FAIL", d); fails += !ok; };

    // ---- WAV入出力ラウンドトリップ(16/24/32bit, ステレオ) ----
    {
        WavData w; w.sampleRate = 44100; w.ch.assign(2, std::vector<float>(1000));
        for (int i = 0; i < 1000; ++i) { w.ch[0][i] = 0.5f * std::sin(i * 0.1f); w.ch[1][i] = -0.25f * std::sin(i * 0.07f); }
        for (int bits : {16, 24, 32}) {
            WavData r; std::string e; bool ok = WavIO::write("/tmp/rt.wav", w, bits, &e) && WavIO::read("/tmp/rt.wav", r, &e);
            double md = 0; if (ok) for (int c = 0; c < 2; ++c) for (int i = 0; i < 1000; ++i) md = std::max(md, (double) std::abs(r.ch[(size_t) c][(size_t) i] - w.ch[(size_t) c][(size_t) i]));
            double tol = bits == 16 ? 1e-4 : bits == 24 ? 1e-6 : 0.0; snprintf(b, sizeof b, "max diff %.2e, sr=%.0f ch=%zu", md, ok ? r.sampleRate : 0.0, ok ? r.ch.size() : 0);
            check(bits == 16 ? "WAV roundtrip 16-bit stereo" : bits == 24 ? "WAV roundtrip 24-bit stereo" : "WAV roundtrip float32 stereo", ok && md <= tol && r.sampleRate == 44100 && r.ch.size() == 2, b);
        }
        std::string e; WavData r; bool rej = !WavIO::read("/tmp/rt.wav.nonexistent", r, &e); check("reject missing file with error message", rej && !e.empty(), e.c_str());
    }

    // ---- 全サンプルレートで 読込→解析→補正→再解析 ----
    for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        EditSession s; auto x = melody(sr); s.setAudio({x}, sr);
        auto t0 = std::chrono::steady_clock::now(); s.analyse();
        s.editor.settings.scale = ScaleType::Major; s.editor.settings.root = 0; s.editor.settings.pitchCenter = 1; s.editor.settings.pitchDrift = 1;
        s.editor.autoTargets(false); auto y = s.render();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        EditSession v; v.setAudio(y, sr); v.analyse();
        bool ok = s.editor.getNotes().size() == 2 && v.editor.getNotes().size() == 2 && y[0].size() == x.size();
        double e0 = 1e9, e1 = 1e9;
        if (ok) { e0 = (v.editor.getNotes()[0].note.medianMidi - 64.0) * 100; e1 = (v.editor.getNotes()[1].note.medianMidi - 67.0) * 100; }
        bool fin = true; for (float q : y[0]) fin &= std::isfinite(q);
        snprintf(b, sizeof b, "notes %zu->%zu, center err %+.1f / %+.1f c, process %.0f ms (audio 1.2 s)", s.editor.getNotes().size(), v.editor.getNotes().size(), e0, e1, ms);
        char name[64]; snprintf(name, sizeof name, "pipeline @ %.1f kHz", sr / 1000);
        check(name, ok && fin && std::abs(e0) < 10 && std::abs(e1) < 10, b);
    }

    // ---- プロジェクト保存・復元(手動編集を含む) + WAV書き出し ----
    {
        EditSession s; s.setAudio({melody(48000)}, 48000); s.analyse();
        s.editor.settings.scale = ScaleType::Major; s.editor.autoTargets(false);
        s.editor.setManualTarget(1, 69.0); s.editor.setNoteTiming(1, s.editor.getNotes()[1].note.startSec + 0.1, s.editor.getNotes()[1].note.endSec + 0.1);
        s.formantShiftSemis = 1.5f; s.editor.settings.vibrato.depth = 0.5f; s.editor.settings.pitchCenter = 0.8f;
        std::string e; bool saved = s.saveProject("/tmp/test.vtsproj", &e);
        EditSession l; bool loaded = l.loadProject("/tmp/test.vtsproj", &e);
        bool same = saved && loaded && l.editor.getNotes().size() == s.editor.getNotes().size();
        if (same) { for (size_t i = 0; i < s.editor.getNotes().size(); ++i) { auto &a = s.editor.getNotes()[i], &c = l.editor.getNotes()[i];
            same &= std::abs(a.targetMidi - c.targetMidi) < 1e-6 && a.manualTarget == c.manualTarget && std::abs(a.newStartSec - c.newStartSec) < 1e-6 && std::abs(a.note.startSec - c.note.startSec) < 1e-6; }
            same &= std::abs(l.formantShiftSemis - 1.5f) < 1e-6 && std::abs(l.editor.settings.vibrato.depth - 0.5f) < 1e-6 && std::abs(l.editor.settings.pitchCenter - 0.8f) < 1e-6; }
        check("project save/load roundtrip (notes, manual, timing)", same, e.c_str());
        bool orig = loaded && l.original[0].size() == s.original[0].size();
        double md = 0; if (orig) for (size_t i = 0; i < s.original[0].size(); ++i) md = std::max(md, (double) std::abs(l.original[0][i] - s.original[0][i]));
        snprintf(b, sizeof b, "original audio max diff %.1e", md); check("original audio preserved (non-destructive)", orig && md == 0.0, b);
        auto r1 = s.render(), r2 = l.render(); bool eq = r1[0].size() == r2[0].size(); double dd = 0;
        if (eq) for (size_t i = 0; i < r1[0].size(); ++i) dd = std::max(dd, (double) std::abs(r1[0][i] - r2[0][i]));
        snprintf(b, sizeof b, "render diff after reload %.2e, len %zu", dd, r1[0].size()); check("render identical after project reload", eq && dd < 1e-5, b);
        bool ex = s.exportWav("/tmp/out.wav", 24, &e); WavData w; bool rd = ex && WavIO::read("/tmp/out.wav", w, &e);
        snprintf(b, sizeof b, "len %.3f s (expect 1.300 = 1.2+0.1)", rd ? w.ch[0].size() / w.sampleRate : 0.0);
        check("WAV export of edited result (timing edit)", rd && std::abs(w.ch[0].size() / w.sampleRate - 1.3) < 0.01, b);
        check("A/B: original untouched by render", s.original[0] == melody(48000), "");
    }
    // ---- 音符操作(分割/結合/一括移動/タイミング移動)とUndo、状態Blob ----
    {
        EditSession s; s.setAudio({melody(48000)}, 48000); s.analyse(); auto& ed = s.editor;
        const size_t n0 = ed.getNotes().size(); double mid = (ed.getNotes()[0].note.startSec + ed.getNotes()[0].note.endSec) / 2;
        bool sp = ed.splitNote(0, mid, s.frames) && ed.getNotes().size() == n0 + 1 && std::abs(ed.getNotes()[0].note.endSec - mid) < 1e-9
                  && std::abs(ed.getNotes()[1].note.startSec - mid) < 1e-9;
        bool mg = ed.mergeNotes(0, s.frames) && ed.getNotes().size() == n0;
        check("split / merge notes", sp && mg, "");
        double t0 = ed.getNotes()[0].targetMidi, t1 = ed.getNotes()[1].targetMidi;
        ed.transposeNotes({0, 1}, 2.0);
        bool tr = std::abs(ed.getNotes()[0].targetMidi - (t0 + 2)) < 1e-9 && std::abs(ed.getNotes()[1].targetMidi - (t1 + 2)) < 1e-9 && ed.getNotes()[0].manualTarget;
        ed.undo(); bool un = std::abs(ed.getNotes()[0].targetMidi - t0) < 1e-9 && std::abs(ed.getNotes()[1].targetMidi - t1) < 1e-9;
        check("batch transpose = 1 undo step", tr && un, "");
        double s1 = ed.getNotes()[1].note.startSec; ed.moveNotes({1}, 0.25);
        check("move note in time", std::abs(ed.getNotes()[1].newStartSec - (s1 + 0.25)) < 1e-9, "");
        auto blob = s.toBlob(); EditSession r; std::string e; bool ok = r.fromBlob(blob.data(), blob.size(), &e);
        bool eq = ok && r.editor.getNotes().size() == ed.getNotes().size() && std::abs(r.editor.getNotes()[1].newStartSec - ed.getNotes()[1].newStartSec) < 1e-9 && r.original == s.original;
        check("plugin state blob roundtrip", eq, e.c_str());
        std::vector<uint8_t> bad(blob.begin(), blob.begin() + 30); EditSession q; std::string e2;
        check("corrupt state blob rejected", !q.fromBlob(bad.data(), bad.size(), &e2) && !e2.empty(), e2.c_str());
    }
    return fails;
}
