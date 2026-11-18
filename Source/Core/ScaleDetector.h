#pragma once
// VOCAL TUNE STUDIO - ScaleDetector (JUCE非依存コア)
#include "NoteSegmenter.h"
#include <array>
#include <cmath>

namespace vts {

enum class ScaleType { Chromatic, Major, NaturalMinor, HarmonicMinor, MelodicMinor, Pentatonic, Custom };

struct KeyResult { int root = 0; bool minor = false; double score = 0; };

class ScaleDetector {
public:
    // root=0..11(C=0)を基準に、半音クラスごとの許可マスクを返す(index=root相対ではなく絶対ピッチクラス)
    static std::array<bool, 12> scaleMask(ScaleType t, int root, unsigned customMask = 0xFFF)
    {
        static const std::vector<int> major{0,2,4,5,7,9,11}, nmin{0,2,3,5,7,8,10}, hmin{0,2,3,5,7,8,11},
                                      mmin{0,2,3,5,7,9,11}, pent{0,2,4,7,9};
        std::array<bool, 12> m{};
        auto setAll = [&](const std::vector<int>& iv) { for (int d : iv) m[(size_t) ((root + d) % 12)] = true; };
        switch (t) {
            case ScaleType::Chromatic: m.fill(true); break;
            case ScaleType::Major: setAll(major); break;
            case ScaleType::NaturalMinor: setAll(nmin); break;
            case ScaleType::HarmonicMinor: setAll(hmin); break;
            case ScaleType::MelodicMinor: setAll(mmin); break;
            case ScaleType::Pentatonic: setAll(pent); break;
            case ScaleType::Custom: for (int d = 0; d < 12; ++d) if (customMask & (1u << d)) m[(size_t) ((root + d) % 12)] = true; break;
        }
        return m;
    }

    // 最寄りの許可音(整数MIDIノート)。許可音が無い場合は最寄り半音。
    static double nearestScaleNote(double midi, const std::array<bool, 12>& mask)
    {
        double best = std::round(midi), bd = 1e9;
        for (int d = -7; d <= 7; ++d) {
            int n = (int) std::round(midi) + d; int pc = ((n % 12) + 12) % 12;
            if (!mask[(size_t) pc]) continue;
            double dist = std::abs(n - midi);
            if (dist < bd) { bd = dist; best = n; }
        }
        return best;
    }

    // Krumhansl-Schmuckler。音符を継続時間で重み付けしたピッチクラス分布と相関を取る。
    static KeyResult detectKey(const std::vector<Note>& notes)
    {
        static const double maj[12] = {6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88};
        static const double min_[12] = {6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17};
        double h[12] = {0};
        for (auto& n : notes) { int pc = (((int) std::lround(n.medianMidi)) % 12 + 12) % 12; h[pc] += n.endSec - n.startSec; }
        KeyResult best; best.score = -2;
        for (int root = 0; root < 12; ++root)
            for (int mn = 0; mn < 2; ++mn) {
                double a[12], b[12];
                for (int i = 0; i < 12; ++i) { a[i] = h[(root + i) % 12]; b[i] = mn ? min_[i] : maj[i]; }
                double c = corr(a, b);
                if (c > best.score) { best.score = c; best.root = root; best.minor = mn == 1; }
            }
        return best;
    }
private:
    static double corr(const double* a, const double* b) {
        double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; } ma /= 12; mb /= 12;
        double num = 0, da = 0, db = 0;
        for (int i = 0; i < 12; ++i) { num += (a[i]-ma)*(b[i]-mb); da += (a[i]-ma)*(a[i]-ma); db += (b[i]-mb)*(b[i]-mb); }
        return (da > 0 && db > 0) ? num / std::sqrt(da * db) : 0;
    }
};

} // namespace vts
