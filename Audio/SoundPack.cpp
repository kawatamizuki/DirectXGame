#include "SoundPack.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
    constexpr unsigned int kPackKey = 0xA3C59AC3u;      // 鍵の元(名前のハッシュと混ぜる)
    constexpr char kMagic[4] = { 'G', 'S', 'P', 'K' };

    void PutU16(std::string& out, unsigned int value)
    {
        out.push_back(static_cast<char>(value & 0xFF));
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
    }
    void PutU32(std::string& out, unsigned int value)
    {
        for (int i = 0; i < 4; ++i) { out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF)); }
    }

    // 範囲を確かめながら読む小さな読み取り器。範囲を超えたら、okがfalseになる。
    struct Reader
    {
        const unsigned char* data;
        size_t size;
        size_t pos = 0;
        bool ok = true;

        bool Has(size_t count) const { return ok && count <= size - pos; }
        unsigned int U16()
        {
            if (!Has(2)) { ok = false; return 0; }
            const unsigned int v = data[pos] | (static_cast<unsigned int>(data[pos + 1]) << 8);
            pos += 2;
            return v;
        }
        unsigned int U32()
        {
            if (!Has(4)) { ok = false; return 0; }
            unsigned int v = 0;
            for (int i = 0; i < 4; ++i) { v |= static_cast<unsigned int>(data[pos + i]) << (8 * i); }
            pos += 4;
            return v;
        }
    };
}

unsigned int SoundPack::Hash(const unsigned char* data, size_t size)
{
    unsigned int hash = 2166136261u;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

void SoundPack::Scramble(const std::string& name, unsigned char* data, size_t size)
{
    // 名前のハッシュと固定の鍵から、xorshift32 の乱数の列を作り、その上位の1バイトを、順番に、かける(XOR)。
    unsigned int state = Hash(reinterpret_cast<const unsigned char*>(name.data()), name.size()) ^ kPackKey;
    if (state == 0) { state = 0x12345678u; }
    for (size_t i = 0; i < size; ++i)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        data[i] ^= static_cast<unsigned char>(state >> 16);
    }
}

bool SoundPack::Write(const std::string& path, const std::vector<SoundPackEntry>& entries, std::string& message)
{
    if (entries.size() > kMaxEntries)
    {
        message = "パックに入れるファイルが多すぎます";
        return false;
    }

    std::string out;
    out.append(kMagic, 4);
    PutU32(out, kVersion);
    PutU32(out, static_cast<unsigned int>(entries.size()));
    for (const SoundPackEntry& entry : entries)
    {
        if (entry.name.empty() || entry.name.size() > kMaxNameLength || entry.data.size() > 0x7FFFFFFFu)
        {
            message = "パックに入れられないファイルがあります: " + entry.name;
            return false;
        }
        PutU16(out, static_cast<unsigned int>(entry.name.size()));
        out += entry.name;
        PutU32(out, static_cast<unsigned int>(entry.data.size()));
        PutU32(out, Hash(entry.data.data(), entry.data.size()));
    }
    for (const SoundPackEntry& entry : entries)
    {
        std::vector<unsigned char> scrambled = entry.data;
        Scramble(entry.name, scrambled.data(), scrambled.size());
        out.append(reinterpret_cast<const char*>(scrambled.data()), scrambled.size());
    }

    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path())
    {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    const std::string tempPath = path + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            message = "パックを書けません: " + tempPath;
            return false;
        }
        file.write(out.data(), static_cast<std::streamsize>(out.size()));
        file.flush();
        if (!file)
        {
            file.close();
            std::filesystem::remove(tempPath, ec);
            message = "パックの書き込みに失敗しました: " + path;
            return false;
        }
    }
    std::filesystem::rename(tempPath, target, ec);
    if (ec)
    {
        std::filesystem::remove(tempPath, ec);
        message = "パックを置き換えられません: " + path;
        return false;
    }
    message = "パックを書きました: " + path;
    return true;
}

bool SoundPack::Read(const std::string& path, std::vector<SoundPackEntry>& out, std::string& message)
{
    out.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        message = "音のパックがありません: " + path;
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Reader reader{ bytes.data(), bytes.size() };
    if (!reader.Has(4) || std::memcmp(bytes.data(), kMagic, 4) != 0)
    {
        message = "音のパックの形式が違います: " + path;
        return false;
    }
    reader.pos = 4;
    const unsigned int version = reader.U32();
    const unsigned int count = reader.U32();
    if (!reader.ok || version != kVersion || count > kMaxEntries)
    {
        message = "音のパックの版が違う、または壊れています: " + path;
        return false;
    }

    struct Header { std::string name; unsigned int size; unsigned int hash; };
    std::vector<Header> headers;
    headers.reserve(count);
    for (unsigned int i = 0; i < count; ++i)
    {
        Header header;
        const unsigned int nameLength = reader.U16();
        if (!reader.ok || nameLength == 0 || nameLength > kMaxNameLength || !reader.Has(nameLength))
        {
            message = "音のパックの見出しが壊れています: " + path;
            return false;
        }
        header.name.assign(reinterpret_cast<const char*>(bytes.data() + reader.pos), nameLength);
        reader.pos += nameLength;
        header.size = reader.U32();
        header.hash = reader.U32();
        if (!reader.ok)
        {
            message = "音のパックの見出しが壊れています: " + path;
            return false;
        }
        headers.push_back(std::move(header));
    }

    // 中身の大きさの合計が、残りのバイト数と、ちょうど合うこと(途中で切れた・余計な物が付いたファイルを見つける)。
    size_t total = 0;
    for (const Header& header : headers)
    {
        if (header.size > bytes.size()) { message = "音のパックの大きさが合いません: " + path; return false; }
        total += header.size;
        if (total > bytes.size()) { message = "音のパックの大きさが合いません: " + path; return false; }
    }
    if (total != bytes.size() - reader.pos)
    {
        message = "音のパックの大きさが合いません(途中で切れている?): " + path;
        return false;
    }

    std::string skipped;
    size_t offset = reader.pos;
    for (const Header& header : headers)
    {
        SoundPackEntry entry;
        entry.name = header.name;
        entry.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + header.size));
        offset += header.size;
        Scramble(entry.name, entry.data.data(), entry.data.size());
        if (Hash(entry.data.data(), entry.data.size()) != header.hash)
        {
            skipped += (skipped.empty() ? "" : ", ") + header.name;
            continue;
        }
        out.push_back(std::move(entry));
    }

    message = "音のパックを読みました: " + std::to_string(out.size()) + " 件";
    if (!skipped.empty())
    {
        message += "(壊れていて飛ばした: " + skipped + ")";
    }
    return true;
}
