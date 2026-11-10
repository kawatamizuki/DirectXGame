#include "CommercialArea.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include "FacilityData.h"

namespace
{
    struct Point
    {
        double x;
        double z;
    };

    // 多角形を、半平面 (axis=0:X / 1:Z)の (sign * (座標 - limit) <= 0) の側へ切り取る(Sutherland–Hodgman)。
    std::vector<Point> ClipHalfPlane(const std::vector<Point>& polygon, int axis, double limit, double sign)
    {
        std::vector<Point> out;
        if (polygon.empty())
        {
            return out;
        }
        auto value = [&](const Point& p) { return sign * ((axis == 0 ? p.x : p.z) - limit); };
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const Point& a = polygon[i];
            const Point& b = polygon[(i + 1) % polygon.size()];
            const double va = value(a);
            const double vb = value(b);
            const bool aIn = va <= 0.0;
            const bool bIn = vb <= 0.0;
            if (aIn)
            {
                out.push_back(a);
            }
            if (aIn != bIn)
            {
                const double t = va / (va - vb);
                out.push_back({ a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t });
            }
        }
        return out;
    }

    double PolygonArea(const std::vector<Point>& polygon)
    {
        if (polygon.size() < 3)
        {
            return 0.0;
        }
        double sum = 0.0;
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const Point& a = polygon[i];
            const Point& b = polygon[(i + 1) % polygon.size()];
            sum += a.x * b.z - b.x * a.z;
        }
        return std::fabs(sum) * 0.5;
    }
}

AreaRect MakeAreaRect(float centerX, float centerZ, float side, float cellSize)
{
    float minX = centerX - side * 0.5f;
    float minZ = centerZ - side * 0.5f;
    if (cellSize > 0.0f)
    {
        minX = std::round(minX / cellSize) * cellSize;
        minZ = std::round(minZ / cellSize) * cellSize;
    }
    AreaRect rect;
    rect.minX = minX;
    rect.minZ = minZ;
    rect.maxX = minX + side;
    rect.maxZ = minZ + side;
    return rect;
}

float FootprintArea(const OrientedRect& footprint)
{
    return (footprint.width > 0.0f && footprint.depth > 0.0f) ? footprint.width * footprint.depth : 0.0f;
}

float OverlapArea(const OrientedRect& footprint, const AreaRect& rect)
{
    if (!(FootprintArea(footprint) > 0.0f) || !(rect.Area() > 0.0f))
    {
        return 0.0f;
    }
    // 敷地の4隅(OrientedRectの規約: 原点から、+X方向に幅、+Z方向に奥行き。yaw で回す。OrientedRectsOverlap と同じ)。
    const double c = std::cos(static_cast<double>(footprint.yaw));
    const double s = std::sin(static_cast<double>(footprint.yaw));
    const double ax = c, az = -s;           // ローカル+X
    const double bx = s, bz = c;            // ローカル+Z
    const double w = footprint.width, d = footprint.depth;
    const double ox = footprint.origin.x, oz = footprint.origin.z;
    std::vector<Point> polygon = {
        { ox, oz },
        { ox + ax * w, oz + az * w },
        { ox + ax * w + bx * d, oz + az * w + bz * d },
        { ox + bx * d, oz + bz * d }
    };
    polygon = ClipHalfPlane(polygon, 0, rect.minX, -1.0);   // x >= minX
    polygon = ClipHalfPlane(polygon, 0, rect.maxX, 1.0);    // x <= maxX
    polygon = ClipHalfPlane(polygon, 1, rect.minZ, -1.0);   // z >= minZ
    polygon = ClipHalfPlane(polygon, 1, rect.maxZ, 1.0);    // z <= maxZ
    return static_cast<float>(PolygonArea(polygon));
}

float OverlapRatio(const OrientedRect& footprint, const AreaRect& rect)
{
    const float area = FootprintArea(footprint);
    if (!(area > 0.0f))
    {
        return 0.0f;
    }
    return (std::min)(1.0f, OverlapArea(footprint, rect) / area);
}

bool AreaRectsOverlap(const AreaRect& a, const AreaRect& b, float epsilon)
{
    return a.minX < b.maxX - epsilon && b.minX < a.maxX - epsilon
        && a.minZ < b.maxZ - epsilon && b.minZ < a.maxZ - epsilon;
}

bool CanPlaceAreaRect(const AreaRect& rect, const std::vector<CommercialArea>& areas, uint32_t ignoreId, float epsilon)
{
    if (!(rect.Area() > 0.0f))
    {
        return false;
    }
    for (const CommercialArea& other : areas)
    {
        if (other.id == ignoreId)
        {
            continue;
        }
        if (AreaRectsOverlap(rect, other.rect, epsilon))
        {
            return false;
        }
    }
    return true;
}

bool IsAreaFacilityType(BuildingType type)
{
    return FindFacilityDefinition(type) != nullptr;
}

