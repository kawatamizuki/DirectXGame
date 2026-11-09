#pragma once
#include <cstdint>
#include <vector>
#include "OrientedRectangleOverlap.h"
#include "BuildingType.h"

// ---------------------------------------------------------------------------
// 商業エリア(商店街)の区画(P2-A)。
//   プレイヤーが地面に仮置きする、ワールドの軸に沿った正方形の区画。置いただけでは、住民の行動・売上・評判・期限に何も影響しない。
//   周知(公開)すると、周知した週から猶予期間を数え始める(猶予後の評判の変化は P2-D。P2-A では、数えて見せるだけ)。
//   施設の所属は、保存せず、毎回、位置から求める(建物の建築・解体・区画の移動に、自動で追従する。IDの付け替えが要らない)。
//   このファイルは、画面・ゲームのオブジェクトに依存しない純粋な規則(単体テストできる)。
// ---------------------------------------------------------------------------

// ワールドの軸に沿った矩形(XZ平面)。
struct AreaRect
{
    float minX = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxZ = 0.0f;

    float Width() const { return maxX - minX; }
    float Depth() const { return maxZ - minZ; }
    float Area() const { return (maxX > minX && maxZ > minZ) ? (maxX - minX) * (maxZ - minZ) : 0.0f; }
    float CenterX() const { return (minX + maxX) * 0.5f; }
    float CenterZ() const { return (minZ + maxZ) * 0.5f; }
    bool Contains(float x, float z) const { return x >= minX && x <= maxX && z >= minZ && z <= maxZ; }
};

// 区画の調整用の設定。数値は、全て仮(試作用)。Editorの「商業エリア」パネルで変えられる(保存はしない)。
struct CommercialAreaSettings
{
    float sideLegacyCells = 8.0f;           // 区画の一辺(マス基準。8×8マス)。大きさの変更・自由な形は、将来
    float membershipMinOverlap = 0.5f;      // 施設が所属する条件: 建物の敷地面積のうち、区画の中にある割合がこれ以上(50%。仮)
    int plannedCapacity = 8;                // 計画規模(店舗の定員。仮)
    int minProgressStores = 4;              // 最低進捗を達成する店舗数(仮)
    int expandedRangeStores = 6;            // 来訪範囲が広がる店舗数(仮。P2-C で使う)
    // 計画の完成は、計画規模(plannedCapacity)の店舗数。
    int graceWeeks = 8;                     // 周知してから、開発が間に合っていなくても許される週数(仮。猶予後の評判の変化は P2-D)
    float placementEpsilon = 1e-3f;         // 区画どうしの重なりの判定の許容(辺が接するだけは、重なっていないとする)
};

// 周知後の履歴の種類。周知後の移設・撤回・規模変更が、進捗のリセットや罰則逃れにならないように、記録を残す(実際の減点は P2-D)。
enum class AreaHistoryKind : uint8_t
{
    Published,      // 周知した
    Moved,          // 周知した後に、動かした
    Resized,        // 周知した後に、規模(計画規模)を変えた(P2-A では、まだ起きない。将来)
    Withdrawn       // 周知した後に、撤回(削除)した(削除した区画の記録は、管理側の撤回の一覧に残る)
};

struct AreaHistoryEntry
{
    int week = 0;                           // 通算の週(GameClock::GetWeekCount)
    AreaHistoryKind kind = AreaHistoryKind::Published;
};

// 達成状態(周知した後に、一度でも届いた一番高い段階)。減らない(店舗が減っても、一度届いた事実は残る)。
enum class AreaMilestone : uint8_t
{
    None,           // 最低進捗に届いていない
    Minimum,        // 最低進捗(minProgressStores 店舗以上)
    Expanded,       // 来訪範囲の拡大(expandedRangeStores 店舗以上)
    Complete        // 計画上の完成(計画規模の店舗数)
};

// 1つの区画。
struct CommercialArea
{
    uint32_t id = 0;                        // 識別ID(通し番号。保存される。0は未割当)
    AreaRect rect;                          // 区画の矩形(ワールド)
    bool published = false;                 // 周知済みか(false=非公開の仮置き)
    int publishedWeek = 0;                  // 周知した週(publishedの時だけ意味を持つ)
    int plannedCapacity = 8;                // 周知した時の計画規模(周知した後に設定を変えても、この値で数える。契約した計画上の規模)
    AreaMilestone achieved = AreaMilestone::None; // 達成状態(周知した後に、一度でも届いた段階)
    std::vector<AreaHistoryEntry> history;  // 周知後の履歴
};

