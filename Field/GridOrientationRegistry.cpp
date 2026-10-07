#include <cmath>
#include "GridOrientationRegistry.h"
#include "DebugRenderer.h"

using namespace DirectX;

OrientedRect GridOrientationRegistry::MakeCellRect(const OrientedCell& cell, float cellSize)
{
    // ローカル(localCellX*cellSize, localCellZ*cellSize)を、yawで回転してoriginを足した点が
    // マスの最小角になる(BuildController::LocalToWorldと同じ回転規約)。
    float c = cosf(cell.yaw);
    float s = sinf(cell.yaw);
    float lx = static_cast<float>(cell.localCellX) * cellSize;
    float lz = static_cast<float>(cell.localCellZ) * cellSize;

    OrientedRect rect;
    rect.origin = XMFLOAT3(
        cell.origin.x + lx * c + lz * s,
        cell.origin.y,
        cell.origin.z - lx * s + lz * c);
    rect.yaw = cell.yaw;
    rect.width = cellSize;
    rect.depth = cellSize;
    return rect;
}

bool GridOrientationRegistry::ContainsPoint(const OrientedCell& cell, const XMFLOAT3& worldPos, float cellSize)
{
    // ワールド→ローカルは、回転の逆(-yaw)を掛ける。
    float c = cosf(cell.yaw);
    float s = sinf(cell.yaw);
    float dx = worldPos.x - cell.origin.x;
    float dz = worldPos.z - cell.origin.z;
    float localX = dx * c - dz * s;
    float localZ = dx * s + dz * c;

    return static_cast<int>(std::floor(localX / cellSize)) == cell.localCellX &&
           static_cast<int>(std::floor(localZ / cellSize)) == cell.localCellZ;
}

void GridOrientationRegistry::IndexCell(int cellIndex, float cellSize)
{
    OrientedRect rect = MakeCellRect(m_cells[cellIndex], cellSize);
    for (const GridCoord& worldCell : SampleOrientedRectangleCells(rect, cellSize))
    {
        m_worldIndex[worldCell].push_back(cellIndex);
    }
}

void GridOrientationRegistry::RebuildIndex(float cellSize)
{
    m_worldIndex.clear();
    for (int i = 0; i < static_cast<int>(m_cells.size()); ++i)
    {
        IndexCell(i, cellSize);
    }
}

std::optional<GridOrientationRegistry::OrientedCell> GridOrientationRegistry::GetOrientation(const XMFLOAT3& worldPos, float cellSize) const
{
    auto it = m_worldIndex.find(WorldToGrid(worldPos, cellSize));
    if (it == m_worldIndex.end())
    {
        return std::nullopt;
    }

    // 索引の番号は登録順に昇順で積まれているので、最初に見つかったものが最古(=優先)。
    for (int cellIndex : it->second)
    {
        if (ContainsPoint(m_cells[cellIndex], worldPos, cellSize))
        {
            return m_cells[cellIndex];
        }
    }

    return std::nullopt;
}

bool GridOrientationRegistry::TryClaimCell(float cellSize, float yaw, const XMFLOAT3& origin, int localCellX, int localCellZ)
{
    OrientedCell candidate;
    candidate.yaw = yaw;
    candidate.origin = origin;
    candidate.localCellX = localCellX;
    candidate.localCellZ = localCellZ;

    OrientedRect candidateRect = MakeCellRect(candidate, cellSize);
    float tolerance = cellSize * kOverlapToleranceRatio;

    // 新しいマスが触れるワールドのマスに登録済みのマスだけを調べれば、重なる相手は必ず見つかる。
    for (const GridCoord& worldCell : SampleOrientedRectangleCells(candidateRect, cellSize))
    {
        auto it = m_worldIndex.find(worldCell);
        if (it == m_worldIndex.end())
        {
            continue;
        }

        for (int cellIndex : it->second)
        {
            if (OrientedRectsOverlap(candidateRect, MakeCellRect(m_cells[cellIndex], cellSize), tolerance))
            {
                return false; // 先着優先: 既にあるマスを残す
            }
        }
    }

    m_cells.push_back(candidate);
    IndexCell(static_cast<int>(m_cells.size()) - 1, cellSize);
    return true;
}

