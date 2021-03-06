#include "writer.h"

#include <util/code.h>
#include <util/file.h>

#include <memory.h>

template <typename T>
inline void Write(ostream& output, T value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

namespace NCodesearch {

////////////////////////////////////////////////////////////////
// TIndexWriter

TIndexWriter::TIndexWriter(const TIndexWriterConfig& config)
    : Config(config)
    , Chunk(TRI_COUNT)
    , DataOffset(0)
{
    if (Config.Verbose) {
        cerr << "Indexer config:\n";
        Config.Print(cerr);
        cerr << "===============\n";
    }
    Encoder = CreateEncoder(Config.CompressionMethod);
}

TIndexWriter::~TIndexWriter() {
    if (Encoder)
        delete Encoder;
}

void TIndexWriter::Index(const vector<string>& files, const char* idxFile, const char* datFile) {
    DataOffset = 0;
    Chunk.Reset(TRI_COUNT);

    TBufferedFileOutput idxOutput(idxFile);
    TBufferedFileOutput datOutput(datFile);

    TDocId filesCount = files.size();
    Write(idxOutput, static_cast<uint32_t>(Config.CompressionMethod));
    Write(idxOutput, filesCount);
    for (TDocId i = 0; i != filesCount; ++i) {
        Write(idxOutput, DataOffset);
        Write(datOutput, static_cast<TOffset>(files[i].size()));
        datOutput.Get() << files[i];
        DataOffset += sizeof(TOffset) + files[i].size();
    }

    for (TDocId i = 0; i != filesCount; ++i)
        Index(i + 1, files[i].c_str(), idxOutput, datOutput);
    if (Chunk.Size)
        FlushChunk(idxOutput, datOutput);
}

void TIndexWriter::Index(TDocId docId, const char* filename, ostream& idxOutput, ostream& datOutput) {
    if (Config.Verbose)
        cerr << "Indexing " << docId << ": " << filename << '\n';

    TBufferedFileInput bufInput(filename);
    istream& input = bufInput.Get();

    char chars[4];
    memset(chars, 0, sizeof(chars));
    if (!input.get(chars[0]) || !input.get(chars[1]))
        return;
    vector<bool> used(TRI_COUNT);
    while (input.get(chars[2])) {
        TTrigram tri = TByte(chars[0]) | (TByte(chars[1]) << 8) | (TByte(chars[2]) << 16);
        if (!used[tri]) {
            Chunk.Add(tri, docId);
            used[tri] = true;
        }
        chars[0] = chars[1];
        chars[1] = chars[2];
    }
    if (Chunk.Size >= Config.ChunkSize)
        FlushChunk(idxOutput, datOutput);
}

void TIndexWriter::FlushChunk(ostream& idxOutput, ostream& datOutput) {
    if (Config.Verbose)
        cerr << "Flush chunk [size = " << Chunk.Size << "]\n";
    Write(idxOutput, Chunk.Number++);
    for (TTrigram tri = 0, last = Chunk.Lists.size(); tri != last; ++tri) {
        Write(idxOutput, DataOffset);
        TPostingList& list = Chunk.Lists[tri];
        if (list.empty())
            continue;
        TDocId lastDoc = list.back();
        for (size_t i = list.size() - 1; i; --i)
            list[i] -= list[i - 1];
        list[0] -= Chunk.LastDocs[tri];
        Chunk.LastDocs[tri] = lastDoc;
        DataOffset += Encoder->Encode(datOutput, list);
        list.clear();
        list.shrink_to_fit();
    }
    Write(idxOutput, DataOffset);
    Chunk.Size = 0;
}

////////////////////////////////////////////////////////////////
// TIndexWriterConfig

void TIndexWriterConfig::SetDefault() {
    Verbose = false;
    ChunkSize = 1 << 27;
    CompressionMethod = C_ELIAS_DELTA;
}

void TIndexWriterConfig::Print(ostream& output) const {
    char buffer[64];
    OUTPUT_CONFIG_VALUE(Verbose, "%d");
    OUTPUT_CONFIG_VALUE(ChunkSize, "%lu");
    OUTPUT_CONFIG_HEADER(CompressionMethod);
    output << buffer << CompressionMethod << '\n';  // TODO: literal repesentation
}

} // NCodesearch

