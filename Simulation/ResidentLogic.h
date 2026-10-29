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

    // 内部状態(今の乱数列の位置)。デバッグ(記録のON/OFFで乱数の消費が変わらないことの確認)と、将来の再現用。
    uint32_t GetState() const { return m_state; }

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

// ---- 外出を考える確率(家にいる住民の、1回の判定あたり) ----
// 判定の確率が、どの場合のものか。
enum class OutingChanceBranch
{
    NeedDriven,             // 需要が不足している(不足による外出の確率)
    Spontaneous,            // 足りていて、体力に余裕がある(自発の確率 × 住民の自発性)
    SpontaneousLowStamina   // 足りているが、体力が足りない(確率は0。乱数は引くので、必ず不発)
};

// 外出を考える確率。branchには、どの場合かを返す(nullptrなら要らない)。
float ComputeOutingChance(const ResidentAgent& agent, const ResidentSimSettings& settings, OutingChanceBranch* branch = nullptr);

// ---- 外出の目的 ----
// 今の需要から、外出の主な目的を決める。needDrivenには、不足による外出か(trueなら、不足している需要の方のカテゴリ)を返す。
// 需要が足りているなら、自発的な外出(カテゴリは乱数で、娯楽を多めに)。
struct OutingPurposeChoice
{
    bool needDriven = false;
    FacilityCategory category = FacilityCategory::Daily;
    UsagePurpose purpose = UsagePurpose::TopUp;
};

// 目的を決めた時の、判断の過程(デバッグ用)。ChoosePurpose が実際に使った値と乱数を、そのまま書き出す(後から計算し直さない)。
struct PurposeTrace
{
    bool lifeUnmet = false;             // 生活の需要が不足していたか
    bool activityUnmet = false;         // アクティビティの需要が不足していたか
    bool tieBreakDrawn = false;         // 両方不足で同じ値だったため、カテゴリを乱数で決めたか
    float tieBreakRoll = -1.0f;         // その乱数(tieBreakDrawnの時だけ)
    bool categoryDrawn = false;         // 自発的な外出で、カテゴリを乱数で決めたか
    float categoryRoll = -1.0f;         // その乱数(categoryDrawnの時だけ。小さいほど娯楽)
    float leisureChance = 0.0f;         // 娯楽にする確率(categoryDrawnの時だけ)
    float lifeDeficit = 0.0f;           // 生活の不足の割合(0〜1。日用品の時だけ。まとめ買いの確率に使う)
    float purposeRoll = -1.0f;          // 利用目的を決めた乱数
    float purposeChance = 0.0f;         // 乱数がこれ未満なら、日用品はまとめ買い・娯楽は映画(それ以外は買い足し・軽食)
};

// outTrace: 判断の過程を受け取る(nullptrなら要らない)。乱数の消費の順序・回数は、outTraceの有無で変わらない。
OutingPurposeChoice ChoosePurpose(const ResidentAgent& agent, const ResidentSimSettings& settings, SimRandom& random, PurposeTrace* outTrace = nullptr);

// ---- 施設の選び方(重み付き抽選) ----
struct FacilityCandidate
{
    uint32_t objectId = 0;          // 施設のGameObject::id
    BuildingType type = BuildingType::Shop;
    bool reachable = false;         // 道路でつながっていて、経路が見つかったか
    float pathLength = 0.0f;        // 今いる所(自宅、立ち寄りの時は今いる施設)から施設までの道のり(ワールド単位)。reachableの時だけ意味を持つ。距離の重みに使う
    float walkLength = -1.0f;       // 体力の判定に使う、この先に歩く道のりの合計(行き + 家までの帰り)。負なら、往復(pathLengthの2倍)として扱う
    bool outOfRange = false;        // 立ち寄りの範囲(今いる施設からの道のりの上限)の外か
};

// 候補を選ばれなかった理由。
enum class CandidateReject
{
    None,               // 抽選の対象になった
    Unreachable,        // 道路でつながっていない・経路が無い(利用・売上を発生させない)
    NotEnoughStamina,   // 往復の移動と利用に必要な体力が足りない
    WrongCategory,      // 今の目的のカテゴリではない
    VisitedThisOuting,  // 今の外出で、もう使った施設(立ち寄りで、同じ施設に戻らない)
    OutOfRange          // 立ち寄りの範囲の外(遠すぎる)
};

