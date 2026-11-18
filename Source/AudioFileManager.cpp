#include "AudioFileManager.h"
#include "Core/Resampler.h"
#include "Core/WavIO.h"

bool AudioFileManager::load(const juce::File& file, double targetSr, std::vector<std::vector<float>>& channels, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr) { error = "Unsupported or unreadable audio file"; return false; }
    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0) { error = "Empty audio file"; return false; }
    if (reader->lengthInSamples > (juce::int64) reader->sampleRate * 60 * 20) { error = "File too long (limit: 20 minutes)"; return false; }

    const int numCh = juce::jmin(2, (int) reader->numChannels);
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buf(numCh, n);
    if (! reader->read(&buf, 0, n, 0, true, numCh > 1)) { error = "Failed to read audio data"; return false; }

    channels.assign((size_t) numCh, {});
    for (int c = 0; c < numCh; ++c)
    {
        std::vector<float> x(buf.getReadPointer(c), buf.getReadPointer(c) + n);
        channels[(size_t) c] = vts::resample(x, reader->sampleRate, targetSr);   // 同一レートなら素通し
    }
    return true;
}

bool AudioFileManager::exportWav(const juce::File& file, const std::vector<std::vector<float>>& channels, double sampleRate, int bits, juce::String& error)
{
    vts::WavData w;
    w.sampleRate = sampleRate;
    w.ch = channels;
    std::string e;
    if (! vts::WavIO::write(file.getFullPathName().toStdString(), w, bits, &e)) { error = juce::String(e); return false; }
    return true;
}