std::vector<AreaMembers> ComputeAreaMembers(const std::vector<CommercialArea>& areas, const std::vector<AreaFacility>& facilities,
                                            const CommercialAreaSettings& settings)
{
    std::vector<AreaMembers> result(areas.size());
    for (size_t i = 0; i < areas.size(); ++i)
    {
        result[i].areaId = areas[i].id;
    }
    constexpr float kTieEpsilon = 1e-6f;
    const float threshold = (std::max)(0.0f, settings.membershipMinOverlap);

    for (const AreaFacility& facility : facilities)
    {
        if (!IsAreaFacilityType(facility.type))
        {
            continue;
        }
        // 条件(敷地面積のうち、区画の中にある割合が しきい値以上)を満たす区画の中で、割合が一番大きい区画(同じなら、IDが小さい区画)。
        int best = -1;
        float bestRatio = 0.0f;
        for (size_t i = 0; i < areas.size(); ++i)
        {
            const float ratio = OverlapRatio(facility.footprint, areas[i].rect);
            if (!(ratio > 0.0f) || ratio + kTieEpsilon < threshold)
            {
                continue;
            }
            const bool better = (best < 0)
                || (ratio > bestRatio + kTieEpsilon)
                || (std::fabs(ratio - bestRatio) <= kTieEpsilon && areas[i].id < areas[static_cast<size_t>(best)].id);
            if (better)
            {
                best = static_cast<int>(i);
                bestRatio = ratio;
            }
        }
        if (best >= 0)
        {
            result[static_cast<size_t>(best)].facilityIds.push_back(facility.objectId);
            result[static_cast<size_t>(best)].overlapRatios.push_back(bestRatio);
        }
    }
    // 施設の並びに依らず、結果が同じになるように、idの小さい順に並べる。
    for (AreaMembers& members : result)
    {
        std::vector<size_t> order(members.facilityIds.size());
        for (size_t i = 0; i < order.size(); ++i) { order[i] = i; }
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return members.facilityIds[a] < members.facilityIds[b]; });
        std::vector<uint32_t> ids;
        std::vector<float> ratios;
        for (size_t index : order)
        {
            ids.push_back(members.facilityIds[index]);
            ratios.push_back(members.overlapRatios[index]);
        }
        members.facilityIds = std::move(ids);
        members.overlapRatios = std::move(ratios);
    }
    return result;
}

AreaComposition ComputeAreaComposition(const std::vector<BuildingType>& memberTypes, const CommercialAreaSettings& settings)
{
    AreaComposition result;
    for (BuildingType type : memberTypes)
    {
        const FacilityDefinition* facility = FindFacilityDefinition(type);
        if (!facility)
        {
            continue;
        }
        ++result.storeCount;
        for (int p = 0; p < kUsagePurposeCount; ++p)
        {
            const float suitability = (std::max)(0.0f, facility->purposeSuitability[p]);    // 元の適性値
            result.supply[p] += suitability;
            if (suitability >= settings.strongSuitability)
            {
                ++result.strongCount[p];
            }
        }
    }
    if (result.storeCount == 0)
    {
        return result;
    }

    // 割合(供給量の分布)。合計が0(適性が全て0の施設だけ)なら、全て0のまま。
    float total = 0.0f;
    for (int p = 0; p < kUsagePurposeCount; ++p) { total += result.supply[p]; }
    float maxShare = 0.0f;
    if (total > 0.0f)
    {
        for (int p = 0; p < kUsagePurposeCount; ++p)
        {
            result.share[p] = result.supply[p] / total;
            maxShare = (std::max)(maxShare, result.share[p]);
        }
    }
    result.dailyShare = result.share[static_cast<int>(UsagePurpose::TopUp)] + result.share[static_cast<int>(UsagePurpose::BulkBuy)];

    // 専門性: 一番大きい割合が、均等(1÷用途の数)から、どれだけ離れているか(0〜1)。複雑な式を使わない。
    const float even = 1.0f / static_cast<float>(kUsagePurposeCount);
    result.specialization = (std::min)(1.0f, (std::max)(0.0f, (maxShare - even) / (1.0f - even)));

    for (int p = 0; p < kUsagePurposeCount; ++p)
    {
        if (result.strongCount[p] >= 1) { ++result.coveredPurposes; }
        result.overlapStores += (std::max)(0, result.strongCount[p] - 1);
    }

    // 特色のラベル(日用品寄りの割合の、しきい値)。優劣を示さない。
    if (total <= 0.0f) { result.character = AreaCharacter::Mixed; }
    else if (result.dailyShare >= settings.dailyFocusedShare) { result.character = AreaCharacter::DailyFocused; }
    else if (result.dailyShare <= settings.leisureFocusedShare) { result.character = AreaCharacter::LeisureFocused; }
    else { result.character = AreaCharacter::Mixed; }
    return result;
}

