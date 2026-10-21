#include "AssetImporter.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <Windows.h>

namespace fs = std::filesystem;

namespace
{
    // 取り込む1つのまとまり(ドロップされた1つのフォルダ、または、ドロップされたファイル全部)でコピーする物の計画。
    struct Plan
    {
        std::vector<std::pair<fs::path, fs::path>> files; // (元のファイル, 取り込み先フォルダの中での相対パス)
        std::set<std::string> relativeKeys;               // 重複して足さないための、相対パスの目印
        uint64_t bytes = 0;
        size_t models = 0;
        size_t skipped = 0;
        uint64_t skippedBytes = 0;

        bool Add(const fs::path& source, const fs::path& relative)
        {
            // (パス → 文字列は、システムの文字コードで表せない文字があると例外になる。UTF-8で作れば、どんな名前でも安全)
            const std::string key = relative.lexically_normal().generic_u8string();
            if (!relativeKeys.insert(key).second)
            {
                return false; // 既に計画に入っている
            }
            std::error_code ec;
            const uint64_t size = static_cast<uint64_t>(fs::file_size(source, ec));
            files.emplace_back(source, relative.lexically_normal());
            bytes += ec ? 0 : size;
            if (AssetImporter::IsModelExtension(LowerExtensionOf(source)))
            {
                ++models;
            }
            return true;
        }

        static std::string LowerExtensionOf(const fs::path& path)
        {
            // ワイド文字から作る(.string()は、システムの文字コードで表せない文字で例外になる)。拡張子の判定に使うのは英数字だけ。
            std::string extension;
            for (wchar_t c : path.extension().wstring())
            {
                extension += (c < 0x80) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '?';
            }
            return extension;
        }
    };

    std::string LowerExtension(const fs::path& path)
    {
        return Plan::LowerExtensionOf(path);
    }

    bool IsImageExtension(const std::string& e)
    {
        return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp" || e == ".dds";
    }

    // 相対パスが、元のフォルダの外(..)を指していないか。
    bool StaysInside(const fs::path& relative)
    {
        for (const fs::path& part : relative.lexically_normal())
        {
            if (part == "..")
            {
                return false;
            }
        }
        return !relative.is_absolute() && !relative.empty();
    }

    // pathが、rootそのもの、またはrootの下にあるか(実在しなくても、字面で比べる)。
    bool IsSameOrUnder(const fs::path& path, const fs::path& root)
    {
        std::error_code ec;
        const fs::path a = fs::weakly_canonical(path, ec);
        const fs::path b = fs::weakly_canonical(root, ec);
        auto ia = a.begin();
        for (auto ib = b.begin(); ib != b.end(); ++ib, ++ia)
        {
            if (ia == a.end() || *ia != *ib)
            {
                return false;
            }
        }
        return true;
    }

    std::string ToUtf8Message(const fs::path& path)
    {
        // 表示用。UTF-8で作る(ImGuiはUTF-8を表示する。システムの文字コードで表せない名前でも、例外にならない)。
        return path.filename().u8string();
    }

    std::string Trim(const std::string& text)
    {
        size_t begin = text.find_first_not_of(" \t\r\n");
        size_t end = text.find_last_not_of(" \t\r\n");
        return begin == std::string::npos ? std::string() : text.substr(begin, end - begin + 1);
    }

    std::string LowerAscii(std::string text)
    {
        for (char& c : text) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return text;
    }

    // OBJ内の「mtllib 名前」を全て返す。
    std::vector<std::string> ReadMtlNames(const fs::path& obj)
    {
        std::vector<std::string> names;
        std::ifstream file(obj, std::ios::binary);
        std::string line;
        // モデル本体は大きいことがあるが、mtllib は先頭付近にあるのが普通。最初の200行だけ見る(全部読むと重い)。
        for (int i = 0; i < 200 && std::getline(file, line); ++i)
        {
            if (line.rfind("mtllib", 0) == 0)
            {
                const std::string name = Trim(line.substr(6));
                if (!name.empty())
                {
                    names.push_back(name);
                }
            }
        }
        return names;
    }

