#include "FontAtlas.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

// stb_truetype(ImGuiに同梱のもの)を、このファイルの中だけで使う。STBTT_STATICで関数を
// このファイル専用(static)にするので、ImGui側が同じヘッダーを使っていても名前は衝突しない。
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace
{
    // 図形(単色)用に左上へ確保する白い領域の大きさ(ピクセル)。
    constexpr int kSolidBlockSize = 2;

    // 文字どうしが隣のUVを拾って滲まないようにする余白(ピクセル)。
    constexpr int kGlyphPadding = 1;

    stbtt_fontinfo* AsInfo(void* p) { return static_cast<stbtt_fontinfo*>(p); }
    const stbtt_fontinfo* AsInfo(const void* p) { return static_cast<const stbtt_fontinfo*>(p); }

    bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            return false;
        }
        std::streamsize size = file.tellg();
        if (size <= 0)
        {
            return false;
        }
        file.seekg(0, std::ios::beg);
        out.resize(static_cast<size_t>(size));
        return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
    }
}

FontAtlas::~FontAtlas()
{
    delete AsInfo(m_fontInfo);
    m_fontInfo = nullptr;
}

bool FontAtlas::Initialize(int atlasSize, const std::vector<std::string>& fontPaths)
{
    m_size = atlasSize;
    m_pixels.assign(static_cast<size_t>(atlasSize) * static_cast<size_t>(atlasSize), 0);
    ClearAtlas();

    // 優先順: プロジェクトの Fonts/UIFont.ttf (ユーザーが後から置ける。ドット絵風フォントなどに差し替え用)
    // → Windows標準のメイリオ → 游ゴシック → MSゴシック。
    std::vector<std::string> candidates = fontPaths;
    if (candidates.empty())
    {
        candidates =
        {
            "Fonts/UIFont.ttf",
            "C:\\Windows\\Fonts\\meiryo.ttc",
            "C:\\Windows\\Fonts\\YuGothM.ttc",
            "C:\\Windows\\Fonts\\msgothic.ttc",
        };
    }

    for (const std::string& path : candidates)
    {
        std::vector<uint8_t> data;
        if (!ReadFile(path, data))
        {
            continue;
        }

        stbtt_fontinfo* info = new stbtt_fontinfo();
        int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
        if (offset < 0 || !stbtt_InitFont(info, data.data(), offset))
        {
            delete info;
            continue;
        }

        delete AsInfo(m_fontInfo);
        m_fontInfo = info;
        m_fontData = std::move(data); // stbttは生のポインタで参照するので、移した後もこのvectorを保持し続ける
        m_hasFont = true;
        return true;
    }

    m_hasFont = false;
    return false;
}