void GridOrientationRegistry::RemoveCellsOverlapping(const OrientedRect& rect, float cellSize)
{
    float tolerance = cellSize * kOverlapToleranceRatio;

    size_t before = m_cells.size();
    for (size_t i = m_cells.size(); i-- > 0;)
    {
        if (OrientedRectsOverlap(rect, MakeCellRect(m_cells[i], cellSize), tolerance))
        {
            m_cells.erase(m_cells.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    // 番号がずれるので索引は作り直す(道路を確定した時だけの稀な処理)。
    if (m_cells.size() != before)
    {
        RebuildIndex(cellSize);
    }
}

bool GridOrientationRegistry::IsWorldCellCovered(const GridCoord& worldCell, float cellSize) const
{
    (void)cellSize; // 索引が既に「面積を持って触れるワールドのマス」でキー化されている
    return m_worldIndex.find(worldCell) != m_worldIndex.end();
}

XMFLOAT3 GridOrientationRegistry::ComputeChunkOrigin(const XMFLOAT3& anchorPoint, float yaw, float cellSize)
{
    // anchorPoint(セル中心であることが多い)からローカル座標で半セルぶん引いた点を
    // 原点にする。yawが90度刻みならhalfCellLocalの2成分が符号だけ入れ替わる形に
    // なるため、結果は必ず従来通りワールドセルの角に一致する(回帰なし)。
    // 斜めのyawでは、その道路の実際の位置に応じた原点になる。
    XMVECTOR halfCellLocal = XMVectorSet(cellSize * 0.5f, 0.0f, cellSize * 0.5f, 0.0f);
    XMVECTOR rotationQuat = XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);
    XMVECTOR offset = XMVector3Rotate(halfCellLocal, rotationQuat);
    XMVECTOR originVec = XMLoadFloat3(&anchorPoint) - offset;

    XMFLOAT3 origin;
    XMStoreFloat3(&origin, originVec);
    return origin;
}

namespace
{
    // マス(矩形)の4隅を反時計回りに返す。
    void GetCellCorners(const OrientedRect& rect, XMFLOAT3 outCorners[4])
    {
        float c = cosf(rect.yaw);
        float s = sinf(rect.yaw);
        XMFLOAT3 axisX(c, 0.0f, -s);
        XMFLOAT3 axisZ(s, 0.0f, c);

        outCorners[0] = rect.origin;
        outCorners[1] = XMFLOAT3(rect.origin.x + axisX.x * rect.width, rect.origin.y, rect.origin.z + axisX.z * rect.width);
        outCorners[2] = XMFLOAT3(outCorners[1].x + axisZ.x * rect.depth, rect.origin.y, outCorners[1].z + axisZ.z * rect.depth);
        outCorners[3] = XMFLOAT3(rect.origin.x + axisZ.x * rect.depth, rect.origin.y, rect.origin.z + axisZ.z * rect.depth);
    }
}

void GridOrientationRegistry::DrawDebugOverlay(DebugRenderer& debugRenderer, float cellSize) const
{
    // 道路メッシュ(RoadMeshBuilder::kRoadHeightOffset=0.02)より少し上に浮かせて、
    // 地面・道路とのZファイティングを避ける。
    constexpr float kDebugHeightOffset = 0.05f;
    const XMFLOAT4 boundaryColor(1.0f, 0.6f, 0.0f, 1.0f); // オレンジ: マスの境界
    const XMFLOAT4 arrowColor(0.0f, 1.0f, 1.0f, 1.0f);    // シアン: 基準角度(ローカル+Z方向)

    for (const OrientedCell& cell : m_cells)
    {
        OrientedRect rect = MakeCellRect(cell, cellSize);
        XMFLOAT3 corners[4];
        GetCellCorners(rect, corners);
        for (int i = 0; i < 4; ++i)
        {
            corners[i].y = kDebugHeightOffset;
        }

        for (int i = 0; i < 4; ++i)
        {
            debugRenderer.AddLine(corners[i], corners[(i + 1) % 4], boundaryColor);
        }

        // 基準角度を、建物のyaw規約(ローカル+Z→ワールド(sin(yaw),0,cos(yaw)))と
        // 同じ向きの矢印(線分)で示す。
        XMFLOAT3 center(
            (corners[0].x + corners[2].x) * 0.5f,
            kDebugHeightOffset,
            (corners[0].z + corners[2].z) * 0.5f);
        float arrowLength = cellSize * 0.4f;
        XMFLOAT3 tip(
            center.x + sinf(cell.yaw) * arrowLength,
            kDebugHeightOffset,
            center.z + cosf(cell.yaw) * arrowLength);

        debugRenderer.AddLine(center, tip, arrowColor);
    }
}

void GridOrientationRegistry::DrawOrientedGridOverlay(DebugRenderer& debugRenderer, float cellSize, const XMFLOAT4& color) const
{
    // yaw≈0(ワールド軸に揃ったまま)のマスは何も描かない。Field::DrawGridOverlayの
    // 既存のワールド軸グリッド線が既に正しい絵になっているので、ここで重ねて描く必要が
    // ない(そのままにしておくことで「yaw=0は今まで通り」を保証する)。
    //
    // 各マスは登録時に確定したローカルのマス番号から直接4隅を求めるため、同じ(origin,yaw)を
    // 共有するマス同士は必ず隙間なく等間隔に並ぶ(ワールド座標からの近似的な逆算は一切しない)。
    constexpr float kYawEpsilon = 0.0001f;

    for (const OrientedCell& cell : m_cells)
    {
        if (fabsf(cell.yaw) < kYawEpsilon)
        {
            continue;
        }

        XMFLOAT3 corners[4];
        GetCellCorners(MakeCellRect(cell, cellSize), corners);

        for (int i = 0; i < 4; ++i)
        {
            debugRenderer.AddLine(corners[i], corners[(i + 1) % 4], color);
        }
    }
}