const char* AreaCharacterName(AreaCharacter character)
{
    switch (character)
    {
    case AreaCharacter::None:           return "店なし";
    case AreaCharacter::DailyFocused:   return "日用品特化";
    case AreaCharacter::Mixed:          return "混合";
    case AreaCharacter::LeisureFocused: return "娯楽特化";
    }
    return "?";
}

AreaThresholds ComputeAreaThresholds(int plannedCapacity, const CommercialAreaSettings& settings)
{
    AreaThresholds t;
    t.minStores = (std::max)(0, settings.minProgressStores);
    if (plannedCapacity <= 0)
    {
        // 計画規模が無い(0): 完成は、ない。拡大は、最低進捗以上。
        t.completeStores = 0;
        t.expandedStores = (std::max)(t.minStores, settings.expandedRangeStores);
        return t;
    }
    t.completeStores = (std::max)(plannedCapacity, t.minStores);
    t.expandedStores = (std::max)(t.minStores, (std::min)(settings.expandedRangeStores, t.completeStores));
    return t;
}

const char* DescribeAreaSettingsConflict(const CommercialAreaSettings& settings)
{
    if (settings.plannedCapacity < settings.minProgressStores)
    {
        return "計画規模が最低進捗より小さい: 判定では、計画の完成を最低進捗まで引き上げて扱う(最低進捗を満たさずに、完成にならない)";
    }
    if (settings.expandedRangeStores < settings.minProgressStores || settings.expandedRangeStores > settings.plannedCapacity)
    {
        return "来訪範囲の拡大の店舗数が、最低進捗〜計画規模の外: 判定では、その間に収めて扱う";
    }
    return nullptr;
}

AreaProgress ComputeAreaProgress(int storeCount, int plannedCapacity, const CommercialAreaSettings& settings)
{
    AreaProgress progress;
    const AreaThresholds t = ComputeAreaThresholds(plannedCapacity, settings);
    progress.storeCount = (std::max)(0, storeCount);
    progress.plannedCapacity = t.completeStores;      // 整合させた計画規模(計画規模が最低進捗より小さければ、最低進捗)
    progress.ratio = progress.plannedCapacity > 0
        ? (std::min)(1.0f, static_cast<float>(progress.storeCount) / static_cast<float>(progress.plannedCapacity))
        : 0.0f;
    // 段階は、順序が保たれた、整合したしきい値で判定する(最低進捗を満たさずに、拡大・完成にならない)。
    if (t.completeStores > 0 && progress.storeCount >= t.completeStores)
    {
        progress.current = AreaMilestone::Complete;
    }
    else if (progress.storeCount >= t.expandedStores && progress.storeCount >= t.minStores)
    {
        progress.current = AreaMilestone::Expanded;
    }
    else if (progress.storeCount >= t.minStores)
    {
        progress.current = AreaMilestone::Minimum;
    }
    return progress;
}

int GraceWeeksLeft(const CommercialArea& area, int currentWeek, const CommercialAreaSettings& settings)
{
    if (!area.published)
    {
        return settings.graceWeeks;
    }
    return settings.graceWeeks - (currentWeek - area.publishedWeek);
}

const char* AreaMilestoneName(AreaMilestone milestone)
{
    switch (milestone)
    {
    case AreaMilestone::None:     return "未達成";
    case AreaMilestone::Minimum:  return "最低進捗";
    case AreaMilestone::Expanded: return "来訪範囲の拡大";
    case AreaMilestone::Complete: return "計画の完成";
    }
    return "?";
}

const char* AreaHistoryKindName(AreaHistoryKind kind)
{
    switch (kind)
    {
    case AreaHistoryKind::Published: return "周知した";
    case AreaHistoryKind::Moved:     return "動かした";
    case AreaHistoryKind::Resized:   return "規模を変えた";
    case AreaHistoryKind::Withdrawn: return "撤回した";
    }
    return "?";
}

std::vector<const char*> ValidateAreas(const std::vector<CommercialArea>& areas, const std::vector<AreaMembers>& members, float epsilon)
{
    std::vector<const char*> problems;
    std::unordered_set<uint32_t> ids;
    for (size_t i = 0; i < areas.size(); ++i)
    {
        if (areas[i].id == 0 || !ids.insert(areas[i].id).second)
        {
            problems.push_back("区画のIDが0、または重複している");
            break;
        }
    }
    bool overlapFound = false;
    for (size_t i = 0; i < areas.size() && !overlapFound; ++i)
    {
        for (size_t j = i + 1; j < areas.size(); ++j)
        {
            if (AreaRectsOverlap(areas[i].rect, areas[j].rect, epsilon))
            {
                problems.push_back("区画どうしが重なっている");
                overlapFound = true;
                break;
            }
        }
    }
    std::unordered_set<uint32_t> seen;
    for (const AreaMembers& area : members)
    {
        for (uint32_t id : area.facilityIds)
        {
            if (!seen.insert(id).second)
            {
                problems.push_back("施設が、2つ以上の区画に所属している(二重所属)");
                return problems;
            }
        }
    }
    return problems;
}