// 施設の重みの内訳(1候補ぶん)。ComputeFacilityWeight が実際に使った係数と途中値(画面は、これを見せるだけ。別の式で計算し直さない)。
struct FacilityWeightBreakdown
{
    float suitability = 1.0f;       // 今の目的への適性(施設の表の値)
    float quality = 1.0f;           // 施設の品質(下限0.01を適用した後)
    float qualityExponent = 0.0f;   // 品質の指数 = 設定の「品質の効き方」 × この住民の品質重視度
    float qualityFactor = 1.0f;     // 品質係数 = 品質 ^ 品質の指数
    float priceYen = 0.0f;          // 施設の値段(円)
    float priceRatio = 0.0f;        // 値段 ÷ 基準価格
    float priceStrength = 0.0f;     // 値段の効き = 設定の「値段の効き方」 × この住民の価格感度
    float priceFactor = 1.0f;       // 値段係数 = 1 ÷ (1 + 値段の効き × 値段の比)
    float legacyDistance = 0.0f;    // 道のり(旧マス基準に直した値)
    float distanceScale = 1.0f;     // 距離の目安 = 設定の「距離の目安」 × この住民の距離許容度(下限0.1)
    float distanceFactor = 1.0f;    // 距離係数 = exp(-道のり ÷ 距離の目安)
    float product = 0.0f;           // 適性 × 品質係数 × 値段係数 × 距離係数(下限を適用する前)
    float floorWeight = 0.0f;       // 重みの下限(設定)
    float weight = 0.0f;            // 下限を適用した後の重み = max(product, floorWeight) = ComputeFacilityWeight の返り値
};

// 候補1つの評価の結果(デバッグ表示・ログ用。抽選の根拠が、全て追えるようにする)。
struct CandidateEvaluation
{
    FacilityCandidate candidate;
    CandidateReject reject = CandidateReject::None;
    float repeatFactor = 1.0f;      // 利用履歴による重みの倍率(1=下がらない。直近に同じ施設を使っていると、1未満)
    float weight = 0.0f;            // 選択の重み(rejectがNoneの時だけ、0より大きい)。repeatFactorを掛けた後の値
    float chance = 0.0f;            // 選ばれる確率(重み / 重みの合計)
    bool chosen = false;

    // ---- 判断の過程(デバッグ用。抽選の結果には影響しない) ----
    float requiredStamina = -1.0f;  // 体力の判定に使った、必要な体力(判定まで進んだ候補だけ。負なら判定していない)
    bool hasBreakdown = false;      // breakdownが有効か(抽選の対象になった候補だけ)
    FacilityWeightBreakdown breakdown; // 重みの内訳(下限適用後の基本の重みまで)
    float weightAfterRepeat = 0.0f; // 基本の重み × 再訪補正の倍率(重みの下限を適用する前)
    float rangeBegin = 0.0f;        // 抽選の累積の範囲の始まり(重みを順に足した値。乱数×合計がこの範囲に入った候補が選ばれる)
    float rangeEnd = 0.0f;          // 同じく、終わり
};

// 施設選びの抽選の過程(デバッグ用)。ChooseFacility が実際に引いた乱数と合計を、そのまま書き出す。
struct FacilityChoiceTrace
{
    float randomUnit = -1.0f;       // 抽選に使った乱数(0以上1未満)。候補が無くても、乱数は1つ引く
    float totalWeight = 0.0f;       // 全候補の重みの合計(選ばれる確率の分母)
    float pickTarget = 0.0f;        // 乱数 × 正の重みの合計(候補の累積の範囲と比べる値)
    int pickedIndex = -1;           // 選ばれた候補の添字(-1=選べなかった)
};

// 選択の重み = 目的の適性 × 品質^(品質の効き方 × 住民の係数) × 値段の係数 × 距離の係数(下限あり)。
// 品質・値段・適性は施設の種類の固定の値(商店街に所属しても変わらない)。pathLengthはワールド単位。
float ComputeFacilityWeight(const ResidentAgent& agent, const FacilityDefinition& facility, UsagePurpose purpose,
                            float pathLength, const ResidentSimSettings& settings);

// 同じ計算で、途中の係数も返す(ComputeFacilityWeight は、これの weight を返すだけ。画面とルールが食い違わないように、計算は1か所)。
void ComputeFacilityWeightBreakdown(const ResidentAgent& agent, const FacilityDefinition& facility, UsagePurpose purpose,
                                    float pathLength, const ResidentSimSettings& settings, FacilityWeightBreakdown& out);

// 外出と利用に必要な体力(往復の移動 + 利用)。
float RequiredStamina(const FacilityDefinition& facility, float pathLength, const ResidentSimSettings& settings);

