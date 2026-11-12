#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "ISaveable.h"
#include "ISnapshotFormat.h"
#include "WorldSnapshot.h"

struct GameContext;
class Field;
class RoadSystem;
class BuildController;
class IOccupancyGrid;
class GridOrientationRegistry;

// セーブ/ロードの司令塔。「今の街の状態」をスナップショット(WorldSnapshot)としてコピーして
// ファイルに書き(Save)、ファイルから読んで街をその状態に復元する(Load)。
//
// 操作の履歴を再生する方式ではなく**状態そのもののコピー**なので、プレイ時間が長くなっても
// 保存・読み込みの重さは街の大きさにしか比例せず、建物の改築や数値の変化のように
// 「置いた操作」だけでは決まらない状態も、そのまま保存できる。
//
// 保存する本体: 道路の区間、建物(配置の確定結果を含む)、青マス(先着の優先順のまま)。
// 作り直す派生データ: 道路ノード・占有・道路メッシュ(RoadSystem::RestoreSegments)。
// システムごとの状態(時計・フラグ・住民など)は、ISaveableを実装して登録するだけで"state"の区画に入る
// (新しい状態を足す時に、この中核は触らない)。詳しくはdocs/design/save_load.md。
class ScenarioManager
{
public:
    // プレイヤーのクイックセーブの保存先(Git管理しない)と、ゲーム開始時に読み込む初期マップ(Git管理する)。
    static constexpr const char* kQuickSavePath = "Saves/quicksave.json";
    static constexpr const char* kStartScenarioPath = "Scenarios/tutorial.json";
    // テストマップの置き場(TestMapStore の既定の場所と同じ)。通常のセーブ(Save)は、この中へ書けない(元のテストマップを、誤って上書きしないため)。
    static constexpr const char* kTestMapsDirectory = "TestMaps";

    ScenarioManager();
    ~ScenarioManager();

    // 街を構成する各システムを借りる(所有はGame)。
    void Initialize(
        GameContext* context, Field* field, RoadSystem* roadSystem, BuildController* buildController,
        IOccupancyGrid* occupancy, GridOrientationRegistry* orientationRegistry);

    // 状態を持つシステムを登録する(保存・読み込み・全消去の対象になる)。登録するだけでよい。
    void RegisterSaveable(ISaveable* saveable);

    // ファイルに書く形式を差し替える(既定はJSON)。formatは呼び出し側が所有する。
    void SetFormat(const ISnapshotFormat* format);

    // 読み込みで街が入れ替わった時に呼ばれる(例: エディタの選択を解除する。オブジェクトが入れ替わるため)。
    void SetOnWorldReplaced(std::function<void()> callback) { m_onWorldReplaced = std::move(callback); }

    // 今の街をpathに保存する。成功したらtrue。messageに結果(または失敗の理由)が入る。
    // 一時ファイルに書いてから置き換えるので、書き込み中に落ちても、前のセーブは壊れない。
    // ただし、保護したフォルダ(既定: TestMaps/ とその下)の中のパスには、書かない(パスの書き方を変えても、すり抜けられない。PathGuard)。
    bool Save(const std::string& path, std::string& message);

    // 特定のファイルへの、通常のセーブを、一時的に止める(例: テストマップ使用中は、普段のクイックセーブを、守る)。空なら、止めない。
    // pathの書き方を変えても、すり抜けられない(PathGuard)。止めている間に、そのファイルへ Save すると、失敗して、reason を知らせる。
    void SetBlockedSavePath(const std::string& path, const std::string& reason) { m_blockedSavePath = path; m_blockedReason = reason; }

    // 「Scenario」パネルから読み込む時の、確認の要否の判定(テストマップ使用中は、読み込むと、読み込む前の街の控えが捨てられるため)。
    // 判定が true を返すパスは、パネルが確認ダイアログを出し、「読み込む」を押された時だけ Load する。キャンセルすれば、何も変わらない。
    // 判定が空(既定)なら、確認は出ない。F5/F9・耐久試験など、パネル以外の読み込みには、影響しない。
    void SetLoadConfirmCheck(std::function<bool(const std::string& path)> check) { m_loadConfirmCheck = std::move(check); }

    // 直近に、ファイルから読み込んだ(Load が成功した)パスと、その回数。メモリからの読み込み(巻き戻し・耐久試験・元の街に戻す)は、数えない。
    const std::string& LastLoadPath() const { return m_lastLoadPath; }
    unsigned LoadSerial() const { return m_loadSerial; }

