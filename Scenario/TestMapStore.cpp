#include "TestMapStore.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include "AtomicFile.h"
#include "Json.h"

namespace
{
    namespace fs = std::filesystem;

    constexpr size_t kMaxIdLength = 48;
    constexpr size_t kMaxNameLength = 120;          // バイト数(日本語は1文字3バイト)
    constexpr size_t kMaxTextLength = 4000;
    constexpr size_t kMaxFocusMetrics = 32;

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    // FNV-1a 64bit。
    std::uint64_t Fnv1a(const std::string& text)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : text)
        {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string NowText()
    {
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        char text[32];
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
        return text;
    }

    const char* BuildText()
    {
        return __DATE__ " " __TIME__;
    }

    Json MetaToJson(const TestMapMeta& meta)
    {
        Json json = Json::MakeObject();
        json.Set("schema", TestMapStore::kSchemaVersion);
        json.Set("name", meta.name);
        json.Set("description", meta.description);
        json.Set("purpose", meta.purpose);
        Json metrics = Json::MakeArray();
        for (const std::string& metric : meta.focusMetrics) { metrics.Push(metric); }
        json.Set("focusMetrics", std::move(metrics));
        json.Set("createdAt", meta.createdAt);
        json.Set("gameBuild", meta.gameBuild);
        return json;
    }

    TestMapMeta MetaFromJson(const Json& json)
    {
        TestMapMeta meta;
        meta.name = json.GetString("name");
        meta.description = json.GetString("description");
        meta.purpose = json.GetString("purpose");
        meta.createdAt = json.GetString("createdAt");
        meta.gameBuild = json.GetString("gameBuild");
        if (const Json* metrics = json.Find("focusMetrics"))
        {
            if (metrics->IsArray())
            {
                for (size_t i = 0; i < metrics->Size() && i < kMaxFocusMetrics; ++i)
                {
                    if (metrics->At(i).IsString()) { meta.focusMetrics.push_back(metrics->At(i).AsString()); }
                }
            }
        }
        return meta;
    }

    // メタ情報を、長さ・中身で整える(保存・更新の前)。問題があれば理由を返す(空なら問題なし)。
    std::string CheckMeta(TestMapMeta& meta)
    {
        if (meta.name.size() > kMaxNameLength) { return "名前が長すぎます"; }
        if (meta.description.size() > kMaxTextLength || meta.purpose.size() > kMaxTextLength) { return "説明・検証目的が長すぎます"; }
        if (meta.focusMetrics.size() > kMaxFocusMetrics) { return "注目する指標が多すぎます"; }
        for (const std::string& metric : meta.focusMetrics)
        {
            if (metric.size() > kMaxNameLength) { return "注目する指標の名前が長すぎます"; }
        }
        return std::string();
    }

    // 街の部分(メタ情報 "testMap" を除いた内容)。
    Json WithoutMeta(const Json& root)
    {
        Json copy = Json::MakeObject();
        for (const auto& member : root.Members())
        {
            if (member.first != "testMap") { copy.Set(member.first, member.second); }
        }
        return copy;
    }
}

TestMapStore::TestMapStore(std::string root)
    : m_root(std::move(root))
{
}

std::string TestMapStore::DraftDirectory() const
{
    return (fs::path(m_root) / kDraftSubdirectory).generic_string();
}

std::string TestMapStore::DirectoryOf(TestMapFolder folder) const
{
    return folder == TestMapFolder::Official ? fs::path(m_root).generic_string() : DraftDirectory();
}

bool TestMapStore::IsValidId(const std::string& id, std::string* why)
{
    auto fail = [&](const char* text) { if (why) { *why = text; } return false; };
    if (id.empty()) { return fail("名前(ファイル名)が空です"); }
    if (id.size() > kMaxIdLength) { return fail("名前(ファイル名)が長すぎます(48文字まで)"); }
    for (char c : id)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '_' || c == '-') || u >= 0x80)
        {
            return fail("名前(ファイル名)に使えるのは、半角の英数字・「_」・「-」だけです(日本語の名前は、表示名に書けます)");
        }
    }
    if (!std::isalnum(static_cast<unsigned char>(id[0]))) { return fail("名前(ファイル名)の先頭は、英数字にしてください"); }
    const std::string lower = Lower(id);
    static const char* kReserved[] = { "con", "prn", "aux", "nul", "drafts", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
                                       "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
    for (const char* reserved : kReserved)
    {
        if (lower == reserved) { return fail("その名前は、予約されているので、使えません"); }
    }
    return true;
}

bool TestMapStore::ComputeFingerprint(const std::string& fileBytes, std::uint64_t& out)
{
    Json root;
    std::string error;
    if (!Json::Parse(fileBytes, root, error) || !root.IsObject())
    {
        return false;
    }
    out = Fnv1a(WithoutMeta(root).Dump(false));
    return true;
}

