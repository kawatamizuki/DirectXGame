#pragma once
#include <unordered_map>
#include <optional>
#include <vector>
#include <DirectXMath.h>
#include "GridCoord.h"
#include "OrientedRectangleOverlap.h"

class DebugRenderer;

// 「道路・建物に揃った向きのローカル格子」のマスを管理する。
// 各マスは(基準原点origin, 向きyaw, ローカルのマス番号)で表され、ワールドのマス割りとは
// 独立している(以前はワールドの1x1チャンクにローカルマス番号を紐づけて早い者勝ちで
// 記録していたが、斜めの道路では隣り合うローカルマスが同じワールドチャンクに入り、
// 後のマスが捨てられて青マスが歯抜けになるのが原因で、独立管理に作り直した)。
//
// 優先順位は登録順(先着)。後から登録しようとしたマスが既存のマスと重なる場合は登録せず、
// 元からあったマスをそのまま残す(別々の道路の青マスが重ならないようにするため)。
// 前提: cellSizeは常に同じ値(Fieldのセルサイズ)で呼び出すこと(空間索引のキーに使うため)。
class GridOrientationRegistry
{
public:
    // 確定済みの1マス。yawだけでなくorigin(その向きを決めた基準原点)も保持する。
    // 後から同じ場所を問い合わせた時、常に「そのマスを主張した瞬間に使われた基準点」を
    // 再現できる(原点を機械的に求め直すと、斜めの道路では実際の道路の位置とズレる)。
    struct OrientedCell
    {
        float yaw = 0.0f;
        DirectX::XMFLOAT3 origin{};

        // ローカル座標系(origin, yaw)でのマス番号。マスの範囲は
        // [localCellX*cellSize, (localCellX+1)*cellSize) x [localCellZ*cellSize, (localCellZ+1)*cellSize)。
        int localCellX = 0;
        int localCellZ = 0;
    };

    // worldPosを含むマスの情報を返す。どのマスにも含まれなければstd::nullopt
    // (=呼び出し側が近くの道路を探すなどして、この場の角度を決めてよい、ということ)。
    // 複数のマスにまたがる(許容範囲内でわずかに重なっている)場合は、先に登録された方を返す。
    std::optional<OrientedCell> GetOrientation(const DirectX::XMFLOAT3& worldPos, float cellSize) const;

    // (origin, yaw)のローカルマス(localCellX, localCellZ)を登録する。既存のマスと
    // (許容範囲を超えて)重なる場合は何もせずfalseを返す(先着優先。同じマスの重複登録も
    // この規則で弾かれる)。登録できたらtrue。
    bool TryClaimCell(float cellSize, float yaw, const DirectX::XMFLOAT3& origin, int localCellX, int localCellZ);

    // rectと(許容範囲を超えて)重なる既存のマスを全て削除する。後から引かれた道路が
    // 既存の青マスの上を通った時に、道路の上にマスが残らないようにするために使う。
    void RemoveCellsOverlapping(const OrientedRect& rect, float cellSize);

    // 指定したワールドのマスに、登録済みのマスが(面積を持って)触れているか。
    // 触れていれば、元のワールド軸グリッドはそのマスでは使われない扱いにする
    // (Field::DrawGridOverlayが灰色の線を消すのに使う)。
    bool IsWorldCellCovered(const GridCoord& worldCell, float cellSize) const;

    // anchorPoint(道路区間の始点など、ローカル座標系の基準にしたいワールド座標)とyawから、
    // ComputePlacementFrame/道路側の青マス生成で実際に使うべき原点を求める。
    // anchorPointはセルの中心であることが多いが、そのまま原点にすると生成される
    // ローカルグリッドがワールドセル半分ぶんズレてしまい、yaw=0/90/180/270度の
    // (=道路の無い場所と同じはずの)ケースで既存のワールド軸グリッドと食い違う
    // 大きな後退になる。ここでローカル座標(cellSize/2, cellSize/2)ぶん引いておくことで、
    // 90度刻みのyawでは必ずワールドセルの角(=既存動作と一致)になり、斜めのyawでも
    // 一貫した規則でグリッドの位相が決まる(GridToWorldCornerがGridToWorldから
    // half-cellを引く関係の、回転版の一般化)。
    static DirectX::XMFLOAT3 ComputeChunkOrigin(const DirectX::XMFLOAT3& anchorPoint, float yaw, float cellSize);

    // マスを矩形(ワールド座標)に変換する。
    static OrientedRect MakeCellRect(const OrientedCell& cell, float cellSize);

    // 登録済みの全マスを可視化する(マスの枠+基準角度を示す矢印)。
    // 「なぜこの場所だけ道路の向きに揃わないのか」を目で確認できるようにするための
    // デバッグ専用表示(BuildControllerのチェックボックスでON/OFFする)。
    void DrawDebugOverlay(DebugRenderer& debugRenderer, float cellSize) const;

    // 登録済みのマスのうち、基準角度がワールド軸から外れているもの(斜め道路に揃った
    // マス)だけ、そのローカル格子に沿った四角形を描く。yaw≈0のマスは何もしない
    // (ワールド軸に揃っているので、Field::DrawGridOverlayの通常のグリッド線がそのまま
    // 正しい絵になるため、触らないことで「yaw=0は今まで通り」を保証する)。
    void DrawOrientedGridOverlay(DebugRenderer& debugRenderer, float cellSize, const DirectX::XMFLOAT4& color) const;

private:
    // 新しいマスが既存のマスと重なっているとみなす貫入量(cellSizeに対する割合)。
    // 別々の道路のマスが折れ曲がり部分でほんのわずかに触れ合うだけの場合まで
    // 「重なり」として弾いて、不自然な歯抜けにならないようにするための許容値。
    static constexpr float kOverlapToleranceRatio = 0.05f;

    // cell(index番目)を、それが触れるワールドのマスの索引に登録する。
    void IndexCell(int cellIndex, float cellSize);
    void RebuildIndex(float cellSize);

    // worldPosがcellの範囲内か(ローカル座標に戻してマス番号が一致するか)。
    static bool ContainsPoint(const OrientedCell& cell, const DirectX::XMFLOAT3& worldPos, float cellSize);

    // 登録順(=優先順)のマス一覧。
    std::vector<OrientedCell> m_cells;

    // ワールドのマス→そのマスに触れるm_cellsの番号一覧(候補を絞るための索引)。
    std::unordered_map<GridCoord, std::vector<int>, GridCoordHash> m_worldIndex;
};
