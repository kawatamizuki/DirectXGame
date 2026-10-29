#pragma once
#include <cstdint>
#include <string>
#include <vector>

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
    void DrawHeader(DemandSystem& demand);
    void DrawCurrentTab(DemandSystem& demand, const std::vector<GameObject>& objects);
    void DrawDecisionTab(DemandSystem& demand, const std::vector<GameObject>& objects);
    void DrawHistoryTab(DemandSystem& demand, const std::vector<GameObject>& objects);
    void DrawAnomalyTab(DemandSystem& demand, const std::vector<GameObject>& objects);
    void DrawDecisionDetail(const SimDecisionDetail& detail, const std::vector<GameObject>& objects);

    bool* m_open = nullptr;
    uint32_t m_target = 0;          // 表示対象の住民(見た目のオブジェクトのid)
    uint32_t m_targetRunId = 0;     // 対象を決めた時の、記録の世界(ロードで変わる)。違えば、対象は無効
    uint32_t m_lastSelection = 0;   // 前回、ゲームから知らされた選択
    bool m_pinned = false;          // 固定(選択が変わっても、切り替えない)

    uint64_t m_selectedDetailId = 0;    // 「判断の詳細」タブで見ている判断(0=最新)
    int m_selectedCandidate = -1;       // 計算式を出す候補の添字(-1=選ばれた候補)
    bool m_jumpToDetailTab = false;     // 次のフレームで、「判断の詳細」タブへ切り替える

    // 処理履歴の絞り込み。
    bool m_showNotMet = true;           // 不成立(灰)を出すか
    bool m_onlyProblems = false;        // 注意(黄)と異常(赤)だけ出すか
};
