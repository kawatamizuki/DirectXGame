#include "ResidentLogic.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kMinStaminaCap = 20.0f;     // ペナルティを受けても、体力の上限が、これより下がらない

    // 整数から、0〜1の値を作る(住民の特徴を、種から決める時のばらつき用)。
    float HashUnit(uint32_t seed, uint32_t salt)
    {
        uint32_t h = seed * 2654435761u + salt * 40503u + 0x9E3779B9u;
        h ^= h >> 15; h *= 2246822519u;
        h ^= h >> 13; h *= 3266489917u;
        h ^= h >> 16;
        return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }

    float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

// ---------------------------------------------------------------------------
// 乱数
// ---------------------------------------------------------------------------
uint32_t SimRandom::NextU32()
{
    m_state ^= m_state << 13;
    m_state ^= m_state >> 17;
    m_state ^= m_state << 5;
    return m_state;
}

float SimRandom::Next01()
{
    return static_cast<float>(NextU32() >> 8) / static_cast<float>(1u << 24);
}

float SimRandom::Range(float minValue, float maxValue)
{
    return Lerp(minValue, maxValue, Next01());
}

// ---------------------------------------------------------------------------
// 住民の特徴
// ---------------------------------------------------------------------------
ResidentTraits MakeResidentTraits(uint32_t seed, float baseStaminaMax)
{
    ResidentTraits traits;
    traits.staminaCapBase = baseStaminaMax * Lerp(0.85f, 1.15f, HashUnit(seed, 1));
    traits.needDecayScale = Lerp(0.8f, 1.25f, HashUnit(seed, 2));
    traits.qualityWeight = Lerp(0.6f, 1.5f, HashUnit(seed, 3));
    traits.priceSensitivity = Lerp(0.5f, 1.6f, HashUnit(seed, 4));
    traits.distanceTolerance = Lerp(0.7f, 1.5f, HashUnit(seed, 5));
    traits.spontaneity = Lerp(0.5f, 1.6f, HashUnit(seed, 6));
    return traits;
}

float EffectiveStaminaCap(const ResidentAgent& agent, const ResidentSimSettings& settings)
{
    const float cap = agent.traits.staminaCapBase - static_cast<float>(agent.penaltyLevel) * settings.penaltyStaminaCapStep;
    return (cap > kMinStaminaCap) ? cap : kMinStaminaCap;
}

bool IsLifeNeedUnmet(const ResidentAgent& agent, const ResidentSimSettings& settings)
{
    return agent.lifeNeed < settings.needSatisfiedThreshold;
}

bool IsActivityNeedUnmet(const ResidentAgent& agent, const ResidentSimSettings& settings)
{
    return agent.activityNeed < settings.needSatisfiedThreshold;
}

NeedStage GetNeedStage(float need, const ResidentSimSettings& settings)
{
    if (need >= settings.needSatisfiedThreshold) { return NeedStage::Satisfied; }
    if (need < settings.needCriticalThreshold)   { return NeedStage::Critical; }
    if (need < settings.needSevereThreshold)     { return NeedStage::Severe; }
    return NeedStage::Short;
}

const char* NeedStageName(NeedStage stage)
{
    switch (stage)
    {
    case NeedStage::Satisfied: return "充足";
    case NeedStage::Short:     return "不足";
    case NeedStage::Severe:    return "深刻";
    case NeedStage::Critical:  return "危機的";
    }
    return "?";
}

float ComputeNeedPressure(const ResidentAgent& agent, const ResidentSimSettings& settings)
{
    const int life = static_cast<int>(GetNeedStage(agent.lifeNeed, settings));
    const int activity = static_cast<int>(GetNeedStage(agent.activityNeed, settings));
    const float dailyImportance = (std::max)(0.0f, settings.dailyImportance);
    const float leisureImportance = (std::max)(0.0f, settings.leisureImportance);
    const float maxImportance = (std::max)((std::max)(dailyImportance, leisureImportance), 1e-6f);
    const float lifePressure = settings.needPressureByStage[life] * dailyImportance;
    const float activityPressure = settings.needPressureByStage[activity] * leisureImportance;
    return Clamp((std::max)(lifePressure, activityPressure) / maxImportance, 0.0f, 1.0f);
}

float StaminaRatio(const ResidentAgent& agent, const ResidentSimSettings& settings)
{
    const float cap = (std::max)(1.0f, EffectiveStaminaCap(agent, settings));
    return Clamp(agent.stamina / cap, 0.0f, 1.0f);
}

void InitializeResident(ResidentAgent& agent, uint32_t seed, const ResidentSimSettings& settings)
{
    agent.traits = MakeResidentTraits(seed, settings.staminaMax);
    agent.lifeNeed = Lerp(settings.initialNeedMin, settings.initialNeedMax, HashUnit(seed, 11));
    agent.activityNeed = Lerp(settings.initialNeedMin, settings.initialNeedMax, HashUnit(seed, 12));
    agent.penaltyLevel = 0;
    agent.satisfaction = settings.satisfactionMax;
    agent.stamina = EffectiveStaminaCap(agent, settings);
    agent.unmetLifeHours = 0.0f;
    agent.unmetActivityHours = 0.0f;
    agent.satisfiedHours = 0.0f;
    agent.history.clear();
}

