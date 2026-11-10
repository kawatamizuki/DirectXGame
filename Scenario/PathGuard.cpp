#include "PathGuard.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    // Windowsは、フォルダ名・ファイル名の末尾の「.」と空白を、無視する(「TestMaps 」「TestMaps.」は「TestMaps」と同じ)。
    // 文字の比較で、すり抜けられないよう、各部分の末尾の「.」と空白を、落として揃える(「.」「..」そのものは、既に lexically_normal が処理している)。
    fs::path StripTrailing(const fs::path& path)
    {
        fs::path result;
        for (const fs::path& part : path)
        {
            std::string name = part.string();
            if (name != "." && name != "..")
            {
                while (!name.empty() && (name.back() == ' ' || name.back() == '.')) { name.pop_back(); }
                if (name.empty() && !part.string().empty()) { continue; }
                result /= fs::path(name);
            }
            else
            {
                result /= part;
            }
        }
        return result;
    }

    // 大文字小文字を区別しない、区切りを「/」に揃えた文字列(Windowsのパスの比較用)。
    std::string Canonical(const fs::path& path)
    {
        std::string text = StripTrailing(path).generic_string();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        while (text.size() > 1 && text.back() == '/') { text.pop_back(); }
        return text;
    }

    // 怪しい書き方か。「ドライブ名の後ろ以外の ':'」(NTFSの別の流れ: file.json:stream)・デバイス/長いパスの接頭辞(\\?\ \\.\)・制御文字。
    bool IsSuspicious(const std::string& path)
    {
        for (size_t i = 0; i < path.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(path[i]);
            if (c < 0x20) { return true; }
            if (path[i] == ':' && i != 1) { return true; }
        }
        if (path.rfind("\\\\?\\", 0) == 0 || path.rfind("\\\\.\\", 0) == 0 || path.rfind("//?/", 0) == 0 || path.rfind("//./", 0) == 0) { return true; }
        return false;
    }
}

bool PathGuard::IsInsideDirectory(const std::string& path, const std::string& directory)
{
    // 標準ライブラリは、パスの文字を変換できない時(日本語を含む、など)に、例外を投げることがある。判定できないので、安全側(書かせない)。
    try
    {
        return IsInsideDirectoryImpl(path, directory);
    }
    catch (...)
    {
        return true;
    }
}

bool PathGuard::IsInsideDirectoryImpl(const std::string& path, const std::string& directory)
{
    if (path.empty() || directory.empty())
    {
        return true;    // 判定できない: 安全側(書かせない)
    }
    if (IsSuspicious(path))
    {
        return true;
    }

    std::error_code ec;
    fs::path target = fs::absolute(fs::path(path), ec);
    if (ec) { return true; }
    target = target.lexically_normal();
    fs::path root = fs::absolute(fs::path(directory), ec);
    if (ec) { return true; }
    root = root.lexically_normal();

    // (1) 文字の比較(正規化後)。directory が、まだ無くても、これで判定できる。
    const std::string rootText = Canonical(root);
    const std::string targetText = Canonical(target);
    if (targetText == rootText || (targetText.size() > rootText.size() && targetText.compare(0, rootText.size(), rootText) == 0 && targetText[rootText.size()] == '/'))
    {
        return true;
    }

    // (2) 実体の比較: target の、親をたどり、既に存在する物が、directory と同じ物なら、中にある(短い名前・ジャンクション・末尾のドット/空白・シンボリックリンクを見抜く)。
    fs::path current = target;
    for (int guard = 0; guard < 64; ++guard)
    {
        ec.clear();
        if (fs::exists(current, ec) && !ec)
        {
            std::error_code ec2;
            if (fs::exists(root, ec2) && !ec2 && fs::equivalent(current, root, ec2) && !ec2)
            {
                return true;
            }
        }
        const fs::path parent = current.parent_path();
        if (parent.empty() || parent == current) { break; }
        current = parent;
    }
    return false;
}
