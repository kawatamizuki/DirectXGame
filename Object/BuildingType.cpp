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
        0,  // capacity
        100 // cost(仮)
    };

    // Office: CityKitCommercialの低〜中層ビルモデル。5人分の雇用枠を持つ。
    static const BuildingDefinition officeDef
    {
        "Office",
        "CityKitCommercial/Models/OBJ format/building-a.obj",
        1, 1,
        0,
        0,
        5,  // capacity
        300 // cost(仮)
    };

    // Shop: v0では需要ロジック未接続。モデルは暫定で、Officeと同じキットの小さな2階建て(1階が店舗風)。
    // 店の種類(八百屋・魚屋など)を増やす時は、BuildingTypeに値を足し、ここに定義を1つ足す。
    static const BuildingDefinition shopDef
    {
        "Shop",
        "CityKitCommercial/Models/OBJ format/building-d.obj",
        1, 1,
        0,
        0,
        0,
        200 // cost(仮)
    };

    switch (type)
    {
    case BuildingType::House:  return houseDef;
    case BuildingType::Office: return officeDef;
    case BuildingType::Shop:   return shopDef;
    }

    return houseDef;
}