// ---------------------------------------------------------------------------
// 外出を考える確率
// ---------------------------------------------------------------------------
float ComputeOutingChance(const ResidentAgent& agent, const ResidentSimSettings& settings, OutingChanceBranch* branch)
{
    const bool unmet = IsLifeNeedUnmet(agent, settings) || IsActivityNeedUnmet(agent, settings);
    float chance = 0.0f;
    OutingChanceBranch which = OutingChanceBranch::SpontaneousLowStamina;
    if (unmet)
    {
        chance = settings.needDrivenChancePerTick;
        which = OutingChanceBranch::NeedDriven;
    }
    else if (agent.stamina >= settings.spontaneousMinStamina)
    {
        chance = settings.spontaneousChancePerTick * agent.traits.spontaneity;
        which = OutingChanceBranch::Spontaneous;
    }
    if (branch)
    {
        *branch = which;
    }
    return chance;
}

// ---------------------------------------------------------------------------
// 外出の理由(P2-0)
// ---------------------------------------------------------------------------
bool ChooseOutingReason(const ResidentAgent& agent, const ResidentSimSettings& settings, SimRandom& random, bool areaAvailable,
                        OutingReason& out, OutingReasonTrace* outTrace)
{
    OutingReasonTrace trace;
    trace.staminaRatio = StaminaRatio(agent, settings);
    trace.pressure = ComputeNeedPressure(agent, settings);
    trace.areaAvailable = areaAvailable;

    // 手軽: 基本の重み。
    trace.weight[static_cast<int>(OutingReason::Quick)] = (std::max)(0.0f, settings.reasonQuickWeight);

    // がっつり: 体力に余裕があるほど・切迫した需要が無いほど・自発的な住民ほど、重い。切迫度は、ここ1か所だけで反映する(手軽には掛けない)。
    const float staminaTerm = std::pow(trace.staminaRatio, (std::max)(0.0f, settings.substantialStaminaExponent));
    const float pressureTerm = (std::max)(0.0f, 1.0f - trace.pressure * Clamp(settings.substantialPressurePenalty, 0.0f, 1.0f));
    trace.weight[static_cast<int>(OutingReason::Substantial)] =
        (std::max)(0.0f, settings.reasonSubstantialWeight) * staminaTerm * pressureTerm * (std::max)(0.0f, agent.traits.spontaneity);

    // エリア: 外出の目的の候補になる区画が無い間は、重み0(他の理由から、同じ乱数で選ぶのと同じ)。候補がある時は、体力に余裕があり、需要が切迫していないほど、重い。
    const float areaPressureTerm = (std::max)(0.0f, 1.0f - trace.pressure * Clamp(settings.areaPressurePenalty, 0.0f, 1.0f));
    trace.weight[static_cast<int>(OutingReason::Area)] =
        areaAvailable ? (std::max)(0.0f, settings.reasonAreaWeight) * staminaTerm * areaPressureTerm : 0.0f;

    float total = 0.0f;
    for (float w : trace.weight) { total += w; }
    trace.total = total;

    // 乱数は、重みの合計が0でも、いつも1回引く(乱数の消費を、状態によって変えない)。
    trace.roll = random.Next01();
    const std::vector<float> weights(trace.weight, trace.weight + kOutingReasonCount);
    const int picked = PickWeightedIndex(weights, trace.roll);
    trace.valid = (picked >= 0);
    if (trace.valid)
    {
        trace.chosen = static_cast<OutingReason>(picked);
        out = trace.chosen;
    }
    if (outTrace)
    {
        *outTrace = trace;
    }
    return trace.valid;
}

