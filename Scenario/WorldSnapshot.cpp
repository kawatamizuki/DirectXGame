#include "WorldSnapshot.h"

namespace
{
    void ScaleVec3(DirectX::XMFLOAT3& v, float factor)
    {
        v.x *= factor;
        v.y *= factor;
        v.z *= factor;
    }
}

void ScaleWorldSnapshot(WorldSnapshot& snapshot, float factor)
{
    // 0以下・数でない値で変換すると、街が壊れる(潰れる・裏返る)ので、何もしない。
    if (!(factor > 0.0f) || factor == 1.0f)
    {
        return;
    }

    for (RoadSegment& road : snapshot.roads)
    {
        ScaleVec3(road.start, factor);
        ScaleVec3(road.end, factor);
        road.width *= factor;
    }

    for (SnapshotBuilding& building : snapshot.buildings)
    {
        ScaleVec3(building.transform.position, factor);
        ScaleVec3(building.transform.scale, factor);

        // 占有範囲と基準座標系。マスの番号・マスの数は、変えない(同じマスを占め続ける)。
        BuildingPlacement& placement = building.placement;
        ScaleVec3(placement.footprintRect.origin, factor);
        placement.footprintRect.width *= factor;
        placement.footprintRect.depth *= factor;
        ScaleVec3(placement.frameOrigin, factor);
    }

    for (SnapshotOrientationCell& cell : snapshot.orientationCells)
    {
        ScaleVec3(cell.origin, factor);
    }

    for (SnapshotProp& prop : snapshot.props)
    {
        ScaleVec3(prop.transform.position, factor);
        ScaleVec3(prop.transform.scale, factor);
    }

    snapshot.cellSize *= factor;
}

void RefineSnapshotGrid(WorldSnapshot& snapshot, int factor)
{
    if (factor <= 1)
    {
        return;
    }

    for (SnapshotBuilding& building : snapshot.buildings)
    {
        BuildingPlacement& placement = building.placement;
        placement.localCellX *= factor;
        placement.localCellZ *= factor;
        placement.localWidthCells *= factor;
        placement.localDepthCells *= factor;
    }

    // 1つの青マスは、同じ場所を覆うfactor x factor個の小さいマスになる。元の並び(優先順)の位置に、まとめて入れる。
    std::vector<SnapshotOrientationCell> refined;
    refined.reserve(snapshot.orientationCells.size() * static_cast<size_t>(factor) * static_cast<size_t>(factor));
    for (const SnapshotOrientationCell& cell : snapshot.orientationCells)
    {
        for (int dz = 0; dz < factor; ++dz)
        {
            for (int dx = 0; dx < factor; ++dx)
            {
                SnapshotOrientationCell small = cell;
                small.cellX = cell.cellX * factor + dx;
                small.cellZ = cell.cellZ * factor + dz;
                refined.push_back(small);
            }
        }
    }
    snapshot.orientationCells = std::move(refined);

    snapshot.cellSize /= static_cast<float>(factor);
}