    // MTL内の、テクスチャ(map_*、bump、decal、disp、norm)の参照を全て返す(MTLのあるフォルダからの相対パスの文字列)。
    std::vector<std::string> ReadTextureNames(const fs::path& mtl)
    {
        std::vector<std::string> names;
        std::ifstream file(mtl, std::ios::binary);
        std::string line;
        while (std::getline(file, line))
        {
            const std::string trimmed = Trim(line);
            const std::string lower = LowerAscii(trimmed);
            const bool isTexture = lower.rfind("map_", 0) == 0 || lower.rfind("bump", 0) == 0 || lower.rfind("decal", 0) == 0 ||
                                   lower.rfind("disp", 0) == 0 || lower.rfind("norm", 0) == 0;
            if (!isTexture)
            {
                continue;
            }
            const size_t space = trimmed.find_first_of(" \t");
            if (space == std::string::npos)
            {
                continue;
            }
            // 名前にスペースがあることもあるので、残り全部を候補にする。オプション(-s 1 1 1 など)が付いていたら、最後の1語。
            const std::string remainder = Trim(trimmed.substr(space));
            names.push_back(remainder);
            const size_t lastSpace = remainder.find_last_of(" \t");
            if (lastSpace != std::string::npos)
            {
                names.push_back(remainder.substr(lastSpace + 1));
            }
        }
        return names;
    }

    // 元のフォルダ sourceRoot の下の、相対パス relative のファイルを計画に足す(拡張子・文字コード・外への参照を確かめる)。
    void AddCompanion(Plan& plan, const fs::path& sourceRoot, const fs::path& file, std::vector<std::string>& warnings)
    {
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
        {
            return;
        }
        const fs::path relative = fs::relative(file, sourceRoot, ec);
        if (ec || !StaysInside(relative))
        {
            warnings.push_back("ドロップされたフォルダの外にあるため取り込めません: " + ToUtf8Message(file));
            return;
        }
        if (!AssetImporter::IsAllowedExtension(LowerExtension(file)))
        {
            return;
        }
        if (!AssetImporter::IsRepresentableInAnsi(relative))
        {
            warnings.push_back("名前に使えない文字があるため取り込めません: " + ToUtf8Message(file));
            return;
        }
        plan.Add(file, relative);
    }

    void CollectModelFile(Plan& plan, const fs::path& model, std::vector<std::string>& warnings)
    {
        const fs::path directory = model.parent_path();
        const std::string extension = LowerExtension(model);

        if (!AssetImporter::IsRepresentableInAnsi(model.filename()))
        {
            warnings.push_back("名前に使えない文字があるため取り込めません: " + ToUtf8Message(model));
            return;
        }
        plan.Add(model, model.filename());

        if (extension == ".obj")
        {
            // OBJ → MTL → テクスチャ、の順に、参照をたどって一緒に取り込む(フォルダの中での相対位置は保つ)。
            for (const std::string& mtlName : ReadMtlNames(model))
            {
                const fs::path mtlPath = directory / mtlName;
                std::error_code ec;
                if (!fs::is_regular_file(mtlPath, ec))
                {
                    warnings.push_back("MTLが見つかりません: " + mtlName);
                    continue;
                }
                AddCompanion(plan, directory, mtlPath, warnings);

                for (const std::string& textureName : ReadTextureNames(mtlPath))
                {
                    const fs::path texturePath = mtlPath.parent_path() / textureName;
                    if (fs::is_regular_file(texturePath, ec))
                    {
                        AddCompanion(plan, directory, texturePath, warnings);
                    }
                }
            }
        }
        else if (extension == ".fbx")
        {
            // FBX: 同じフォルダの画像と、同じ場所の Textures フォルダの中の画像を、一緒に取り込む。
            std::error_code ec;
            for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            {
                std::error_code fileEc;
                if (it->is_regular_file(fileEc) && IsImageExtension(LowerExtension(it->path())))
                {
                    AddCompanion(plan, directory, it->path(), warnings);
                }
            }
            const fs::path textures = directory / "Textures";
            if (fs::is_directory(textures, ec))
            {
                for (fs::recursive_directory_iterator it(textures, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
                {
                    std::error_code fileEc;
                    if (it->is_regular_file(fileEc) && !it->is_symlink(fileEc) && IsImageExtension(LowerExtension(it->path())))
                    {
                        AddCompanion(plan, directory, it->path(), warnings);
                    }
                }
            }
        }
    }

    void CollectFolder(Plan& plan, const fs::path& folder, std::vector<std::string>& warnings)
    {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code fileEc;
            if (it->is_symlink(fileEc))
            {
                continue; // リンクはたどらない(ループや、フォルダの外への参照を避ける)
            }
            if (!it->is_regular_file(fileEc))
            {
                continue;
            }

            const fs::path& file = it->path();
            std::error_code sizeEc;
            if (!AssetImporter::IsAllowedExtension(LowerExtension(file)))
            {
                ++plan.skipped;
                plan.skippedBytes += static_cast<uint64_t>(fs::file_size(file, sizeEc));
                continue;
            }
            const fs::path relative = fs::relative(file, folder, fileEc);
            if (fileEc || !StaysInside(relative))
            {
                continue;
            }
            if (!AssetImporter::IsRepresentableInAnsi(relative))
            {
                warnings.push_back("名前に使えない文字があるため飛ばしました: " + ToUtf8Message(file));
                ++plan.skipped;
                plan.skippedBytes += static_cast<uint64_t>(fs::file_size(file, sizeEc));
                continue;
            }
            plan.Add(file, relative);
        }
    }

    std::string MakeTimestamp()
    {
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
        localtime_s(&local, &now);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &local);
        return buffer;
    }

