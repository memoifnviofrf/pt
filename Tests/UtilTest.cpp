#include "../Source/Core/Resampler.h"
#include "../Source/Core/Presets.h"
#include <cstdio>
using namespace vts;

int main()
{
    int fails = 0; char b[160];
    auto check = [&](const char* name, bool ok, const char* d) { printf("%-44s %s  %s\n", name, ok ? "PASS" : "FAIL", d); fails += !ok; };
    struct C { double a, b; } cs[] = { {48000, 44100}, {44100, 48000}, {96000, 48000}, {48000, 96000}, {192000, 48000}, {44100, 88200} };
    for (auto& c : cs) {
        const double f = 1000.0; std::vector<float> x((size_t) (c.a * 0.5));
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.5 * std::sin(2 * M_PI * f * i / c.a));
        auto y = resample(x, c.a, c.b);
        double maxErr = 0; size_t skip = (size_t) (0.02 * c.b);
        for (size_t i = skip; i + skip < y.size(); ++i) maxErr = std::max(maxErr, std::abs((double) y[i] - 0.5 * std::sin(2 * M_PI * f * i / c.b)));
        snprintf(b, sizeof b, "max err %.2e, len %zu (expect %zu)", maxErr, y.size(), (size_t) std::floor(x.size() * c.b / c.a));
        char n[64]; snprintf(n, sizeof n, "resample %.1f -> %.1f kHz", c.a / 1000, c.b / 1000);
        check(n, maxErr < 2e-3, b);
    }
    // 折り返し防止: 48k->16k で 12kHz(ナイキスト超過) は強く減衰
    { std::vector<float> x(48000); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) std::sin(2 * M_PI * 12000.0 * i / 48000.0);
      auto y = resample(x, 48000, 16000); double r = 0; for (size_t i = 400; i + 400 < y.size(); ++i) r += (double) y[i] * y[i];
      r = std::sqrt(r / (y.size() - 800)); snprintf(b, sizeof b, "alias level %.1f dB", 20 * std::log10(r + 1e-12));
      check("anti-alias on downsample (12kHz @ 16kHz)", r < 0.01, b); }
    bool ok = !presets().empty(); for (auto& p : presets()) ok &= p.center >= 0 && p.center <= 100 && p.drift >= 0 && p.drift <= 100 && p.modulation >= 0 && p.modulation <= 100
        && p.transition >= 0 && p.transition <= 100 && p.formantSemis >= -12 && p.formantSemis <= 12 && p.vibDepthPct >= 0 && p.vibDepthPct <= 200
        && p.vibRatePct >= 50 && p.vibRatePct <= 200 && p.vibOnsetSec >= -1 && p.vibOnsetSec <= 1 && p.vibAmountPct >= 0 && p.vibAmountPct <= 100 && p.vibSmoothPct >= 0 && p.vibSmoothPct <= 100;
    snprintf(b, sizeof b, "%zu presets", presets().size()); check("presets within parameter ranges", ok, b);
    return fails;
}
