#include "JsonFormat.h"

bool JsonFormat::Matches(const std::string& bytes) const
{
    // 先頭の空白・UTF-8 BOMを飛ばして、最初の文字が '{' ならJSONのセーブとみなす。
    size_t i = 0;
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        i = 3;
    }
    while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\n' || bytes[i] == '\r'))
    {
        ++i;
    }
    return i < bytes.size() && bytes[i] == '{';
}

bool JsonFormat::Encode(const Json& root, std::string& outBytes, std::string& error) const
{
    error.clear();
    if (!root.IsObject())
    {
        error = "セーブデータの一番外側はオブジェクトである必要があります";
        return false;
    }
    outBytes = root.Dump(true);
    return true;
}

bool JsonFormat::Decode(const std::string& bytes, Json& outRoot, std::string& error) const
{
    Json parsed;
    if (!Json::Parse(bytes, parsed, error))
    {
        return false;
    }
    if (!parsed.IsObject())
    {
        error = "セーブデータの一番外側はオブジェクトである必要があります";
        return false;
    }
    outRoot = std::move(parsed);
    return true;
}
