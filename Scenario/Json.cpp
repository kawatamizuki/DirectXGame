#include "Json.h"
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{
    const Json kNullJson;
    const std::string kEmptyString;

    // 入れ子の深さの上限。壊れた/悪意のある入力で再帰が深くなりすぎてスタックが溢れるのを防ぐ。
    constexpr int kMaxDepth = 200;

    // 数値を、読み戻した時に元と完全に一致する最短の文字列にする(人が読みやすいように、
    // 0.1fが「0.10000000149011612」にならず「0.1」と書かれるようにする)。
    std::string FormatNumber(double value)
    {
        if (!std::isfinite(value))
        {
            return "null"; // JSONでは表せない値(NaN/無限大)。読み込み側は既定値になる
        }

        // 整数ならそのまま整数で書く(範囲は倍精度で正確に表せる範囲に限る)。
        if (value == std::floor(value) && std::fabs(value) < 1e15)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
            return buffer;
        }

        char buffer[40];
        bool isFloatValue = (static_cast<double>(static_cast<float>(value)) == value);
        if (isFloatValue)
        {
            // floatで表せる値は、floatとして元に戻る最短の桁数(1〜9桁)で書く。
            float target = static_cast<float>(value);
            for (int precision = 1; precision <= 9; ++precision)
            {
                std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
                float back = 0.0f;
                std::from_chars(buffer, buffer + std::char_traits<char>::length(buffer), back);
                if (back == target)
                {
                    break;
                }
            }
        }
        else
        {
            for (int precision = 1; precision <= 17; ++precision)
            {
                std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
                double back = 0.0;
                std::from_chars(buffer, buffer + std::char_traits<char>::length(buffer), back);
                if (back == value)
                {
                    break;
                }
            }
        }

        // ロケールによっては小数点がカンマになるので、必ずピリオドに直す。
        for (char* p = buffer; *p; ++p)
        {
            if (*p == ',')
            {
                *p = '.';
            }
        }
        return buffer;
    }

    void AppendEscapedString(std::string& out, const std::string& text)
    {
        out.push_back('"');
        for (unsigned char c : text)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                }
                else
                {
                    out.push_back(static_cast<char>(c)); // UTF-8のバイトはそのまま(日本語もそのまま読める形で書く)
                }
                break;
            }
        }
        out.push_back('"');
    }

    void AppendUtf8(std::string& out, uint32_t codepoint)
    {
        if (codepoint < 0x80)
        {
            out.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    // JSONの構文解析器(再帰下降)。失敗したら、何行何列目で何が悪いかをerrorに入れてfalseを返す。
    class Parser
    {
    public:
        Parser(const std::string& text, std::string& error) : m_text(text), m_error(error) {}

        bool ParseDocument(Json& out)
        {
            // 先頭のUTF-8 BOMは読み飛ばす(メモ帳などで保存し直されたファイルでも読めるように)。
            if (m_text.size() >= 3 &&
                static_cast<unsigned char>(m_text[0]) == 0xEF &&
                static_cast<unsigned char>(m_text[1]) == 0xBB &&
                static_cast<unsigned char>(m_text[2]) == 0xBF)
            {
                m_pos = 3;
            }

            SkipWhitespace();
            if (!ParseValue(out, 0))
            {
                return false;
            }
            SkipWhitespace();
            if (m_pos != m_text.size())
            {
                return Fail("値の後ろに余計な文字があります");
            }
            return true;
        }

    private:
        bool Fail(const std::string& message)
        {
            int line = 1;
            int column = 1;
            for (size_t i = 0; i < m_pos && i < m_text.size(); ++i)
            {
                if (m_text[i] == '\n')
                {
                    line++;
                    column = 1;
                }
                else
                {
                    column++;
                }
            }
            m_error = std::to_string(line) + "行" + std::to_string(column) + "列: " + message;
            return false;
        }

        bool AtEnd() const { return m_pos >= m_text.size(); }

        void SkipWhitespace()
        {
            while (!AtEnd())
            {
                char c = m_text[m_pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    ++m_pos;
                }
                else
                {
                    break;
                }
            }
        }

        bool Expect(char expected)
        {
            if (AtEnd() || m_text[m_pos] != expected)
            {
                return Fail(std::string("'") + expected + "' が必要です");
            }
            ++m_pos;
            return true;
        }

        bool ParseLiteral(const char* literal, Json value, Json& out)
        {
            size_t length = std::char_traits<char>::length(literal);
            if (m_text.compare(m_pos, length, literal) != 0)
            {
                return Fail("不正な値です");
            }
            m_pos += length;
            out = std::move(value);
            return true;
        }

        bool ParseValue(Json& out, int depth)
        {
            if (depth > kMaxDepth)
            {
                return Fail("入れ子が深すぎます");
            }
            if (AtEnd())
            {
                return Fail("値が必要ですが、ファイルが途中で終わっています");
            }

            char c = m_text[m_pos];
            switch (c)
            {
            case '{': return ParseObject(out, depth);
            case '[': return ParseArray(out, depth);
            case '"':
            {
                std::string text;
                if (!ParseString(text))
                {
                    return false;
                }
                out = Json(std::move(text));
                return true;
            }
            case 't': return ParseLiteral("true", Json(true), out);
            case 'f': return ParseLiteral("false", Json(false), out);
            case 'n': return ParseLiteral("null", Json(), out);
            default:
                if (c == '-' || (c >= '0' && c <= '9'))
                {
                    return ParseNumber(out);
                }
                return Fail("不正な文字です");
            }
        }

        bool ParseNumber(Json& out)
        {
            size_t start = m_pos;
            if (m_text[m_pos] == '-')
            {
                ++m_pos;
            }
            if (AtEnd() || !(m_text[m_pos] >= '0' && m_text[m_pos] <= '9'))
            {
                return Fail("数値が不正です");
            }
            while (!AtEnd() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9')
            {
                ++m_pos;
            }
            if (!AtEnd() && m_text[m_pos] == '.')
            {
                ++m_pos;
                if (AtEnd() || !(m_text[m_pos] >= '0' && m_text[m_pos] <= '9'))
                {
                    return Fail("小数点の後ろに数字が必要です");
                }
                while (!AtEnd() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9')
                {
                    ++m_pos;
                }
            }
            if (!AtEnd() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E'))
            {
                ++m_pos;
                if (!AtEnd() && (m_text[m_pos] == '+' || m_text[m_pos] == '-'))
                {
                    ++m_pos;
                }
                if (AtEnd() || !(m_text[m_pos] >= '0' && m_text[m_pos] <= '9'))
                {
                    return Fail("指数部に数字が必要です");
                }
                while (!AtEnd() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9')
                {
                    ++m_pos;
                }
            }

            double value = 0.0;
            auto result = std::from_chars(m_text.data() + start, m_text.data() + m_pos, value);
            if (result.ec != std::errc())
            {
                m_pos = start;
                return Fail("数値が範囲外です");
            }
            out = Json(value);
            return true;
        }

        bool ParseHex4(uint32_t& outValue)
        {
            if (m_pos + 4 > m_text.size())
            {
                return Fail("\\u の後ろに16進数4桁が必要です");
            }
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                char c = m_text[m_pos + i];
                value <<= 4;
                if (c >= '0' && c <= '9') { value |= static_cast<uint32_t>(c - '0'); }
                else if (c >= 'a' && c <= 'f') { value |= static_cast<uint32_t>(c - 'a' + 10); }
                else if (c >= 'A' && c <= 'F') { value |= static_cast<uint32_t>(c - 'A' + 10); }
                else { return Fail("\\u の後ろに16進数4桁が必要です"); }
            }
            m_pos += 4;
            outValue = value;
            return true;
        }

        bool ParseString(std::string& out)
        {
            if (!Expect('"'))
            {
                return false;
            }
            out.clear();
            while (true)
            {
                if (AtEnd())
                {
                    return Fail("文字列が閉じられていません");
                }
                unsigned char c = static_cast<unsigned char>(m_text[m_pos]);
                if (c == '"')
                {
                    ++m_pos;
                    return true;
                }
                if (c < 0x20)
                {
                    return Fail("文字列の中に制御文字があります");
                }
                if (c != '\\')
                {
                    out.push_back(static_cast<char>(c));
                    ++m_pos;
                    continue;
                }

                ++m_pos; // バックスラッシュ
                if (AtEnd())
                {
                    return Fail("エスケープが途中で終わっています");
                }
                char e = m_text[m_pos++];
                switch (e)
                {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                {
                    uint32_t codepoint = 0;
                    if (!ParseHex4(codepoint))
                    {
                        return false;
                    }
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
                    {
                        // 上位サロゲート: 続く \uXXXX(下位サロゲート)と合わせて1文字にする。
                        if (m_pos + 2 <= m_text.size() && m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u')
                        {
                            m_pos += 2;
                            uint32_t low = 0;
                            if (!ParseHex4(low))
                            {
                                return false;
                            }
                            if (low < 0xDC00 || low > 0xDFFF)
                            {
                                return Fail("サロゲートペアが不正です");
                            }
                            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        }
                        else
                        {
                            return Fail("サロゲートペアが不正です");
                        }
                    }
                    else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF)
                    {
                        return Fail("サロゲートペアが不正です");
                    }
                    AppendUtf8(out, codepoint);
                    break;
                }
                default:
                    return Fail("不正なエスケープです");
                }
            }
        }

        bool ParseArray(Json& out, int depth)
        {
            if (!Expect('['))
            {
                return false;
            }
            out = Json::MakeArray();
            SkipWhitespace();
            if (!AtEnd() && m_text[m_pos] == ']')
            {
                ++m_pos;
                return true;
            }
            while (true)
            {
                SkipWhitespace();
                Json item;
                if (!ParseValue(item, depth + 1))
                {
                    return false;
                }
                out.Push(std::move(item));
                SkipWhitespace();
                if (AtEnd())
                {
                    return Fail("配列が閉じられていません");
                }
                if (m_text[m_pos] == ',')
                {
                    ++m_pos;
                    continue;
                }
                if (m_text[m_pos] == ']')
                {
                    ++m_pos;
                    return true;
                }
                return Fail("',' または ']' が必要です");
            }
        }

        bool ParseObject(Json& out, int depth)
        {
            if (!Expect('{'))
            {
                return false;
            }
            out = Json::MakeObject();
            SkipWhitespace();
            if (!AtEnd() && m_text[m_pos] == '}')
            {
                ++m_pos;
                return true;
            }
            while (true)
            {
                SkipWhitespace();
                std::string key;
                if (!ParseString(key))
                {
                    return false;
                }
                SkipWhitespace();
                if (!Expect(':'))
                {
                    return false;
                }
                SkipWhitespace();
                Json value;
                if (!ParseValue(value, depth + 1))
                {
                    return false;
                }
                out.Set(key, std::move(value));
                SkipWhitespace();
                if (AtEnd())
                {
                    return Fail("オブジェクトが閉じられていません");
                }
                if (m_text[m_pos] == ',')
                {
                    ++m_pos;
                    continue;
                }
                if (m_text[m_pos] == '}')
                {
                    ++m_pos;
                    return true;
                }
                return Fail("',' または '}' が必要です");
            }
        }

        const std::string& m_text;
        std::string& m_error;
        size_t m_pos = 0;
    };
}

Json Json::MakeObject()
{
    Json json;
    json.m_type = Type::Object;
    return json;
}

Json Json::MakeArray()
{
    Json json;
    json.m_type = Type::Array;
    return json;
}

int Json::AsInt(int defaultValue) const
{
    if (m_type != Type::Number || !std::isfinite(m_number))
    {
        return defaultValue;
    }
    if (m_number > static_cast<double>(INT_MAX) || m_number < static_cast<double>(INT_MIN))
    {
        return defaultValue;
    }
    return static_cast<int>(std::lround(m_number));
}

const std::string& Json::AsString() const
{
    return m_type == Type::String ? m_string : kEmptyString;
}

Json& Json::Set(const std::string& key, Json value)
{
    if (m_type != Type::Object)
    {
        m_type = Type::Object;
        m_members.clear();
    }
    for (auto& member : m_members)
    {
        if (member.first == key)
        {
            member.second = std::move(value);
            return *this;
        }
    }
    m_members.emplace_back(key, std::move(value));
    return *this;
}

const Json* Json::Find(const std::string& key) const
{
    if (m_type != Type::Object)
    {
        return nullptr;
    }
    for (const auto& member : m_members)
    {
        if (member.first == key)
        {
            return &member.second;
        }
    }
    return nullptr;
}

double Json::GetNumber(const std::string& key, double defaultValue) const
{
    const Json* found = Find(key);
    return found ? found->AsNumber(defaultValue) : defaultValue;
}

float Json::GetFloat(const std::string& key, float defaultValue) const
{
    const Json* found = Find(key);
    return found ? found->AsFloat(defaultValue) : defaultValue;
}

int Json::GetInt(const std::string& key, int defaultValue) const
{
    const Json* found = Find(key);
    return found ? found->AsInt(defaultValue) : defaultValue;
}

bool Json::GetBool(const std::string& key, bool defaultValue) const
{
    const Json* found = Find(key);
    return found ? found->AsBool(defaultValue) : defaultValue;
}

std::string Json::GetString(const std::string& key, const std::string& defaultValue) const
{
    const Json* found = Find(key);
    return (found && found->IsString()) ? found->AsString() : defaultValue;
}

void Json::Push(Json value)
{
    if (m_type != Type::Array)
    {
        m_type = Type::Array;
        m_items.clear();
    }
    m_items.push_back(std::move(value));
}

const Json& Json::At(size_t index) const
{
    if (m_type != Type::Array || index >= m_items.size())
    {
        return kNullJson;
    }
    return m_items[index];
}

std::string Json::Dump(bool pretty) const
{
    std::string out;
    DumpTo(out, pretty, 0);
    if (pretty)
    {
        out.push_back('\n');
    }
    return out;
}

void Json::DumpTo(std::string& out, bool pretty, int depth) const
{
    auto newline = [&](int level)
    {
        out.push_back('\n');
        out.append(static_cast<size_t>(level) * 2, ' ');
    };

    switch (m_type)
    {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += m_bool ? "true" : "false";
        break;
    case Type::Number:
        out += FormatNumber(m_number);
        break;
    case Type::String:
        AppendEscapedString(out, m_string);
        break;
    case Type::Array:
    {
        if (m_items.empty())
        {
            out += "[]";
            break;
        }

        // 数値などの単純な値だけの配列は、1行にまとめて読みやすくする(座標 [0.5, 0, 0.5] など)。
        bool allScalar = true;
        for (const Json& item : m_items)
        {
            if (item.IsArray() || item.IsObject())
            {
                allScalar = false;
                break;
            }
        }

        out.push_back('[');
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            if (i > 0)
            {
                out += (pretty && allScalar) ? ", " : ",";
            }
            if (pretty && !allScalar)
            {
                newline(depth + 1);
            }
            m_items[i].DumpTo(out, pretty, depth + 1);
        }
        if (pretty && !allScalar)
        {
            newline(depth);
        }
        out.push_back(']');
        break;
    }
    case Type::Object:
    {
        if (m_members.empty())
        {
            out += "{}";
            break;
        }
        out.push_back('{');
        for (size_t i = 0; i < m_members.size(); ++i)
        {
            if (i > 0)
            {
                out.push_back(',');
            }
            if (pretty)
            {
                newline(depth + 1);
            }
            AppendEscapedString(out, m_members[i].first);
            out += pretty ? ": " : ":";
            m_members[i].second.DumpTo(out, pretty, depth + 1);
        }
        if (pretty)
        {
            newline(depth);
        }
        out.push_back('}');
        break;
    }
    }
}

bool Json::Parse(const std::string& text, Json& out, std::string& error)
{
    error.clear();
    Json result;
    Parser parser(text, error);
    if (!parser.ParseDocument(result))
    {
        return false;
    }
    out = std::move(result);
    return true;
}