// 削除した(撤回した)周知済みの区画の記録(撤回で罰則を逃れていないかを、後から調べられるように残す)。
struct WithdrawnAreaRecord
{
    uint32_t areaId = 0;
    int publishedWeek = 0;
    int withdrawnWeek = 0;
    int plannedCapacity = 0;
    AreaMilestone achieved = AreaMilestone::None;
};

// ---- 所属の判定の入力(ゲームのオブジェクトに依存しない) ----
// 区画の所属を考える対象の施設1軒(商業施設の種類の建物だけ)。
struct AreaFacility
{
    uint32_t objectId = 0;
    BuildingType type = BuildingType::Shop;
    OrientedRect footprint;                 // 建物の敷地(BuildingPlacement::footprintRect)
};

// 区画1つの、所属の結果。
struct AreaMembers
{
    uint32_t areaId = 0;
    std::vector<uint32_t> facilityIds;      // 所属する施設(objectId。小さい順)
    std::vector<float> overlapRatios;       // 同じ並びの、敷地面積に対する区画内の割合(0〜1)
};

// ---- 純粋な計算 ----
// 区画の矩形を、center を中心に、一辺 side の正方形で作る。格子(cellSize)の線に、隅を合わせる(cellSize<=0なら、そのまま)。
AreaRect MakeAreaRect(float centerX, float centerZ, float side, float cellSize);

// 向き付きの矩形(建物の敷地)と、軸に沿った矩形(区画)が重なる面積(多角形の切り取りで、正確に求める)。
float OverlapArea(const OrientedRect& footprint, const AreaRect& rect);

// 敷地の面積(向きに依らない: 幅 × 奥行き)。
float FootprintArea(const OrientedRect& footprint);

// 敷地面積のうち、区画の中にある割合(0〜1。敷地面積が0なら0)。
float OverlapRatio(const OrientedRect& footprint, const AreaRect& rect);

// 区画どうしが、面積を持って重なっているか(辺が接するだけ・epsilon以内は、重なっていない)。
bool AreaRectsOverlap(const AreaRect& a, const AreaRect& b, float epsilon);

// rect を置けるか(他の区画と重ならない。ignoreId の区画は、無いものとして扱う=動かす時の自分自身)。
bool CanPlaceAreaRect(const AreaRect& rect, const std::vector<CommercialArea>& areas, uint32_t ignoreId, float epsilon);

// 商業施設として、区画の所属の対象になる種類か(コンビニ・スーパー・映画館・カフェ。住宅・オフィスは対象外)。
bool IsAreaFacilityType(BuildingType type);

// 全ての区画の所属を求める。ルール:
//  ・敷地面積のうち、区画の中にある割合が membershipMinOverlap 以上なら所属(中心の位置では判定しない)。
//  ・施設は、1つの区画にだけ所属する(二重所属しない)。2つ以上の区画の条件を満たす時は、割合が大きい区画。同じ割合なら、IDが小さい区画。
//  ・戻り値は、areas と同じ並び。
std::vector<AreaMembers> ComputeAreaMembers(const std::vector<CommercialArea>& areas, const std::vector<AreaFacility>& facilities,
                                            const CommercialAreaSettings& settings);

// 進捗(店舗数と段階)。計画達成率と、実際の商業的魅力は、別物(魅力は P2-C 以降。ここでは出さない)。
struct AreaProgress
{
    int storeCount = 0;                     // 所属する店舗数
    int plannedCapacity = 0;                // 計画規模
    float ratio = 0.0f;                     // 計画達成率 = 店舗数 ÷ 計画規模(0〜1に収める)
    AreaMilestone current = AreaMilestone::None;    // 今の店舗数での段階
};

AreaProgress ComputeAreaProgress(int storeCount, int plannedCapacity, const CommercialAreaSettings& settings);

// 周知した週から数えた、猶予の残り週数(0以下は、猶予が過ぎている)。周知していなければ、猶予の全期間。
int GraceWeeksLeft(const CommercialArea& area, int currentWeek, const CommercialAreaSettings& settings);

// 段階の名前・履歴の名前(画面・デバッグ表示用)。
const char* AreaMilestoneName(AreaMilestone milestone);
const char* AreaHistoryKindName(AreaHistoryKind kind);

// 不変条件の点検(デバッグ・テスト用)。問題があれば、文章を返す(空なら問題なし)。
//  ・区画どうしが重なっていない  ・IDの重複が無い  ・施設の二重所属が無い
std::vector<const char*> ValidateAreas(const std::vector<CommercialArea>& areas, const std::vector<AreaMembers>& members, float epsilon);
