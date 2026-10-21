#include "AssetCatalog.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>
#include "ModelLoaderRegistry.h"
#include "Debug.h"

namespace fs = std::filesystem;

namespace
{
    std::string ToLower(std::string text)
    {
        for (char& c : text)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    // 形式の優先順位(小さいほど優先)。同じ名前のモデルが複数の形式で有る時、優先が高い1つだけを残す。
    // OBJは読み込みが単純で、Kenneyの素材では標準の形式。FBXはその次。
    int FormatRank(const std::string& extension)
    {
        if (extension == ".obj") { return 0; }
        if (extension == ".fbx") { return 1; }
        return 2;
    }

    // フォルダ名が「形式の名前」か(OBJ、FBX、GLB、"OBJ format" など)。Kenneyなどのパックは、同じモデルを
    // 形式ごとのフォルダ(Models/OBJ format/、Models/FBX format/ ...)に分けて入れている。
    bool IsFormatFolderName(const std::string& segment)
    {
        const std::string lower = ToLower(segment);
        static const char* kNames[] = {
            "obj", "fbx", "glb", "gltf", "blend", "dae", "usd", "usdz",
            "obj format", "fbx format", "glb format", "gltf format", "blend format", "dae format"
        };
        for (const char* name : kNames)
        {
            if (lower == name) { return true; }
        }
        return false;
    }

    // 「同じモデルの、形式違いのファイル」を見つけるための目印を作る。パック + フォルダ(形式名のフォルダは区別しない)
    // + ファイル名(大文字小文字を無視)。
    // 例: ".../Models/OBJ format/x.obj" と ".../Models/FBX format/x.fbx" は同じ目印になる。
    //     ".../Buildings/a.fbx" と ".../Props/a.fbx" は(別のフォルダなので)違う目印になる。
    std::string MakeSameModelKey(const AssetEntry& entry)
    {
        std::string key = entry.pack + "|";
        std::string directory = entry.path.substr(0, entry.path.find_last_of('/') == std::string::npos ? 0 : entry.path.find_last_of('/'));
        size_t position = 0;
        while (position <= directory.size())
        {
            size_t slash = directory.find('/', position);
            std::string segment = directory.substr(position, slash == std::string::npos ? std::string::npos : slash - position);
            key += IsFormatFolderName(segment) ? std::string("<format>") : ToLower(segment);
            key += "/";
            if (slash == std::string::npos) { break; }
            position = slash + 1;
        }
        return key + "|" + ToLower(entry.name);
    }

    // relativePath(区切りは「/」)の先頭からdepth階層ぶんを取り出す。ファイルがそれより浅い場所にある時は、
    // ファイル名を除いた、あるだけのフォルダを返す(フォルダが無ければ空)。
    std::string TakePackPrefix(const std::string& relativePath, int depth)
    {
        std::string result;
        size_t position = 0;
        for (int level = 0; level < depth; ++level)
        {
            size_t slash = relativePath.find('/', position);
            if (slash == std::string::npos)
            {
                break; // 残りはファイル名なので、パック名には含めない
            }
            result = relativePath.substr(0, slash);
            position = slash + 1;
        }
        return result;
    }
}

std::vector<AssetRoot> AssetCatalog::DefaultRoots()
{
    return {
        { "Assets/Models", 3 },    // 採用した素材: Assets/Models/<パック名>/...
        { "Assets/Incoming", 3 },  // これから見る素材(Gitに入れない): Assets/Incoming/<パック名>/...
        { "CityKitCommercial", 1 },
        { "CityKitSuburban", 1 },
        { "RoadTiles", 1 },
        { "Models", 2 }            // Models/Models(キャラクター)、Models/RoadModels(道路まわり)など
    };
}

void AssetCatalog::Scan()
{
    m_entries.clear();
    m_packs.clear();

    ModelLoaderRegistry& registry = ModelLoaderRegistry::Default();
    std::set<std::string> seenPaths; // 同じファイルを複数のルートが指していても、1回だけ数える

    for (const AssetRoot& root : m_roots)
    {
        std::error_code ec;
        if (!fs::is_directory(fs::path(root.path), ec))
        {
            continue; // 無いフォルダは飛ばす
        }

        fs::recursive_directory_iterator it(fs::path(root.path), fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc))
            {
                continue;
            }

            const fs::path& filePath = it->path();
            std::string relative = filePath.generic_string();
            if (!registry.CanLoad(relative))
            {
                continue; // 読めない形式(GLB、画像、テキストなど)は一覧に出さない
            }

            if (!seenPaths.insert(relative).second)
            {
                continue;
            }

            AssetEntry entry;
            entry.path = relative;
            entry.name = filePath.stem().string();
            entry.directory = filePath.parent_path().generic_string();
            entry.extension = ModelLoaderRegistry::GetLowerExtension(relative);
            entry.pack = TakePackPrefix(relative, root.packDepth);
            if (entry.pack.empty())
            {
                entry.pack = root.path; // ルート直下のファイルは、ルート名を使う
            }

            std::error_code sizeEc, timeEc;
            entry.fileSize = static_cast<uint64_t>(fs::file_size(filePath, sizeEc));
            entry.modifiedTime = static_cast<int64_t>(fs::last_write_time(filePath, timeEc).time_since_epoch().count());
            m_entries.push_back(std::move(entry));
        }
    }

    // 並べ方: パック → パス(いつも同じ順になる。ファイルシステムの列挙順に左右されない)。
    std::sort(m_entries.begin(), m_entries.end(), [](const AssetEntry& a, const AssetEntry& b)
    {
        if (a.pack != b.pack) { return a.pack < b.pack; }
        return a.path < b.path;
    });

    // 重複の判定: 「同じモデルの形式違い」(MakeSameModelKey が同じ物)の中で、最も優先の形式の1つだけを残し、他に印を付ける。
    std::map<std::string, size_t> best; // パック+名前 → 今のところ優先の項目の添字
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const std::string key = MakeSameModelKey(m_entries[i]);
        auto found = best.find(key);
        if (found == best.end())
        {
            best[key] = i;
        }
        else if (FormatRank(m_entries[i].extension) < FormatRank(m_entries[found->second].extension))
        {
            best[key] = i; // こちらの方が優先
        }
    }
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const std::string key = MakeSameModelKey(m_entries[i]);
        m_entries[i].duplicate = (best[key] != i);
    }

    std::set<std::string> packs;
    for (const AssetEntry& entry : m_entries)
    {
        packs.insert(entry.pack);
    }
    m_packs.assign(packs.begin(), packs.end());

    BuildFolderTree();
}

