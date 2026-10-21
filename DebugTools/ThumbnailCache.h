#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "AssetCatalog.h"
#include "LightingState.h"
#include "OffscreenTarget.h"

class Renderer;
class ModelPreviewRenderer;

// モデルのサムネイル(小さな画像)を、ディスクにキャッシュして管理する。
//
// 仕組み:
//  ・サムネイルは「Cache/Thumbnails/<名前>_<キー>.png」に保存する。キーは、モデルのパス・ファイルサイズ・更新日時・
//    見た目の版(kVersion)から決まる。モデルのファイルが変われば、キーが変わるので、自動で作り直される。
//  ・画像が既にあれば、PNGを読むだけ(速い)。無ければ、モデルを1回だけ読み込んで描き、PNGに保存する(重いので、時間枠の中で少しずつ)。
//  ・使うのはImGuiなどで表示するためのSRV。見えている項目から先に用意する。
//  ・保存したPNGは元のモデルが消えても残るので、「見て判断する」ための記録として使える。
class ThumbnailCache
{
public:
    // 画面に出すサムネイルの大きさ(正方形)。実際はこの2倍の大きさで描いて、2x2で平均して縮小する(縁のギザギザを減らす)。
    static constexpr UINT kThumbnailSize = 128;
    static constexpr UINT kRenderSize = kThumbnailSize * 2;

    // サムネイルの見た目(カメラの向き・背景・光・大きさ)を変えたら上げる。上げると、古いサムネイルは使われなくなる。
    static constexpr int kVersion = 2; // 2: Blender製MTLの色をsRGBに直すようにした(Quaterniusなどが暗かった)

    // メモリ(GPU)に持っておくサムネイルの最大数。超えたら、使われていない古い物から捨てる(ディスクには残る)。
    static constexpr size_t kMaxResident = 512;

    // これより大きなモデルファイルは、自動ではサムネイルを作らない(街全体が入ったシーンなど、読み込みに数秒かかり、
    // その間は画面が止まるため)。クリックされた時だけ作る(RequestAnyway)。SimplePolyの Scene_City.fbx(約9MB)は約4.6秒かかった。
    static constexpr uint64_t kLargeFileBytes = 4ull * 1024 * 1024;

    // renderer/previewは呼び出し側が所有する(借りるだけ)。lightingは、サムネイルを描く時の固定の光。
    // cacheDirectoryは作業フォルダからの相対パス。無ければ作る。
    bool Initialize(Renderer* renderer, ModelPreviewRenderer* preview, const LightingState& lighting,
        const std::string& cacheDirectory = "Cache/Thumbnails");

    // entryのサムネイルを返す。まだ用意できていなければnullptrを返し、用意する予約を入れる(見えている項目を優先)。
    ID3D11ShaderResourceView* Get(const AssetEntry& entry);

    // 大きなファイル(kLargeFileBytes超)で、まだ「作る」と指示されていない項目か。trueの間は自動では生成しない。
    bool IsLargeSkipped(const AssetEntry& entry) const;

    // 大きなファイルでも、今すぐ作るよう指示する(クリックされた時用)。
    void RequestAnyway(const AssetEntry& entry);

    // 読み込みか生成に失敗した項目か(失敗した物は、再び試さない)。
    bool HasFailed(const AssetEntry& entry) const;

    // 見えていない項目も、空いた時間に順に用意するため、まとめて予約する(Get()による予約より後回しになる)。
    void EnqueueBackground(const std::vector<AssetEntry>& entries);

    // 予約を処理する。budgetMs(ミリ秒)を使い切るまで、ただし最低1件は進める。一時停止中は何もしない。
    void Update(float budgetMs);

    // entriesのサムネイルを、メモリとディスクから消して、予約し直す(「作り直す」ボタン用)。
    void Regenerate(const std::vector<AssetEntry>& entries);

    void SetPaused(bool paused) { m_paused = paused; }
    bool IsPaused() const { return m_paused; }

    // 統計(パネルの表示用)。
    size_t GetPendingCount() const { return m_queue.size(); }
    size_t GetGeneratedCount() const { return m_generatedCount; }       // 今回の起動で、モデルを読んで作った数
    size_t GetLoadedFromDiskCount() const { return m_loadedFromDiskCount; } // 今回の起動で、キャッシュのPNGを読んだ数
    size_t GetFailedCount() const { return m_failed.size(); }
    float GetLastGenerateMs() const { return m_lastGenerateMs; }        // 直近の1件の生成にかかった時間
    float GetTotalGenerateMs() const { return m_totalGenerateMs; }
    float GetSlowestGenerateMs() const { return m_slowestMs; }          // 今回の起動で、最も時間がかかった1件の時間
    const std::string& GetSlowestPath() const { return m_slowestPath; } // その項目のパス

    // ---- テストしやすいように公開している部分 ----

    // entryのキー(16桁の16進数)。パス・サイズ・更新日時・版が同じなら、いつも同じ値。
    static std::string MakeKey(const AssetEntry& entry);

    // 保存先のパス(キャッシュフォルダ/名前_キー.png)。
    std::string GetCachePath(const AssetEntry& entry) const;

    // RGBA(幅*高さ*4バイト)を、縦横とも半分に縮小する(2x2の平均)。幅・高さは偶数であること。
    static std::vector<uint8_t> DownscaleHalf(const std::vector<uint8_t>& rgba, UINT width, UINT height);

    // RGBAの画素をPNGファイルに保存する(途中で失敗しても、既存のファイルを壊さない)。
    static bool WritePng(const std::string& path, const std::vector<uint8_t>& rgba, UINT width, UINT height);

private:
    struct Request
    {
        AssetEntry entry;
        std::string key;
    };

    struct Resident
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        uint64_t lastUse = 0;
    };

    void Enqueue(const AssetEntry& entry, const std::string& key, bool front);
    void Process(const Request& request);
    bool LoadFromDisk(const std::string& path, const std::string& key);
    bool Generate(const Request& request);
    bool StoreFromPixels(const std::string& key, const std::vector<uint8_t>& rgba, UINT width, UINT height);
    void Evict();

    Renderer* m_renderer = nullptr;
    ModelPreviewRenderer* m_preview = nullptr;
    LightingState m_lighting;
    std::string m_cacheDirectory;
    OffscreenTarget m_target;

    std::unordered_map<std::string, Resident> m_resident;
    std::unordered_set<std::string> m_failed;
    std::unordered_set<std::string> m_forced; // 大きなファイルだが、作るよう指示された項目のキー
    std::deque<Request> m_queue;
    std::unordered_set<std::string> m_queued;
    uint64_t m_useCounter = 0;

    bool m_paused = false;
    size_t m_generatedCount = 0;
    size_t m_loadedFromDiskCount = 0;
    float m_lastGenerateMs = 0.0f;
    float m_totalGenerateMs = 0.0f;
    float m_slowestMs = 0.0f;
    std::string m_slowestPath;
};
