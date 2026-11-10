#pragma once
#include <cstdint>
#include <vector>
#include "OrientedRectangleOverlap.h"
#include "BuildingType.h"
#include "FacilityData.h"

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

    // ---- 施設構成の評価(P2-B)。施設データの「用途への適性」(元の値。コンビニの買い足し 1.6 など)を使う ----
    float strongSuitability = 1.0f;         // 「その用途に強い」とみなす、適性の元の値(現在の施設は、強い用途が1つずつ: 1.6 / 1.6 / 2.0 / 1.8。次に高いのは 0.8)
    float dailyFocusedShare = 0.7f;         // 日用品寄りの割合が、これ以上なら「日用品特化」(仮。ラベルを付けるだけで、優劣は無い)
    float leisureFocusedShare = 0.3f;       // 日用品寄りの割合が、これ以下なら「娯楽特化」(その間は「混合」)

    // ---- 評判(P2-D)。週末に、目標値へ少しずつ近づける。目標値 = 1.0 + 計画の履行の項 + 実際の利用の項。数値は全て仮 ----
    float reputationMin = 0.5f;             // 評判の下限
    float reputationMax = 1.2f;             // 評判の上限
    float reputationDropPerWeek = 0.03f;    // 1週に下がる量の上限(急な大幅減点を避ける)
    float reputationRecoverPerWeek = 0.02f; // 1週に回復する量の上限
    // 計画の履行(現在の店舗数で評価する。過去の最高到達段階だけでは、達成済みとしない)
    float planPenaltyMax = 0.4f;            // 店舗数が0の時の、目標値からの減点(最低進捗までの不足の割合に比例)
    float planCompleteBonus = 0.05f;        // 今、計画規模の店舗数がそろっている時の、目標値への加点
    int reorganizeGraceWeeks = 4;           // 一度最低進捗に届いた後、店舗数が下回ってから、減点が始まるまでの猶予(再整備の猶予。初回の開発猶予 graceWeeks とは別)
    int recoveryHoldWeeks = 2;              // 再整備の猶予の計測を、取り消すために、最低進捗を保っていなければならない週数(一時的な操作で、猶予を引き延ばせない)
    // 実際の利用(先週の、エリアを目的にした外出・区画内の利用から決める。猶予中でも、働く。計画の履行とは独立)
    int useMinSamples = 5;                  // 各項を使うのに必要な、最低のサンプル数(人数・利用回数)。足りない週は、その項は0
    float useSuccessWeight = 0.04f;         // 利用成功率の項の最大(±)。補助的な指標
    float useSuccessNeutral = 0.85f;        // 成功率がこれなら、項は0(ほぼ100%に固定されても、小さな加点で頭打ち)
    float useNeedWeight = 0.06f;            // 需要への貢献の項の最大(0〜+)
    float useNeedNeutral = 0.5f;            // 利用で実際に回復した需要 ÷ 施設の標準の回復量 が、これ以下なら、項は0(需要が満たされていても、減点しない。加点も無い)
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

    // ---- 評判(P2-D。保存される。周知した区画だけが変わる) ----
    float reputation = 1.0f;                // 評判(1.0が中立)
    int belowMinimumSinceWeek = 0;          // 最低進捗を下回り始めた週(0=下回っていない)。再整備の猶予の計測の始点。一時的な操作では取り消されない(AreaReputation.h)
    int metMinimumStreakWeeks = 0;          // 最低進捗を保っている週数(連続)。recoveryHoldWeeks 以上で、上の計測を取り消す
    float lastReputationTarget = 1.0f;      // 直近の更新の目標値(表示・デバッグ用)
    float lastPlanTerm = 0.0f;              // 同じく、計画の履行の項
    float lastUseTerm = 0.0f;               // 同じく、実際の利用の項(成功率の項 + 需要への貢献の項)
    float lastReputationDelta = 0.0f;       // 直近の更新での、評判の変化量
    int lastPlanState = 0;                  // 直近の更新での、計画の履行の状態(AreaPlanState)
};

