#include "BuildingType.h"
#include "WorldScale.h"

namespace
{
    // 敷地の大きさ(下の表)は「最初の1マスを単位」に書いてある。マスを細かく分割した分だけ、マス数に直す(建物の大きさは変わらない)。
    constexpr int kS = WorldScale::kSubdivision;
}

const BuildingDefinition& GetBuildingDefinition(BuildingType type)
{
    // House: CityKitSuburbanの一軒家モデル。今は、1軒に住民1人(populationRepresented=1、representativeAgentCount=1)。
    // (以前は、代表住民6人でアパート1棟(人口50人)を表していた。住民ごとの内部パラメータを変えて試すため、1軒1人にした)
    static const BuildingDefinition houseDef
    {
        "House",
        "CityKitSuburban/Models/OBJ format/building-type-a.obj",
        1 * kS, 1 * kS, // footprintWidthCells, footprintDepthCells
        1,  // populationRepresented
        1,  // representativeAgentCount
        0,  // capacity
        100 // cost(仮)
    };

    // Office: CityKitCommercialの低〜中層ビルモデル。仕事の概念は一旦停止中なので、今は建てられるだけ。
    static const BuildingDefinition officeDef
    {
        "Office",
        "CityKitCommercial/Models/OBJ format/building-a.obj",
        1 * kS, 1 * kS,
        0,
        0,
        5,  // capacity(停止中)
        300 // cost(仮)
    };

    // 以下の施設のモデルは、全て暫定(CityKitCommercialの別々の建物)。適切なモデルに差し替える時は、ここのパスだけ変える。
    // Shop(コンビニ): 小さい敷地。モデルは、Officeと同じキットの小さな2階建て(1階が店舗風)。
    static const BuildingDefinition shopDef
    {
        "Convenience",
        "CityKitCommercial/Models/OBJ format/building-d.obj",
        1 * kS, 1 * kS,
        0,
        0,
        0,
        200 // cost(仮)
    };

    // Supermarket(スーパー): 横長の敷地(2x1)。
    static const BuildingDefinition supermarketDef
    {
        "Supermarket",
        "CityKitCommercial/Models/OBJ format/building-e.obj",
        2 * kS, 1 * kS,
        0,
        0,
        0,
        500 // cost(仮)
    };

    // Cinema(映画館): 大きい敷地(2x2)。
    static const BuildingDefinition cinemaDef
    {
        "Cinema",
        "CityKitCommercial/Models/OBJ format/building-i.obj",
        2 * kS, 2 * kS,
        0,
        0,
        0,
        800 // cost(仮)
    };

    // Cafe(カフェ): 小さい敷地。
    static const BuildingDefinition cafeDef
    {
        "Cafe",
        "CityKitCommercial/Models/OBJ format/building-j.obj",
        1 * kS, 1 * kS,
        0,
        0,
        0,
        300 // cost(仮)
    };

    switch (type)
    {
    case BuildingType::House:       return houseDef;
    case BuildingType::Office:      return officeDef;
    case BuildingType::Shop:        return shopDef;
    case BuildingType::Supermarket: return supermarketDef;
    case BuildingType::Cinema:      return cinemaDef;
    case BuildingType::Cafe:        return cafeDef;
    }

    return houseDef;
}
