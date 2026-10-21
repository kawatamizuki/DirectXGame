#include "BakedModelCache.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include "Debug.h"

namespace fs = std::filesystem;

static_assert(sizeof(Vertex) == 12 * sizeof(float), "Vertex に隙間(パディング)があると、バイト列の比較・保存の前提が崩れる");

namespace
{
    // ---- ファイルの形式 ----
    // [ヘッダー 56バイト]
    //   magic(4)="GMSH" / version(4) / vertexCount(4)=展開後の頂点数 / uniqueCount(4)=重複を除いた頂点数 / indexSize(4)=2か4
    //   / boundsMin(12) / boundsMax(12) / texturePathLength(4) / checksum(8)=ヘッダー(checksum自身を除く)と、その後ろ全部をつなげたFNV-1a
    // [テクスチャのパス(文字列、texturePathLengthバイト)]
    // [重複のない頂点 × uniqueCount(1つ48バイト)]
    // [インデックス × vertexCount(1つ indexSize バイト)]
    // 数値は全てこの機械(x64、リトルエンディアン)の並びのまま。別の機械へ持っていく物ではなく、各自の Cache/ に作られる物なので、それで足りる。
    constexpr char kMagic[4] = { 'G', 'M', 'S', 'H' };
    constexpr size_t kHeaderSize = 56;
    constexpr size_t kChecksumOffset = 48;                   // ヘッダーの中の、チェックの値の位置
    constexpr uint32_t kMaxVertices = 100u * 1000u * 1000u;  // これを超える頂点数は、壊れたファイルとみなす
    constexpr uint32_t kMaxTexturePathLength = 4096;

    bool g_enabled = true;
    std::string g_directory = "Cache/Models";
    BakedModelCache::Stats g_stats;

    // 同じフォルダの .mtl の更新日時の目印を、フォルダごとに覚えておく(同じフォルダの数百個のモデルを読む時に、毎回フォルダを列挙しない)。
    std::unordered_map<std::string, std::string> g_mtlStampByDirectory;

    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    uint64_t Fnv1a(const void* data, size_t size, uint64_t hash = kFnvOffset)
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= kFnvPrime;
        }
        return hash;
    }

    std::string Hex16(uint64_t value)
    {
        static const char digits[] = "0123456789abcdef";
        std::string text(16, '0');
        for (int i = 15; i >= 0; --i)
        {
            text[i] = digits[value & 0xF];
            value >>= 4;
        }
        return text;
    }

    // ファイル名に使える形に直す(英数字・_・- 以外は _ に。長すぎる時は切る)。名前はエクスプローラーで見て分かるようにするための物で、
    // 一意性はキー(ハッシュ)が受け持つ。
    std::string SafeStem(const std::string& stem)
    {
        std::string result;
        for (char c : stem)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            result += (u < 0x80 && (std::isalnum(u) || c == '-' || c == '_')) ? c : '_';
            if (result.size() >= 40) { break; }
        }
        return result.empty() ? std::string("model") : result;
    }

    // フォルダの中の .mtl ファイルの「名前:大きさ:更新日時」を並べた文字列(MTLの色を変えた時に、キャッシュを作り直すための目印)。
    const std::string& MtlStamp(const std::string& directory)
    {
        auto found = g_mtlStampByDirectory.find(directory);
        if (found != g_mtlStampByDirectory.end())
        {
            return found->second;
        }

        std::string stamp;
        std::error_code ec;
        std::vector<std::string> parts;
        for (fs::directory_iterator it(fs::path(directory.empty() ? "." : directory), fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc))
            {
                continue;
            }
            std::string extension = it->path().extension().string();
            for (char& c : extension) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            if (extension != ".mtl")
            {
                continue;
            }
            std::error_code sizeEc, timeEc;
            parts.push_back(it->path().filename().string() + ":" +
                std::to_string(static_cast<uint64_t>(fs::file_size(it->path(), sizeEc))) + ":" +
                std::to_string(static_cast<int64_t>(fs::last_write_time(it->path(), timeEc).time_since_epoch().count())));
        }
        std::sort(parts.begin(), parts.end());
        for (const std::string& part : parts) { stamp += part + ";"; }
        return g_mtlStampByDirectory.emplace(directory, std::move(stamp)).first->second;
    }

    template <typename T>
    void Append(std::vector<uint8_t>& buffer, const T& value)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
        buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
    }

    template <typename T>
    T Read(const uint8_t* bytes, size_t offset)
    {
        T value;
        std::memcpy(&value, bytes + offset, sizeof(T));
        return value;
    }

    // 頂点のバイト列が同じ物を、同じ頂点とみなす(位置・法線・UV・色が、全く同じ値の時だけまとめる)。
    struct VertexPointerHash
    {
        size_t operator()(const Vertex* vertex) const { return static_cast<size_t>(Fnv1a(vertex, sizeof(Vertex))); }
    };
    struct VertexPointerEqual
    {
        bool operator()(const Vertex* a, const Vertex* b) const { return std::memcmp(a, b, sizeof(Vertex)) == 0; }
    };

    double ElapsedMs(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
}