void AssetCatalog::BuildFolderTree()
{
    m_folders.clear();

    // 木に出すフォルダの集まり(パスの文字列。親のパスは必ず子より先に並ぶ)。
    std::set<std::string> directories;
    auto addWithAncestors = [&](const std::string& directory)
    {
        std::string current;
        size_t position = 0;
        while (position < directory.size())
        {
            size_t slash = directory.find('/', position);
            std::string segment = directory.substr(position, slash == std::string::npos ? std::string::npos : slash - position);
            current = current.empty() ? segment : current + "/" + segment;
            directories.insert(current);
            if (slash == std::string::npos) { break; }
            position = slash + 1;
        }
    };

    for (const AssetEntry& entry : m_entries)
    {
        if (!entry.directory.empty())
        {
            addWithAncestors(entry.directory);
        }
    }

    // Assets/Models と Assets/Incoming の下は、空のフォルダも出す(実行中に作ったフォルダがすぐ見えるように)。
    for (const char* root : { "Assets/Models", "Assets/Incoming" })
    {
        std::error_code ec;
        if (!fs::is_directory(fs::path(root), ec))
        {
            continue;
        }
        addWithAncestors(root);
        fs::recursive_directory_iterator it(fs::path(root), fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code dirEc;
            if (it->is_directory(dirEc))
            {
                addWithAncestors(it->path().generic_string());
            }
        }
    }

    FolderNode root;
    root.name = "すべて";
    m_folders.push_back(root);

    std::map<std::string, int> indexOf;
    indexOf[""] = 0;
    for (const std::string& path : directories)
    {
        const size_t slash = path.find_last_of('/');
        const std::string parentPath = (slash == std::string::npos) ? std::string() : path.substr(0, slash);

        FolderNode node;
        node.path = path;
        node.name = (slash == std::string::npos) ? path : path.substr(slash + 1);
        auto parentIt = indexOf.find(parentPath);
        node.parent = (parentIt != indexOf.end()) ? parentIt->second : 0;
        const int index = static_cast<int>(m_folders.size());
        indexOf[path] = index;
        m_folders.push_back(node);
        m_folders[node.parent].children.push_back(index);
    }

    // 子は名前順(大文字小文字を無視)。
    for (FolderNode& node : m_folders)
    {
        std::sort(node.children.begin(), node.children.end(), [this](int a, int b)
        {
            return ToLower(m_folders[a].name) < ToLower(m_folders[b].name);
        });
    }

    // モデルの数(重複を除く)を、フォルダとその親全てに足す。
    for (const AssetEntry& entry : m_entries)
    {
        if (entry.duplicate)
        {
            continue;
        }
        auto found = indexOf.find(entry.directory);
        int index = (found != indexOf.end()) ? found->second : 0;
        while (index >= 0)
        {
            ++m_folders[index].modelCount;
            index = m_folders[index].parent;
        }
    }
}

