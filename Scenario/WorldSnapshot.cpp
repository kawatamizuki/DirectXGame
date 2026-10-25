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