void BakedModelCache::SetEnabled(bool enabled) { g_enabled = enabled; }
bool BakedModelCache::IsEnabled() { return g_enabled; }

void BakedModelCache::SetDirectory(const std::string& directory)
{
    g_directory = directory;
}

const std::string& BakedModelCache::GetDirectory()
{
    return g_directory;
}

BakedModelCache::Stats BakedModelCache::GetStats() { return g_stats; }

void BakedModelCache::ResetStats()
{
    g_stats = Stats();
    g_mtlStampByDirectory.clear(); // MTLの目印も取り直す(テストで、MTLを書き換えた後に使う)
}

std::string BakedModelCache::GetCachePath(const std::string& modelPath)
{
    std::error_code sizeEc, timeEc, existsEc;
    const fs::path path(modelPath);
    if (!fs::is_regular_file(path, existsEc))
    {
        return std::string();
    }

    const uint64_t fileSize = static_cast<uint64_t>(fs::file_size(path, sizeEc));
    const int64_t modified = static_cast<int64_t>(fs::last_write_time(path, timeEc).time_since_epoch().count());
    const std::string directory = path.parent_path().generic_string();

    const std::string identity = path.generic_string() + "|" + std::to_string(fileSize) + "|" + std::to_string(modified) +
        "|" + MtlStamp(directory) + "|v" + std::to_string(kVersion);
    const uint64_t key = Fnv1a(identity.data(), identity.size());

    return g_directory + "/" + SafeStem(path.stem().string()) + "_" + Hex16(key) + ".mesh";
}

bool BakedModelCache::Serialize(const ModelData& data, std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    const size_t vertexCount = data.vertices.size();
    if (vertexCount == 0 || vertexCount % 3 != 0 || vertexCount > kMaxVertices ||
        data.texturePath.size() > kMaxTexturePathLength)
    {
        return false;
    }

    // 重複のない頂点の表と、各頂点がその表の何番目かを求める。
    std::vector<uint32_t> indices;
    indices.reserve(vertexCount);
    std::vector<Vertex> unique;
    std::unordered_map<const Vertex*, uint32_t, VertexPointerHash, VertexPointerEqual> lookup;
    lookup.reserve(vertexCount);
    for (const Vertex& vertex : data.vertices)
    {
        auto inserted = lookup.emplace(&vertex, static_cast<uint32_t>(unique.size()));
        if (inserted.second)
        {
            unique.push_back(vertex);
        }
        indices.push_back(inserted.first->second);
    }

    const uint32_t indexSize = (unique.size() <= 65536) ? 2u : 4u;

    // ヘッダーの後ろ(テクスチャのパス・頂点・インデックス)を先に作って、チェックの値を求める。
    std::vector<uint8_t> body;
    body.reserve(data.texturePath.size() + unique.size() * sizeof(Vertex) + indices.size() * indexSize);
    body.insert(body.end(), data.texturePath.begin(), data.texturePath.end());
    const uint8_t* vertexBytes = reinterpret_cast<const uint8_t*>(unique.data());
    body.insert(body.end(), vertexBytes, vertexBytes + unique.size() * sizeof(Vertex));
    for (uint32_t index : indices)
    {
        if (indexSize == 2)
        {
            Append(body, static_cast<uint16_t>(index));
        }
        else
        {
            Append(body, index);
        }
    }

    outBytes.reserve(kHeaderSize + body.size());
    outBytes.insert(outBytes.end(), kMagic, kMagic + 4);
    Append(outBytes, static_cast<uint32_t>(kVersion));
    Append(outBytes, static_cast<uint32_t>(vertexCount));
    Append(outBytes, static_cast<uint32_t>(unique.size()));
    Append(outBytes, indexSize);
    Append(outBytes, data.boundsMin);
    Append(outBytes, data.boundsMax);
    Append(outBytes, static_cast<uint32_t>(data.texturePath.size()));

    // チェックの値は、ヘッダー(チェック自身を除く)と、その後ろ全部をつなげたFNV-1a。バウンディングボックスの値が1バイト壊れても検出できる。
    const uint64_t checksum = Fnv1a(body.data(), body.size(), Fnv1a(outBytes.data(), kChecksumOffset));
    Append(outBytes, checksum);
    outBytes.insert(outBytes.end(), body.begin(), body.end());
    return outBytes.size() >= kHeaderSize;
}

