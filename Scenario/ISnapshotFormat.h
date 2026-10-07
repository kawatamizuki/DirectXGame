#pragma once
#include <string>
#include "Json.h"

// 「値の木(Json)」と「ファイルのバイト列」を相互に変換する形式。
// セーブの中核(WorldSnapshot・ISaveable・ScenarioManager)は値の木だけを扱い、
// ファイルに書く形(今はJSONテキスト、将来はバイナリ)はこのインターフェースの実装に閉じ込める。
// 容量・読み込み速度・改ざん対策が必要になったら、同じインターフェースのBinaryFormatを
// 足すだけで切り替えられる(保存処理側は変わらない)。
class ISnapshotFormat
{
public:
    virtual ~ISnapshotFormat() = default;

    // ファイルの拡張子(ドットなし。例: "json")。
    virtual const char* Extension() const = 0;

    // bytesが、この形式で書かれたデータに見えるか(先頭の数バイトで判定する)。
    virtual bool Matches(const std::string& bytes) const = 0;

    // 値の木をバイト列にする。失敗したらfalseでerrorに理由を入れる。
    virtual bool Encode(const Json& root, std::string& outBytes, std::string& error) const = 0;

    // バイト列を値の木にする。壊れたデータでもクラッシュせず、falseでerrorに理由を入れる。
    virtual bool Decode(const std::string& bytes, Json& outRoot, std::string& error) const = 0;
};
