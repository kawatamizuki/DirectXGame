#pragma once
#include "ISnapshotFormat.h"

// ISnapshotFormatのJSONテキスト版。人が読めて、バックアップや差分確認・手での修正ができる
// (開発中はこれを使う)。インデント付きで書き出す。
class JsonFormat : public ISnapshotFormat
{
public:
    const char* Extension() const override { return "json"; }
    bool Matches(const std::string& bytes) const override;
    bool Encode(const Json& root, std::string& outBytes, std::string& error) const override;
    bool Decode(const std::string& bytes, Json& outRoot, std::string& error) const override;
};
