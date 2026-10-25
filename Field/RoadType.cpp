#include "RoadType.h"
#include "WorldScale.h"
#include <cmath>

const RoadTypeDefinition& GetRoadTypeDefinition(RoadType type)
{
    // 1マスあたりの建設費は今は全種類20円(仮)。後半で道幅によって変える予定なので、種類ごとの表のままにしてある。

    // Narrow: 安価・省スペースだが交通容量が低い
    static const RoadTypeDefinition narrowDef
    {
        "Narrow",
        WorldScale::FromLegacy(0.6f),  // width(マス基準。マスの大きさに連動する)
        50.0f, // capacity
        20,    // costPerCell(仮)
        1.0f   // pedestrianFriendliness(未使用)
    };

    // Normal: バランス型
    static const RoadTypeDefinition normalDef
    {
        "Normal",
        WorldScale::FromLegacy(1.0f),   // width(同上)
        100.0f, // capacity
        20,     // costPerCell(仮)
        0.7f    // pedestrianFriendliness(未使用)
    };

    // Large: 交通容量が高いが建設コストが高く土地を多く使う
    static const RoadTypeDefinition largeDef
    {
        "Large",
        WorldScale::FromLegacy(1.6f),   // width(同上)
        200.0f, // capacity
        20,     // costPerCell(仮。いずれ他の種類より高くする)
        0.4f    // pedestrianFriendliness(未使用)
    };

    switch (type)
    {
    case RoadType::Narrow: return narrowDef;
    case RoadType::Normal: return normalDef;
    case RoadType::Large:  return largeDef;
    }

    return normalDef;
}

int CalculateRoadCells(float length, float cellSize)
{
    if (!(length > 0.0f) || !(cellSize > 0.0f))
    {
        return 0;
    }

    // 1e-3は「1マスの千分の1」。浮動小数の誤差で、ちょうどnマスの道がn+1マスに数えられるのを防ぐ。
    float cells = std::ceil(length / cellSize - 1e-3f);
    return (cells < 1.0f) ? 1 : static_cast<int>(cells);
}

int CalculateRoadCost(float length, float cellSize, RoadType type)
{
    return CalculateRoadCells(length, cellSize) * GetRoadTypeDefinition(type).costPerCell;
}
