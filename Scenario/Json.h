#pragma once
#include <string>
#include <utility>
#include <vector>

// 形式に依存しない「値の木」(オブジェクト/配列/数値/文字列/真偽/null)。
// セーブデータの中身はこの木だけで表し、ファイルに書く形(今はJSONテキスト、将来はバイナリ)は
// ISnapshotFormatが決める。各システムの保存処理(ISaveable)は、この木にだけ書き込むので、
// ファイル形式を知らずに済む。
// オブジェクトのキーは「入れた順」を保つ(同じ内容なら必ず同じ並びで書き出されるので、
// 保存→読み込み→再保存でファイルが完全に一致する確認ができ、差分も見やすい)。
class Json
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Json() = default;
    Json(bool value) : m_type(Type::Bool), m_bool(value) {}
    Json(int value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Json(unsigned int value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Json(float value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Json(double value) : m_type(Type::Number), m_number(value) {}
    Json(const char* value) : m_type(Type::String), m_string(value ? value : "") {}
    Json(std::string value) : m_type(Type::String), m_string(std::move(value)) {}

    static Json MakeObject();
    static Json MakeArray();

    Type GetType() const { return m_type; }
    bool IsNull() const { return m_type == Type::Null; }
    bool IsBool() const { return m_type == Type::Bool; }
    bool IsNumber() const { return m_type == Type::Number; }
    bool IsString() const { return m_type == Type::String; }
    bool IsArray() const { return m_type == Type::Array; }
    bool IsObject() const { return m_type == Type::Object; }

    // 型が違う時は既定値を返す(読み込み側が、足りない・型違いの項目で落ちないようにするため)。
    bool AsBool(bool defaultValue = false) const { return m_type == Type::Bool ? m_bool : defaultValue; }
    double AsNumber(double defaultValue = 0.0) const { return m_type == Type::Number ? m_number : defaultValue; }
    float AsFloat(float defaultValue = 0.0f) const { return m_type == Type::Number ? static_cast<float>(m_number) : defaultValue; }
    int AsInt(int defaultValue = 0) const;
    const std::string& AsString() const; // 文字列でなければ空文字

    // ---- オブジェクト ----
    // keyの値を設定する(既にあれば置き換え、無ければ末尾に追加)。自分がオブジェクトでなければ、オブジェクトに変わる。
    Json& Set(const std::string& key, Json value);
    // keyの値を返す。無い/オブジェクトでなければnullptr。
    const Json* Find(const std::string& key) const;
    bool Has(const std::string& key) const { return Find(key) != nullptr; }
    const std::vector<std::pair<std::string, Json>>& Members() const { return m_members; }

    // keyの値を、型が合っていればその値、無い/型違いなら既定値で返す便利関数。
    double GetNumber(const std::string& key, double defaultValue = 0.0) const;
    float GetFloat(const std::string& key, float defaultValue = 0.0f) const;
    int GetInt(const std::string& key, int defaultValue = 0) const;
    bool GetBool(const std::string& key, bool defaultValue = false) const;
    std::string GetString(const std::string& key, const std::string& defaultValue = std::string()) const;

    // ---- 配列 ----
    void Push(Json value); // 自分が配列でなければ、配列に変わる。
    size_t Size() const { return m_items.size(); }
    // indexの要素を返す。範囲外ならnull(例外やクラッシュにしない)。
    const Json& At(size_t index) const;
    const std::vector<Json>& Items() const { return m_items; }

    // ---- 文字列への書き出し / 文字列からの読み込み ----
    // pretty==trueなら見やすいように改行とインデントを付ける(数値だけの配列は1行にまとめる)。
    std::string Dump(bool pretty) const;

    // textをJSONとして読む。成功したらtrueでoutに入る。失敗したらfalseでerrorに「何行何列目で何が悪いか」が入る
    // (入力が壊れていてもクラッシュしない)。先頭のUTF-8 BOMは読み飛ばす。
    static bool Parse(const std::string& text, Json& out, std::string& error);

private:
    void DumpTo(std::string& out, bool pretty, int depth) const;

    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::vector<Json> m_items;                                  // 配列の要素
    std::vector<std::pair<std::string, Json>> m_members;        // オブジェクトのキーと値(入れた順)
};