bool BakedModelCache::Deserialize(const uint8_t* bytes, size_t size, ModelData& out)
{
    out.Clear();
    if (!bytes || size < kHeaderSize || std::memcmp(bytes, kMagic, 4) != 0)
    {
        return false;
    }

    const uint32_t version = Read<uint32_t>(bytes, 4);
    const uint32_t vertexCount = Read<uint32_t>(bytes, 8);
    const uint32_t uniqueCount = Read<uint32_t>(bytes, 12);
    const uint32_t indexSize = Read<uint32_t>(bytes, 16);
    const DirectX::XMFLOAT3 boundsMin = Read<DirectX::XMFLOAT3>(bytes, 20);
    const DirectX::XMFLOAT3 boundsMax = Read<DirectX::XMFLOAT3>(bytes, 32);
    const uint32_t texturePathLength = Read<uint32_t>(bytes, 44);
    const uint64_t checksum = Read<uint64_t>(bytes, 48);

    // 数値の範囲を全て確かめる(壊れたファイルで、巨大なメモリを確保したり、範囲外を読んだりしないため)。
    if (version != kVersion || vertexCount == 0 || vertexCount % 3 != 0 || vertexCount > kMaxVertices ||
        uniqueCount == 0 || uniqueCount > vertexCount || (indexSize != 2 && indexSize != 4) ||
        (indexSize == 2 && uniqueCount > 65536) || texturePathLength > kMaxTexturePathLength)
    {
        return false;
    }

    const uint64_t expectedSize = static_cast<uint64_t>(kHeaderSize) + texturePathLength +
        static_cast<uint64_t>(uniqueCount) * sizeof(Vertex) + static_cast<uint64_t>(vertexCount) * indexSize;
    if (expectedSize != size)
    {
        return false;
    }

    if (Fnv1a(bytes + kHeaderSize, size - kHeaderSize, Fnv1a(bytes, kChecksumOffset)) != checksum)
    {
        return false;
    }

    const uint8_t* texturePathBytes = bytes + kHeaderSize;
    const uint8_t* vertexBytes = texturePathBytes + texturePathLength;
    const uint8_t* indexBytes = vertexBytes + static_cast<size_t>(uniqueCount) * sizeof(Vertex);

    // 元の並び(三角形ごとに頂点を並べる形)へ展開する。
    std::vector<Vertex> vertices(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i)
    {
        const uint32_t index = (indexSize == 2)
            ? static_cast<uint32_t>(Read<uint16_t>(indexBytes, static_cast<size_t>(i) * 2))
            : Read<uint32_t>(indexBytes, static_cast<size_t>(i) * 4);
        if (index >= uniqueCount)
        {
            return false; // 範囲外のインデックス
        }
        std::memcpy(&vertices[i], vertexBytes + static_cast<size_t>(index) * sizeof(Vertex), sizeof(Vertex));
    }

    out.vertices = std::move(vertices);
    out.texturePath.assign(reinterpret_cast<const char*>(texturePathBytes), texturePathLength);
    out.boundsMin = boundsMin;
    out.boundsMax = boundsMax;
    return true;
}

