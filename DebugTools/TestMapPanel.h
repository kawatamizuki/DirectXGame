#pragma once
#include <string>
#include <vector>
#include "TestMapStore.h"

class TestMapSession;

// Editorの「テストマップ」パネル(段階2)。手作業で作った街を、テストマップとして保存し、一覧から選んで読み込む。
//  ・一覧: 種別(正式 / 下書き)・表示名・id・作成日時。選ぶと、説明・検証目的・注目する指標・指紋などを、下に出す。
//  ・読み込み: 確認のあと、街を入れ替える(読み込む前の街は、メモリに控える)。読み込んだ街は、通常どおり、建築・編集・時間進行できる。
//    元のテストマップのファイルは、変わらない(通常のセーブ・F5は Saves/ へ書く)。「元の街に戻す」で、読み込む前の街へ戻せる。
//  ・保存: 「今の街を、新しいテストマップとして保存」。既存の名前は、上書きしない。保存先は、下書き(既定)か、正式。
//  ・誤操作の防止: 上書きの操作は、存在しない。削除は、idを打ち込んで確認する(正式マップは、Git管理のファイルである旨も、出す)。名前変更・正式への採用・元の街に戻す・読み込みは、確認つき。
//  ・使用中のテストマップ名と、「配置が元と同じか(建築・編集で変えたか)」を、常に、上に出す。
// 画面(ImGui)の部分だけ。保存・読み込みの規則は、TestMapStore / TestMapSession が持つ。ENABLE_EDITOR のビルドだけが呼ぶ。
class TestMapPanel
{
public:
    void SetOpenFlag(bool* open) { m_open = open; }
    // canOperate: 今、街を入れ替えてよいか(ゲームシーンで、耐久試験などが動いていない)。whyNot: だめな時の理由。
    void Draw(TestMapStore& store, TestMapSession& session, bool canOperate, const char* whyNot);

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

    std::string m_message;
    bool m_messageIsError = false;
};