// ---------------------------------------------------------------------------
// 外出の目的
// ---------------------------------------------------------------------------
OutingPurposeChoice ChoosePurpose(const ResidentAgent& agent, const ResidentSimSettings& settings, SimRandom& random, PurposeTrace* outTrace)
{
    // 判断の過程は、実際に使った値・引いた乱数をそのまま書き出す(traceの有無で、乱数の消費は変わらない)。
    PurposeTrace trace;
    OutingPurposeChoice choice;
    trace.lifeStage = GetNeedStage(agent.lifeNeed, settings);
    trace.activityStage = GetNeedStage(agent.activityNeed, settings);
    trace.lifeUnmet = trace.lifeStage != NeedStage::Satisfied;
    trace.activityUnmet = trace.activityStage != NeedStage::Satisfied;

    // カテゴリ: 重み = 重要度 × 段階の補正。両方の需要が足りていても、同じ式(余暇が多めになる補正を、設定で持つ)。
    trace.dailyWeight = (std::max)(0.0f, settings.dailyImportance) * (std::max)(0.0f, settings.dailyStageMultiplier[static_cast<int>(trace.lifeStage)]);
    trace.leisureWeight = (std::max)(0.0f, settings.leisureImportance) * (std::max)(0.0f, settings.leisureStageMultiplier[static_cast<int>(trace.activityStage)]);
    const float weightSum = trace.dailyWeight + trace.leisureWeight;
    trace.leisureChance = (weightSum > 0.0f) ? trace.leisureWeight / weightSum : 0.0f;

    // 乱数は、いつも2回(カテゴリ・利用目的)引く。
    const float categoryRoll = random.Next01();
    trace.categoryDrawn = true;
    trace.categoryRoll = categoryRoll;
    choice.category = (categoryRoll < trace.leisureChance) ? FacilityCategory::Leisure : FacilityCategory::Daily;
    // 選んだカテゴリの需要が、不足していたか(不足による外出か、そうでない外出か)。
    choice.needDriven = (choice.category == FacilityCategory::Daily) ? trace.lifeUnmet : trace.activityUnmet;

    if (choice.category == FacilityCategory::Daily)
    {
        // 生活が大きく不足しているほど、まとめ買い。そうでなければ、買い足し。
        const float deficit = Clamp((settings.needSatisfiedThreshold - agent.lifeNeed) / settings.needSatisfiedThreshold, 0.0f, 1.0f);
        const float bulkChance = choice.needDriven ? (0.2f + deficit) : 0.3f;
        const float roll = random.Next01();
        trace.lifeDeficit = deficit;
        trace.purposeRoll = roll;
        trace.purposeChance = bulkChance;
        choice.purpose = (roll < bulkChance) ? UsagePurpose::BulkBuy : UsagePurpose::TopUp;
    }
    else
    {
        const float roll = random.Next01();
        trace.purposeRoll = roll;
        trace.purposeChance = 0.5f;
        choice.purpose = (roll < 0.5f) ? UsagePurpose::Movie : UsagePurpose::Light;
    }
    if (outTrace)
    {
        *outTrace = trace;
    }
    return choice;
}

// ---------------------------------------------------------------------------
// 施設の選び方
// ---------------------------------------------------------------------------
float ComputeDistanceFactor(const ResidentAgent& agent, float pathLength, const ResidentSimSettings& settings, OutingReason reason,
                            float distanceScaleFactor, float* outLegacyDistance, float* outScale, float* outShape)
{
    const int reasonIndex = (std::max)(0, (std::min)(static_cast<int>(reason), kOutingReasonCount - 1));
    const float legacyDistance = pathLength / WorldScale::FromLegacy(1.0f);
    const float scale = (std::max)(0.1f, settings.distanceScaleLegacyCells * agent.traits.distanceTolerance
                                       * (std::max)(0.01f, settings.reasonDistanceScale[reasonIndex]) * (std::max)(0.01f, distanceScaleFactor));
    const float shape = (std::max)(0.1f, settings.reasonDistanceShape[reasonIndex]);
    const float distanceRatio = (std::max)(0.0f, legacyDistance) / scale;
    if (outLegacyDistance) { *outLegacyDistance = legacyDistance; }
    if (outScale) { *outScale = scale; }
    if (outShape) { *outShape = shape; }
    return std::exp(-((shape == 1.0f) ? distanceRatio : std::pow(distanceRatio, shape)));
}

bool ReevaluateReasonWithoutArea(const OutingReasonTrace& trace, OutingReason& out)
{
    std::vector<float> weights(trace.weight, trace.weight + kOutingReasonCount);
    weights[static_cast<int>(OutingReason::Area)] = 0.0f;
    const int picked = PickWeightedIndex(weights, trace.roll);   // 同じ乱数で、他の理由から選び直す(乱数は、引き直さない)
    if (picked < 0)
    {
        return false;
    }
    out = static_cast<OutingReason>(picked);
    return true;
}

