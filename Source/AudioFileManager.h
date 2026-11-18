#pragma once
// VOCAL TUNE STUDIO - AudioFileManager (JUCE): 音声ファイルの読み込み(WAV/AIFF/FLAC等) と WAV書き出し
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>

class AudioFileManager
{
public:
    // targetSampleRate(ホストのレート)へ変換して返す。モノ/ステレオのみ対応(3ch以上は先頭2ch)。
    static bool load(const juce::File& file, double targetSampleRate, std::vector<std::vector<float>>& channels, juce::String& error);

    // 編集結果のWAV書き出し(bits: 16 / 24 / 32=float)
    static bool exportWav(const juce::File& file, const std::vector<std::vector<float>>& channels, double sampleRate, int bits, juce::String& error);
};
