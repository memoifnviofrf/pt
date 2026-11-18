#pragma once
// VOCAL TUNE STUDIO - ProjectManager (JUCE): プロジェクトファイル(.vtsproj)の保存・復元と、DAWプロジェクト内に保存するプラグイン状態
#include <juce_audio_utils/juce_audio_utils.h>
#include "Core/EditSession.h"

class ProjectManager
{
public:
    // .vtsproj(テキスト) と .vtsproj.audio.wav(元音源, float32) を保存/読み込み
    static bool save(const vts::EditSession& session, const juce::File& file, juce::String& error);
    static bool load(vts::EditSession& session, const juce::File& file, juce::String& error);

    // DAW内保存: パラメーター(XML) + セッション(元音源・編集状態)を1つのブロックに
    static void packState(juce::MemoryBlock& dest, const juce::ValueTree& parameters, const vts::EditSession* session);
    // parametersXml: 復元したパラメーター状態。blob: セッションのバイト列(無ければ空)
    static bool unpackState(const void* data, int size, juce::String& parametersXml, std::vector<uint8_t>& sessionBlob);
};
