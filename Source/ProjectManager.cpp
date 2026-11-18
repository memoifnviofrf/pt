#include "ProjectManager.h"

bool ProjectManager::save(const vts::EditSession& session, const juce::File& file, juce::String& error)
{
    std::string e;
    if (! session.hasAudio()) { error = "Nothing to save"; return false; }
    if (! session.saveProject(file.getFullPathName().toStdString(), &e)) { error = juce::String(e); return false; }
    return true;
}

bool ProjectManager::load(vts::EditSession& session, const juce::File& file, juce::String& error)
{
    std::string e;
    if (! session.loadProject(file.getFullPathName().toStdString(), &e)) { error = juce::String(e); return false; }
    return true;
}

void ProjectManager::packState(juce::MemoryBlock& dest, const juce::ValueTree& parameters, const vts::EditSession* session)
{
    juce::MemoryOutputStream out(dest, false);
    juce::String xml;
    if (auto x = parameters.createXml()) xml = x->toString();
    out.writeString(xml);
    std::vector<uint8_t> blob;
    if (session != nullptr && session->hasAudio()) blob = session->toBlob();
    out.writeInt64((juce::int64) blob.size());
    if (! blob.empty()) out.write(blob.data(), blob.size());
}

bool ProjectManager::unpackState(const void* data, int size, juce::String& parametersXml, std::vector<uint8_t>& sessionBlob)
{
    if (data == nullptr || size < 16) return false;
    juce::MemoryInputStream in(data, (size_t) size, false);
    parametersXml = in.readString();
    const juce::int64 n = in.readInt64();
    if (n < 0 || (juce::int64) in.getNumBytesRemaining() < n) return false;
    sessionBlob.resize((size_t) n);
    if (n > 0 && in.read(sessionBlob.data(), (int) n) != (int) n) return false;
    return true;
}