int ChooseArea(const ResidentAgent& agent, const std::vector<AreaCandidate>& candidates, const OutingPurposeChoice& purpose,
               const ResidentSimSettings& settings, float roll, std::vector<AreaCandidateEvaluation>& evals, AreaChoiceTrace* trace)
{
    (void)purpose;
    evals.clear();
    evals.reserve(candidates.size());
    std::vector<float> weights;
    weights.reserve(candidates.size());
    for (const AreaCandidate& candidate : candidates)
    {
        AreaCandidateEvaluation e;
        e.candidate = candidate;
        e.publishFactor = candidate.published ? settings.areaPublishedFactor : settings.areaUnpublishedFactor;
        e.reputation = settings.areaReputation * (std::max)(0.0f, candidate.reputation);
        if (!(e.publishFactor > 0.0f)) { e.reject = AreaReject::NotAvailable; }
        else if (candidate.memberCount <= 0) { e.reject = AreaReject::NoMember; }
        else if (candidate.usableCount <= 0) { e.reject = AreaReject::NoUsableMember; }
        else
        {
            // 距離: 道路で、実際に使える所属施設までの道のり(P2-0の距離の効き方。区画の中心への直線距離ではない)。
            e.distanceFactor = ComputeDistanceFactor(agent, candidate.nearestPathLength, settings, OutingReason::Area, 1.0f, &e.legacyDistance, &e.distanceScale);
            // 適合: 今の利用目的への、使える所属施設の供給量。頭打ち(店が増えても、過剰に有利にならない)。下限あり。
            const float cap = (std::max)(0.01f, settings.areaFitSupplyCap);
            e.fitFactor = Clamp(candidate.supplyForPurpose / cap, (std::max)(0.0f, settings.areaFitFloor), 1.0f);
            // 魅力: 用途の充実度(強い店がある用途の種類数)。1種類だけなら増えない。最大でも、増え方の分まで。
            const float coverage = static_cast<float>((std::max)(0, candidate.coveredPurposes - 1)) / static_cast<float>((std::max)(1, kUsagePurposeCount - 1));
            e.coverageFactor = 1.0f + (std::max)(0.0f, settings.areaCoverageGain) * Clamp(coverage, 0.0f, 1.0f);
            const float product = e.distanceFactor * e.publishFactor * e.fitFactor * e.coverageFactor * e.reputation;
            e.weight = (std::max)(product, settings.minimumWeight);
        }
        weights.push_back(e.weight);
        evals.push_back(e);
    }
    float total = 0.0f;
    for (float w : weights) { total += w; }
    if (total > 0.0f)
    {
        for (AreaCandidateEvaluation& e : evals) { e.chance = e.weight / total; }
    }
    const int picked = PickWeightedIndex(weights, roll);
    if (picked >= 0)
    {
        evals[static_cast<size_t>(picked)].chosen = true;
    }
    if (trace)
    {
        trace->roll = roll;
        trace->totalWeight = total;
        trace->pickedIndex = picked;
    }
    return picked;
}

void ComputeFacilityWeightBreakdown(const ResidentAgent& agent, const FacilityDefinition& facility, UsagePurpose purpose,
                                    float pathLength, const ResidentSimSettings& settings, FacilityWeightBreakdown& out,
                                    OutingReason reason, float distanceScaleFactor)
{
    const int purposeIndex = static_cast<int>(purpose);
    out.suitability = (purposeIndex >= 0 && purposeIndex < kUsagePurposeCount) ? facility.purposeSuitability[purposeIndex] : 1.0f;

    out.quality = (std::max)(0.01f, facility.quality);
    out.qualityExponent = settings.qualityExponent * agent.traits.qualityWeight;
    out.qualityFactor = std::pow(out.quality, out.qualityExponent);

    out.priceYen = static_cast<float>(facility.pricePerVisit);
    out.priceRatio = out.priceYen / (std::max)(1.0f, settings.priceReferenceYen);
    out.priceStrength = settings.priceStrength * agent.traits.priceSensitivity;
    out.priceFactor = 1.0f / (1.0f + out.priceStrength * out.priceRatio);

    // 距離: 道のり(マス基準に直した値)が、距離の目安の分だけ離れると、約37%(1/e)になる(形 k=1 の時。連続的で、しきい値は無い)。
    // 目安・形は、外出の理由ごと(手軽は、強く下がる。がっつりは、減り方が弱い)。
    const int reasonIndex = (std::max)(0, (std::min)(static_cast<int>(reason), kOutingReasonCount - 1));
    out.reason = static_cast<OutingReason>(reasonIndex);
    out.distanceFactor = ComputeDistanceFactor(agent, pathLength, settings, out.reason, distanceScaleFactor, &out.legacyDistance, &out.distanceScale, &out.distanceShape);

    out.product = (std::max)(0.0f, out.suitability) * out.qualityFactor * out.priceFactor * out.distanceFactor;
    // 到達できる候補を、無条件に0にしない(弱い候補も、ごくまれに選ばれる)。
    out.floorWeight = settings.minimumWeight;
    out.weight = (std::max)(out.product, out.floorWeight);
}

float ComputeFacilityWeight(const ResidentAgent& agent, const FacilityDefinition& facility, UsagePurpose purpose,
                            float pathLength, const ResidentSimSettings& settings, OutingReason reason, float distanceScaleFactor)
{
    FacilityWeightBreakdown breakdown;
    ComputeFacilityWeightBreakdown(agent, facility, purpose, pathLength, settings, breakdown, reason, distanceScaleFactor);
    return breakdown.weight;
}

float FacilityUseStaminaCost(const FacilityDefinition& facility, int useIndex, const ResidentSimSettings& settings)
{
    // 2軒目以降は、少しずつ、高くなる(複雑な疲労の仕組みは、持たない)。
    const float multiplier = 1.0f + (std::max)(0.0f, settings.additionalUseStaminaCostStep) * static_cast<float>((std::max)(0, useIndex));
    return facility.staminaCost * multiplier;
}

