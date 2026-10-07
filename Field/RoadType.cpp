#include "RoadType.h"

const RoadTypeDefinition& GetRoadTypeDefinition(RoadType type)
{
    // Narrow: 安価・省スペースだが交通容量が低い
    static const RoadTypeDefinition narrowDef
    {
        "Narrow",
        0.6f,  // width
        50.0f, // capacity
        50.0f, // cost(未使用)
        1.0f   // pedestrianFriendliness(未使用)
    };

    // Normal: バランス型
    static const RoadTypeDefinition normalDef
    {
        "Normal",
        1.0f,   // width
        100.0f, // capacity
        100.0f, // cost(未使用)
        0.7f    // pedestrianFriendliness(未使用)
    };

    // Large: 交通容量が高いが建設コストが高く土地を多く使う
    static const RoadTypeDefinition largeDef
    {
        "Large",
        1.6f,   // width
        200.0f, // capacity
        220.0f, // cost(未使用)
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
