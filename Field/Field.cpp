#include <cmath>
#include "Field.h"
#include "Collision.h"
#include "DebugRenderer.h"
#include "GridOrientationRegistry.h"

using namespace DirectX;

void Field::Initialize(int widthCells, int depthCells, float cellSize)
{
    m_widthCells = widthCells;
    m_depthCells = depthCells;
    m_cellSize = cellSize;
}

bool Field::IsInBounds(const GridCoord& coord) const
{
    int minX = -(m_widthCells / 2);
    int minZ = -(m_depthCells / 2);
    int maxX = minX + m_widthCells - 1;
    int maxZ = minZ + m_depthCells - 1;

    return coord.x >= minX && coord.x <= maxX &&
           coord.z >= minZ && coord.z <= maxZ;
}

void Field::DrawGridOverlay(DebugRenderer& debugRenderer, const GridOrientationRegistry& orientationRegistry) const
{
    int minX = -(m_widthCells / 2);
    int minZ = -(m_depthCells / 2);

    XMFLOAT4 gridColor(0.4f, 0.4f, 0.4f, 1.0f);

    // セル単位で走査し、向き付きのマスが触れていないセルだけワールド軸の境界線を引く。
    // 向きが確定しているマスが触れているセルは(yawが0度=軸に揃ったままの場合も含めて)、
    // 建物配置の実際の判定が既にそのマス専用のローカル座標系だけを見て決まっているため、
    // 「元のワールド軸グリッド」はそのセルではもう使われていない扱いにする。
    // 見た目もこれに合わせて、元の線を描かない(yaw=0の場合はDrawOrientedGridOverlayも
    // 何も描かないため、そのセル自体の線は消えるが、隣接する未確定セル側の線が同じ境界を
    // 描くため見た目の穴にはならない)。
    for (int gx = 0; gx < m_widthCells; ++gx)
    {
        for (int gz = 0; gz < m_depthCells; ++gz)
        {
            GridCoord cell{ minX + gx, minZ + gz };

            if (orientationRegistry.IsWorldCellCovered(cell, m_cellSize))
            {
                continue;
            }

            XMFLOAT3 corner = GridToWorldCorner(cell, m_cellSize);
            float x0 = corner.x;
            float z0 = corner.z;
            float x1 = x0 + m_cellSize;
            float z1 = z0 + m_cellSize;

            debugRenderer.AddLine(XMFLOAT3(x0, 0.0f, z0), XMFLOAT3(x1, 0.0f, z0), gridColor);
            debugRenderer.AddLine(XMFLOAT3(x1, 0.0f, z0), XMFLOAT3(x1, 0.0f, z1), gridColor);
            debugRenderer.AddLine(XMFLOAT3(x1, 0.0f, z1), XMFLOAT3(x0, 0.0f, z1), gridColor);
            debugRenderer.AddLine(XMFLOAT3(x0, 0.0f, z1), XMFLOAT3(x0, 0.0f, z0), gridColor);
        }
    }

    // 道路に揃って向きが確定しているチャンクだけ、そのローカル格子の線を追加で描く。
    // 通常のグリッド線と同じ色だとどこが回転しているのか分からなかったため、
    // 水色に変えて一目で区別できるようにする。
    XMFLOAT4 orientedGridColor(0.3f, 0.7f, 1.0f, 1.0f);
    orientationRegistry.DrawOrientedGridOverlay(debugRenderer, m_cellSize, orientedGridColor);
}

float Field::GetHeightAt(float /*worldX*/, float /*worldZ*/) const
{
    // v1: 地面は平面(y=0)固定。
    // 将来ハイトマップ地形にする際はここでサンプリングする。
    return 0.0f;
}

bool Field::RaycastGround(const Ray& ray, XMFLOAT3& outHitPoint) const
{
    // v1: y=0の平面との交差判定のみ。
    // 将来ハイトマップ地形にする際はこの中身だけ差し替える。
    return IntersectRayPlane(
        ray,
        XMFLOAT3(0.0f, 0.0f, 0.0f),
        XMFLOAT3(0.0f, 1.0f, 0.0f),
        outHitPoint);
}
