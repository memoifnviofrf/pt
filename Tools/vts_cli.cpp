// VOCAL TUNE STUDIO - ヘッドレスCLI: WAV読み込み→解析→補正→WAV書き出し
#include "../Source/Core/EditSession.h"
#include <cstdio>
#include <cstring>
#include <string>
using namespace vts;

int main(int argc, char** argv)
{
    if (argc < 3) { printf("usage: vts_cli in.wav out.wav [--scale auto|chromatic|major|minor|hminor|mminor|pent] [--root 0-11]\n"
                           "       [--center 0-100] [--drift 0-100] [--mod 0-100] [--transition 0-100] [--formant semis] [--min Hz] [--max Hz] [--bits 16|24|32] [--project file]\n"); return 2; }
    EditSession s; std::string err, scale = "auto"; int root = -1, bits = 24; std::string proj;
    float center = 100, drift = 0, mod = 0, trans = 50;
    for (int i = 3; i + 1 < argc; i += 2) { std::string k = argv[i], v = argv[i + 1];
        if (k == "--scale") scale = v; else if (k == "--root") root = std::stoi(v); else if (k == "--center") center = std::stof(v);
        else if (k == "--drift") drift = std::stof(v); else if (k == "--mod") mod = std::stof(v); else if (k == "--transition") trans = std::stof(v);
        else if (k == "--formant") s.formantShiftSemis = std::stof(v); else if (k == "--min") s.minHz = std::stod(v); else if (k == "--max") s.maxHz = std::stod(v);
        else if (k == "--bits") bits = std::stoi(v); else if (k == "--project") proj = v; }
    if (!s.loadWav(argv[1], &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    s.analyse();
    auto& st = s.editor.settings; st.pitchCenter = center / 100; st.pitchDrift = drift / 100; st.pitchModulation = mod / 100; st.noteTransition = trans / 100;
    if (scale == "auto") { st.scale = s.key.minor ? ScaleType::NaturalMinor : ScaleType::Major; st.root = s.key.root; }
    else { st.scale = scale == "chromatic" ? ScaleType::Chromatic : scale == "minor" ? ScaleType::NaturalMinor : scale == "hminor" ? ScaleType::HarmonicMinor
                    : scale == "mminor" ? ScaleType::MelodicMinor : scale == "pent" ? ScaleType::Pentatonic : ScaleType::Major; st.root = root < 0 ? 0 : root; }
    s.editor.autoTargets(false);
    static const char* nn[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    printf("%zu notes, key detected: %s %s (score %.2f), scale used root=%d\n", s.editor.getNotes().size(), nn[s.key.root], s.key.minor ? "minor" : "major", s.key.score, st.root);
    for (auto& e : s.editor.getNotes()) printf("  %7.3f-%7.3fs  midi %.2f (%+.0f c)  -> %.0f\n", e.note.startSec, e.note.endSec, e.note.medianMidi, (e.note.medianMidi - std::round(e.note.medianMidi)) * 100.0, e.targetMidi);
    if (!s.exportWav(argv[2], bits, &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (!proj.empty() && !s.saveProject(proj, &err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    printf("wrote %s\n", argv[2]); return 0;
}