float RequiredStamina(const FacilityDefinition& facility, int useIndex, const ResidentSimSettings& settings)
{
    return FacilityUseStaminaCost(facility, useIndex, settings);
}

float ComputeRepeatVisitFactor(const ResidentAgent& agent, uint32_t facilityObjectId, bool needDrivenDaily, const ResidentSimSettings& settings)
{
    const int window = (std::max)(1, settings.repeatVisitWindow);
    const float base = Clamp(settings.repeatVisitWeightFactor, 0.0f, 1.0f);
    float factor = 1.0f;
    const size_t count = (std::min)(agent.history.size(), static_cast<size_t>(window));
    for (size_t i = 0; i < count; ++i)
    {
        if (agent.history[i].facilityObjectId != facilityObjectId)
        {
            continue;
        }
        // 直前(i=0)は base。window回前に近づくほど、1に近づく。
        const float t = static_cast<float>(i) / static_cast<float>(window);
        factor = (std::min)(factor, Lerp(base, 1.0f, t));
    }
    if (needDrivenDaily)
    {
        // 不足による日用品の外出は、必要な買い物なので、下げ幅を弱める。
        factor = Lerp(1.0f, factor, Clamp(settings.needDrivenDailyRepeatStrength, 0.0f, 1.0f));
    }
    return factor;
}

StopoverBlock ExplainStopoverBlock(const ResidentAgent& agent, float hour, const ResidentSimSettings& settings)
{
    // 判定の順序は、以前の CanConsiderStopover の && の並び(家・回数・時刻・体力)のまま。ここが唯一の定義。
    if (!(agent.homeObjectId != 0))                       { return StopoverBlock::NoHome; }
    if (!(agent.outingStopCount < settings.maxStopovers)) { return StopoverBlock::MaxStops; }
    if (!(hour < settings.stopoverLatestHour))            { return StopoverBlock::TooLate; }
    if (!(agent.stamina > 0.0f))                          { return StopoverBlock::LowStamina; } // 体力が0。少ない時は、確率が下がる(連続的)
    return StopoverBlock::None;
}

bool CanConsiderStopover(const ResidentAgent& agent, float hour, const ResidentSimSettings& settings)
{
    return ExplainStopoverBlock(agent, hour, settings) == StopoverBlock::None;
}

float ComputeStopoverChance(const ResidentAgent& agent, const ResidentSimSettings& settings, StopoverChanceBreakdown* out, float pull)
{
    StopoverChanceBreakdown b;
    const float decay = Clamp(settings.stopoverChanceDecay, 0.0f, 1.0f);
    b.baseChance = settings.stopoverBaseChance;
    b.countDecayFactor = std::pow(decay, static_cast<float>((std::max)(0, agent.outingStopCount)));
    b.staminaRatio = StaminaRatio(agent, settings);
    b.staminaFactor = std::pow(b.staminaRatio, (std::max)(0.0f, settings.stopoverStaminaExponent));
    b.pressure = ComputeNeedPressure(agent, settings);
    const float floorFactor = Clamp(settings.stopoverNeedFloor, 0.0f, 1.0f);
    b.needFactor = floorFactor + (1.0f - floorFactor) * b.pressure;
    const int reasonIndex = (std::max)(0, (std::min)(static_cast<int>(agent.outingReason), kOutingReasonCount - 1));
    b.reason = static_cast<OutingReason>(reasonIndex);
    b.reasonMultiplier = (std::max)(0.0f, settings.stopoverReasonMultiplier[reasonIndex]);
    // 引き寄せ(P2-C2): 近くに使える施設が複数あると、少し寄りやすい。控えめ(最大 +強さ)。体力・回数の係数は、上で既に掛かっている(二重に数えない)。
    b.pull = Clamp(pull, 0.0f, 1.0f);
    b.pullMultiplier = 1.0f + (std::max)(0.0f, settings.stopoverPullGain) * b.pull;
    b.chance = Clamp(b.baseChance * b.countDecayFactor * b.staminaFactor * b.needFactor * b.reasonMultiplier * b.pullMultiplier, 0.0f, 1.0f);
    if (out)
    {
        *out = b;
    }
    return b.chance;
}

namespace
{
    // 候補の、最大の用途適性(元の値)。
    float MaxSuitability(const FacilityDefinition& facility)
    {
        float best = 0.0f;
        for (int p = 0; p < kUsagePurposeCount; ++p) { best = (std::max)(best, facility.purposeSuitability[p]); }
        return best;
    }

    // 候補のカテゴリの需要の不足の割合(0〜1)。
    float NeedGapOf(const ResidentAgent& agent, FacilityCategory category)
    {
        const float need = (category == FacilityCategory::Daily) ? agent.lifeNeed : agent.activityNeed;
        return Clamp(1.0f - need / 100.0f, 0.0f, 1.0f);
    }

