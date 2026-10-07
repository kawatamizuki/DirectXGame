#pragma once
#include <vector>
#include "GridCoord.h"
#include "OrientedRectangleOverlap.h"

// 建物配置(BuildController)と道路配置(RoadSystem)が共有する
// 「このマスは空いているか?」の判定インターフェース。
//
// v1実装(CellOccupancyGrid)はセル単位の簡易判定だが、
// 呼び出し側は常にこのインターフェース越しにしか占有判定を呼ばないため、
// 将来より精密な判定(ポリゴン交差など)に差し替えたくなっても、
// Game側で保持する具象クラスを1つ差し替えるだけで済む。
class IOccupancyGrid
{
public:
    virtual ~IOccupancyGrid() = default;

    virtual bool IsFree(const GridCoord& coord) const = 0;
    virtual void SetOccupied(const GridCoord& coord, bool occupied) = 0;

    // 複数セルをまとめて確認/設定する(道路が通過するセル一覧など)。
    // デフォルト実装は単セル版をループするだけ。
    virtual bool IsFreeRange(const std::vector<GridCoord>& coords) const;
    virtual void SetOccupiedRange(const std::vector<GridCoord>& coords, bool occupied);

    // 向き付き矩形(道路区間・建物のfootprint)を、ワールドのマス単位ではなく
    // 実際の形のまま扱う判定。斜めに置いた建物と斜めの道路は、ワールド軸のマス単位で
    // 見ると辺がマスをまたぐため、ぴったり隣接していても同じマスに触れて衝突扱いに
    // なってしまう。矩形どうしの厳密な重なり(辺が接するだけならOK)で判定することで、
    // 軸に揃った場合も斜めの場合も同じ基準で「隣接配置できる/できない」が決まる。
    // 占有者の種類(ビット)。道路を引く時は「建物には重ならないが、道路は(接続のために)
    // 重なってよい」ため、種類を指定して判定できるようにしている。
    enum OccupantKind : unsigned
    {
        kOccupantBuilding = 1,
        kOccupantRoad = 2,
        kOccupantAll = kOccupantBuilding | kOccupantRoad,
    };

    // kindMaskに含まれる種類の占有者とだけ照合して、rectが空いているかを返す。
    virtual bool IsRectFree(const OrientedRect& rect, unsigned kindMask = kOccupantAll) const = 0;
    virtual void OccupyRect(const OrientedRect& rect, OccupantKind kind) = 0;

    // 現在占有されているセルの数(Profilerのメモリ内訳表示に使う)。
    virtual size_t GetOccupiedCellCount() const = 0;
};