    std::string FormatMegabytes(uint64_t bytes)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return buffer;
    }
}

bool AssetImporter::IsAllowedExtension(const std::string& e)
{
    return e == ".obj" || e == ".fbx" || e == ".mtl" || IsImageExtension(e) ||
           e == ".txt" || e == ".md" || e == ".html" || e == ".pdf";
}

bool AssetImporter::IsModelExtension(const std::string& e)
{
    return e == ".obj" || e == ".fbx";
}

std::string AssetImporter::SanitizeName(const std::wstring& name)
{
    std::string result;
    bool lastUnderscore = false;
    for (wchar_t c : name)
    {
        const bool ok = c < 0x80 && (std::isalnum(static_cast<unsigned char>(c)) || c == L'_' || c == L'-' || c == L' ');
        const char out = ok ? static_cast<char>(c) : '_';
        if (out == '_')
        {
            if (lastUnderscore) { continue; }
            lastUnderscore = true;
        }
        else
        {
            lastUnderscore = false;
        }
        result += out;
        if (result.size() >= 48) { break; }
    }
    // 前後の _ とスペースを削る。
    const size_t begin = result.find_first_not_of("_ ");
    const size_t end = result.find_last_not_of("_ ");
    result = (begin == std::string::npos) ? std::string() : result.substr(begin, end - begin + 1);
    return result.empty() ? std::string("Imported") : result;
}

std::string AssetImporter::MakeUniqueName(const fs::path& parent, const std::string& name)
{
    std::error_code ec;
    if (!fs::exists(parent / name, ec))
    {
        return name;
    }
    for (int suffix = 2; suffix < 10000; ++suffix)
    {
        const std::string candidate = name + "_" + std::to_string(suffix);
        if (!fs::exists(parent / candidate, ec))
        {
            return candidate;
        }
    }
    return name + "_" + MakeTimestamp();
}

bool AssetImporter::IsRepresentableInAnsi(const fs::path& path)
{
    const std::wstring wide = path.wstring();
    if (wide.empty())
    {
        return true;
    }
    BOOL usedDefault = FALSE;
    const int size = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, &usedDefault);
    if (size <= 0)
    {
        return false;
    }
    std::string narrow(static_cast<size_t>(size), '\0');
    usedDefault = FALSE;
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), static_cast<int>(wide.size()), narrow.data(), size, nullptr, &usedDefault);
    return !usedDefault;
}

