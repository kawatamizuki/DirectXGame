#include "FacilityData.h"

namespace
{
    // 施設の表。数値は全て仮。差別化の意図:
    //  - コンビニ: 近くにある利便性(買い足し向き。品質はふつう、値段は安い)。
    //  - スーパー: まとめ買い向き(買い足しは少し苦手)。コンビニの単純な上位互換にならないよう、目的の適性を分ける。
    //  - 映画館: 外出の主目的(遠くても行く理由になる)。回復量が大きい。
    //  - カフェ: 軽食・お茶(短時間・安い)。映画のついでにも向く。
    const FacilityDefinition kFacilities[] =
    {
        // type                  name           category                  quality price life  act  hours stamina  { TopUp, BulkBuy, Movie, Light }
        { BuildingType::Shop,        "コンビニ", FacilityCategory::Daily,   1.0f,  120, 30.0f,  0.0f, 0.4f,  4.0f, { 1.6f, 0.5f, 0.1f, 0.8f }, false },
        { BuildingType::Supermarket, "スーパー", FacilityCategory::Daily,   1.3f,  400, 70.0f,  0.0f, 1.0f,  8.0f, { 0.6f, 1.6f, 0.1f, 0.4f }, false },
        { BuildingType::Cinema,      "映画館",   FacilityCategory::Leisure, 1.5f,  600,  0.0f, 70.0f, 2.0f, 14.0f, { 0.1f, 0.1f, 2.0f, 0.3f }, false },
        { BuildingType::Cafe,        "カフェ",   FacilityCategory::Leisure, 1.1f,  250,  5.0f, 35.0f, 0.8f,  5.0f, { 0.3f, 0.2f, 0.4f, 1.8f }, true  },
    };
}

const FacilityDefinition* FindFacilityDefinition(BuildingType type)
{
    for (const FacilityDefinition& facility : kFacilities)
    {
        if (facility.type == type)
        {
            return &facility;
        }
    }
    return nullptr;
}

const char* FacilityCategoryName(FacilityCategory category)
{
    switch (category)
    {
    case FacilityCategory::Daily:   return "日用品";
    case FacilityCategory::Leisure: return "娯楽";
    }
    return "?";
}

const char* UsagePurposeName(UsagePurpose purpose)
{
    switch (purpose)
    {
    case UsagePurpose::TopUp:   return "買い足し";
    case UsagePurpose::BulkBuy: return "まとめ買い";
    case UsagePurpose::Movie:   return "映画";
    case UsagePurpose::Light:   return "軽食";
    case UsagePurpose::Count:   break;
    }
    return "?";
}
