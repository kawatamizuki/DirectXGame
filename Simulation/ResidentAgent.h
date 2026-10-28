#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <DirectXMath.h>
#include "WorldScale.h"
#include "FacilityData.h"

// 住民の行動状態。
enum class ResidentAgentState
{
    AtHome,         // 自宅にいる
    GoingOut,       // 施設へ移動中(外出)
    Visiting,       // 施設の中で利用中(見た目は出さない)
    ReturningHome,  // 自宅へ移動中(帰宅)

    // ---- 仕事(通勤)の概念は一旦停止中。古いセーブとの対応のために、値だけ残してある(今は使わない) ----
    Commuting,      // (停止中)職場へ移動中
    AtWork,         // (停止中)職場にいる
    CommutingHome,  // (停止中)職場から帰宅中

    // 住んでいた住宅が解体された。元の住宅の位置に立って、何もしない(外出も施設の利用もしない)。需要・体力の週の処理は続く。
    // 新しい住宅への自動の再入居は、まだ無い(今は、住居が無いままになる)。
    Homeless
};

// 住民ごとの、内部パラメータ(性格のようなもの)。生成時に、乱数の種から決まり、セーブされる。
// 全員が同じ行動にならないよう、住民ごとに値を変える(1が標準。範囲は MakeResidentTraits)。
struct ResidentTraits
{
    float staminaCapBase = 100.0f;      // 体力の上限の基準
    float needDecayScale = 1.0f;        // 需要の減りやすさ(大きいほど、週末に大きく減る)
    float qualityWeight = 1.0f;         // 品質を気にする度合い(大きいほど、品質の高い店を選びやすい)
    float priceSensitivity = 1.0f;      // 値段を気にする度合い(大きいほど、高い店を避ける)
    float distanceTolerance = 1.0f;     // 遠くても平気な度合い(大きいほど、遠くの店に行きやすい)
    float spontaneity = 1.0f;           // 自発的に外出する度合い(大きいほど、用事が無くても出かける)
};

// 住民ごとの特徴を、種から決める(同じ種なら、いつも同じ値)。baseStaminaMaxは設定の体力の基準。
ResidentTraits MakeResidentTraits(uint32_t seed, float baseStaminaMax);

// 最近使った施設の記録(同じ施設への連続利用の重みを下げるなどに使う。P1)。
struct VisitRecord
{
    uint32_t facilityObjectId = 0;   // 施設のGameObject::id
    int weekIndex = 0;               // 利用した週(1から数える通算の週)
    float hour = 0.0f;               // 利用した時刻
};

// 住民1人分の状態。
// 家・施設・自分自身の参照は、削除機能ができた時にインデックスがずれても壊れないよう、
// GameObjectの添字ではなくid(GameObject::id)で持つ(GameObjectFactory::FindByIdで解決する)。
struct ResidentAgent
{
    uint32_t objectId = 0;       // 見た目のGameObject(小さいマーカー)のid
    uint32_t homeObjectId = 0;   // 自宅のGameObjectのid。0なら、住んでいた住宅が解体された(下のhomePositionが、元の住宅の位置)
    DirectX::XMFLOAT3 homePosition{}; // 住宅が解体された時の、元の住宅の位置(homeObjectIdが0の間だけ意味を持つ。保存される)

    ResidentAgentState state = ResidentAgentState::AtHome;

    // ---- 内部パラメータ ----
    ResidentTraits traits;                  // 住民ごとの性格
    float stamina = 100.0f;                 // 今の体力
    int penaltyLevel = 0;                   // 体力の回復のペナルティの段階(週末に、需要の不足が続いていると上がる)
    float lifeNeed = 100.0f;                // 生活の需要の充足度(0〜100。80以上で充足)
    float activityNeed = 100.0f;            // アクティビティの需要の充足度(0〜100。80以上で充足)

    // 今、需要の不足・充足が「続いている」時間(ゲーム内の時間。状態が変わったら0から数え直す)。週末のペナルティの判定に使い、判定後に0へ戻す。
    float unmetLifeHours = 0.0f;
    float unmetActivityHours = 0.0f;
    float satisfiedHours = 0.0f;

    std::vector<VisitRecord> history;       // 最近使った施設(新しい順に最大kMaxHistory件)
    static constexpr size_t kMaxHistory = 8;

    // ---- 今の外出 ----
    uint32_t targetFacilityId = 0;          // 向かっている(利用中の)施設のid(0=なし)
    UsagePurpose purpose = UsagePurpose::TopUp; // 今の外出の利用目的
    float visitHoursLeft = 0.0f;            // 施設の中に、あと何ゲーム時間いるか
    int outingStopCount = 0;                // 今の外出での立ち寄り回数(P1で使う)
    bool usedFoodLikeFacilityThisOuting = false; // 今の外出で、食事系の施設を使ったか(P1で使う)
    std::string lastDecisionNote;           // 直近の施設選びの要約(Inspector・デバッグ用。保存しない)

    // ---- 移動 ----
    // 移動経路。外出の時は自宅位置→…→施設位置、帰宅の時は逆向きに並べ替えて使う
    // (どちら向きに並んでいるかはwaypointsHomeToTargetが示す)。
    std::vector<DirectX::XMFLOAT3> waypoints;
    bool waypointsHomeToTarget = true;        // trueなら自宅→施設の向き、falseなら施設→自宅の向き
    std::vector<size_t> segmentIndices;       // 経路が通る道路区間(混雑表示用)
    size_t currentWaypointIndex = 0;          // 現在向かっているwaypointの添字

    float speed = WorldScale::FromLegacy(2.0f); // 移動速度(units/sec)。1秒に2マス進む(マスの大きさに連動)

    // ---- 停止中の仕事の概念(今は使わない。消さずに残してある) ----
    bool hasOffice = false;
    uint32_t officeObjectId = 0;
    float scheduleOffsetHours = 0.0f;
};
