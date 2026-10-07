#include "BuildingType.h"

const BuildingDefinition& GetBuildingDefinition(BuildingType type)
{
    // House: CityKitSuburbanの一軒家モデル。代表住民6人でアパート1棟(人口50人)を表す。
    static const BuildingDefinition houseDef
    {
        "House",
        "CityKitSuburban/Models/OBJ format/building-type-a.obj",
        1, 1, // footprintWidthCells, footprintDepthCells
        50, // populationRepresented
        6,  // representativeAgentCount
        0   // capacity
    };

    // Office: CityKitCommercialの低〜中層ビルモデル。5人分の雇用枠を持つ。
    static const BuildingDefinition officeDef
    {
        "Office",
        "CityKitCommercial/Models/OBJ format/building-a.obj",
        1, 1,
        0,
        0,
        5 // capacity
    };

    // Shop: v0では需要ロジック未接続。モデルは暫定でcube.objのまま。
    static const BuildingDefinition shopDef
    {
        "Shop",
        "Models/cube.obj",
        1, 1,
        0,
        0,
        0
    };

    switch (type)
    {
    case BuildingType::House:  return houseDef;
    case BuildingType::Office: return officeDef;
    case BuildingType::Shop:   return shopDef;
    }

    return houseDef;
}