// 歩く道のりの合計(行き + 帰り)に必要な体力 = 施設の利用の分 + 移動の分。
float RequiredStaminaForWalk(const FacilityDefinition& facility, float totalWalkLength, const ResidentSimSettings& settings);

// 利用履歴による重みの倍率(0より大きく、1以下)。直近repeatVisitWindow回の利用に、同じ施設があれば下がる。
// 直前の利用 = repeatVisitWeightFactor、古くなるほど1に近づく(複数あれば、一番下がる物)。完全には禁止しない。
// needDrivenDaily: 不足による日用品の外出か。trueなら、必要な買い物を妨げないよう、下げ幅を弱める(needDrivenDailyRepeatStrength)。
float ComputeRepeatVisitFactor(const ResidentAgent& agent, uint32_t facilityObjectId, bool needDrivenDaily, const ResidentSimSettings& settings);

// 候補を評価し、重み付き抽選で1つ選ぶ。選ばれた候補の添字(evaluationsの添字)を返す。1つも選べなければ-1。
// evaluationsには、全ての候補の評価(除外の理由・重み・確率・選ばれたか)を入れて返す。
// visitsThisOuting: 今の外出で、もう使った施設の数(立ち寄りの時だけ。履歴の新しい方から、この数の施設は、候補から外す)。通常の施設選びは0。
// outTrace: 抽選の過程(乱数・合計)を受け取る(nullptrなら要らない)。乱数の消費の順序・回数は、outTraceの有無で変わらない。
int ChooseFacility(const ResidentAgent& agent, const std::vector<FacilityCandidate>& candidates, const OutingPurposeChoice& purpose,
                   const ResidentSimSettings& settings, SimRandom& random, std::vector<CandidateEvaluation>& evaluations,
                   int visitsThisOuting = 0, FacilityChoiceTrace* outTrace = nullptr);

// ---- 立ち寄り(主目的の施設を使った後、近くの施設に寄る) ----
// 立ち寄りを考えない理由(None=考えてよい)。
enum class StopoverBlock
{
    None,           // 考えてよい
    NoHome,         // 帰る家が無い
    MaxStops,       // 立ち寄りの回数が上限に達している
    TooLate,        // 時刻が遅い
    LowStamina      // 体力が足りない
};

// 立ち寄りを考えてよいか、だめなら最初に当てはまった理由(判定の順序は、家・回数・時刻・体力)。
StopoverBlock ExplainStopoverBlock(const ResidentAgent& agent, float hour, const ResidentSimSettings& settings);

// 立ち寄りを考えてよいか(回数の上限・時刻・体力・帰る家があるか)。確率の判定は別(ComputeStopoverChance)。
bool CanConsiderStopover(const ResidentAgent& agent, float hour, const ResidentSimSettings& settings);

// 立ち寄る確率 = 基本の確率 × 減衰^(今までの立ち寄りの回数)。主目的の施設の利用より低く、回数が増えるほど下がる。
float ComputeStopoverChance(const ResidentAgent& agent, const ResidentSimSettings& settings);

// 同じ外出で食事系を使った後に、別の食事系の施設へ寄る時の、実際に寄る確率(それ以外は1)。
float ComputeFoodChainAcceptChance(const ResidentAgent& agent, const FacilityDefinition& candidate, const ResidentSimSettings& settings);

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

// ---- 道路が無くなった時の扱い(暫定。数値は、ResidentSimSettingsで調整できる) ----
// 迂回して帰る時の、体力の消費(通常の移動と同じ、道のりあたりの消費 × roadLossStaminaCostScale)。routeLength: 迂回の道のり全体。
float RoadLossStaminaCost(float routeLength, const ResidentSimSettings& settings);

// 道路が無くなったことによる、満足度の低下(0未満にはならない)。stranded: 立ち往生したか(falseなら、迂回して帰ることになっただけ)。
void ApplyRoadLossPenalty(ResidentAgent& agent, bool stranded, const ResidentSimSettings& settings);

// 住居が無い住民を、元の住宅の位置に立たせる時の、位置のずらし(同じ位置に何人もいても、重ならないように)。
// index: その位置に、既にいる住居なしの住民の数(0なら、ずらさない)。spacing: ずらす距離の基準(ワールド単位)。
// 戻り値は、(x, z)のずれ。渦を巻くように外へ広がる(同じindexなら、いつも同じ値)。
DirectX::XMFLOAT2 HomelessOffset(int index, float spacing);
