#pragma once
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "IOccupancyGrid.h"

// IOccupancyGridのv1実装。
// 占有セルをunordered_setで管理するだけの単純な方式
// (サイズ非依存なので、将来フィールドを拡張しても作り直し不要)。
// それに加えて、向き付き矩形(道路区間・建物)の厳密な判定用に、占有した矩形そのものも
// 保持する(斜めの形を、ワールドのマス単位に丸めずに判定するため)。
class CellOccupancyGrid : public IOccupancyGrid
{
public:
    bool IsFree(const GridCoord& coord) const override;
    void SetOccupied(const GridCoord& coord, bool occupied) override;
    bool IsRectFree(const OrientedRect& rect, unsigned kindMask = kOccupantAll) const override;
    void OccupyRect(const OrientedRect& rect, OccupantKind kind) override;
    void Clear() override;
    size_t GetOccupiedCellCount() const override { return m_occupiedCells.size(); }

private:
    // 矩形の空間ハッシュの分割幅。候補を絞るための索引にだけ使い、判定結果には影響しない
    // (Fieldのセルサイズと一致している必要はない)。
    static constexpr float kIndexCellSize = 1.0f;

    std::unordered_set<GridCoord, GridCoordHash> m_occupiedCells;

    // 占有済みの矩形(追加順)と、その種類(m_rectsと同じ添字)。
    std::vector<OrientedRect> m_rects;
    std::vector<OccupantKind> m_rectKinds;

    // ワールドセル→そのセルに触れる矩形の番号。IsRectFreeで調べる矩形が触れるセルに
    // 登録された矩形だけをSAT判定すれば済むようにするための空間ハッシュ。
    std::unordered_map<GridCoord, std::vector<int>, GridCoordHash> m_rectIndex;
};
