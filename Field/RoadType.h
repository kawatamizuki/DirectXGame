#pragma once

// 道路の種類。
enum class RoadType
{
    Narrow, // 細い道路: 安価・省スペースだが交通容量が低い
    Normal, // 普通の道路
    Large   // 大きい道路: 交通容量が高いが建設コストが高く土地を多く使う
};

// 1つの道路種別が持つ性能値をまとめたデータ。
struct RoadTypeDefinition
{
    const char* name;              // 表示名
    float width;                   // 道幅(見た目・占有判定に使用)
    float capacity;                // 交通容量の目安(将来、渋滞判定の基準に使う想定)
    float cost;                    // 建設コスト(経済システムが無いため今回は未使用、データのみ用意)
    float pedestrianFriendliness;  // 歩行者の快適さ(今回は未使用、データのみ用意)
};

// typeに対応する定義を返す。
const RoadTypeDefinition& GetRoadTypeDefinition(RoadType type);
