
#include "../Source/Core/EditSession.h"

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>
#include <system_error>

using namespace vts;

namespace {

static double env(double f)
{
    struct F {
        double fc, bw, g;
    };

    F fm[] = {
        {700, 120, 1.0},
        {1250, 150, 0.7},
        {2600, 250, 0.4}
    };

    double a = 0.02;

    for (auto& q : fm)
        a += q.g / std::sqrt(
            1.0 + std::pow((f - q.fc) / q.bw, 2)
        );

    return a;
}

static std::vector<float> melody(double sr)
{
    std::vector<float> x;

    double ph = 0;
    const double nm[2] = {64.35, 66.70};

    for (int k = 0; k < 2; ++k) {
        size_t N = static_cast<size_t>(0.6 * sr);

        for (size_t i = 0; i < N; ++i) {
            double t = static_cast<double>(i) / sr;

            double m = nm[k] +
                (k == 1
                    ? 0.2 * std::sin(2 * M_PI * 5.5 * t)
                    : 0);

            double f0 = 440.0 *
                std::pow(2.0, (m - 69) / 12.0);

            ph += 2 * M_PI * f0 / sr;

            double s = 0;

            for (int h = 1; h * f0 < 6000; ++h)
                s += env(h * f0) * std::cos(h * ph);

            x.push_back(static_cast<float>(0.15 * s));
        }
    }

    return x;
}

}