bool BakedModelCache::TryLoad(const std::string& modelPath, ModelData& out)
{
    if (!g_enabled)
    {
        return false;
    }

    const auto start = std::chrono::steady_clock::now();

    const std::string cachePath = GetCachePath(modelPath);
    if (cachePath.empty())
    {
        return false;
    }

    std::ifstream file(cachePath, std::ios::binary);
    if (!file)
    {
        return false; // まだ作っていない(ミス)
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // 中身が正しくても、保存した時に有ったテクスチャのファイルが、今は無い(消した・動かした)なら、保存した結果は古い。
    // ローダーで読み直せば、別の場所のテクスチャを見つけられるかもしれないので、捨てて作り直す。
    bool valid = Deserialize(bytes.data(), bytes.size(), out);
    if (valid && !out.texturePath.empty())
    {
        std::error_code textureEc;
        valid = fs::exists(fs::path(out.texturePath), textureEc);
    }

    if (!valid)
    {
        // 壊れている・版が違う・チェックが合わない: 捨てて、ローダーで読み直して作り直してもらう。
        ++g_stats.invalidated;
        std::error_code ec;
        fs::remove(fs::path(cachePath), ec);
        out.Clear();
        return false;
    }

    ++g_stats.hits;
    g_stats.hitMs += ElapsedMs(start);
    return true;
}

bool BakedModelCache::Store(const std::string& modelPath, const ModelData& data, double loaderMs)
{
    if (!g_enabled)
    {
        return false;
    }

    ++g_stats.misses;
    g_stats.missLoadMs += loaderMs;

    const auto start = std::chrono::steady_clock::now();

    const std::string cachePath = GetCachePath(modelPath);
    std::vector<uint8_t> bytes;
    if (cachePath.empty() || !Serialize(data, bytes))
    {
        ++g_stats.storeFailures;
        return false;
    }

    std::error_code ec;
    fs::create_directories(fs::path(g_directory), ec);
    if (ec)
    {
        ++g_stats.storeFailures;
        return false;
    }

    // 一時ファイルに書き切ってから置き換える(書き込み中に落ちても、半端なキャッシュが残らない)。
    const std::string tempPath = cachePath + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            ++g_stats.storeFailures;
            return false;
        }
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file)
        {
            file.close();
            fs::remove(fs::path(tempPath), ec);
            ++g_stats.storeFailures;
            return false;
        }
    }

    fs::rename(fs::path(tempPath), fs::path(cachePath), ec);
    if (ec)
    {
        fs::remove(fs::path(tempPath), ec);
        ++g_stats.storeFailures;
        return false;
    }

    ++g_stats.stores;
    g_stats.storeMs += ElapsedMs(start);
    return true;
}

void BakedModelCache::MeasureDisk(size_t& outFileCount, uint64_t& outBytes)
{
    outFileCount = 0;
    outBytes = 0;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(g_directory), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec))
    {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && it->path().extension() == ".mesh")
        {
            ++outFileCount;
            outBytes += static_cast<uint64_t>(fs::file_size(it->path(), fileEc));
        }
    }
}

size_t BakedModelCache::ClearAll()
{
    size_t removed = 0;
    std::error_code ec;
    std::vector<fs::path> targets;
    for (fs::directory_iterator it(fs::path(g_directory), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec))
    {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc))
        {
            continue;
        }
        const std::string extension = it->path().extension().string();
        if (extension == ".mesh" || extension == ".tmp")
        {
            targets.push_back(it->path());
        }
    }
    for (const fs::path& target : targets)
    {
        std::error_code removeEc;
        if (fs::remove(target, removeEc))
        {
            ++removed;
        }
    }
    return removed;
}
