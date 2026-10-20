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
    int costPerCell;               // 1マスぶん(Fieldのセルの大きさ分)の道路の建設費(円)。道幅の種類ごとに変えられる
    float pedestrianFriendliness;  // 歩行者の快適さ(今回は未使用、データのみ用意)
};

// typeに対応する定義を返す。
const RoadTypeDefinition& GetRoadTypeDefinition(RoadType type);

// 長さlength(ワールド単位)の道路が占めるマス数(切り上げ)。長さ0以下は0。
// 浮動小数の誤差(ちょうど6マスの道が6.0000004になる等)で1マス増えないよう、わずかな許容を引いてから切り上げる。
int CalculateRoadCells(float length, float cellSize);

// 長さlengthの道路を引くのにかかる金額(円)= マス数 x その種類の1マスあたりの建設費。
// 直線モードもフリーハンドも同じ式で、フリーハンドは(手ぶれ補正後の)道の全長を渡す。
// 直線に引けば直線モードと同額になり、うねるほど長くなって高くなる。
int CalculateRoadCost(float length, float cellSize, RoadType type);
