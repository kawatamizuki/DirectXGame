#pragma once
#include <cstdint>
#include <vector>
#include <DirectXMath.h>
#include "ResidentAgent.h"
#include "ResidentSimSettings.h"
#include "FacilityData.h"

// 住民の外出・施設選び・需要・体力・週のペナルティの「ルール」だけを集めた関数群。
// 画面・道路・時計を知らない(数値を渡すと、結果を返す)ので、単体テストで、ルールの正しさを確かめられる。
// どの数値を使うかは、全て ResidentSimSettings(調整用の設定)から取る。

// 小さな乱数(xorshift32)。同じ種なら、同じ列。施設選びの再現(デバッグ)のために、標準の乱数ではなく、自前にしている。
class SimRandom
{
public:
    explicit SimRandom(uint32_t seed = 1) { Seed(seed); }
    void Seed(uint32_t seed) { m_state = (seed == 0) ? 0x9E3779B9u : seed; }
    uint32_t NextU32();
    float Next01();                              // 0以上1未満
    float Range(float minValue, float maxValue); // minValue以上maxValue未満

private:
    uint32_t m_state = 1;
};

// 今の体力の上限(住民の基準から、ペナルティの段階ぶんを引く)。下限は設けてある(0にならない)。
float EffectiveStaminaCap(const ResidentAgent& agent, const ResidentSimSettings& settings);

// 需要が不足(しきい値未満)か。
bool IsLifeNeedUnmet(const ResidentAgent& agent, const ResidentSimSettings& settings);
bool IsActivityNeedUnmet(const ResidentAgent& agent, const ResidentSimSettings& settings);

// 住民が生まれた時の、需要・体力・特徴を決める(種から。同じ種なら同じ)。
void InitializeResident(ResidentAgent& agent, uint32_t seed, const ResidentSimSettings& settings);

// ---- 外出の目的 ----
// 今の需要から、外出の主な目的を決める。needDrivenには、不足による外出か(trueなら、不足している需要の方のカテゴリ)を返す。
// 需要が足りているなら、自発的な外出(カテゴリは乱数で、娯楽を多めに)。
struct OutingPurposeChoice
{
    bool needDriven = false;
    FacilityCategory category = FacilityCategory::Daily;
    UsagePurpose purpose = UsagePurpose::TopUp;
};
OutingPurposeChoice ChoosePurpose(const ResidentAgent& agent, const ResidentSimSettings& settings, SimRandom& random);

// ---- 施設の選び方(重み付き抽選) ----
struct FacilityCandidate
{
    uint32_t objectId = 0;          // 施設のGameObject::id
    BuildingType type = BuildingType::Shop;
    bool reachable = false;         // 道路でつながっていて、経路が見つかったか
    float pathLength = 0.0f;        // 自宅から施設までの道のり(ワールド単位)。reachableの時だけ意味を持つ
};

// 候補を選ばれなかった理由。
enum class CandidateReject
{
    None,               // 抽選の対象になった
    Unreachable,        // 道路でつながっていない・経路が無い(利用・売上を発生させない)
    NotEnoughStamina,   // 往復の移動と利用に必要な体力が足りない
    WrongCategory       // 今の目的のカテゴリではない
};

// 候補1つの評価の結果(デバッグ表示・ログ用。抽選の根拠が、全て追えるようにする)。
struct CandidateEvaluation
{
    FacilityCandidate candidate;
    CandidateReject reject = CandidateReject::None;
    float weight = 0.0f;            // 選択の重み(rejectがNoneの時だけ、0より大きい)
    float chance = 0.0f;            // 選ばれる確率(重み / 重みの合計)
    bool chosen = false;
};

// 選択の重み = 目的の適性 × 品質^(品質の効き方 × 住民の係数) × 値段の係数 × 距離の係数(下限あり)。
// 品質・値段・適性は施設の種類の固定の値(商店街に所属しても変わらない)。pathLengthはワールド単位。
float ComputeFacilityWeight(const ResidentAgent& agent, const FacilityDefinition& facility, UsagePurpose purpose,
                            float pathLength, const ResidentSimSettings& settings);

// 外出と利用に必要な体力(往復の移動 + 利用)。
float RequiredStamina(const FacilityDefinition& facility, float pathLength, const ResidentSimSettings& settings);

// 候補を評価し、重み付き抽選で1つ選ぶ。選ばれた候補の添字(evaluationsの添字)を返す。1つも選べなければ-1。
// evaluationsには、全ての候補の評価(除外の理由・重み・確率・選ばれたか)を入れて返す。
int ChooseFacility(const ResidentAgent& agent, const std::vector<FacilityCandidate>& candidates, const OutingPurposeChoice& purpose,
                   const ResidentSimSettings& settings, SimRandom& random, std::vector<CandidateEvaluation>& evaluations);

// 重みの列から、0以上1未満のunitに対応する添字を選ぶ(重みの比で選ぶ)。重みが全て0以下なら-1。
int PickWeightedIndex(const std::vector<float>& weights, float unit);

// ---- 利用の結果・時間の経過・週 ----
// 施設を利用した結果(需要の回復・体力の消費・履歴)を、住民に反映する。
void ApplyFacilityUse(ResidentAgent& agent, const FacilityDefinition& facility, uint32_t facilityObjectId, int weekIndex, float hour,
                      const ResidentSimSettings& settings);

// 活動時間のうち、deltaHoursぶん進んだ時の、需要の不足・充足が「続いている時間」(状態が変わったら0から数え直す)と、
// 体力の回復(家にいる時だけ)を更新する。
void AdvanceResidentTime(ResidentAgent& agent, float deltaHours, const ResidentSimSettings& settings);

// 週末の処理: 先に、ペナルティの段階を判定し、その後で、需要を自然に減らす。継続時間は、0に戻す。
//  - 悪化: 不足が猶予を超えて続き、かつ、週末になっても改善できていない(今も不足)時。週末までに改善できれば、回避できる。
//  - 回復: 両方が充足している状態が、一定時間以上、週末まで続いていた時(1段階軽くなる)。
//  - 週末に新しく減った分は、その週の「未対応の不足」として、すぐに罰しない(判定の後で減らす)。
void ProcessResidentWeekEnd(ResidentAgent& agent, const ResidentSimSettings& settings);

// 週の始まり(就寝明け)の処理: 体力を、上限まで(回復量の範囲で)回復する。
void ProcessResidentWeekStart(ResidentAgent& agent, const ResidentSimSettings& settings);

// 住居が無い住民を、元の住宅の位置に立たせる時の、位置のずらし(同じ位置に何人もいても、重ならないように)。
// index: その位置に、既にいる住居なしの住民の数(0なら、ずらさない)。spacing: ずらす距離の基準(ワールド単位)。
// 戻り値は、(x, z)のずれ。渦を巻くように外へ広がる(同じindexなら、いつも同じ値)。
DirectX::XMFLOAT2 HomelessOffset(int index, float spacing);