// 削除した(撤回した)周知済みの区画の記録(撤回で罰則を逃れていないかを、後から調べられるように残す)。
struct WithdrawnAreaRecord
{
    uint32_t areaId = 0;
    int publishedWeek = 0;
    int withdrawnWeek = 0;
    int plannedCapacity = 0;
    AreaMilestone achieved = AreaMilestone::None;
    float reputation = 1.0f;                // 撤回した時の評判(責任逃れを防ぐため、記録に残す。P2-D)
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

// ---- 施設構成の評価(P2-B) ----
// エリアに所属する施設の構成を、「量」と「割合」を分けて評価する。住民の行動・集客・売上には、まだ、影響しない(エリア目的の外出は P2-C)。
//  ・供給量(supply): 用途ごとの、所属施設の適性(元の値)の合計。店が増えれば増える「量」。
//  ・割合(share): 供給量を、合計で割った分布(合計1)。店の数に依らない「構成」。2店舗でも6店舗でも、同じ種類の比率なら、同じ割合。
//  ・どちらも、集客力ではない(需要との対応は、P2-C 以降)。専門型・汎用型のどちらかが有利になる評価にしない。
//  ・用途の充実度(coveredPurposes): 「強い適性」を持つ店がある用途の種類数。施設どうしの補完性(映画館+カフェで立ち寄りやすい、など)とは、別の概念で、補完性は P2-C。
enum class AreaCharacter : uint8_t
{
    None,           // 店が無い
    DailyFocused,   // 日用品特化(日用品寄りの割合が高い)
    Mixed,          // 混合
    LeisureFocused  // 娯楽特化
};

struct AreaComposition
{
    int storeCount = 0;                                 // 所属する店舗数
    float supply[kUsagePurposeCount] = {};              // 用途ごとの供給量 = 所属施設の適性(元の値)の合計
    int strongCount[kUsagePurposeCount] = {};           // 用途ごとの、適性(元の値)が strongSuitability 以上の店の数
    float share[kUsagePurposeCount] = {};               // 供給量の割合(合計1。店が無ければ全て0)。元の適性値ではない
    float dailyShare = 0.0f;                            // 日用品寄りの割合 = 買い足しの割合 + まとめ買いの割合(0〜1)
    float specialization = 0.0f;                        // 専門性(0〜1)= (一番大きい割合 − 均等な割合) ÷ (1 − 均等な割合)。1に近いほど1つの用途に偏る。1−この値が、汎用性
    int coveredPurposes = 0;                            // 用途の充実度: 強い適性を持つ店がある用途の種類数(0〜用途の数)
    int overlapStores = 0;                              // 同種の重なり: 用途ごとの「強い店の数 − 1」(0未満は0)の合計。競争が起きる材料(減点ではない)
    AreaCharacter character = AreaCharacter::None;
};

// 所属する施設の種類の一覧から、構成を評価する(施設の定義は FindFacilityDefinition で引く。施設でない種類は、無視する)。
AreaComposition ComputeAreaComposition(const std::vector<BuildingType>& memberTypes, const CommercialAreaSettings& settings);

const char* AreaCharacterName(AreaCharacter character);

// 進捗(店舗数と段階)。計画達成率と、実際の商業的魅力は、別物(魅力は P2-C 以降。ここでは出さない)。
struct AreaProgress
{
    int storeCount = 0;                     // 所属する店舗数
    int plannedCapacity = 0;                // 計画規模
    float ratio = 0.0f;                     // 計画達成率 = 店舗数 ÷ 計画規模(0〜1に収める)
    AreaMilestone current = AreaMilestone::None;    // 今の店舗数での段階
};

// 段階の店舗数のしきい値(整合させた値)。設定・計画規模が変わっても、順序が破綻しない: 最低進捗 <= 来訪範囲の拡大 <= 計画の完成。
//  ・計画規模が最低進捗より小さくても、「完成」は、最低進捗以上(最低進捗を満たさずに、完成にならない)。
//  ・来訪範囲の拡大が、最低進捗より小さい・計画規模より大きい設定でも、その間に収める。
struct AreaThresholds
{
    int minStores = 0;          // 最低進捗(= 設定。負は0)
    int expandedStores = 0;     // 来訪範囲の拡大(最低進捗〜完成の間に収めた値)
    int completeStores = 0;     // 計画の完成(= max(計画規模, 最低進捗))
};
AreaThresholds ComputeAreaThresholds(int plannedCapacity, const CommercialAreaSettings& settings);

// 設定の矛盾(計画規模 < 最低進捗、拡大のしきい値が範囲外)の説明。矛盾が無ければ nullptr。矛盾があっても、判定は ComputeAreaThresholds で整合させるので、破綻しない(見せるだけ)。
const char* DescribeAreaSettingsConflict(const CommercialAreaSettings& settings);

AreaProgress ComputeAreaProgress(int storeCount, int plannedCapacity, const CommercialAreaSettings& settings);

// 周知した週から数えた、猶予の残り週数(0以下は、猶予が過ぎている)。周知していなければ、猶予の全期間。
int GraceWeeksLeft(const CommercialArea& area, int currentWeek, const CommercialAreaSettings& settings);

// 段階の名前・履歴の名前(画面・デバッグ表示用)。
const char* AreaMilestoneName(AreaMilestone milestone);
const char* AreaHistoryKindName(AreaHistoryKind kind);

// 不変条件の点検(デバッグ・テスト用)。問題があれば、文章を返す(空なら問題なし)。
//  ・区画どうしが重なっていない  ・IDの重複が無い  ・施設の二重所属が無い
std::vector<const char*> ValidateAreas(const std::vector<CommercialArea>& areas, const std::vector<AreaMembers>& members, float epsilon);