ImportResult AssetImporter::Import(const std::vector<fs::path>& sources, const ImportOptions& options)
{
    ImportResult result;
    std::error_code ec;

    const fs::path root(options.incomingRoot);
    fs::create_directories(root, ec);
    if (ec)
    {
        result.message = "取り込み先のフォルダを作れませんでした: " + options.incomingRoot;
        return result;
    }

    // 取り込む物の計画を作る。フォルダは1つずつ別の取り込み先、ファイルは全部まとめて1つの取り込み先(Dropped_<日時>)。
    struct Job
    {
        std::string folderName;
        Plan plan;
        bool isFolder = false;
    };
    std::vector<Job> jobs;
    Job droppedFiles;
    droppedFiles.folderName = "Dropped_" + (options.timestamp.empty() ? MakeTimestamp() : options.timestamp);

    std::vector<std::string> notes; // 取り込まなかった理由(ドロップ元ごと)

    for (const fs::path& source : sources)
    {
        std::error_code sourceEc;
        if (!fs::exists(source, sourceEc))
        {
            result.warnings.push_back("見つかりません: " + ToUtf8Message(source));
            continue;
        }
        if (IsSameOrUnder(source, root))
        {
            notes.push_back("「" + ToUtf8Message(source) + "」は、既に " + options.incomingRoot + " の中にあるので取り込みません。");
            continue;
        }

        if (fs::is_directory(source, sourceEc))
        {
            Job job;
            job.isFolder = true;
            job.folderName = SanitizeName(source.filename().wstring());
            CollectFolder(job.plan, source, result.warnings);
            jobs.push_back(std::move(job));
        }
        else if (fs::is_regular_file(source, sourceEc))
        {
            const std::string extension = LowerExtension(source);
            if (IsModelExtension(extension))
            {
                CollectModelFile(droppedFiles.plan, source, result.warnings);
            }
            else
            {
                ++result.skippedFiles;
                std::error_code sizeEc;
                result.skippedBytes += static_cast<uint64_t>(fs::file_size(source, sizeEc));
                notes.push_back("「" + ToUtf8Message(source) + "」はモデル(.obj / .fbx)ではないので取り込みません。モデルと一緒に必要な物は、自動で一緒に取り込まれます。");
            }
        }
    }
    if (!droppedFiles.plan.files.empty())
    {
        jobs.push_back(std::move(droppedFiles));
    }

    // 計画を実行する。
    for (Job& job : jobs)
    {
        result.skippedFiles += job.plan.skipped;
        result.skippedBytes += job.plan.skippedBytes;

        if (job.plan.files.empty())
        {
            notes.push_back("「" + job.folderName + "」には、取り込める物(モデル・MTL・画像・説明書き)がありませんでした。");
            continue;
        }
        if (job.plan.bytes > options.maxTotalBytes)
        {
            notes.push_back("「" + job.folderName + "」は合計 " + FormatMegabytes(job.plan.bytes) + " で、上限(" +
                            FormatMegabytes(options.maxTotalBytes) + ")を超えるので取り込みません。使う物だけを別のフォルダに分けて、もう一度ドロップしてください。");
            continue;
        }

        const std::string uniqueName = MakeUniqueName(root, job.folderName);
        const fs::path destination = root / uniqueName;
        std::error_code createEc;
        fs::create_directories(destination, createEc);
        if (createEc)
        {
            notes.push_back("「" + uniqueName + "」を作れませんでした。");
            continue;
        }

        size_t copiedHere = 0;
        for (const auto& item : job.plan.files)
        {
            const fs::path target = destination / item.second;
            // 取り込み先の外へ出ないことを、念のため確かめる(相対パスに「..」は入らない作りだが、保険)。
            if (!IsSameOrUnder(target, destination))
            {
                result.warnings.push_back("取り込み先の外を指すため飛ばしました: " + item.second.generic_u8string());
                continue;
            }

            std::error_code copyEc;
            fs::create_directories(target.parent_path(), copyEc);
            if (!copyEc)
            {
                fs::copy_file(item.first, target, fs::copy_options::skip_existing, copyEc);
            }
            if (copyEc)
            {
                result.warnings.push_back("コピーできませんでした: " + item.second.generic_u8string() + " (" + copyEc.message() + ")");
                continue;
            }

            std::error_code sizeEc;
            ++copiedHere;
            ++result.copiedFiles;
            result.copiedBytes += static_cast<uint64_t>(fs::file_size(target, sizeEc));
            if (IsModelExtension(LowerExtension(target)))
            {
                ++result.copiedModels;
            }
        }

        if (copiedHere == 0)
        {
            std::error_code removeEc;
            fs::remove_all(destination, removeEc); // 何もコピーできなかった空の取り込み先は残さない
            notes.push_back("「" + job.folderName + "」は1つもコピーできませんでした。");
            continue;
        }

        result.importedFolders.push_back((fs::path(options.incomingRoot) / uniqueName).generic_string());
    }

    result.ok = !result.importedFolders.empty();

    // 利用者に見せるまとめ。
    std::ostringstream message;
    if (result.ok)
    {
        message << result.copiedFiles << " 個のファイル(モデル " << result.copiedModels << " 個、" << FormatMegabytes(result.copiedBytes)
                << ")を取り込みました: ";
        for (size_t i = 0; i < result.importedFolders.size(); ++i)
        {
            message << (i ? "、" : "") << result.importedFolders[i];
        }
        message << "\n";
        if (result.skippedFiles > 0)
        {
            message << "使わない形式などで飛ばした物: " << result.skippedFiles << " 個(" << FormatMegabytes(result.skippedBytes) << ")\n";
        }
        if (result.copiedModels == 0)
        {
            message << "モデル(.obj / .fbx)が見つかりませんでした。\n";
        }
        message << "ライセンスを確認して THIRD_PARTY_LICENSES.md に記録してください(Assets/Incoming はGitに入りません。使う物は Assets/Models へ移します)。";
    }
    else
    {
        message << "取り込めませんでした。";
    }
    for (const std::string& note : notes)
    {
        message << "\n" << note;
    }
    if (!result.warnings.empty())
    {
        message << "\n(注意が " << result.warnings.size() << " 件あります。Logパネルを見てください)";
    }
    result.message = message.str();
    return result;
}