int main()
{
    int fails = 0;
    char b[200];

    auto check = [&](const char* name, bool ok, const char* d) {
        std::printf(
            "%-44s %s  %s\n",
            name,
            ok ? "PASS" : "FAIL",
            d
        );

        fails += !ok;
    };

    // --------------------------------------------------
    // Windows / Linux compatible temporary directory
    // --------------------------------------------------

    std::error_code fsError;

    const auto uniqueId =
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();

    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() /
        ("vts_session_test_" + std::to_string(uniqueId));

    std::filesystem::create_directories(tempDir, fsError);

    if (fsError) {
        std::printf(
            "Cannot create temporary directory: %s\n",
            fsError.message().c_str()
        );
        return 1;
    }

    const auto rtPath = tempDir / "rt.wav";
    const auto missingPath = tempDir / "rt.wav.nonexistent";
    const auto projectPath = tempDir / "test.vtsproj";
    const auto outPath = tempDir / "out.wav";

    // --------------------------------------------------
    // WAV roundtrip tests
    // --------------------------------------------------

    {
        WavData w;
        w.sampleRate = 44100;
        w.ch.assign(2, std::vector<float>(1000));

        for (int i = 0; i < 1000; ++i) {
            w.ch[0][i] = 0.5f * std::sin(i * 0.1f);
            w.ch[1][i] = -0.25f * std::sin(i * 0.07f);
        }

        for (int bits : {16, 24, 32}) {
            WavData r;
            std::string e;

            bool ok =
                WavIO::write(rtPath.string(), w, bits, &e) &&
                WavIO::read(rtPath.string(), r, &e);

            double md = 0;

            if (ok && r.ch.size() == 2 &&
                r.ch[0].size() == 1000 &&
                r.ch[1].size() == 1000) {

                for (int c = 0; c < 2; ++c) {
                    for (int i = 0; i < 1000; ++i) {
                        md = std::max(
                            md,
                            static_cast<double>(
                                std::abs(
                                    r.ch[c][i] - w.ch[c][i]
                                )
                            )
                        );
                    }
                }
            }
            else {
                ok = false;
            }

            double tol =
                bits == 16 ? 1e-4 :
                bits == 24 ? 1e-6 : 0.0;

            std::snprintf(
                b,
                sizeof b,
                "max diff %.2e, sr=%.0f ch=%zu",
                md,
                ok ? r.sampleRate : 0.0,
                ok ? r.ch.size() : 0
            );

            check(
                bits == 16 ? "WAV roundtrip 16-bit stereo" :
                bits == 24 ? "WAV roundtrip 24-bit stereo" :
                             "WAV roundtrip float32 stereo",
                ok && md <= tol &&
                r.sampleRate == 44100 &&
                r.ch.size() == 2,
                b
            );
        }

        std::string e;
        WavData r;

        bool rej =
            !WavIO::read(missingPath.string(), r, &e);

        check(
            "reject missing file with error message",
            rej && !e.empty(),
            e.c_str()
        );
    }

    // --------------------------------------------------
    // Analysis / correction / re-analysis
    // --------------------------------------------------

    for (double sr : {
        44100.0,
        48000.0,
        88200.0,
        96000.0,
        192000.0
    }) {
        EditSession s;

        auto x = melody(sr);

        s.setAudio({x}, sr);

        auto t0 = std::chrono::steady_clock::now();

        s.analyse();

        s.editor.settings.scale = ScaleType::Major;
        s.editor.settings.root = 0;
        s.editor.settings.pitchCenter = 1;
        s.editor.settings.pitchDrift = 1;

        s.editor.autoTargets(false);

        auto y = s.render();

        double ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0
            ).count();

        EditSession v;

        v.setAudio(y, sr);
        v.analyse();

        bool channelOk =
            !y.empty() &&
            y.size() == 1 &&
            y[0].size() == x.size();

        bool ok =
            s.editor.getNotes().size() == 2 &&
            v.editor.getNotes().size() == 2 &&
            channelOk;

        double e0 = 1e9;
        double e1 = 1e9;

        if (ok) {
            e0 =
                (v.editor.getNotes()[0].note.medianMidi - 64.0)
                * 100;

            e1 =
                (v.editor.getNotes()[1].note.medianMidi - 67.0)
                * 100;
        }

        bool fin = channelOk;

        if (channelOk) {
            for (float q : y[0])
                fin &= std::isfinite(q);
        }

        std::snprintf(
            b,
            sizeof b,
            "notes %zu->%zu, center err %+.1f / %+.1f c, process %.0f ms",
            s.editor.getNotes().size(),
            v.editor.getNotes().size(),
            e0,
            e1,
            ms
        );

        char name[64];

        std::snprintf(
            name,
            sizeof name,
            "pipeline @ %.1f kHz",
            sr / 1000
        );

        check(
            name,
            ok && fin &&
            std::abs(e0) < 10 &&
            std::abs(e1) < 10,
            b
        );
    }

    // --------------------------------------------------
    // Project save / load / render / export
    // --------------------------------------------------

    {
        EditSession s;

        s.setAudio({melody(48000)}, 48000);
        s.analyse();

        const bool hasTwoNotes =
            s.editor.getNotes().size() >= 2;

        s.editor.settings.scale = ScaleType::Major;
        s.editor.autoTargets(false);

        if (hasTwoNotes) {
            s.editor.setManualTarget(1, 69.0);

            const auto& note =
                s.editor.getNotes()[1].note;

            s.editor.setNoteTiming(
                1,
                note.startSec + 0.1,
                note.endSec + 0.1
            );
        }

        s.formantShiftSemis = 1.5f;
        s.editor.settings.vibrato.depth = 0.5f;
        s.editor.settings.pitchCenter = 0.8f;

        std::string e;

        bool saved =
            s.saveProject(projectPath.string(), &e);

        EditSession l;

        bool loaded =
            saved &&
            l.loadProject(projectPath.string(), &e);

        bool same =
            hasTwoNotes &&
            saved &&
            loaded &&
            l.editor.getNotes().size() ==
                s.editor.getNotes().size();

        if (same) {
            for (size_t i = 0;
                 i < s.editor.getNotes().size();
                 ++i) {

                auto& a = s.editor.getNotes()[i];
                auto& c = l.editor.getNotes()[i];

                same &=
                    std::abs(a.targetMidi - c.targetMidi) < 1e-6 &&
                    a.manualTarget == c.manualTarget &&
                    std::abs(a.newStartSec - c.newStartSec) < 1e-6 &&
                    std::abs(a.note.startSec - c.note.startSec) < 1e-6;
            }

            same &=
                std::abs(l.formantShiftSemis - 1.5f) < 1e-6 &&
                std::abs(l.editor.settings.vibrato.depth - 0.5f) < 1e-6 &&
                std::abs(l.editor.settings.pitchCenter - 0.8f) < 1e-6;
        }

        check(
            "project save/load roundtrip",
            same,
            e.c_str()
        );

        // Original audio preservation

        bool orig =
            loaded &&
            !l.original.empty() &&
            !s.original.empty() &&
            !l.original[0].empty() &&
            l.original[0].size() == s.original[0].size();

        double md = 0;

        if (orig) {
            for (size_t i = 0; i < s.original[0].size(); ++i) {
                md = std::max(
                    md,
                    static_cast<double>(
                        std::abs(
                            l.original[0][i] - s.original[0][i]
                        )
                    )
                );
            }
        }

        std::snprintf(
            b,
            sizeof b,
            "original audio max diff %.1e",
            md
        );

        check(
            "original audio preserved",
            orig && md == 0.0,
            b
        );

        // Render comparison
        // Never access channel [0] before checking emptiness.

        auto r1 = s.render();
        auto r2 = loaded ? l.render()
                         : std::vector<std::vector<float>>{};

        bool eq =
            !r1.empty() &&
            !r2.empty() &&
            r1[0].size() == r2[0].size();

        double dd = 0;

        if (eq) {
            for (size_t i = 0; i < r1[0].size(); ++i) {
                dd = std::max(
                    dd,
                    static_cast<double>(
                        std::abs(r1[0][i] - r2[0][i])
                    )
                );
            }
        }

        std::snprintf(
            b,
            sizeof b,
            "render diff %.2e, len %zu",
            dd,
            eq ? r1[0].size() : 0
        );

        check(
            "render identical after project reload",
            eq && dd < 1e-5,
            b
        );

        // WAV export

        bool ex =
            s.exportWav(outPath.string(), 24, &e);

        WavData w;

        bool rd =
            ex &&
            WavIO::read(outPath.string(), w, &e) &&
            !w.ch.empty() &&
            w.sampleRate > 0;

        double seconds = 0;

        if (rd && !w.ch[0].empty()) {
            seconds =
                static_cast<double>(w.ch[0].size()) /
                w.sampleRate;
        }
        else {
            rd = false;
        }

        std::snprintf(
            b,
            sizeof b,
            "len %.3f s (expect 1.300)",
            seconds
        );

        check(
            "WAV export of edited result",
            rd && std::abs(seconds - 1.3) < 0.01,
            b
        );

        check(
            "A/B: original untouched by render",
            !s.original.empty() &&
            s.original[0] == melody(48000),
            ""
        );
    }

    // --------------------------------------------------
    // Note operations / Undo / State Blob
    // --------------------------------------------------

    {
        EditSession s;

        s.setAudio({melody(48000)}, 48000);
        s.analyse();

        auto& ed = s.editor;

        const size_t n0 = ed.getNotes().size();

        bool sp = false;
        bool mg = false;
        bool tr = false;
        bool un = false;
        bool moved = false;

        if (n0 >= 2) {
            const double mid =
                (
                    ed.getNotes()[0].note.startSec +
                    ed.getNotes()[0].note.endSec
                ) / 2.0;

            sp =
                ed.splitNote(0, mid, s.frames) &&
                ed.getNotes().size() == n0 + 1 &&
                std::abs(
                    ed.getNotes()[0].note.endSec - mid
                ) < 1e-9 &&
                std::abs(
                    ed.getNotes()[1].note.startSec - mid
                ) < 1e-9;

            if (sp) {
                mg =
                    ed.mergeNotes(0, s.frames) &&
                    ed.getNotes().size() == n0;
            }
        }

        check(
            "split / merge notes",
            sp && mg,
            n0 >= 2 ? "" : "requires at least two detected notes"
        );

        if (mg && ed.getNotes().size() >= 2) {
            double t0 = ed.getNotes()[0].targetMidi;
            double t1 = ed.getNotes()[1].targetMidi;

            ed.transposeNotes({0, 1}, 2.0);

            tr =
                std::abs(
                    ed.getNotes()[0].targetMidi - (t0 + 2)
                ) < 1e-9 &&
                std::abs(
                    ed.getNotes()[1].targetMidi - (t1 + 2)
                ) < 1e-9 &&
                ed.getNotes()[0].manualTarget;

            bool undoCalled = ed.undo();

            un =
                undoCalled &&
                std::abs(
                    ed.getNotes()[0].targetMidi - t0
                ) < 1e-9 &&
                std::abs(
                    ed.getNotes()[1].targetMidi - t1
                ) < 1e-9;

            double s1 =
                ed.getNotes()[1].note.startSec;

            ed.moveNotes({1}, 0.25);

            moved =
                std::abs(
                    ed.getNotes()[1].newStartSec -
                    (s1 + 0.25)
                ) < 1e-9;
        }

        check(
            "batch transpose / undo",
            tr && un,
            ""
        );

        check(
            "move note in time",
            moved,
            ""
        );

        // Plugin state blob roundtrip

        auto blob = s.toBlob();

        EditSession r;
        std::string e;

        bool ok =
            !blob.empty() &&
            r.fromBlob(blob.data(), blob.size(), &e);

        bool eq =
            ok &&
            r.editor.getNotes().size() ==
                ed.getNotes().size() &&
            r.editor.getNotes().size() >= 2 &&
            ed.getNotes().size() >= 2 &&
            std::abs(
                r.editor.getNotes()[1].newStartSec -
                ed.getNotes()[1].newStartSec
            ) < 1e-9 &&
            r.original == s.original;

        check(
            "plugin state blob roundtrip",
            eq,
            e.c_str()
        );

        // Corrupted state rejection

        std::vector<uint8_t> bad;

        if (blob.size() >= 30) {
            bad.assign(
                blob.begin(),
                blob.begin() + 30
            );
        }

        EditSession q;
        std::string e2;

        bool rejected =
            !bad.empty() &&
            !q.fromBlob(
                bad.data(),
                bad.size(),
                &e2
            ) &&
            !e2.empty();

        check(
            "corrupt state blob rejected",
            rejected,
            e2.c_str()
        );
    }

    // --------------------------------------------------
    // Cleanup
    // --------------------------------------------------

    std::filesystem::remove_all(tempDir, fsError);

    std::printf(
        "\nSessionTest: %d failure(s)\n",
        fails
    );

    return fails == 0 ? 0 : 1;
}
