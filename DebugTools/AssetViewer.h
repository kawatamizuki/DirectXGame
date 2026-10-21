#pragma once
#include <memory>
#include <string>
#include <vector>
#include "AssetCatalog.h"
#include "Model.h"
#include "ModelPreviewRenderer.h"
#include "OffscreenTarget.h"

class Renderer;
class DayNightCycle;

// 選んだモデルを、大きく並べて見るための別ウィンドウ「Asset Viewer」。Assetsパネル(軽い一覧)とは別の、**重い処理を引き受ける機能**。
// 「モデルがどんな風に描かれるか」を確認するためのもので、特定のフォルダやモデルを一括で読み込む。
//
// ・普段は閉じていて、何も読み込まない(メモリも使わない)。Assetsのダブルクリック・Vキー・右クリックのメニューで開く。
// ・開く時に渡された一覧を、上限(既定64)まで、1フレームに少しずつ(時間枠)読み込む。進み具合を表示する。
//   上限を超える時は、件数を警告する。閉じる時に、読み込んだモデルを全て解放する。
// ・読み込んだモデルを、格子状に並べた1つの小さな世界として、ウィンドウの大きさのテクスチャに描く(OffscreenTarget)。
//   カメラは全体を見渡せる軌道カメラ(左ドラッグ=回転、中/右ドラッグ=平行移動、ホイール=ズーム)。名前の札を重ねて出す。
// ・見た目の確認用に、時刻(昼夜の光)、全部を背面から、背景の明るさを切り替えられる。
class AssetViewer
{
public:
    // rendererとdayNightCycleは呼び出し側が所有する(借りるだけ)。
    void Initialize(Renderer* renderer, const DayNightCycle* dayNightCycle);

    // 開閉の状態は、Editorメニュー(EditorUi)のパネル一覧と共有するため、外の変数を借りる。
    void SetOpenFlag(bool* openFlag) { m_openFlag = openFlag ? openFlag : &m_ownOpen; }
    bool IsOpen() const { return *m_openFlag; }

    // modelsを一括で読み込んで見る。ウィンドウを開く。titleは見出し(フォルダ名やモデル名)。
    // 既に開いている時は、今の内容を捨てて、新しい内容に入れ替える。
    void Open(const std::vector<AssetEntry>& models, const std::string& title);

    // 読み込みを時間枠の中で進める。閉じている間は、持っているモデルを解放して終わり(負荷ゼロ)。
    void Update();

    // ウィンドウを描く(ImGuiのフレームの中で呼ぶ)。モデルの絵(OffscreenTarget)への描画も、ここで行う。
    void DrawDebugUI();

    // ---- 診断・テスト用 ----
    size_t GetItemCount() const { return m_items.size(); }
    size_t GetLoadedCount() const;
    size_t GetFailedCount() const;
    size_t GetSkippedLargeCount() const;
    size_t GetRequestedCount() const { return m_requested; }
    bool IsLoading() const { return m_loadIndex < m_items.size(); }

    // 1フレームに読み込みに使う時間枠(ミリ秒)。
    static constexpr float kFrameBudgetMs = 8.0f;
    // 一括で読み込む数の上限の既定値と、設定できる最大。
    static constexpr int kDefaultMaxModels = 64;
    static constexpr int kMaxModelsLimit = 512;
    // この大きさを超えるファイルは、複数のモデルを開く時は、自動では読み込まない(1つだけ開く時は読み込む)。
    static constexpr uint64_t kLargeFileBytes = 4ull * 1024 * 1024;

private:
    struct Item
    {
        enum class State { Pending, Loaded, Failed, SkippedLarge };

        AssetEntry entry;
        std::unique_ptr<Model> model;   // 読み込み済みなら有効(Modelの住所を変えないよう、unique_ptrで持つ)
        State state = State::Pending;
        float loadMs = 0.0f;
        DirectX::XMFLOAT3 slot = { 0.0f, 0.0f, 0.0f };  // 並べた時の中心
        Transform transform;                             // 描く時の置き方
    };

    // 持っているモデルを全て解放して、空に戻す。
    void Release();

    // 格子の位置を決め、読み込み済みの各モデルの Transform(中心・大きさ・向き)を作り直す。
    void Layout();
    void UpdateTransform(Item& item);

    // 全体が見えるカメラの位置に戻す。
    void FitCamera();

    // 1つ読み込む(時間枠の中で繰り返し呼ぶ)。
    void LoadOne(Item& item);

    // ウィンドウの大きさに合わせて、絵の入れ物を作り直す(16ピクセル単位。変わった時だけ)。
    void EnsureTarget(UINT width, UINT height);

    void DrawControls();
    void DrawSceneImage(float width, float height);
    void DrawInfoPanel();

    Renderer* m_renderer = nullptr;
    const DayNightCycle* m_dayNightCycle = nullptr;
    ModelPreviewRenderer m_preview; // モデルを絵に描く部品(軽いので、サムネイル用とは別に自分で持つ)

    bool m_ownOpen = false;
    bool* m_openFlag = &m_ownOpen;

    std::string m_title;
    std::vector<AssetEntry> m_source;   // 開く時に渡された一覧(「再読み込み」に使う)
    std::vector<Item> m_items;          // 上限までの項目
    size_t m_requested = 0;             // 渡された件数(上限を超えた分も含む)
    size_t m_loadIndex = 0;             // 次に読み込む項目の添字
    float m_totalLoadMs = 0.0f;
    int m_cols = 1;
    int m_rows = 1;

    int m_maxModels = kDefaultMaxModels;
    bool m_loadLarge = false;           // 大きいファイルも読み込むか

    OffscreenTarget m_target;
    ModelPreviewRenderer::SceneView m_view;
    float m_timeOfDay = 12.0f;          // 光の時刻(0〜24)
    float m_backgroundBrightness = 1.0f;
    bool m_flipAll = false;             // 全部を背面から見る(モデルをY軸まわりに180度回す)
    bool m_showLabels = true;

    int m_selected = -1;                // 選択中の項目(無ければ-1)
    std::string m_selectedLicense;
};
