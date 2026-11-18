#pragma once
// VOCAL TUNE STUDIO - WavIO: 依存なしのWAV読み書き(PCM16/24/32, float32, EXTENSIBLE対応)
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace vts {

struct WavData { double sampleRate = 0; std::vector<std::vector<float>> ch; };

class WavIO {
public:
    static bool read(const std::string& path, WavData& out, std::string* err = nullptr)
    {
        std::ifstream f(path, std::ios::binary); if (!f) return fail(err, "cannot open file");
        std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return readBytes(b, out, err);
    }

    static bool readBytes(const std::vector<uint8_t>& b, WavData& out, std::string* err = nullptr)
    {
        if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return fail(err, "not a RIFF/WAVE file");
        size_t pos = 12; int fmtTag = 0, nch = 0, bits = 0; uint32_t sr = 0; const uint8_t* data = nullptr; size_t dsz = 0;
        while (pos + 8 <= b.size()) {
            uint32_t sz = u32(&b[pos + 4]); const uint8_t* c = &b[pos + 8];
            size_t avail = std::min<size_t>(sz, b.size() - (pos + 8));
            if (!std::memcmp(&b[pos], "fmt ", 4) && avail >= 16) {
                fmtTag = u16(c); nch = u16(c + 2); sr = u32(c + 4); bits = u16(c + 14);
                if (fmtTag == 0xFFFE && avail >= 26) fmtTag = u16(c + 24);
            } else if (!std::memcmp(&b[pos], "data", 4)) { data = c; dsz = avail; }
            pos += 8 + sz + (sz & 1);
        }
        if (!data || nch < 1 || nch > 2 || sr == 0) return fail(err, "unsupported or missing fmt/data (mono/stereo only)");
        const int bps = bits / 8; if (!((fmtTag == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmtTag == 3 && bits == 32))) return fail(err, "unsupported sample format");
        const size_t frames = dsz / (size_t) (bps * nch);
        out.sampleRate = sr; out.ch.assign((size_t) nch, std::vector<float>(frames));
        for (size_t i = 0; i < frames; ++i) for (int c = 0; c < nch; ++c) {
            const uint8_t* p = data + (i * (size_t) nch + (size_t) c) * (size_t) bps; float v;
            if (fmtTag == 3) { float t; std::memcpy(&t, p, 4); v = t; }
            else if (bits == 16) v = (int16_t) u16(p) / 32768.0f;
            else if (bits == 24) { int32_t t = (int32_t) ((p[0] << 8) | (p[1] << 16) | ((uint32_t) p[2] << 24)); v = (float) (t / 2147483648.0); }
            else v = (float) ((int32_t) u32(p) / 2147483648.0);
            out.ch[(size_t) c][i] = v;
        }
        return true;
    }

    // bits: 16 / 24 / 32(=float32)
    static bool write(const std::string& path, const WavData& w, int bits = 24, std::string* err = nullptr)
    {
        std::vector<uint8_t> o; if (!writeBytes(w, bits, o, err)) return false;
        std::ofstream f(path, std::ios::binary); if (!f) return fail(err, "cannot write file");
        f.write((const char*) o.data(), (std::streamsize) o.size()); return (bool) f;
    }

    static bool writeBytes(const WavData& w, int bits, std::vector<uint8_t>& o, std::string* err = nullptr)
    {
        if (w.ch.empty() || (bits != 16 && bits != 24 && bits != 32)) return fail(err, "bad arguments");
        const int nch = (int) w.ch.size(); const size_t n = w.ch[0].size(); const int bps = bits / 8;
        o.clear(); auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back((uint8_t) (v >> (8 * i))); };
        auto p16 = [&](uint16_t v) { o.push_back((uint8_t) v); o.push_back((uint8_t) (v >> 8)); };
        const uint32_t dsz = (uint32_t) (n * (size_t) nch * (size_t) bps);
        o.insert(o.end(), { 'R','I','F','F' }); p32(36 + dsz); o.insert(o.end(), { 'W','A','V','E','f','m','t',' ' }); p32(16);
        p16(bits == 32 ? 3 : 1); p16((uint16_t) nch); p32((uint32_t) w.sampleRate); p32((uint32_t) w.sampleRate * (uint32_t) nch * (uint32_t) bps);
        p16((uint16_t) (nch * bps)); p16((uint16_t) bits); o.insert(o.end(), { 'd','a','t','a' }); p32(dsz);
        for (size_t i = 0; i < n; ++i) for (int c = 0; c < nch; ++c) {
            float v = std::isfinite(w.ch[(size_t) c][i]) ? w.ch[(size_t) c][i] : 0.f;
            if (bits == 32) { uint32_t t; std::memcpy(&t, &v, 4); p32(t); continue; }
            v = std::max(-1.f, std::min(1.f, v));
            if (bits == 16) p16((uint16_t) (int16_t) std::lrint(v * 32767.0));
            else { int32_t t = (int32_t) std::lrint(v * 8388607.0); o.push_back((uint8_t) t); o.push_back((uint8_t) (t >> 8)); o.push_back((uint8_t) (t >> 16)); }
        }
        return true;
    }
private:
    static bool fail(std::string* e, const char* m) { if (e) *e = m; return false; }
    static uint16_t u16(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
    static uint32_t u32(const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
};

} // namespace vts