    // pathが、保護したフォルダの中を指すか(通常のセーブが、書いてはいけない場所か)。
    bool IsProtectedPath(const std::string& path) const;
    // 保護するフォルダを足す(既定で、テストマップの置き場が入っている)。
    void AddProtectedDirectory(const std::string& directory) { m_protectedDirectories.push_back(directory); }

    // pathのセーブデータを読み込み、街をその状態に置き換える。成功したらtrue。
    // ファイルの読み込み・構文・内容の検証が全て成功してから街を消すので、失敗しても今の街は壊れない。
    bool Load(const std::string& path, std::string& message);

    // ---- メモリ上のセーブデータ(巻き戻し用のキーフレームなど。ファイルを経由しない) ----
    // 今の街を、セーブデータの文字列(bytes)にする。
    bool CaptureBytes(std::string& bytes, std::string& message) const;
    // セーブデータの文字列から、街を置き換える(Load と同じ検証・処理)。labelは、メッセージに出す名前。
    // quiet==true なら、ログ・「直近のメッセージ」を更新しない(巻き戻しのシークで、何度も呼ぶ時用)。
    bool LoadFromBytes(const std::string& bytes, const std::string& label, std::string& message, bool quiet);

    // ゲーム開始時の初期マップ(kStartScenarioPath)があれば読み込む。読み込んだらtrue(無ければ何もしない)。
    bool LoadStartScenario();

    // ---- 下位の操作(単体テスト・診断用に公開) ----
    // 今の街をスナップショットにコピーする。
    void CaptureSnapshot(WorldSnapshot& snapshot) const;
    // スナップショットの内容で街を置き換える(全消去 → 復元)。
    bool ApplySnapshot(const WorldSnapshot& snapshot, std::string& message);
    // 街を空にする(建物・住民・道路・占有・青マス・各ISaveableの状態)。地面などは残す。
    void ClearWorld();

    // 直近の保存・読み込みの結果のメッセージ。
    const std::string& GetLastMessage() const { return m_lastMessage; }

    // 保存・読み込みを操作するデバッグ用ImGuiパネル(「Scenario」ウィンドウ)。
    // openがnullptrでなければ、タイトルバーの×で閉じられ、*openがfalseの間は描かない。
    void DrawDebugUI(bool* open = nullptr);

private:
    // ApplySnapshotの本体。snapshotは、既に今のマスの大きさに合わせてある物。spaceScaleは、各ISaveableに伝える変換の比
    // (今のマスの大きさ / 保存した時のマスの大きさ)。
    bool ApplySnapshotImpl(const WorldSnapshot& snapshot, float spaceScale, std::string& message);

    bool ReadFileBytes(const std::string& path, std::string& bytes, std::string& error) const;

    GameContext* m_context = nullptr;
    Field* m_field = nullptr;
    RoadSystem* m_roadSystem = nullptr;
    BuildController* m_buildController = nullptr;
    IOccupancyGrid* m_occupancy = nullptr;
    GridOrientationRegistry* m_orientationRegistry = nullptr;

    std::vector<ISaveable*> m_saveables;
    std::vector<std::string> m_protectedDirectories;   // 通常のセーブが書けないフォルダ
    std::function<bool(const std::string&)> m_loadConfirmCheck;   // パネルからの読み込みに確認が要るか(空=不要)
    std::string m_pendingLoadPath;                     // 確認待ちの読み込み先
    bool m_openLoadConfirm = false;                    // 確認ダイアログを開く合図
    std::string m_blockedSavePath;                    // 一時的に、通常のセーブを止めているファイル(空=止めていない)
    std::string m_blockedReason;
    std::string m_lastLoadPath;                        // 直近に、ファイルから読み込んだパス
    unsigned m_loadSerial = 0;                         // ファイルからの読み込みが、成功した回数

    std::unique_ptr<ISnapshotFormat> m_defaultFormat; // 既定のJSON形式
    const ISnapshotFormat* m_format = nullptr;        // 使っている形式(既定か、差し替えられたもの)

    std::function<void()> m_onWorldReplaced;
    std::string m_lastMessage;

    // デバッグパネル用: 保存・読み込み先のパスの入力欄。
    char m_pathBuffer[260] = {};
};
