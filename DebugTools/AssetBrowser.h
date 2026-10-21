#pragma once
#include <functional>
#include <string>
#include <vector>
#include "AssetCatalog.h"
#include "ThumbnailCache.h"
#include "ModelPreviewRenderer.h"

class Renderer;
class DayNightCycle;

// 素材(モデル)を、サムネイルの画像と、フォルダの木で見るImGuiパネル「Assets」。Unityのプロジェクトウィンドウに相当する。
// **軽い**ことが役目: モデルを回して見るような重い処理は無く(それは AssetViewer という別のウィンドウ)、
// 画像(サムネイル)の一覧とフォルダの整理だけを行う。
//
// ・F2(Game側でキーを受ける)で開閉する。閉じている間は何も処理しない(負荷ゼロ)。初めて開いた時に、一覧を作る。
// ・サムネイルは、初回だけモデルを読んで作り、Cache/Thumbnails/に保存する。2回目以降は画像を読むだけ。
// ・素材は、プロジェクト内のフォルダだけを探す(AssetCatalog::DefaultRoots)。絶対パスは使わない。
// ・左にフォルダの木、右にサムネイルの一覧。Assets/Models と Assets/Incoming の下では、フォルダの作成・名前の変更・移動ができる。
// ・ビューワー(別ウィンドウ)で開く操作と、シーンへ置く操作は、外から渡された関数を呼ぶだけ(この部品はそれらを知らない)。
// ・Updateで処理(サムネイルの生成の進行)、DrawDebugUIでパネルを描く。他のデバッグパネルと同じ作法。
class AssetBrowser
{
public:
    // モデルをドラッグする時の、ImGuiのペイロードの種類(中身は、モデルのパスの文字列)。Game側が、ゲームの画面へ落とされたかを見るのに使う。
    static constexpr const char* kModelPayloadType = "ASSET_MODEL";

    // ビューワーを開く関数: models(開くモデル)とtitle(ビューワーの見出し)を渡す。
    using OpenViewerFn = std::function<void(const std::vector<AssetEntry>& models, const std::string& title)>;
    // 「シーンの中央に置く」の関数。
    using PlaceFn = std::function<void(const AssetEntry& entry)>;

    // rendererとdayNightCycleは呼び出し側が所有する(借りるだけ)。dayNightCycleは、サムネイルの光に使う。
    void Initialize(Renderer* renderer, const DayNightCycle* dayNightCycle);

    // パネルの開閉。開閉の状態は、Editorメニュー(EditorUi)のパネル一覧と共有するため、外の変数を借りる。
    // 渡さなければ、自分の変数を使う(単体テスト・Editorメニューが無い時)。
    void SetOpenFlag(bool* openFlag) { m_openFlag = openFlag ? openFlag : &m_ownOpen; }
    void Toggle() { *m_openFlag = !*m_openFlag; }
    bool IsOpen() const { return *m_openFlag; }

    // 開いている間だけ、サムネイルの生成を進める。
    void Update();

    // パネルを描く(ImGuiのフレームの中で呼ぶ)。
    void DrawDebugUI();

    // ---- 外とのつなぎ ----
    void SetOpenViewerHandler(OpenViewerFn handler) { m_openViewer = std::move(handler); }
    void SetPlaceAtCenterHandler(PlaceFn handler) { m_placeAtCenter = std::move(handler); }
    // フォルダの名前の変更・移動の前に呼ぶ「使用中か」の判定(シーンのPropが使っている時にtrue)。
    void SetFolderLockCheck(FolderLockCheck check) { m_lockCheck = std::move(check); }

    // 素材を探し直して、一覧を作り直す(外からファイルを足した後など)。
    void Rescan();

    // 取り込み(エクスプローラーからのドロップ)の後に呼ぶ: 探し直して、folder を選び、サムネイルの生成を予約する。
    void ShowFolder(const std::string& folder);

    // 取り込み(エクスプローラーからのドロップ)の結果を、Assetsパネルに出す: 取り込んだフォルダを選び、結果のメッセージを
    // 「取り込み結果」のウィンドウで見せる(OKと「エクスプローラーで開く」)。取り込めなかった時(folderが空)は、メッセージだけ。
    void ShowImportResult(const std::string& message, const std::string& folder);

    // サムネイルの状態(生成待ちの数・作成数など)。診断や、将来の別の画面での表示に使う。
    const ThumbnailCache& GetThumbnailCache() const { return m_thumbnails; }
    const AssetCatalog& GetCatalog() const { return m_catalog; }
    bool IsCatalogReady() const { return m_initialized; }