    float NoveltyOf(const FacilityDefinition& candidate, UsagePurpose lastPurpose)
    {
        const float best = MaxSuitability(candidate);
        if (!(best > 0.0f)) { return 0.0f; }
        const int index = static_cast<int>(lastPurpose);
        const float same = (index >= 0 && index < kUsagePurposeCount) ? candidate.purposeSuitability[index] : best;
        return Clamp(1.0f - same / best, 0.0f, 1.0f);
    }
}

float ComputeComplementFactor(const ResidentAgent& agent, const FacilityDefinition& candidate, UsagePurpose lastPurpose,
                              const ResidentSimSettings& settings, ComplementBreakdown* out)
{
    ComplementBreakdown b;
    b.novelty = NoveltyOf(candidate, lastPurpose);
    b.needGap = NeedGapOf(agent, candidate.category);
    const float floorTerm = Clamp(settings.complementNeedFloor, 0.0f, 1.0f);
    b.needTerm = floorTerm + (1.0f - floorTerm) * b.needGap;
    b.factor = 1.0f + (std::max)(0.0f, settings.complementGain) * b.novelty * b.needTerm;
    if (out) { *out = b; }
    return b.factor;
}

float ComputeStopoverAffinity(float complementFactor, float areaBoost, const ResidentSimSettings& settings)
{
    const float product = (std::max)(0.0f, complementFactor) * (std::max)(0.0f, areaBoost);
    return (std::min)(product, (std::max)(1.0f, settings.stopoverAffinityCap));
}

float ComputeStopoverPull(const ResidentAgent& agent, UsagePurpose lastPurpose, const std::vector<StopoverPullCandidate>& nearby,
                          int visitsThisOuting, const ResidentSimSettings& settings, StopoverPullBreakdown* out)
{
    StopoverPullBreakdown b;
    const int useIndex = (std::max)(0, visitsThisOuting);
    const float noveltyFloor = Clamp(settings.stopoverPullNoveltyFloor, 0.0f, 1.0f);
    const float floorTerm = Clamp(settings.complementNeedFloor, 0.0f, 1.0f);
    for (const StopoverPullCandidate& c : nearby)
    {
        // 今の外出で、もう使った施設は、数えない(戻らない)。
        bool visited = false;
        for (size_t i = 0; i < agent.history.size() && static_cast<int>(i) < visitsThisOuting; ++i)
        {
            if (agent.history[i].facilityObjectId == c.objectId) { visited = true; break; }
        }
        const FacilityDefinition* facility = FindFacilityDefinition(c.type);
        if (visited || facility == nullptr) { continue; }
        ++b.considered;
        // 体力が足りない施設は、使えない(利用に必要な体力)。
        if (agent.stamina < RequiredStamina(*facility, useIndex, settings)) { continue; }
        ++b.usable;
        const float distanceFactor = ComputeDistanceFactor(agent, c.distance, settings, agent.outingReason, settings.stopoverDistanceScaleFactor);
        const float needTerm = floorTerm + (1.0f - floorTerm) * NeedGapOf(agent, facility->category);
        const float suit = Clamp(MaxSuitability(*facility) / 2.0f, 0.0f, 1.0f);
        const float noveltyTerm = noveltyFloor + (1.0f - noveltyFloor) * NoveltyOf(*facility, lastPurpose);
        b.sum += distanceFactor * needTerm * suit * noveltyTerm;
    }
    b.pull = 1.0f - std::exp(-(std::max)(0.0f, settings.stopoverPullSaturation) * b.sum);
    b.multiplier = 1.0f + (std::max)(0.0f, settings.stopoverPullGain) * b.pull;
    if (out) { *out = b; }
    return b.pull;
}

float ComputeFoodChainAcceptChance(const ResidentAgent& agent, const FacilityDefinition& candidate, const ResidentSimSettings& settings)
{
    if (candidate.foodLike && agent.usedFoodLikeFacilityThisOuting)
    {
        return Clamp(settings.foodChainAcceptChance, 0.0f, 1.0f);
    }
    return 1.0f;
}

int PickWeightedIndex(const std::vector<float>& weights, float unit)
{
    float total = 0.0f;
    for (float w : weights) { if (w > 0.0f) { total += w; } }
    if (!(total > 0.0f))
    {
        return -1;
    }
    float target = Clamp(unit, 0.0f, 0.99999994f) * total;
    int lastPositive = -1;
    for (size_t i = 0; i < weights.size(); ++i)
    {
        if (!(weights[i] > 0.0f)) { continue; }
        lastPositive = static_cast<int>(i);
        if (target < weights[i]) { return static_cast<int>(i); }
        target -= weights[i];
    }
    return lastPositive; // 丸め誤差で、最後まで来た時
}

