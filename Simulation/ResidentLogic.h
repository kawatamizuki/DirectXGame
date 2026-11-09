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
    // 内部状態を戻す(セーブした位置から、同じ乱数列を続ける。0は、この乱数では出ない値なので、種と同じ扱い)。
    void SetState(uint32_t state) { m_state = (state == 0) ? 0x9E3779B9u : state; }

private:
    uint32_t m_state = 1;
};

// 今の体力の上限(住民の基準から、ペナルティの段階ぶんを引く)。下限は設けてある(0にならない)。
float EffectiveStaminaCap(const ResidentAgent& agent, const ResidentSimSettings& settings);

// 需要が不足(しきい値未満)か。
bool IsLifeNeedUnmet(const ResidentAgent& agent, const ResidentSimSettings& settings);
bool IsActivityNeedUnmet(const ResidentAgent& agent, const ResidentSimSettings& settings);

// ---- 需要の段階(P2-0) ----
// 需要(0〜100)の段階。しきい値は設定(needSatisfiedThreshold / needSevereThreshold / needCriticalThreshold)。
enum class NeedStage : uint8_t
{
    Satisfied,  // 充足(しきい値以上)
    Short,      // 不足
    Severe,     // 深刻
    Critical    // 危機的
};
constexpr int kNeedStageCount = 4;

NeedStage GetNeedStage(float need, const ResidentSimSettings& settings);
const char* NeedStageName(NeedStage stage);

// 需要の切迫度(0〜1)。生活・余暇それぞれの段階の切迫度(設定)に、カテゴリの重要度を掛けて、大きい方を、重要度の最大値で割った値。
// 「今、どれくらい、切迫した需要があるか」を1つの数で表す。外出の理由の重みと、立ち寄りの確率が、これを参照する。
float ComputeNeedPressure(const ResidentAgent& agent, const ResidentSimSettings& settings);

// 体力の割合(0〜1) = 今の体力 ÷ 今の体力の上限。
float StaminaRatio(const ResidentAgent& agent, const ResidentSimSettings& settings);

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

// ---- 外出の理由(P2-0) ----
// 理由を決めた時の、判断の過程(デバッグ用)。ChooseOutingReason が実際に使った値と乱数を、そのまま書き出す(後から計算し直さない)。
struct OutingReasonTrace
{
    float staminaRatio = 1.0f;          // 体力の割合(0〜1)
    float pressure = 0.0f;              // 需要の切迫度(0〜1)
    float weight[kOutingReasonCount] = {}; // 理由ごとの重み(エリアは、候補が無い間は0)
    float total = 0.0f;                 // 重みの合計(0以下なら、選べる理由が無い)
    float roll = -1.0f;                 // 引いた乱数(いつも1回引く)
    bool areaAvailable = false;         // 「エリア」の候補があったか
    bool valid = false;                 // 理由を選べたか(falseなら、外出を取りやめる)
    OutingReason chosen = OutingReason::Quick;
};

// 外出の理由を、重み付きの抽選で決める。乱数は、いつも1回だけ引く(重みの合計が0でも)。
//  手軽 = 基本の重み
//  がっつり = 基本の重み × 体力の割合^指数 × (1 − 切迫度 × 切迫のペナルティ) × 住民の自発性
//  エリア = areaAvailable の時だけ 基本の重み(今は候補が無いので、いつも0)
// 重みが0の理由は選ばれない(0の理由は、「他の理由で、同じ乱数から選び直す」のと同じ)。全て0なら、falseを返す。
// out: 選んだ理由(falseの時は変えない)。outTrace: 判断の過程(nullptrなら要らない。乱数の消費は、outTraceの有無で変わらない)。
bool ChooseOutingReason(const ResidentAgent& agent, const ResidentSimSettings& settings, SimRandom& random, bool areaAvailable,
                        OutingReason& out, OutingReasonTrace* outTrace = nullptr);

// ---- 外出の目的 ----
// 外出の主な目的(生活=日用品 / 余暇のカテゴリ・利用目的)を、需要の段階×カテゴリの重要度の重みで、決める。
// needDrivenは、選んだカテゴリの需要が不足(充足でない)か。reasonは、この外出の理由(施設の距離の評価に使う。ChoosePurposeは、変えない)。
struct OutingPurposeChoice
{
    bool needDriven = false;
    FacilityCategory category = FacilityCategory::Daily;
    UsagePurpose purpose = UsagePurpose::TopUp;
    OutingReason reason = OutingReason::Quick;
};

