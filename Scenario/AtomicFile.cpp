#include "AtomicFile.h"
#include <filesystem>
#include <fstream>
#include <iterator>

bool WriteFileAtomic(const std::string& path, const std::string& bytes, std::string& error)
{
    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path())
    {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec)
        {
            error = "フォルダを作れません: " + target.parent_path().string();
            return false;
        }
    }

    const std::string tempPath = path + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "一時ファイルを作れません: " + tempPath;
            return false;
        }
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file)
        {
            error = "書き込みに失敗しました(ディスクの空きを確認してください)";
            file.close();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    std::filesystem::rename(tempPath, target, ec);
    if (ec)
    {
        error = "ファイルを置き換えられません: " + ec.message();
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}

bool ReadFileBytes(const std::string& path, std::string& bytes, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "ファイルを開けません: " + path;
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}