bool TestMapStore::ReadEntry(const std::string& path, const std::string& id, TestMapFolder folder, TestMapEntry& out) const
{
    out = TestMapEntry{};
    out.id = id;
    out.folder = folder;
    out.path = path;
    std::string bytes;
    std::string error;
    if (!ReadFileBytes(path, bytes, error))
    {
        out.error = error;
        return false;
    }
    out.sizeBytes = bytes.size();
    Json root;
    if (!Json::Parse(bytes, root, error) || !root.IsObject())
    {
        out.error = "JSONとして読めません: " + error;
        return false;
    }
    if (!root.Has("version"))
    {
        out.error = "セーブデータではありません(versionがありません)";
        return false;
    }
    out.fingerprint = Fnv1a(WithoutMeta(root).Dump(false));
    if (const Json* meta = root.Find("testMap"))
    {
        if (meta->IsObject()) { out.meta = MetaFromJson(*meta); }
    }
    if (out.meta.name.empty()) { out.meta.name = id; }
    out.valid = true;
    return true;
}

std::vector<TestMapEntry> TestMapStore::List() const
{
    std::vector<TestMapEntry> entries;
    for (TestMapFolder folder : { TestMapFolder::Official, TestMapFolder::Draft })
    {
        std::vector<TestMapEntry> part;
        std::error_code ec;
        const fs::path directory(DirectoryOf(folder));
        if (!fs::is_directory(directory, ec)) { continue; }
        for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code ec2;
            if (!it->is_regular_file(ec2)) { continue; }
            const fs::path& file = it->path();
            if (Lower(file.extension().string()) != ".json") { continue; }
            const std::string id = file.stem().string();
            TestMapEntry entry;
            ReadEntry(file.generic_string(), id, folder, entry);
            part.push_back(std::move(entry));
        }
        std::sort(part.begin(), part.end(), [](const TestMapEntry& a, const TestMapEntry& b) { return Lower(a.id) < Lower(b.id); });
        entries.insert(entries.end(), part.begin(), part.end());
    }
    return entries;
}

bool TestMapStore::ResolvePath(const std::string& id, std::string& path, TestMapFolder& folder, bool* ambiguous) const
{
    if (ambiguous) { *ambiguous = false; }
    if (!IsValidId(id)) { return false; }
    const std::string lower = Lower(id);
    bool found = false;
    for (TestMapFolder f : { TestMapFolder::Official, TestMapFolder::Draft })
    {
        std::error_code ec;
        const fs::path directory(DirectoryOf(f));
        if (!fs::is_directory(directory, ec)) { continue; }
        for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code ec2;
            if (!it->is_regular_file(ec2) || Lower(it->path().extension().string()) != ".json") { continue; }
            if (Lower(it->path().stem().string()) == lower)
            {
                if (found) { if (ambiguous) { *ambiguous = true; } return true; }   // 両方にある
                path = it->path().generic_string();
                folder = f;
                found = true;
                break;
            }
        }
    }
    return found;
}

bool TestMapStore::Locate(const std::string& id, std::string& path, TestMapFolder& folder, std::string& message) const
{
    bool ambiguous = false;
    if (!ResolvePath(id, path, folder, &ambiguous))
    {
        message = "テストマップが見つかりません: " + id;
        return false;
    }
    if (ambiguous)
    {
        message = "同じ名前のテストマップが、正式(TestMaps/)と下書き(TestMaps/Drafts/)の両方にあります。どちらかを、整理してください: " + id;
        return false;
    }
    return true;
}

bool TestMapStore::Find(const std::string& id, TestMapEntry& out) const
{
    std::string path;
    TestMapFolder folder = TestMapFolder::Official;
    if (!ResolvePath(id, path, folder)) { return false; }
    ReadEntry(path, fs::path(path).stem().string(), folder, out);   // 壊れたファイルでも、見つかった物として返す(valid=false)
    return true;
}

bool TestMapStore::WriteWithMeta(const std::string& path, const std::string& worldBytes, const TestMapMeta& meta, std::string& error) const
{
    Json root;
    if (!Json::Parse(worldBytes, root, error) || !root.IsObject())
    {
        error = "街のデータを読み直せません: " + error;
        return false;
    }
    Json combined = WithoutMeta(root);
    combined.Set("testMap", MetaToJson(meta));
    return WriteFileAtomic(path, combined.Dump(true), error);
}