// 目的を決めた時の、判断の過程(デバッグ用)。ChoosePurpose が実際に使った値と乱数を、そのまま書き出す(後から計算し直さない)。
struct PurposeTrace
{
    bool lifeUnmet = false;             // 生活の需要が不足していたか
    bool activityUnmet = false;         // アクティビティの需要が不足していたか
    NeedStage lifeStage = NeedStage::Satisfied;     // 生活の需要の段階
    NeedStage activityStage = NeedStage::Satisfied; // 余暇(アクティビティ)の需要の段階
    float dailyWeight = 0.0f;           // 生活の重み = 重要度 × 段階の補正
    float leisureWeight = 0.0f;         // 余暇の重み
    bool tieBreakDrawn = false;         // (P1の名残。今は、いつもfalse)
    float tieBreakRoll = -1.0f;         // (同上)
    bool categoryDrawn = true;          // カテゴリを乱数で決めたか(P2-0からは、いつも決める)
    float categoryRoll = -1.0f;         // その乱数(小さいほど余暇)
    float leisureChance = 0.0f;         // 余暇にする確率 = 余暇の重み ÷ (生活の重み + 余暇の重み)
    float lifeDeficit = 0.0f;           // 生活の不足の割合(0〜1。日用品の時だけ。まとめ買いの確率に使う)
    float purposeRoll = -1.0f;          // 利用目的を決めた乱数
    float purposeChance = 0.0f;         // 乱数がこれ未満なら、日用品はまとめ買い・娯楽は映画(それ以外は買い足し・軽食)
    OutingReasonTrace reason;           // この外出の理由の決め方(呼び出し側が、ChooseOutingReason の結果を入れる)
};

// outTrace: 判断の過程を受け取る(nullptrなら要らない)。乱数の消費は、いつも2回(カテゴリ・利用目的)。outTraceの有無で変わらない。
OutingPurposeChoice ChoosePurpose(const ResidentAgent& agent, const ResidentSimSettings& settings, SimRandom& random, PurposeTrace* outTrace = nullptr);

// ---- 施設の選び方(重み付き抽選) ----
struct FacilityCandidate
{
    uint32_t objectId = 0;          // 施設のGameObject::id
    BuildingType type = BuildingType::Shop;
    bool reachable = false;         // 道路でつながっていて、経路が見つかったか
    float pathLength = 0.0f;        // 今いる所(自宅、立ち寄りの時は今いる施設)から施設までの道のり(ワールド単位)。reachableの時だけ意味を持つ。距離の重みに使う
    bool outOfRange = false;        // 立ち寄りの範囲(今いる施設からの道のりの上限。設定で0なら、上限なし)の外か
};

// 候補を選ばれなかった理由。
enum class CandidateReject
{
    None,               // 抽選の対象になった
    Unreachable,        // 道路でつながっていない・経路が無い(利用・売上を発生させない)
    NotEnoughStamina,   // 利用に必要な体力が足りない(P2-0: 移動では体力を使わないので、施設の利用の分だけ)
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
    OutingReason reason = OutingReason::Quick; // この外出の理由(距離の効き方が変わる)
    float distanceScale = 1.0f;     // 距離の目安 = 設定の「距離の目安」 × この住民の距離許容度 × 理由の倍率 × (立ち寄りなら)立ち寄りの倍率(下限0.1)
    float distanceShape = 1.0f;     // 距離の減り方の形 k(理由ごとの設定。1=指数)
    float distanceFactor = 1.0f;    // 距離係数 = exp(-(道のり ÷ 距離の目安)^k)
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
// reason: この外出の理由(距離の目安の倍率・減り方の形が変わる。既定=手軽=従来と同じ)。
// distanceScaleFactor: 距離の目安にさらに掛ける倍率(立ち寄りは、stopoverDistanceScaleFactor。通常は1)。
float ComputeFacilityWeight(const ResidentAgent& agent, const FacilityDefinition& facility, UsagePurpose purpose,
                            float pathLength, const ResidentSimSettings& settings,
                            OutingReason reason = OutingReason::Quick, float distanceScaleFactor = 1.0f);

// 同じ計算で、途中の係数も返す(ComputeFacilityWeight は、これの weight を返すだけ。画面とルールが食い違わないように、計算は1か所)。
void ComputeFacilityWeightBreakdown(const ResidentAgent& agent, const FacilityDefinition& facility, UsagePurpose purpose,
                                    float pathLength, const ResidentSimSettings& settings, FacilityWeightBreakdown& out,
                                    OutingReason reason = OutingReason::Quick, float distanceScaleFactor = 1.0f);

// 施設を1回使う時の、体力の消費(P2-0)。useIndex: その外出で、何軒目の利用か(0=主目的、1=立ち寄り1軒目…)。
// 消費 = 施設の消費 × (1 + additionalUseStaminaCostStep × useIndex)。移動の分は、無い(移動では体力を消費しない)。
float FacilityUseStaminaCost(const FacilityDefinition& facility, int useIndex, const ResidentSimSettings& settings);

// その施設を使い始めるために必要な体力(=利用の消費。足りなければ、その施設へは行かない)。
float RequiredStamina(const FacilityDefinition& facility, int useIndex, const ResidentSimSettings& settings);

// 利用履歴による重みの倍率(0より大きく、1以下)。直近repeatVisitWindow回の利用に、同じ施設があれば下がる。
// 直前の利用 = repeatVisitWeightFactor、古くなるほど1に近づく(複数あれば、一番下がる物)。完全には禁止しない。
// needDrivenDaily: 不足による日用品の外出か。trueなら、必要な買い物を妨げないよう、下げ幅を弱める(needDrivenDailyRepeatStrength)。
float ComputeRepeatVisitFactor(const ResidentAgent& agent, uint32_t facilityObjectId, bool needDrivenDaily, const ResidentSimSettings& settings);

// 候補を評価し、重み付き抽選で1つ選ぶ。選ばれた候補の添字(evaluationsの添字)を返す。1つも選べなければ-1。
// evaluationsには、全ての候補の評価(除外の理由・重み・確率・選ばれたか)を入れて返す。
// visitsThisOuting: 今の外出で、もう使った施設の数(立ち寄りの時だけ。履歴の新しい方から、この数の施設は、候補から外す)。通常の施設選びは0。
//                   同時に、「何軒目の利用か」(体力の消費の増え方)と、「立ち寄りか」(距離の目安の倍率)にも使う。
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
    LowStamina      // 体力が無い(0以下)。体力が少ない時は、ここではなく、確率が下がる(連続的)
};

