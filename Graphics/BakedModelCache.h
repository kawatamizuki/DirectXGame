#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ModelData.h"

// 一度読んだモデル(ModelData)を、ディスクに「焼いて(bake)」保存しておき、次回からはそれを読むだけにする部品。
// OBJの文字の読み取り・FBXの解析・座標系の変換・色の変換といった重い処理を飛ばし、保存したバイト列をほぼそのまま
// 頂点バッファにできるので、2回目以降の読み込みが軽くなる。
//
// ・ModelLoaderRegistry::LoadFromFile の中で使う(拡張子ごとのローダーの前)。呼び出し側(Model、サムネイル、ビューワー、
//   ゲーム起動時の読み込み)は何も変えずに、全部が速くなる。
// ・保存先: Cache/Models/<名前>_<キー>.mesh (Git管理外。消しても、次の読み込みで作り直されるだけ)。
// ・キー(16桁の16進数) = モデルの相対パス・ファイルサイズ・更新日時・同じフォルダの .mtl の更新日時・kVersion の FNV-1a ハッシュ。
//   モデルやMTLのファイルが変われば、キーが変わるので、自動で作り直される。
// ・形式: ヘッダー + **重複のない頂点の表** + **インデックス**。三角形ごとに頂点を並べる元の形(同じ頂点が何度も入る)より、
//   ディスクの大きさが数分の1になる。読み込み時に、元の並びへ展開する(単純なコピーなので速い)。
// ・安全: 一時ファイルに書いてから置き換える(途中で落ちても壊れない)。壊れた・短い・版が違う・チェックが合わない
//   ファイルは捨てて、ローダーで読み直し、作り直す。保存に失敗しても、そのまま続行する(読み込み自体は成功させる)。
//
// 【重要】ローダーの出力の意味が変わった時(座標系の変換・色の変換・テクスチャの探し方などを変えた時)は、
//   kVersion を上げること。上げないと、古い内容のキャッシュが使われ続ける。
class BakedModelCache
{
public:
    // キャッシュの形式・内容の版。ローダーの出力が変わったら上げる。
    static constexpr uint32_t kVersion = 1;

    // 統計(今回の起動で)。
    struct Stats
    {
        size_t hits = 0;            // キャッシュから読めた数
        size_t misses = 0;          // キャッシュが無く、ローダーで読んだ数
        size_t stores = 0;          // 保存した数
        size_t storeFailures = 0;   // 保存に失敗した数
        size_t invalidated = 0;     // 壊れている・版が違うなどで捨てた数
        double hitMs = 0.0;         // ヒットした読み込みの合計時間(ミリ秒)
        double missLoadMs = 0.0;    // ミスした時の、ローダーでの読み込みの合計時間(ミリ秒)
        double storeMs = 0.0;       // 保存の合計時間(ミリ秒)
    };

    // 使うか(既定はtrue)。falseなら、読み込みも保存も一切しない。
    static void SetEnabled(bool enabled);
    static bool IsEnabled();

    // 保存先のフォルダ(既定は "Cache/Models")。テストで一時フォルダに変えるために使う。
    static void SetDirectory(const std::string& directory);
    static const std::string& GetDirectory();

    // modelPath のキャッシュから読む。成功したら true で out に入る。無い・壊れている・版が違う時は false
    // (壊れている物は捨てる)。
    static bool TryLoad(const std::string& modelPath, ModelData& out);

    // modelPath の読み込み結果 data をキャッシュに保存する。失敗したら false(呼び出し側は無視してよい)。
    // loaderMs は、ローダーで読むのにかかった時間(統計の「ミス」の時間に足す)。
    static bool Store(const std::string& modelPath, const ModelData& data, double loaderMs = 0.0);

    // 統計の取得・リセット。
    static Stats GetStats();
    static void ResetStats();

    // 保存先の中の .mesh ファイルの数と、合計のバイト数。
    static void MeasureDisk(size_t& outFileCount, uint64_t& outBytes);

    // 保存先の .mesh ファイルを全て消す。消した数を返す。
    static size_t ClearAll();

    // ---- テストしやすいように公開している部分 ----

    // modelPath のキャッシュファイルのパス(キャッシュフォルダ/名前_キー.mesh)。モデルのファイルが無ければ空。
    static std::string GetCachePath(const std::string& modelPath);

    // data を、ファイルの中身(バイト列)に直す / バイト列から戻す。戻す時は、形式・大きさ・チェック・範囲を全て確かめる。
    static bool Serialize(const ModelData& data, std::vector<uint8_t>& outBytes);
    static bool Deserialize(const uint8_t* bytes, size_t size, ModelData& out);
};