int AssetCatalog::FindFolder(const std::string& path) const
{
    if (path.empty())
    {
        return m_folders.empty() ? -1 : 0;
    }
    for (size_t i = 0; i < m_folders.size(); ++i)
    {
        if (m_folders[i].path == path)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool AssetCatalog::IsInFolder(const AssetEntry& entry, const std::string& folder, bool includeSubfolders)
{
    if (folder.empty())
    {
        return true;
    }
    if (entry.directory == folder)
    {
        return true;
    }
    return includeSubfolders && entry.directory.size() > folder.size() &&
        entry.directory.compare(0, folder.size(), folder) == 0 && entry.directory[folder.size()] == '/';
}

namespace
{
    // path が prefix そのもの、または prefix の下(prefix + "/...")か。
    bool IsSameOrUnder(const std::string& path, const std::string& prefix)
    {
        return path == prefix || (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 && path[prefix.size()] == '/');
    }

    // 相対パスとして安全か(空でない・絶対パスでない・「..」を含まない・バックスラッシュを含まない)。
    bool IsSafeRelativePath(const std::string& path)
    {
        if (path.empty() || path.find('\\') != std::string::npos || path.find(':') != std::string::npos || path[0] == '/')
        {
            return false;
        }
        size_t position = 0;
        while (position <= path.size())
        {
            size_t slash = path.find('/', position);
            std::string segment = path.substr(position, slash == std::string::npos ? std::string::npos : slash - position);
            if (segment.empty() || segment == "." || segment == "..")
            {
                return false;
            }
            if (slash == std::string::npos) { break; }
            position = slash + 1;
        }
        return true;
    }

    std::string ParentOf(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }

    std::string LeafOf(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }
}

bool AssetCatalog::CanCreateIn(const std::string& folder)
{
    if (!IsSafeRelativePath(folder))
    {
        return false;
    }
    return IsSameOrUnder(folder, "Assets/Models") || IsSameOrUnder(folder, "Assets/Incoming");
}

bool AssetCatalog::CanModify(const std::string& folder)
{
    if (!IsSafeRelativePath(folder))
    {
        return false;
    }
    return (IsSameOrUnder(folder, "Assets/Models") && folder != "Assets/Models") ||
           (IsSameOrUnder(folder, "Assets/Incoming") && folder != "Assets/Incoming");
}

bool AssetCatalog::IsValidFolderName(const std::string& name, std::string& error)
{
    if (name.empty())
    {
        error = "フォルダ名を入力してください。";
        return false;
    }
    if (name.size() > 64)
    {
        error = "フォルダ名が長すぎます(64文字まで)。";
        return false;
    }
    for (char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = (u < 0x80) && (std::isalnum(u) || c == '_' || c == '-' || c == ' ');
        if (!ok)
        {
            error = "フォルダ名に使えるのは、英数字・「_」・「-」・スペースだけです。";
            return false;
        }
    }
    if (name.front() == ' ' || name.back() == ' ')
    {
        error = "フォルダ名の先頭と末尾にスペースは使えません。";
        return false;
    }

    // Windowsの予約名(CON、NUL、COM1 など)は、フォルダにできない。
    static const char* kReserved[] = { "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
    const std::string lower = ToLower(name);
    for (const char* reserved : kReserved)
    {
        if (lower == reserved)
        {
            error = "その名前はWindowsの予約名なので使えません。";
            return false;
        }
    }
    return true;
}

bool AssetCatalog::CreateFolder(const std::string& parentFolder, const std::string& name, std::string& outPath, std::string& error)
{
    outPath.clear();
    if (!CanCreateIn(parentFolder))
    {
        error = "ここにはフォルダを作れません(Assets/Models と Assets/Incoming の下だけです)。";
        return false;
    }
    if (!IsValidFolderName(name, error))
    {
        return false;
    }

    std::error_code ec;
    if (!fs::is_directory(fs::path(parentFolder), ec))
    {
        error = "作成先のフォルダが見つかりません: " + parentFolder;
        return false;
    }

    const std::string target = parentFolder + "/" + name;
    if (fs::exists(fs::path(target), ec))
    {
        error = "同じ名前のフォルダまたはファイルが既にあります: " + name;
        return false;
    }
    if (!fs::create_directory(fs::path(target), ec) || ec)
    {
        error = "フォルダを作れませんでした: " + ec.message();
        return false;
    }

    outPath = target;
    return true;
}

bool AssetCatalog::RenameFolder(const std::string& folder, const std::string& newName, std::string& outPath, std::string& error,
    const FolderLockCheck& isLocked)
{
    outPath.clear();
    if (!CanModify(folder))
    {
        error = "このフォルダは名前を変えられません(Assets/Models と Assets/Incoming の下のフォルダだけです)。";
        return false;
    }
    if (!IsValidFolderName(newName, error))
    {
        return false;
    }
    if (isLocked && isLocked(folder))
    {
        error = "シーンに置いた物がこのフォルダのモデルを使っているので、名前を変えられません。";
        return false;
    }

    std::error_code ec;
    if (!fs::is_directory(fs::path(folder), ec))
    {
        error = "フォルダが見つかりません: " + folder;
        return false;
    }

    const std::string target = ParentOf(folder) + "/" + newName;
    if (target == folder)
    {
        error = "名前が変わっていません。";
        return false;
    }
    // 大文字小文字だけの変更(abc → ABC)は、Windowsでは「同じフォルダ」に見えるので、存在確認の対象にしない。
    if (ToLower(target) != ToLower(folder) && fs::exists(fs::path(target), ec))
    {
        error = "同じ名前のフォルダまたはファイルが既にあります: " + newName;
        return false;
    }

    fs::rename(fs::path(folder), fs::path(target), ec);
    if (ec)
    {
        error = "名前を変えられませんでした: " + ec.message();
        return false;
    }

    outPath = target;
    return true;
}

bool AssetCatalog::MoveFolder(const std::string& folder, const std::string& newParentFolder, std::string& outPath, std::string& error,
    const FolderLockCheck& isLocked)
{
    outPath.clear();
    if (!CanModify(folder))
    {
        error = "このフォルダは移動できません(Assets/Models と Assets/Incoming の下のフォルダだけです)。";
        return false;
    }
    if (!CanCreateIn(newParentFolder))
    {
        error = "移動先は、Assets/Models か Assets/Incoming の下にしてください。";
        return false;
    }
    if (IsSameOrUnder(newParentFolder, folder))
    {
        error = "フォルダを、自分自身またはその中へは移動できません。";
        return false;
    }
    if (ParentOf(folder) == newParentFolder)
    {
        error = "既にそのフォルダの中にあります。";
        return false;
    }
    if (isLocked && isLocked(folder))
    {
        error = "シーンに置いた物がこのフォルダのモデルを使っているので、移動できません。";
        return false;
    }

    std::error_code ec;
    if (!fs::is_directory(fs::path(folder), ec))
    {
        error = "フォルダが見つかりません: " + folder;
        return false;
    }
    if (!fs::is_directory(fs::path(newParentFolder), ec))
    {
        error = "移動先のフォルダが見つかりません: " + newParentFolder;
        return false;
    }

    const std::string target = newParentFolder + "/" + LeafOf(folder);
    if (fs::exists(fs::path(target), ec))
    {
        error = "移動先に同じ名前のフォルダまたはファイルが既にあります: " + LeafOf(folder);
        return false;
    }

    fs::rename(fs::path(folder), fs::path(target), ec);
    if (ec)
    {
        error = "移動できませんでした: " + ec.message();
        return false;
    }

    outPath = target;
    return true;
}

size_t AssetCatalog::CountVisible() const
{
    size_t count = 0;
    for (const AssetEntry& entry : m_entries)
    {
        if (!entry.duplicate)
        {
            ++count;
        }
    }
    return count;
}

std::string AssetCatalog::FindLicenseFile(const AssetEntry& entry)
{
    // モデルのフォルダから、パックのフォルダまで、親へさかのぼって探す。
    fs::path directory = fs::path(entry.path).parent_path();
    const fs::path packPath(entry.pack);

    for (int level = 0; level < 8; ++level)
    {
        std::error_code ec;
        if (fs::is_directory(directory, ec))
        {
            for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
            {
                std::error_code fileEc;
                if (!it->is_regular_file(fileEc))
                {
                    continue;
                }
                std::string lowerName = ToLower(it->path().filename().string());
                if (lowerName.rfind("license", 0) == 0 || lowerName.rfind("licence", 0) == 0)
                {
                    return it->path().generic_string();
                }
            }
        }

        // パックのフォルダまで見たら終わり(それより上は、別のパックのファイルかもしれない)。
        if (directory.generic_string() == packPath.generic_string() || directory.empty() || !directory.has_parent_path() ||
            directory.parent_path() == directory)
        {
            break;
        }
        directory = directory.parent_path();
    }

    return std::string();
}