int DecodeUtf8(const char* text, size_t length, uint32_t& codepoint)
{
    constexpr uint32_t kReplacement = 0xFFFD;

    if (length == 0)
    {
        codepoint = kReplacement;
        return 0;
    }

    const unsigned char* s = reinterpret_cast<const unsigned char*>(text);
    unsigned char lead = s[0];

    if (lead < 0x80)
    {
        codepoint = lead;
        return 1;
    }

    int extra = 0;
    uint32_t value = 0;
    uint32_t minimum = 0; // この長さで表せる最小値(これより小さければ「冗長な符号化」で不正)
    if ((lead & 0xE0) == 0xC0) { extra = 1; value = lead & 0x1F; minimum = 0x80; }
    else if ((lead & 0xF0) == 0xE0) { extra = 2; value = lead & 0x0F; minimum = 0x800; }
    else if ((lead & 0xF8) == 0xF0) { extra = 3; value = lead & 0x07; minimum = 0x10000; }
    else
    {
        codepoint = kReplacement; // 継続バイト単独、または0xF8以上
        return 1;
    }

    if (length < static_cast<size_t>(extra) + 1)
    {
        codepoint = kReplacement; // 途中で途切れている
        return 1;
    }

    for (int i = 1; i <= extra; ++i)
    {
        if ((s[i] & 0xC0) != 0x80)
        {
            codepoint = kReplacement;
            return 1;
        }
        value = (value << 6) | (s[i] & 0x3F);
    }

    // 冗長な符号化・サロゲート・Unicodeの範囲外は不正。
    if (value < minimum || (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF)
    {
        codepoint = kReplacement;
        return 1;
    }

    codepoint = value;
    return extra + 1;
}

void FontAtlas::ClearAtlas()
{
    std::fill(m_pixels.begin(), m_pixels.end(), static_cast<uint8_t>(0));
    m_glyphs.clear();

    // 左上に図形用の白い領域を作り、その右から文字を詰め始める。
    for (int y = 0; y < kSolidBlockSize; ++y)
    {
        for (int x = 0; x < kSolidBlockSize; ++x)
        {
            m_pixels[static_cast<size_t>(y) * m_size + x] = 255;
        }
    }
    m_shelfX = kSolidBlockSize + kGlyphPadding;
    m_shelfY = 0;
    m_shelfHeight = kSolidBlockSize + kGlyphPadding;

    m_needsReset = false;
    MarkDirty(0, 0, m_size, m_size); // 全体を転送し直す
}

void FontAtlas::MarkDirty(int x0, int y0, int x1, int y1)
{
    if (!m_dirty)
    {
        m_dirtyRect = { x0, y0, x1, y1 };
        m_dirty = true;
        return;
    }
    if (x0 < m_dirtyRect.x0) { m_dirtyRect.x0 = x0; }
    if (y0 < m_dirtyRect.y0) { m_dirtyRect.y0 = y0; }
    if (x1 > m_dirtyRect.x1) { m_dirtyRect.x1 = x1; }
    if (y1 > m_dirtyRect.y1) { m_dirtyRect.y1 = y1; }
}

bool FontAtlas::ConsumeDirtyRect(DirtyRect& outRect)
{
    if (!m_dirty)
    {
        return false;
    }
    outRect = m_dirtyRect;
    m_dirty = false;
    return true;
}

bool FontAtlas::ResetIfNeeded()
{
    if (!m_needsReset)
    {
        return false;
    }
    ClearAtlas();
    return true;
}

void FontAtlas::GetSolidUV(float& u, float& v) const
{
    // 白い2x2の領域の真ん中(テクセルの境目)。線形補間で拡大縮小されても白い領域の中に収まる。
    float center = static_cast<float>(kSolidBlockSize) * 0.5f;
    u = center / static_cast<float>(m_size);
    v = center / static_cast<float>(m_size);
}

bool FontAtlas::Allocate(int w, int h, int& outX, int& outY)
{
    int needW = w + kGlyphPadding;
    int needH = h + kGlyphPadding;

    // 今の段に収まらなければ次の段へ。
    if (m_shelfX + needW > m_size)
    {
        m_shelfY += m_shelfHeight;
        m_shelfX = 0;
        m_shelfHeight = 0;
    }

    if (needW > m_size || m_shelfY + needH > m_size)
    {
        return false; // 空きが無い
    }

    outX = m_shelfX;
    outY = m_shelfY;
    m_shelfX += needW;
    if (needH > m_shelfHeight)
    {
        m_shelfHeight = needH;
    }
    return true;
}

float FontAtlas::GetAscent(float pixelSize) const
{
    if (!m_hasFont)
    {
        return pixelSize * 0.8f;
    }
    const stbtt_fontinfo* info = AsInfo(m_fontInfo);
    float scale = stbtt_ScaleForPixelHeight(info, pixelSize);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    return static_cast<float>(ascent) * scale;
}

float FontAtlas::GetLineHeight(float pixelSize) const
{
    if (!m_hasFont)
    {
        return pixelSize;
    }
    const stbtt_fontinfo* info = AsInfo(m_fontInfo);
    float scale = stbtt_ScaleForPixelHeight(info, pixelSize);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    return static_cast<float>(ascent - descent) * scale;
}

const GlyphInfo* FontAtlas::GetGlyph(uint32_t codepoint, float pixelSize)
{
    if (!m_hasFont)
    {
        return nullptr;
    }

    int sizeKey = static_cast<int>(std::lround(pixelSize));
    if (sizeKey < 1)
    {
        sizeKey = 1;
    }

    GlyphKey key{ codepoint, sizeKey };
    auto found = m_glyphs.find(key);
    if (found != m_glyphs.end())
    {
        return &found->second;
    }

    const stbtt_fontinfo* info = AsInfo(m_fontInfo);
    float scale = stbtt_ScaleForPixelHeight(info, static_cast<float>(sizeKey));
    int glyphIndex = stbtt_FindGlyphIndex(info, static_cast<int>(codepoint));

    int advance = 0, leftBearing = 0;
    stbtt_GetGlyphHMetrics(info, glyphIndex, &advance, &leftBearing);

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(info, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);
    int w = x1 - x0;
    int h = y1 - y0;

    GlyphInfo glyph;
    glyph.advance = static_cast<float>(advance) * scale;

    if (w > 0 && h > 0)
    {
        int px = 0, py = 0;
        if (!Allocate(w, h, px, py))
        {
            m_needsReset = true; // 次のフレームの頭で焼き直す
            return nullptr;
        }

        stbtt_MakeGlyphBitmap(
            info, &m_pixels[static_cast<size_t>(py) * m_size + px], w, h, m_size, scale, scale, glyphIndex);
        MarkDirty(px, py, px + w, py + h);

        float invSize = 1.0f / static_cast<float>(m_size);
        glyph.u0 = static_cast<float>(px) * invSize;
        glyph.v0 = static_cast<float>(py) * invSize;
        glyph.u1 = static_cast<float>(px + w) * invSize;
        glyph.v1 = static_cast<float>(py + h) * invSize;
        glyph.w = static_cast<float>(w);
        glyph.h = static_cast<float>(h);
        glyph.xoff = static_cast<float>(x0);
        glyph.yoff = static_cast<float>(y0);
    }

    auto inserted = m_glyphs.emplace(key, glyph);
    return &inserted.first->second;
}
