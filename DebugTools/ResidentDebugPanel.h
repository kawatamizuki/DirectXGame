#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "SimTest.h"
#include "SimTimeline.h"

class DemandSystem;
class GameObject;
struct SimEvent;
struct SimDecisionDetail;

// Editorの「住民デバッグ」パネル。選んだ住民1人について、今の値・判断の詳細(計算の内訳)・処理の履歴・異常を見せる。
//
//  ・データは DemandSystem の SimDebugRecorder(判断・状態遷移の記録)と ResidentDebugView(今の値)から読むだけ。
//    ここで乱数を引いたり、判断を計算し直したりしない(画面は、記録された値を見せるだけ)。
//  ・住民をクリック/住宅をクリックすると、その住民に切り替わる(NotifySelection)。「固定(Pin)」にすると、切り替わらない。
//  ・このパネルを開いても、ゲーム内の時間は止まらない。ゲームの住民情報パネル(2回クリック)とは別物。
//  ・ENABLE_EDITOR のビルドだけが呼ぶ(Releaseの画面には出さない)。
class ResidentDebugPanel
{
public:
    // ImGuiのウィンドウの開閉フラグ(EditorUiが持つ)。nullptrなら常に開く。
    void SetOpenFlag(bool* open) { m_open = open; }

    // ゲームの選択(クリックした住民、または、クリックした住宅の住民)が変わった時に呼ぶ。0なら選択なし。
    // 選択が変わった時だけ表示対象を切り替える(同じ選択のままなら、一覧から選んだ住民を上書きしない)。固定中は切り替えない。
    void NotifySelection(uint32_t residentId);

    // 表示対象を外す(タイトル画面への移動など)。
    void ClearTarget();

    // 表示対象の住民のid(0=なし)。
    uint32_t GetTarget() const { return m_target; }

    // パネルを描く。objectsは、施設の名前の解決などに使う。
    void Draw(DemandSystem& demand, const std::vector<GameObject>& objects);

private:
    // 表示対象を切り替える(ライブに戻し、選んでいた判断・候補・カーソルを捨てる)。
    void SetTarget(uint32_t residentId);

    void DrawHeader(DemandSystem& demand, const std::vector<GameObject>& objects);

    // ---- 履歴のナビゲーション(段階2) ----
    // この住民の時間軸(絞り込み後のイベント)と、カーソルの位置を、今のフレームの記録から作る。タブを描く前に呼ぶ。
    void BuildNavigation(DemandSystem& demand, const std::vector<GameObject>& objects);
    // シークバー・移動ボタン・絞り込み・一時停止の条件・ブックマーク。
    void DrawNavigator(DemandSystem& demand, const std::vector<GameObject>& objects);
    // 時間軸の index 番目を、見る位置(カーソル)にする。履歴モードに切り替わる。
    void SeekToIndex(int index);
    // イベント(番号)を、見る位置にする。別の住民のイベントなら、その住民へ切り替える。履歴モードに切り替わる。
    void SeekToEvent(uint32_t residentId, uint64_t eventId);
    // ライブ(最新のイベントを追う)に戻る。
    void GoLive();
    // カーソルの時点で、直近の「判断」(目的選択)を、「判断の詳細」タブで見るものにする。
    void SyncDecisionToCursor(const SimDebugRecorder& recorder);
    // カーソルのイベントのスナップショット(その時点の、記録された住民の状態)を、「現在」タブの代わりに出す(履歴モードの時)。
    void DrawSnapshotView(const SimEvent& event, DemandSystem& demand, const std::vector<GameObject>& objects);

    void DrawCurrentTab(DemandSystem& demand, const std::vector<GameObject>& objects);
    void DrawDecisionTab(DemandSystem& demand, const std::vector<GameObject>& objects);
    void DrawHistoryTab(DemandSystem& demand, const std::vector<GameObject>& objects);
    void DrawAnomalyTab(DemandSystem& demand, const std::vector<GameObject>& objects);
    // 行動テスト(段階3): 行動・強制の範囲・対象・実行・取消・結果の一覧。
    void DrawTestTab(DemandSystem& demand, const std::vector<GameObject>& objects);
    void DrawDecisionDetail(const SimDecisionDetail& detail, const std::vector<GameObject>& objects);

    bool* m_open = nullptr;
    uint32_t m_target = 0;          // 表示対象の住民(見た目のオブジェクトのid)
    uint32_t m_targetRunId = 0;     // 対象を決めた時の、記録の世界(ロードで変わる)。違えば、対象は無効
    uint32_t m_lastSelection = 0;   // 前回、ゲームから知らされた選択
    bool m_pinned = false;          // 固定(選択が変わっても、切り替えない)

    uint64_t m_selectedDetailId = 0;    // 「判断の詳細」タブで見ている判断(0=最新)
    int m_selectedCandidate = -1;       // 計算式を出す候補の添字(-1=選ばれた候補)
    bool m_decisionManual = false;      // 「判断の詳細」の判断を、自分で選んだか(falseでライブなら、いつも最新の判断)
    bool m_jumpToDetailTab = false;     // 次のフレームで、「判断の詳細」タブへ切り替える

    // 時間軸と処理履歴の絞り込み。
    bool m_showNotMet = true;           // 不成立(灰)を出すか
    bool m_onlyProblems = false;        // 注意(黄)と異常(赤)だけ出すか
    char m_search[64] = "";             // 文字での検索(イベントの種類・説明に含まれる文字。空なら、絞り込まない)

    // ---- 履歴のナビゲーション(段階2) ----
    // ライブ=最新のイベントを追う。履歴=過去のイベント(m_cursorEventId)を固定して読む(街は、巻き戻らない。記録の閲覧位置だけ)。
    bool m_live = true;
    uint64_t m_cursorEventId = 0;       // 履歴モードで見ているイベントの番号
    SimTimelineEvents m_timeline;       // この住民の、絞り込み後のイベント(古い順)。毎フレーム作り直す
    int m_cursorIndex = -1;             // m_timeline の中の、カーソルの添字(-1=時間軸が空)
    bool m_cursorClamped = false;       // カーソルが時間軸の先頭より古く(捨てられた・絞り込まれた)、先頭に寄せたか
    bool m_scrollToCursor = false;      // 次に描く時に、処理履歴の、カーソルの行へスクロールする
    bool m_syncDecisionPending = false; // 次の BuildNavigation で、「判断の詳細」を、カーソルの時点の判断に合わせる
    std::set<uint64_t> m_bookmarks;     // ブックマークしたイベントの番号(全住民ぶん。世界が変わったら消す)
    uint32_t m_bookmarksRunId = 0;      // m_bookmarks が属する、記録の世界
    uint64_t m_lastPausedSeen = 0;      // 最後に見た、一時停止のイベント番号(新しい停止に、自動で切り替えるため)

    // ---- 行動テスト(段階3)。画面の入力(指示を作る材料)と、直近の結果 ----
    int m_testAction = 0;               // SimTestAction の値
    int m_testMode = 0;                 // SimTestMode の値
    int m_testTarget = 0;               // SimTestTarget の値
    float m_testRect[4] = { -20.0f, -20.0f, 20.0f, 20.0f }; // 範囲(minX, minZ, maxX, maxZ)
    int m_testMaxCount = 50;
    int m_testForcedStopovers = 1;
    float m_testStamina = 40.0f;
    uint32_t m_testFacilityId = 0;      // 指定する施設(GameObject::id)
    SimTestSubmitResult m_testLastSubmit;
    bool m_testSubmitted = false;       // 一度でも、指示を出したか
    bool m_testConfirmOpen = false;     // 人数が多い時の、確認の画面を開く要求
};