    // 選択中のモデル(無ければnullptr)。
    const AssetEntry* GetSelectedEntry() const;

    // 今見えている(フォルダ・検索・重複の絞り込み後の)モデルの一覧。
    std::vector<AssetEntry> GetVisibleEntries() const;

private:
    // 初めて開いた時に、サムネイルの仕組みを用意して、素材を探す。
    void EnsureInitialized();

    // 見えている項目(フォルダ・検索・重複の絞り込み後)の添字一覧を作り直す。
    void RebuildVisibleList();

    // 見えている項目のサムネイルを、空いた時間に作るよう予約する。
    void EnqueueVisibleThumbnails();

    // 選択を、フォルダ(path)に変える。
    void SelectFolder(const std::string& path);

    // ビューワーを開く: 選択中のモデル、無ければ今見えている全て。
    void OpenViewerForSelection();
    void OpenViewerForVisible(const std::string& title);

    void DrawToolbar();
    void DrawCacheButton();
    void DrawBreadcrumb();
    void DrawFolderTree();
    void DrawFolderNode(int index);
    void DrawThumbnailGrid();
    void DrawEntryContextMenu(const AssetEntry& entry);
    void DrawFolderPopup();
    void DrawImportPopup();
    void DrawStatusLine();

    // フォルダ操作。成功したら探し直して、新しいフォルダを選ぶ。失敗したら m_folderError に理由を入れる。
    void BeginFolderPopup(int mode, const std::string& target);
    bool ApplyFolderPopup();
    void MoveFolderTo(const std::string& source, const std::string& newParent);

    Renderer* m_renderer = nullptr;
    const DayNightCycle* m_dayNightCycle = nullptr;

    ModelPreviewRenderer m_preview;
    ThumbnailCache m_thumbnails;
    AssetCatalog m_catalog;
    bool m_initialized = false;
    bool m_ownOpen = false;
    bool* m_openFlag = &m_ownOpen;  // 開閉の状態(Editorメニューのパネル一覧の物を借りることもある)

    OpenViewerFn m_openViewer;
    PlaceFn m_placeAtCenter;
    FolderLockCheck m_lockCheck;

    // 一覧の絞り込み。
    char m_searchText[128] = {};
    bool m_includeSubfolders = true; // 選んだフォルダの下のフォルダ内のモデルも出すか
    bool m_showDuplicates = false;   // 同じモデルのOBJ/FBXの重複も出すか
    float m_thumbnailScale = 128.0f; // 一覧のサムネイルの表示の大きさ(64〜192)
    std::string m_selectedFolder;    // 選んでいるフォルダ(空=すべて)
    std::vector<size_t> m_visible;   // m_catalog.GetEntries()の添字(今見える項目)
    bool m_visibleDirty = true;

    // 選択(モデル)。探し直しても保てるように、パスで持つ。
    std::string m_selectedPath;

    // フォルダの作成・名前の変更のポップアップ。
    int m_folderPopupMode = 0;       // 0=なし、1=新しいフォルダ、2=名前の変更
    std::string m_folderPopupTarget; // 作成先の親フォルダ、または名前を変えるフォルダ
    char m_folderNameBuffer[80] = {};
    std::string m_folderError;       // ポップアップや移動の失敗の理由(次の操作まで表示する)
    bool m_folderPopupOpenRequested = false;

    // 取り込み結果のポップアップ。
    std::string m_importMessage;
    std::string m_importFolder;      // 取り込んだフォルダ(「エクスプローラーで開く」用。空なら無い)
    bool m_importPopupRequested = false;

    int m_focusFrame = -100;         // パネルを前面に出す要求を出したフレーム番号
    bool m_wasDrawn = false;         // 前のフレームで描いたか(閉じていたものを開いた瞬間を見つけて、前面に出すため)

    // モデルキャッシュのディスク上の大きさ(測るのは時々だけ)。
    size_t m_cacheFiles = 0;
    uint64_t m_cacheBytes = 0;
    int m_cacheMeasureCounter = 120; // 120フレームごとに測る。最初は、すぐ測る

    // 木の描画中に、木(m_catalog)を作り直さないよう、ドラッグでの移動は描画の後でまとめて行う。
    std::string m_pendingMoveSource;
    std::string m_pendingMoveTarget;

    // 木で、このフォルダまでの親を開く(一度だけ)。作成・移動・取り込みの後に、結果のフォルダが見えるようにする。
    std::string m_expandPath;
};