bool TestMapStore::SaveNew(const std::string& id, const TestMapMeta& metaIn, TestMapFolder folder, std::string& message)
{
    std::string why;
    if (!IsValidId(id, &why)) { message = "保存できません: " + why; return false; }
    std::string existing;
    TestMapFolder existingFolder = TestMapFolder::Official;
    if (ResolvePath(id, existing, existingFolder))
    {
        message = "保存できません: 同じ名前のテストマップが、既にあります(上書きはしません): " + existing;
        return false;
    }
    TestMapMeta meta = metaIn;
    const std::string problem = CheckMeta(meta);
    if (!problem.empty()) { message = "保存できません: " + problem; return false; }
    if (meta.name.empty()) { meta.name = id; }
    meta.createdAt = NowText();
    meta.gameBuild = BuildText();

    if (!m_io.capture) { message = "保存できません: 街の入出力が設定されていません"; return false; }
    std::string bytes;
    std::string error;
    if (!m_io.capture(bytes, error)) { message = "保存できません: " + error; return false; }

    const std::string path = (fs::path(DirectoryOf(folder)) / (id + ".json")).generic_string();
    if (!WriteWithMeta(path, bytes, meta, error)) { message = "保存できません: " + error; return false; }
    message = "テストマップを保存しました: " + path;
    return true;
}

bool TestMapStore::UpdateMeta(const std::string& id, const TestMapMeta& metaIn, std::string& message)
{
    std::string path;
    TestMapFolder folder = TestMapFolder::Official;
    if (!Locate(id, path, folder, message)) { return false; }
    TestMapEntry entry;
    if (!ReadEntry(path, id, folder, entry)) { message = "読めないテストマップは、直せません: " + entry.error; return false; }
    TestMapMeta meta = metaIn;
    const std::string problem = CheckMeta(meta);
    if (!problem.empty()) { message = "更新できません: " + problem; return false; }
    if (meta.name.empty()) { meta.name = id; }
    meta.createdAt = entry.meta.createdAt;
    meta.gameBuild = entry.meta.gameBuild;

    std::string bytes;
    std::string error;
    if (!ReadFileBytes(path, bytes, error) || !WriteWithMeta(path, bytes, meta, error)) { message = "更新できません: " + error; return false; }
    message = "テストマップの情報を更新しました: " + id;
    return true;
}

bool TestMapStore::Rename(const std::string& id, const std::string& newId, std::string& message)
{
    std::string why;
    if (!IsValidId(newId, &why)) { message = "名前を変えられません: " + why; return false; }
    std::string path;
    TestMapFolder folder = TestMapFolder::Official;
    if (!Locate(id, path, folder, message)) { return false; }
    std::string other;
    TestMapFolder otherFolder = TestMapFolder::Official;
    if (ResolvePath(newId, other, otherFolder) && Lower(newId) != Lower(id))
    {
        message = "名前を変えられません: 同じ名前のテストマップが、既にあります: " + newId;
        return false;
    }
    const std::string newPath = (fs::path(DirectoryOf(folder)) / (newId + ".json")).generic_string();
    std::error_code ec;
    fs::rename(path, newPath, ec);
    if (ec) { message = "名前を変えられません: " + ec.message(); return false; }
    if (m_active.valid && Lower(m_active.id) == Lower(id)) { m_active.id = newId; }
    message = "名前を変えました: " + id + " -> " + newId;
    return true;
}

bool TestMapStore::Move(const std::string& id, TestMapFolder to, std::string& message)
{
    std::string path;
    TestMapFolder folder = TestMapFolder::Official;
    if (!Locate(id, path, folder, message)) { return false; }
    if (folder == to) { message = "既に、その場所にあります: " + id; return true; }
    const std::string newPath = (fs::path(DirectoryOf(to)) / (fs::path(path).filename())).generic_string();
    std::error_code ec;
    if (fs::exists(newPath, ec)) { message = "移せません: 移す先に、同じ名前があります"; return false; }
    fs::create_directories(fs::path(DirectoryOf(to)), ec);
    fs::rename(path, newPath, ec);
    if (ec) { message = "移せません: " + ec.message(); return false; }
    message = std::string(to == TestMapFolder::Official ? "正式に採用しました: " : "下書きへ戻しました: ") + id;
    return true;
}

bool TestMapStore::Delete(const std::string& id, std::string& message)
{
    std::string path;
    TestMapFolder folder = TestMapFolder::Official;
    if (!Locate(id, path, folder, message)) { return false; }
    std::error_code ec;
    if (!fs::remove(path, ec) || ec) { message = "削除できません: " + (ec ? ec.message() : std::string("ファイルがありません")); return false; }
    if (m_active.valid && Lower(m_active.id) == Lower(id)) { m_active = ActiveMap{}; }
    message = "削除しました: " + id;
    return true;
}

bool TestMapStore::Load(const std::string& id, std::string& message)
{
    std::string path;
    TestMapFolder folder = TestMapFolder::Official;
    if (!Locate(id, path, folder, message)) { return false; }
    TestMapEntry entry;
    if (!ReadEntry(path, fs::path(path).stem().string(), folder, entry)) { message = "読み込めません: " + entry.error; return false; }
    if (!m_io.loadFile) { message = "読み込めません: 街の入出力が設定されていません"; return false; }
    if (!m_io.loadFile(path, message)) { return false; }
    m_active.valid = true;
    m_active.id = entry.id;
    m_active.name = entry.meta.name;
    m_active.fingerprint = entry.fingerprint;
    return true;
}
