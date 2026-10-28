#pragma once
#include "BuildingType.h"

// 住民が使う「施設」(コンビニ・スーパー・映画館・カフェ)の、建物の種類ごとに固定できるデータ。
// 実際の来訪者数・売上は、住民の行動の「結果」の集計(FacilityLedger)であり、ここには持たない(固定の売上バフにしない)。
// 品質・値段は、建物ごとにプレイヤーが調整するものではなく、種類ごとの値。施設の「魅力」は、品質・値段・距離などから、
// 住民が選ぶ時に計算する評価(FacilityChoice)であり、商店街に所属しても、ここの値は変わらない。
// 数値は全て仮(後で調整する。調整は、FacilityData.cpp の表だけ)。

// 需要の大カテゴリ。住民は、この2つの需要(充足度0〜100)を持つ。
enum class FacilityCategory
{
    Daily,      // 日用品(生活の需要を満たす)
    Leisure     // 娯楽(アクティビティの需要を満たす)
};

// 外出の「利用目的」。需要のゲージを増やすものではなく、コンビニとスーパーの得意分野を分けるためのタグ。
enum class UsagePurpose
{
    TopUp,      // 買い足し(短時間の買い物)
    BulkBuy,    // まとめ買い
    Movie,      // 映画
    Light,      // 軽食・お茶
    Count
};

constexpr int kUsagePurposeCount = static_cast<int>(UsagePurpose::Count);

struct FacilityDefinition
{
    BuildingType type = BuildingType::Shop;
    const char* name = "";                      // 表示名(デバッグ・ログ用)
    FacilityCategory category = FacilityCategory::Daily;

    float quality = 1.0f;                       // 提供品質(1が標準。選ばれやすさに効く)
    int pricePerVisit = 0;                      // 利用1回あたりの売上(円)

    // 1回の利用で回復する需要(0〜100の目盛り)。日用品の施設は生活、娯楽の施設はアクティビティを、主に回復する。
    float lifeRecovery = 0.0f;
    float activityRecovery = 0.0f;

    float visitDurationHours = 0.5f;            // 施設の中にいる時間(ゲーム内の時間)
    float staminaCost = 5.0f;                   // 1回の利用で減る体力(移動の分は別)

    // 利用目的ごとの適性(0.1〜2くらい。大きいほど、その目的で選ばれやすい)。買い足しはコンビニ、まとめ買いはスーパー、など。
    float purposeSuitability[kUsagePurposeCount] = { 1.0f, 1.0f, 1.0f, 1.0f };

    bool foodLike = false;                      // 食事・軽食系か(同じ外出での不自然な連続利用を抑えるための印。P1で使う)
};

// 施設の定義を返す。施設ではない種類(House・Office)は、nullptr。
const FacilityDefinition* FindFacilityDefinition(BuildingType type);

// その種類が、住民の使う施設か。
inline bool IsFacility(BuildingType type) { return FindFacilityDefinition(type) != nullptr; }

// 表示用の名前。
const char* FacilityCategoryName(FacilityCategory category);
const char* UsagePurposeName(UsagePurpose purpose);
