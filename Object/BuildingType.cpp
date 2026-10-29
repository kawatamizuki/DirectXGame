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

    // 施設のモデル。コンビニ・スーパー・映画館は、Sketchfabの素材(CC BY 4.0)を、tools/convert_store_models.ps1 で変換した物
    // (作者・出典は、Assets/Models/Stores/*/CREDITS.txt と THIRD_PARTY_LICENSES.md)。カフェも、変換した物(出所は確認中)。
    // 差し替える時は、ここのパスだけ変える。
    // Shop(コンビニ): 小さい敷地。モデルは「Mini Market」(Elbolillo)。横長(1:0.8)なので、1x1の敷地に、横幅いっぱいで収まる。
    static const BuildingDefinition shopDef
    {
        "Convenience",
        "Assets/Models/Stores/convenience/convenience.obj",
        1 * kS, 1 * kS,
        0,
        0,
        0,
        200, // cost(仮)
        1.0f, // modelScale: 敷地にちょうど収まる大きさ
        true  // alignFrontToFootprint: 正面を敷地の前面の辺にそろえる(道路に正面を向けると、道路の縁に接する)
    };

    // Supermarket(スーパー): 横長の敷地(2x1)。モデルは「Lowpoly Supermarket」(Klieg3D)の店舗部分(縦横比 1:0.56)。
    static const BuildingDefinition supermarketDef
    {
        "Supermarket",
        "Assets/Models/Stores/supermarket/supermarket.obj",
        2 * kS, 1 * kS,
        0,
        0,
        0,
        500, // cost(仮)
        1.0f, // modelScale
        true  // alignFrontToFootprint
    };

    // Cinema(映画館): 大きい敷地(2x2)。モデルは「Cinema」(TRYFIELD)。形が不規則で、背が低い(縦横比 1:0.62、高さ 0.16)。
    static const BuildingDefinition cinemaDef
    {
        "Cinema",
        "Assets/Models/Stores/cinema/cinema.obj",
        2 * kS, 2 * kS,
        0,
        0,
        0,
        800, // cost(仮)
        1.15f, // modelScale: 映画館は、敷地(2x2)の幅いっぱいでも小さく見えるので、見た目を少し大きくする(マスの占有は変わらない)。大きくするほど、幅が敷地の左右にはみ出す(1.15で、片側約0.15マス分。幅が道路に向く向きだと、道路に乗る)
        true  // alignFrontToFootprint
    };

    // Cafe(カフェ): 小さい敷地。モデルは「Coffee Store」(FBXから変換。作者・ライセンスは確認中。Assets/Models/Stores/cafe/CREDITS.txt)。ほぼ正方形(1:0.9)。
    static const BuildingDefinition cafeDef
    {
        "Cafe",
        "Assets/Models/Stores/cafe/cafe.obj",
        1 * kS, 1 * kS,
        0,
        0,
        0,
        300, // cost(仮)
        1.0f, // modelScale
        true  // alignFrontToFootprint
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
