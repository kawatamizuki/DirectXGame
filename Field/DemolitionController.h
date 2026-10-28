#pragma once
#include <functional>
#include <string>
#include <vector>
#include <windows.h>
#include "DemolitionSelection.h"
#include "RoadCutting.h"

struct GameContext;
class Field;
class IOccupancyGrid;
class RoadSystem;
class BuildController;
class DemandSystem;

// 解体ツール(PlacementTool::Demolish)の、入力・プレビュー・確定。
// 操作: 建物・道路を、クリック、または、押したままなぞって選ぶ(赤くプレビュー)。マウスを離した時に、まとめて確定する。
//  ・建物: 1棟100円(種類にかかわらず)。
//  ・道路: 道路に沿った「長さ0.5(マス1つぶん)×道幅」のスライス単位。スライスの中にある、全ての道路の中心線が切り取られる
//    (交差点では、選んだ範囲の中にある、各道路の該当部分だけ)。費用は、切り取った中心線の長さに対する建設費の半額(操作ごとに切り上げ)。
//  ・払えない時は、全体を取り消す(一部だけ消さない)。Esc・右クリックで、選択をやめる(Gameが、Cancel()を呼ぶ)。UIの上でマウスを離した時も、取り消す。
//  ・同じ建物・同じスライスを何度なぞっても、1回だけ選ばれる。
// 建物を消す時は、GameObjectを消し、占有を道路・建物から作り直し、DemandSystemに知らせる(住民・施設の利用・売上の記録を整える)。
// 道路を切る時は、RoadSystemが区間・ノード・メッシュ・青マスを作り直し、DemandSystemが、住民の経路を整える。
class DemolitionController
{
public:
    void Initialize(GameContext* context, Field* field, IOccupancyGrid* occupancy,
                    RoadSystem* roadSystem, BuildController* buildController, DemandSystem* demandSystem);

    // 世界が変わった時(建物・道路を消した後)に呼ばれる。ゲーム側が、エディタの選択・追跡などの参照を外すのに使う。
    void SetOnWorldChanged(std::function<void()> callback) { m_onWorldChanged = std::move(callback); }
    // 結果の知らせ(「3棟を解体しました」「お金が足りません」)を、画面に出すために、ゲーム側へ渡す。
    void SetOnMessage(std::function<void(const std::string&)> callback) { m_onMessage = std::move(callback); }

    // 毎フレーム。入力の処理(選択・確定)と、費用タグの更新。
    void Update();

    // 選択・カーソルの下の建物・道路を、赤いしるしで描く(解体ツールの間だけ)。
    void Draw();

    // 選択中(押している最中)か。Esc・右クリックで、ツールをやめる前に、先に選択だけをやめるかの判断に使う。
    bool HasPending() const { return m_dragging; }

    // 選択をやめる(何も消さない・払わない)。
    void Cancel();

    // idの建物を、まとめて解体する(費用は扱わない。呼び出し側が払う)。実際に消した棟数を返す。
    // 存在しない・建物でないidは、無視する。同じidが重なっていても、1回だけ。
    int DemolishBuildings(const std::vector<uint32_t>& ids);

    // windowsの範囲の道路を、まとめて切り取る(費用は扱わない)。取り除いた長さを返す。
    // 道路・占有・住民の経路を整える。何も切れなければ0。
    float DemolishRoads(const std::vector<RoadCutWindow>& windows);

private:
    // 選んだ道路のスライス1つ。
    struct RoadSlice
    {
        size_t segmentIndex = 0;    // 選んだ時の、道路区間の添字(同じスライスの重複を防ぐ印。道路は、選んでいる間は変わらない)
        int sliceIndex = 0;         // 区間の始点から数えた、スライスの番号
        RoadCutWindow window;       // 切り取る範囲
    };

    // カーソル(ゲームビューの左上が原点のピクセル)の下にある、一番手前の建物のid(無ければ0)。
    uint32_t PickBuildingAt(const POINT& cursor) const;

    // カーソルの下の地面にある道路のスライス(道路面の上にカーソルがある時だけ)。
    bool PickRoadSliceAt(const POINT& cursor, RoadSlice& out) const;

    // カーソルの下の物(建物を優先、無ければ道路)を、選択に足す。
    void AddPickAt(const POINT& cursor);

    // 前回のカーソル位置から今回まで、画面上で細かく刻みながら、選んでいく(マウスを速く動かしても、飛び飛びにならない)。
    void AddPicksAlong(const POINT& from, const POINT& to);

    // 選択中のスライスの範囲(切り取りの計算に渡す)。
    std::vector<RoadCutWindow> SelectedWindows() const;

    // マウスを離した時の確定(払えるか確認 → 払う → 解体)。
    void FinishDrag(bool overUi);

    // 占有を、道路と建物から作り直す。
    void RebuildOccupancy();

    void Notify(const std::string& text) const;

    GameContext* m_context = nullptr;
    Field* m_field = nullptr;
    IOccupancyGrid* m_occupancy = nullptr;
    RoadSystem* m_roadSystem = nullptr;
    BuildController* m_buildController = nullptr;
    DemandSystem* m_demandSystem = nullptr;

    DemolitionSelection m_selection;    // ドラッグで選んだ建物
    std::vector<RoadSlice> m_roadSlices;    // ドラッグで選んだ道路のスライス
    bool m_dragging = false;            // 押している最中か
    POINT m_lastCursor{ 0, 0 };         // 前フレームのカーソル(ドラッグ中の補間に使う)
    uint32_t m_hoverId = 0;             // カーソルの下の建物(押していない間も、赤く見せる)
    bool m_hasHoverRoad = false;        // カーソルの下に、道路のスライスがあるか(建物が無い時)
    RoadSlice m_hoverRoad;

    std::function<void()> m_onWorldChanged;
    std::function<void(const std::string&)> m_onMessage;
};