int ChooseFacility(const ResidentAgent& agent, const std::vector<FacilityCandidate>& candidates, const OutingPurposeChoice& purpose,
                   const ResidentSimSettings& settings, SimRandom& random, std::vector<CandidateEvaluation>& evaluations,
                   int visitsThisOuting, FacilityChoiceTrace* outTrace)
{
    evaluations.clear();
    evaluations.reserve(candidates.size());
    std::vector<float> weights;
    weights.reserve(candidates.size());

    // 不足による日用品の外出は、必要な買い物を妨げないよう、利用履歴による下げ幅を弱める。
    const bool needDrivenDaily = purpose.needDriven && purpose.category == FacilityCategory::Daily;
    // 立ち寄り(2軒目以降)かどうか: 利用の何軒目か(体力の消費の増え方)と、距離の目安の倍率に使う。
    const int useIndex = (std::max)(0, visitsThisOuting);
    const float distanceScaleFactor = (useIndex > 0) ? settings.stopoverDistanceScaleFactor : 1.0f;

    for (const FacilityCandidate& candidate : candidates)
    {
        CandidateEvaluation evaluation;
        evaluation.candidate = candidate;
        const FacilityDefinition* facility = FindFacilityDefinition(candidate.type);

        // 今の外出で、もう使った施設か(履歴の新しい方から、visitsThisOuting件)。
        bool visitedThisOuting = false;
        for (size_t i = 0; i < agent.history.size() && static_cast<int>(i) < visitsThisOuting; ++i)
        {
            if (agent.history[i].facilityObjectId == candidate.objectId) { visitedThisOuting = true; break; }
        }

        if (facility == nullptr || facility->category != purpose.category)
        {
            evaluation.reject = CandidateReject::WrongCategory;
        }
        else if (visitedThisOuting)
        {
            evaluation.reject = CandidateReject::VisitedThisOuting;
        }
        else if (candidate.outOfRange && settings.stopoverMaxDistanceLegacyCells > 0.0f)
        {
            evaluation.reject = CandidateReject::OutOfRange;
        }
        else if (!candidate.reachable)
        {
            evaluation.reject = CandidateReject::Unreachable;
        }
        else
        {
            // 体力の判定: その施設を使うのに必要な体力(P2-0: 移動の分は無い。2軒目以降は、少し高い)。
            evaluation.requiredStamina = RequiredStamina(*facility, useIndex, settings);
            if (agent.stamina < evaluation.requiredStamina)
            {
                evaluation.reject = CandidateReject::NotEnoughStamina;
            }
            else
            {
                evaluation.repeatFactor = ComputeRepeatVisitFactor(agent, candidate.objectId, needDrivenDaily, settings);
                ComputeFacilityWeightBreakdown(agent, *facility, purpose.purpose, candidate.pathLength, settings, evaluation.breakdown,
                                               purpose.reason, distanceScaleFactor);
                evaluation.hasBreakdown = true;
                const float baseWeight = evaluation.breakdown.weight;
                // 立ち寄り(2軒目以降)だけ、補完性・エリア内の補正(頭打ちあり)を掛ける。主目的の施設選びでは1。
                evaluation.affinity = (useIndex > 0) ? ComputeStopoverAffinity(candidate.complementFactor, candidate.areaBoost, settings) : 1.0f;
                evaluation.weightAfterRepeat = baseWeight * evaluation.repeatFactor * evaluation.affinity;
                // 履歴で下げても、到達できる候補を、無条件に0にはしない(下限を守る)。
                evaluation.weight = (std::max)(evaluation.weightAfterRepeat, settings.minimumWeight);
            }
        }
        weights.push_back(evaluation.weight);
        evaluations.push_back(evaluation);
    }

    float total = 0.0f;
    for (float w : weights) { total += w; }
    if (total > 0.0f)
    {
        for (CandidateEvaluation& evaluation : evaluations) { evaluation.chance = evaluation.weight / total; }
    }

    // 抽選の累積の範囲(重みを順に足した値)。PickWeightedIndexと同じく、正の重みだけを足す。
    float positiveTotal = 0.0f;
    for (CandidateEvaluation& evaluation : evaluations)
    {
        evaluation.rangeBegin = positiveTotal;
        if (evaluation.weight > 0.0f) { positiveTotal += evaluation.weight; }
        evaluation.rangeEnd = positiveTotal;
    }

    const float unit = random.Next01();
    const int picked = PickWeightedIndex(weights, unit);
    if (picked >= 0)
    {
        evaluations[static_cast<size_t>(picked)].chosen = true;
    }
    if (outTrace)
    {
        outTrace->randomUnit = unit;
        outTrace->totalWeight = total;
        outTrace->pickTarget = Clamp(unit, 0.0f, 0.99999994f) * positiveTotal;
        outTrace->pickedIndex = picked;
    }
    return picked;
}

