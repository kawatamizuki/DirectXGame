#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// アトラス上の1文字(グリフ)の情報。位置・大きさはすべてピクセル単位。
struct GlyphInfo
{
    // アトラス上のUV座標(0〜1)。
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;

    // 文字の画像の大きさ。w/hが0なら描く画像が無い文字(空白など)。
    float w = 0.0f, h = 0.0f;

    // ペン位置(行の左端・ベースライン)から、画像の左上までのずれ。
    // yoffはベースラインからの距離で、文字は上に伸びるので普通は負になる。
    float xoff = 0.0f, yoff = 0.0f;

    // 次の文字へペンを進める量。
    float advance = 0.0f;
};

// 文字の画像を「使われた時に1文字ずつ」焼いて1枚の画像(アトラス)に詰めていく。
// 日本語は文字数が多いので、事前に全部焼かず、画面に出す文字だけを焼く。
// このクラスはCPU側(画像データの管理・文字の焼き込み・詰め込み)だけを担当し、D3Dのテクスチャへの
// 転送はUIRendererが行う(CPU側だけを画面なしで単体テストできるようにするため)。
// アトラスの左上には、図形(単色)を同じテクスチャで描くための白い領域を確保してある。
class FontAtlas
{
public:
    FontAtlas() = default;
    ~FontAtlas();
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    // atlasSize x atlasSize(R8の1チャンネル)のアトラスを作り、フォントを読み込む。
    // フォントが見つからなくても、図形用の白い領域は使える(文字だけが描けない)。
    // fontPathsは優先順に探すフォントファイルの一覧(空なら既定の候補を使う)。
    bool Initialize(int atlasSize, const std::vector<std::string>& fontPaths = {});

    // フォントを読み込めていて、文字を描けるか。
    bool HasFont() const { return m_hasFont; }

    // codepointの文字を、pixelSizeの高さ(ピクセル)で焼いて返す(焼き済みならそれを返す)。
    // アトラスに空きが無い時はnullptrを返し、次のフレームの頭(ResetIfNeeded)で全部焼き直す。
    const GlyphInfo* GetGlyph(uint32_t codepoint, float pixelSize);

    // pixelSizeでの行の高さ(上端から下端まで)と、上端からベースラインまでの距離。
    float GetLineHeight(float pixelSize) const;
    float GetAscent(float pixelSize) const;

    // アトラスの画像データ(R8、1辺atlasSizeの正方形)。
    const uint8_t* Pixels() const { return m_pixels.data(); }
    int Size() const { return m_size; }

    // 図形(単色)を描く時に使う、白い領域の中心のUV。
    void GetSolidUV(float& u, float& v) const;

    // 前回の取得以降に変更された範囲(矩形)を返して、変更なしの状態に戻す。変更が無ければfalse。
    struct DirtyRect { int x0, y0, x1, y1; }; // x1,y1は含まない
    bool ConsumeDirtyRect(DirtyRect& outRect);

    // 満杯になっていたら、アトラスと焼き済みの文字を全部破棄して白い領域だけの状態に戻す。
    // 戻したらtrue(全体を転送し直す必要がある)。フレームの頭で呼ぶ(描画中に呼ぶと、
    // 既に積んだ文字のUVが無効になるため)。
    bool ResetIfNeeded();

private:
    struct GlyphKey
    {
        uint32_t codepoint;
        int pixelSize;
        bool operator==(const GlyphKey& o) const { return codepoint == o.codepoint && pixelSize == o.pixelSize; }
    };
    struct GlyphKeyHash
    {
        size_t operator()(const GlyphKey& k) const
        {
            return (static_cast<size_t>(k.codepoint) * 2654435761u) ^ static_cast<size_t>(k.pixelSize);
        }
    };

    // 画像を(w x h)ぶん詰められる位置を探す。詰められたらtrue(outX/outYが左上)。
    bool Allocate(int w, int h, int& outX, int& outY);
    void ClearAtlas();
    void MarkDirty(int x0, int y0, int x1, int y1);

    int m_size = 0;
    std::vector<uint8_t> m_pixels;

    // 棚詰め(左から右へ並べ、行が埋まったら次の段へ)の現在位置。
    int m_shelfX = 0;
    int m_shelfY = 0;
    int m_shelfHeight = 0;

    std::unordered_map<GlyphKey, GlyphInfo, GlyphKeyHash> m_glyphs;

    bool m_dirty = false;
    DirtyRect m_dirtyRect{ 0, 0, 0, 0 };
    bool m_needsReset = false;

    // フォントファイルの中身と、stb_truetypeの情報(型を隠すためvoid*で持つ)。
    std::vector<uint8_t> m_fontData;
    void* m_fontInfo = nullptr;
    bool m_hasFont = false;
};

// UTF-8の文字列を次の1文字ぶん読み進める。codepointに結果を入れ、読んだバイト数を返す。
// 不正なバイト列は1バイトだけ消費して、代替文字(U+FFFD)を返す(途中で止まったり無限ループしない)。
int DecodeUtf8(const char* text, size_t length, uint32_t& codepoint);