// 立ち寄りを考えてよいか、だめなら最初に当てはまった理由(判定の順序は、家・回数・時刻・体力)。
// 除外は、本当に不可能な場合だけ(家が無い・回数の上限・時刻の上限・体力が0)。それ以外は、確率で扱う。
StopoverBlock ExplainStopoverBlock(const ResidentAgent& agent, float hour, const ResidentSimSettings& settings);

// 立ち寄りを考えてよいか(回数の上限・時刻・体力・帰る家があるか)。確率の判定は別(ComputeStopoverChance)。
bool CanConsiderStopover(const ResidentAgent& agent, float hour, const ResidentSimSettings& settings);

// 立ち寄る確率の内訳(デバッグ用)。ComputeStopoverChance が実際に使った係数(画面は、これを見せるだけ)。
struct StopoverChanceBreakdown
{
    float baseChance = 0.0f;        // 基本の確率
    float countDecayFactor = 1.0f;  // 回数の減衰 = 減衰 ^ 立ち寄り済みの回数
    float staminaRatio = 1.0f;      // 体力の割合(0〜1)
    float staminaFactor = 1.0f;     // 体力の係数 = 体力の割合 ^ 体力の指数
    float pressure = 0.0f;          // 需要の切迫度(0〜1)
    float needFactor = 1.0f;        // 需要の補正 = 下限 + (1 − 下限) × 切迫度
    OutingReason reason = OutingReason::Quick;
    float reasonMultiplier = 1.0f;  // 理由の倍率
    float chance = 0.0f;            // 結果 = 各係数の積(0〜1に収める)
};

// 立ち寄る確率 = 基本 × 回数の減衰 × 体力の係数 × 需要の補正 × 理由の倍率(体力・需要は、今の値。利用の直後の、更新された値で、判定する)。
float ComputeStopoverChance(const ResidentAgent& agent, const ResidentSimSettings& settings, StopoverChanceBreakdown* out = nullptr);

// 同じ外出で食事系を使った後に、別の食事系の施設へ寄る時の、実際に寄る確率(それ以外は1)。
float ComputeFoodChainAcceptChance(const ResidentAgent& agent, const FacilityDefinition& candidate, const ResidentSimSettings& settings);

// 重みの列から、0以上1未満のunitに対応する添字を選ぶ(重みの比で選ぶ)。重みが全て0以下なら-1。
int PickWeightedIndex(const std::vector<float>& weights, float unit);

// ---- 利用の結果・時間の経過・週 ----
// 施設を利用した結果(需要の回復・体力の消費・履歴)を、住民に反映する。useIndex: その外出で、何軒目の利用か(体力の消費の増え方に使う。0=主目的)。
void ApplyFacilityUse(ResidentAgent& agent, const FacilityDefinition& facility, uint32_t facilityObjectId, int weekIndex, float hour,
                      const ResidentSimSettings& settings, int useIndex = 0);

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
// (P2-0: 移動では体力を消費しないので、迂回して帰る時の体力の消費は、無くなった。満足度の低下だけ残る)
// 道路が無くなったことによる、満足度の低下(0未満にはならない)。stranded: 立ち往生したか(falseなら、迂回して帰ることになっただけ)。
void ApplyRoadLossPenalty(ResidentAgent& agent, bool stranded, const ResidentSimSettings& settings);

// 住居が無い住民を、元の住宅の位置に立たせる時の、位置のずらし(同じ位置に何人もいても、重ならないように)。
// index: その位置に、既にいる住居なしの住民の数(0なら、ずらさない)。spacing: ずらす距離の基準(ワールド単位)。
// 戻り値は、(x, z)のずれ。渦を巻くように外へ広がる(同じindexなら、いつも同じ値)。
DirectX::XMFLOAT2 HomelessOffset(int index, float spacing);
