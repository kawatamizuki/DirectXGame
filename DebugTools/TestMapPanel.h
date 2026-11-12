#pragma once
#include <functional>
#include <string>
#include <vector>
#include "TestMapStore.h"
#include "SimTrial.h"

class TestMapSession;
class SimEndurance;

// Editorの「テストマップ」パネル(段階2)。手作業で作った街を、テストマップとして保存し、一覧から選んで読み込む。
//  ・一覧: 種別(正式 / 下書き)・表示名・id・作成日時。選ぶと、説明・検証目的・注目する指標・指紋などを、下に出す。
//  ・読み込み: 確認のあと、街を入れ替える(読み込む前の街は、メモリに控える)。読み込んだ街は、通常どおり、建築・編集・時間進行できる。
//    元のテストマップのファイルは、変わらない(通常のセーブ・F5は Saves/ へ書く)。「元の街に戻す」で、読み込む前の街へ戻せる。
//  ・保存: 「今の街を、新しいテストマップとして保存」。既存の名前は、上書きしない。保存先は、下書き(既定)か、正式。
//  ・誤操作の防止: 上書きの操作は、存在しない。削除は、idを打ち込んで確認する(正式マップは、Git管理のファイルである旨も、出す)。名前変更・正式への採用・元の街に戻す・読み込みは、確認つき。
//  ・使用中のテストマップ名と、「配置が元と同じか(建築・編集で変えたか)」を、常に、上に出す。
//  ・自動検証(段階3-B): 一覧で選んだテストマップの原本から、指定の週数・回数・基準の種で、住民のシミュレーションを進める。
//    実行・中止・進み具合・試行ごとの簡単な結果を出す。進める処理は SimEndurance(段階3-A)。結果は画面に出すだけ(保存は段階4)。
// 画面(ImGui)の部分だけ。保存・読み込みの規則は、TestMapStore / TestMapSession が持つ。ENABLE_EDITOR のビルドだけが呼ぶ。
class TestMapPanel
{
public:
    void SetOpenFlag(bool* open) { m_open = open; }
    // 自動検証を始める関数(ゲーム側が渡す。Game::StartTestMapTrials)。始められなければ false(message に理由)。
    using TrialStarter = std::function<bool(const std::string& id, const SimTrialConditions& conditions, std::string& message)>;
    // 自動検証の欄に渡す物。
    struct TrialAccess
    {
        SimEndurance* endurance = nullptr;  // 進み具合・結果を読む・中止する
        TrialStarter start;                 // 始める
        bool canStart = false;              // 今、始めてよいか(ゲームシーンのPlayingで、耐久試験・タイムライン・再現が動いていない)
        const char* whyNot = "";            // だめな時の理由
    };

    // canOperate: 今、街を入れ替えてよいか(ゲームシーンで、耐久試験などが動いていない)。whyNot: だめな時の理由。
    void Draw(TestMapStore& store, TestMapSession& session, bool canOperate, const char* whyNot, const TrialAccess& trials);

private:
    enum class Popup { None, Load, Restore, Abandon, Edit, Rename, Promote, Demote, Delete };

    void Refresh(TestMapStore& store);
    const TestMapEntry* FindEntry(const std::string& id) const;
    void SetMessage(const std::string& text, bool error);
    void DrawActive(TestMapStore& store, TestMapSession& session, bool canOperate);
    void DrawList();
    void DrawDetail(const TestMapEntry& entry);
    void DrawActions(TestMapStore& store, TestMapSession& session, const TestMapEntry& entry, bool canOperate);
    void DrawSaveNew(TestMapStore& store, TestMapSession& session, bool canOperate);
    void DrawPopups(TestMapStore& store, TestMapSession& session);
    void DrawTrials(const TrialAccess& trials);
    void DrawTrialSummary(const TrialAccess& trials);   // パネルの上の方に出す、自動検証の短い状況(スクロールしなくても、見えるように)

    bool* m_open = nullptr;
    std::vector<TestMapEntry> m_entries;
    bool m_dirty = true;
    double m_lastRefresh = -100.0;
    std::string m_selected;

    // 配置の変更の確認(街を丸ごと書き出して調べるので、間引く)。
    double m_lastModifiedCheck = -100.0;
    bool m_modifiedKnown = false;
    bool m_modified = false;

    Popup m_popup = Popup::None;
    bool m_openPopupRequest = false;
    std::string m_popupTarget;          // 操作の対象のid(ポップアップを開いた時の物)

    char m_editName[256] = {};
    char m_editDescription[2048] = {};
    char m_editPurpose[2048] = {};
    char m_editMetrics[1024] = {};      // 注目する指標(読点「、」か、カンマ「,」区切り)
    char m_renameId[64] = {};
    char m_deleteConfirm[64] = {};

    char m_newId[64] = {};
    char m_newName[256] = {};
    char m_newDescription[2048] = {};
    char m_newPurpose[2048] = {};
    char m_newMetrics[1024] = {};
    int m_newFolder = 0;                // 0 = 下書き(既定) / 1 = 正式

    // 自動検証の条件(画面の入力)。倍率は、暫定で20倍に固定(入力にしない)。
    int m_trialWeeks = 26;              // 1回の試行で進める週数
    int m_trialCount = 3;               // 試行の回数
    uint32_t m_trialBaseSeed = 12345;   // 基準の種
    bool m_trialDeterminism = true;     // 決定論の確認(最後に、1回目と同じ種で、もう1回)
    std::string m_trialStartError;      // 始められなかった理由(自動検証の欄の中に出す)

    std::string m_message;
    bool m_messageIsError = false;
};
