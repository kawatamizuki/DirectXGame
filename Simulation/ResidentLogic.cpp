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

void InitializeResident(ResidentAgent& agent, uint32_t seed, const ResidentSimSettings& settings)
{
    agent.traits = MakeResidentTraits(seed, settings.staminaMax);
    agent.lifeNeed = Lerp(settings.initialNeedMin, settings.initialNeedMax, HashUnit(seed, 11));
    agent.activityNeed = Lerp(settings.initialNeedMin, settings.initialNeedMax, HashUnit(seed, 12));
    agent.penaltyLevel = 0;
    agent.stamina = EffectiveStaminaCap(agent, settings);
    agent.unmetLifeHours = 0.0f;
    agent.unmetActivityHours = 0.0f;
    agent.satisfiedHours = 0.0f;
    agent.history.clear();
}

// ---------------------------------------------------------------------------
// 外出の目的
// ---------------------------------------------------------------------------
OutingPurposeChoice ChoosePurpose(const ResidentAgent& agent, const ResidentSimSettings& settings, SimRandom& random)
{
    OutingPurposeChoice choice;
    const bool lifeUnmet = IsLifeNeedUnmet(agent, settings);
    const bool activityUnmet = IsActivityNeedUnmet(agent, settings);
    choice.needDriven = lifeUnmet || activityUnmet;

    if (choice.needDriven)
    {
        // 両方が不足なら、より不足している方(同じなら乱数)を優先する。
        if (lifeUnmet && activityUnmet)
        {
            if (std::fabs(agent.lifeNeed - agent.activityNeed) < 0.001f) { choice.category = (random.Next01() < 0.5f) ? FacilityCategory::Daily : FacilityCategory::Leisure; }
            else { choice.category = (agent.lifeNeed < agent.activityNeed) ? FacilityCategory::Daily : FacilityCategory::Leisure; }
        }
        else
        {
            choice.category = lifeUnmet ? FacilityCategory::Daily : FacilityCategory::Leisure;
        }
    }
    else
    {
        // 自発的な外出。需要の回復に限らないので、娯楽を多めに。
        choice.category = (random.Next01() < 0.7f) ? FacilityCategory::Leisure : FacilityCategory::Daily;
    }

    if (choice.category == FacilityCategory::Daily)
    {
        // 生活が大きく不足しているほど、まとめ買い。そうでなければ、買い足し。
        const float deficit = Clamp((settings.needSatisfiedThreshold - agent.lifeNeed) / settings.needSatisfiedThreshold, 0.0f, 1.0f);
        const float bulkChance = choice.needDriven ? (0.2f + deficit) : 0.3f;
        choice.purpose = (random.Next01() < bulkChance) ? UsagePurpose::BulkBuy : UsagePurpose::TopUp;
    }
    else
    {
        choice.purpose = (random.Next01() < 0.5f) ? UsagePurpose::Movie : UsagePurpose::Light;
    }
    return choice;
}

// ---------------------------------------------------------------------------
// 施設の選び方
// ---------------------------------------------------------------------------
float ComputeFacilityWeight(const ResidentAgent& agent, const FacilityDefinition& facility, UsagePurpose purpose,
                            float pathLength, const ResidentSimSettings& settings)
{
    const int purposeIndex = static_cast<int>(purpose);
    const float suitability = (purposeIndex >= 0 && purposeIndex < kUsagePurposeCount) ? facility.purposeSuitability[purposeIndex] : 1.0f;

    const float quality = (std::max)(0.01f, facility.quality);
    const float qualityFactor = std::pow(quality, settings.qualityExponent * agent.traits.qualityWeight);

    const float priceRatio = static_cast<float>(facility.pricePerVisit) / (std::max)(1.0f, settings.priceReferenceYen);
    const float priceFactor = 1.0f / (1.0f + settings.priceStrength * agent.traits.priceSensitivity * priceRatio);

    // 距離: 道のり(マス基準に直した値)が、距離の目安の分だけ離れるごとに、約37%(1/e)になる。
    const float legacyDistance = pathLength / WorldScale::FromLegacy(1.0f);
    const float scale = (std::max)(0.1f, settings.distanceScaleLegacyCells * agent.traits.distanceTolerance);
    const float distanceFactor = std::exp(-(std::max)(0.0f, legacyDistance) / scale);

    const float weight = (std::max)(0.0f, suitability) * qualityFactor * priceFactor * distanceFactor;
    // 到達できる候補を、無条件に0にしない(弱い候補も、ごくまれに選ばれる)。
    return (std::max)(weight, settings.minimumWeight);
}

float RequiredStamina(const FacilityDefinition& facility, float pathLength, const ResidentSimSettings& settings)
{
    const float legacyDistance = pathLength / WorldScale::FromLegacy(1.0f);
    return facility.staminaCost + 2.0f * (std::max)(0.0f, legacyDistance) * settings.staminaPerLegacyCell;
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
                   const ResidentSimSettings& settings, SimRandom& random, std::vector<CandidateEvaluation>& evaluations)
{
    evaluations.clear();
    evaluations.reserve(candidates.size());
    std::vector<float> weights;
    weights.reserve(candidates.size());

    for (const FacilityCandidate& candidate : candidates)
    {
        CandidateEvaluation evaluation;
        evaluation.candidate = candidate;
        const FacilityDefinition* facility = FindFacilityDefinition(candidate.type);

        if (facility == nullptr || facility->category != purpose.category)
        {
            evaluation.reject = CandidateReject::WrongCategory;
        }
        else if (!candidate.reachable)
        {
            evaluation.reject = CandidateReject::Unreachable;
        }
        else if (agent.stamina < RequiredStamina(*facility, candidate.pathLength, settings))
        {
            evaluation.reject = CandidateReject::NotEnoughStamina;
        }
        else
        {
            evaluation.weight = ComputeFacilityWeight(agent, *facility, purpose.purpose, candidate.pathLength, settings);
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

    const int picked = PickWeightedIndex(weights, random.Next01());
    if (picked >= 0)
    {
        evaluations[static_cast<size_t>(picked)].chosen = true;
    }
    return picked;
}

// ---------------------------------------------------------------------------
// 利用の結果・時間・週
// ---------------------------------------------------------------------------
void ApplyFacilityUse(ResidentAgent& agent, const FacilityDefinition& facility, uint32_t facilityObjectId, int weekIndex, float hour,
                      const ResidentSimSettings& /*settings*/)
{
    agent.lifeNeed = Clamp(agent.lifeNeed + facility.lifeRecovery, 0.0f, 100.0f);
    agent.activityNeed = Clamp(agent.activityNeed + facility.activityRecovery, 0.0f, 100.0f);
    agent.stamina = (std::max)(0.0f, agent.stamina - facility.staminaCost);

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