// ---------------------------------------------------------------------------
// 利用の結果・時間・週
// ---------------------------------------------------------------------------
void ApplyFacilityUse(ResidentAgent& agent, const FacilityDefinition& facility, uint32_t facilityObjectId, int weekIndex, float hour,
                      const ResidentSimSettings& settings, int useIndex)
{
    agent.lifeNeed = Clamp(agent.lifeNeed + facility.lifeRecovery, 0.0f, 100.0f);
    agent.activityNeed = Clamp(agent.activityNeed + facility.activityRecovery, 0.0f, 100.0f);
    agent.stamina = (std::max)(0.0f, agent.stamina - FacilityUseStaminaCost(facility, useIndex, settings));

    VisitRecord record;
    record.facilityObjectId = facilityObjectId;
    record.weekIndex = weekIndex;
    record.hour = hour;
    agent.history.insert(agent.history.begin(), record);
    if (agent.history.size() > ResidentAgent::kMaxHistory)
    {
        agent.history.resize(ResidentAgent::kMaxHistory);
    }
}

void AdvanceResidentTime(ResidentAgent& agent, float deltaHours, const ResidentSimSettings& settings)
{
    if (!(deltaHours > 0.0f))
    {
        return;
    }
    const bool lifeUnmet = IsLifeNeedUnmet(agent, settings);
    const bool activityUnmet = IsActivityNeedUnmet(agent, settings);
    // 「不足が続いている時間」「充足が続いている時間」。状態が変わったら、数え直す(続いた時間だけを見るため)。
    agent.unmetLifeHours = lifeUnmet ? agent.unmetLifeHours + deltaHours : 0.0f;
    agent.unmetActivityHours = activityUnmet ? agent.unmetActivityHours + deltaHours : 0.0f;
    agent.satisfiedHours = (!lifeUnmet && !activityUnmet) ? agent.satisfiedHours + deltaHours : 0.0f;

    // 家にいる間は、体力が少しずつ回復する(上限まで)。
    if (agent.state == ResidentAgentState::AtHome)
    {
        const float cap = EffectiveStaminaCap(agent, settings);
        agent.stamina = (std::min)(cap, agent.stamina + settings.staminaHomeRecoveryPerHour * deltaHours);
    }
}

void ProcessResidentWeekEnd(ResidentAgent& agent, const ResidentSimSettings& settings)
{
    // 1. ペナルティの判定(自然減少を反映する前に行う)。
    //    不足が猶予を超えて続き、かつ、週末になっても改善できていない(今も不足)時だけ、悪化する。週末までに改善できれば、回避できる。
    const bool unmetTooLong =
        (IsLifeNeedUnmet(agent, settings) && agent.unmetLifeHours > settings.unmetGraceHours) ||
        (IsActivityNeedUnmet(agent, settings) && agent.unmetActivityHours > settings.unmetGraceHours);
    if (unmetTooLong)
    {
        agent.penaltyLevel = (std::min)(settings.maxPenaltyLevel, agent.penaltyLevel + 1);
    }
    else if (agent.satisfiedHours >= settings.recoverSatisfiedHours && agent.penaltyLevel > 0)
    {
        agent.penaltyLevel -= 1;
    }

    // 2. 需要の自然減少(週末に更新)。
    agent.lifeNeed = Clamp(agent.lifeNeed - settings.weeklyDecayLife * agent.traits.needDecayScale, 0.0f, 100.0f);
    agent.activityNeed = Clamp(agent.activityNeed - settings.weeklyDecayActivity * agent.traits.needDecayScale, 0.0f, 100.0f);

    // 3. 継続時間は、判定が済んだので0へ(新しい週の「不足」は、ここから数える)。
    agent.unmetLifeHours = 0.0f;
    agent.unmetActivityHours = 0.0f;
    agent.satisfiedHours = 0.0f;
}

void ProcessResidentWeekStart(ResidentAgent& agent, const ResidentSimSettings& settings)
{
    const float cap = EffectiveStaminaCap(agent, settings);
    agent.stamina = (std::min)(cap, agent.stamina + settings.staminaSleepRecovery);
}

DirectX::XMFLOAT2 HomelessOffset(int index, float spacing)
{
    if (index <= 0)
    {
        return DirectX::XMFLOAT2(0.0f, 0.0f);
    }
    // ひまわりの種の並び(黄金角で回し、半径は平方根で増やす)。隣り合う点の間隔が、ほぼ一定になる。
    constexpr float kGoldenAngle = 2.39996323f;
    const float radius = spacing * std::sqrt(static_cast<float>(index));
    const float angle = kGoldenAngle * static_cast<float>(index);
    return DirectX::XMFLOAT2(std::cos(angle) * radius, std::sin(angle) * radius);
}

void ApplyRoadLossPenalty(ResidentAgent& agent, bool stranded, const ResidentSimSettings& settings)
{
    const float penalty = stranded ? settings.roadLossStrandedSatisfactionPenalty : settings.roadLossRerouteSatisfactionPenalty;
    agent.satisfaction = (std::max)(0.0f, agent.satisfaction - (std::max)(0.0f, penalty));
}
