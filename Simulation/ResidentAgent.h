#pragma once
#include <vector>
#include <cstdint>
#include <DirectXMath.h>

// 代表住民の行動状態。
enum class ResidentAgentState
{
    AtHome,         // 自宅にいる(まだ職が決まっていない、または出勤時刻まで待機中)
    Commuting,      // 職場へ移動中(出勤)
    AtWork,         // 職場に到着済み(退勤時刻まで待機中)
    CommutingHome   // 自宅へ移動中(帰宅)
};

// 代表住民1人分の状態。
// 家・職場・自分自身の参照は、削除機能ができた時にインデックスがずれても壊れないよう、
// GameObjectの添字ではなくid(GameObject::id)で持つ(GameObjectFactory::FindByIdで解決する)。
struct ResidentAgent
{
    uint32_t objectId = 0;       // 見た目のGameObject(小さいマーカー)のid
    uint32_t homeObjectId = 0;   // 自宅のGameObjectのid

    bool hasOffice = false;
    uint32_t officeObjectId = 0; // 割り当てられた職場のGameObjectのid(hasOffice==trueの時だけ有効)

    ResidentAgentState state = ResidentAgentState::AtHome;

    // 移動経路。出勤の時は自宅位置→…→職場位置、帰宅の時は逆向きに並べ替えて使う
    // (どちら向きに並んでいるかはwaypointsHomeToOfficeが示す)。
    std::vector<DirectX::XMFLOAT3> waypoints;
    bool waypointsHomeToOffice = true;        // trueなら自宅→職場の向き、falseなら職場→自宅の向き
    std::vector<size_t> segmentIndices;       // 経路が通る道路区間(debugLoad再計算用)
    size_t currentWaypointIndex = 0;          // 現在向かっているwaypointの添字

    // 1日の予定(出勤・退勤の時刻)に加える、この住民固有の時刻のずれ(時間単位)。
    // 全員が同じ瞬間に出発しないよう、生成時にResidentScheduleが住民idから決める。
    float scheduleOffsetHours = 0.0f;

    float speed = 2.0f; // 移動速度(units/sec)
};
