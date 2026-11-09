#include "CommercialAreaManager.h"
#include <algorithm>
#include "WorldScale.h"

namespace
{
    // 保存の整数(列挙型)を、範囲内に収めて読む。範囲外・無い値は、既定。
    template <typename Enum>
    Enum ReadEnum(const Json& json, const char* key, int maxValue, Enum defaultValue)
    {
        const int value = json.GetInt(key, static_cast<int>(defaultValue));
        return (value >= 0 && value <= maxValue) ? static_cast<Enum>(value) : defaultValue;
    }
}

const CommercialArea* CommercialAreaManager::Find(uint32_t id) const
{
    for (const CommercialArea& area : m_areas)
    {
        if (area.id == id) { return &area; }
    }
    return nullptr;
}

CommercialArea* CommercialAreaManager::FindMutable(uint32_t id)
{
    for (CommercialArea& area : m_areas)
    {
        if (area.id == id) { return &area; }
    }
    return nullptr;
}

float CommercialAreaManager::SideLength() const
{
    return WorldScale::FromLegacy((std::max)(1.0f, m_settings.sideLegacyCells));
}

uint32_t CommercialAreaManager::PlaceArea(float centerX, float centerZ, float cellSize)
{
    const AreaRect rect = MakeAreaRect(centerX, centerZ, SideLength(), cellSize);
    if (!CanPlaceAreaRect(rect, m_areas, 0, m_settings.placementEpsilon))
    {
        return 0;
    }
    CommercialArea area;
    area.id = m_nextId++;
    area.rect = rect;
    area.plannedCapacity = m_settings.plannedCapacity;
    m_areas.push_back(area);
    return area.id;
}

bool CommercialAreaManager::PreviewRect(uint32_t ignoreId, float centerX, float centerZ, float cellSize, AreaRect& out) const
{
    out = MakeAreaRect(centerX, centerZ, SideLength(), cellSize);
    return CanPlaceAreaRect(out, m_areas, ignoreId, m_settings.placementEpsilon);
}

bool CommercialAreaManager::MoveArea(uint32_t id, float centerX, float centerZ, float cellSize, int currentWeek)
{
    CommercialArea* area = FindMutable(id);
    if (!area)
    {
        return false;
    }
    AreaRect rect;
    if (!PreviewRect(id, centerX, centerZ, cellSize, rect))
    {
        return false;
    }
    const bool changed = rect.minX != area->rect.minX || rect.minZ != area->rect.minZ
        || rect.maxX != area->rect.maxX || rect.maxZ != area->rect.maxZ;
    area->rect = rect;
    if (changed && area->published)
    {
        area->history.push_back({ currentWeek, AreaHistoryKind::Moved }); // 周知後の移設は、記録に残る(進捗・期限は、リセットされない)
    }
    return true;
}

bool CommercialAreaManager::DeleteArea(uint32_t id, int currentWeek)
{
    for (size_t i = 0; i < m_areas.size(); ++i)
    {
        if (m_areas[i].id != id)
        {
            continue;
        }
        const CommercialArea& area = m_areas[i];
        if (area.published)
        {
            WithdrawnAreaRecord record;
            record.areaId = area.id;
            record.publishedWeek = area.publishedWeek;
            record.withdrawnWeek = currentWeek;
            record.plannedCapacity = area.plannedCapacity;
            record.achieved = area.achieved;
            m_withdrawn.push_back(record);
        }
        m_areas.erase(m_areas.begin() + static_cast<std::ptrdiff_t>(i));
        m_members.erase(std::remove_if(m_members.begin(), m_members.end(), [id](const AreaMembers& m) { return m.areaId == id; }), m_members.end());
        return true;
    }
    return false;
}

bool CommercialAreaManager::Publish(uint32_t id, int currentWeek)
{
    CommercialArea* area = FindMutable(id);
    if (!area || area->published)
    {
        return false;
    }
    area->published = true;
    area->publishedWeek = currentWeek;
    area->plannedCapacity = m_settings.plannedCapacity;    // 周知した時の計画規模で固定する
    area->achieved = AreaMilestone::None;
    area->history.push_back({ currentWeek, AreaHistoryKind::Published });
    return true;
}

void CommercialAreaManager::Refresh(const std::vector<AreaFacility>& facilities)
{
    m_members = ComputeAreaMembers(m_areas, facilities, m_settings);
    // 達成状態は、周知した区画だけ。一度届いた段階は、店舗が減っても、下がらない。
    for (CommercialArea& area : m_areas)
    {
        if (!area.published)
        {
            continue;
        }
        const AreaProgress progress = ProgressOf(area.id);
        if (static_cast<int>(progress.current) > static_cast<int>(area.achieved))
        {
            area.achieved = progress.current;
        }
    }
}

const AreaMembers* CommercialAreaManager::MembersOf(uint32_t id) const
{
    for (const AreaMembers& members : m_members)
    {
        if (members.areaId == id) { return &members; }
    }
    return nullptr;
}

AreaProgress CommercialAreaManager::ProgressOf(uint32_t id) const
{
    const CommercialArea* area = Find(id);
    const AreaMembers* members = MembersOf(id);
    return ComputeAreaProgress(members ? static_cast<int>(members->facilityIds.size()) : 0,
                               area ? area->plannedCapacity : m_settings.plannedCapacity, m_settings);
}

uint32_t CommercialAreaManager::FindAreaAt(float x, float z) const
{
    for (const CommercialArea& area : m_areas)
    {
        if (area.rect.Contains(x, z)) { return area.id; }
    }
    return 0;
}

std::vector<const char*> CommercialAreaManager::Validate() const
{
    return ValidateAreas(m_areas, m_members, m_settings.placementEpsilon);
}

size_t CommercialAreaManager::EstimateBytes() const
{
    size_t bytes = sizeof(*this);
    bytes += m_areas.capacity() * sizeof(CommercialArea);
    for (const CommercialArea& area : m_areas) { bytes += area.history.capacity() * sizeof(AreaHistoryEntry); }
    bytes += m_withdrawn.capacity() * sizeof(WithdrawnAreaRecord);
    for (const AreaMembers& members : m_members) { bytes += sizeof(AreaMembers) + members.facilityIds.capacity() * (sizeof(uint32_t) + sizeof(float)); }
    return bytes;
}

// ---------------------------------------------------------------------------
// 保存・復元
// ---------------------------------------------------------------------------
void CommercialAreaManager::Save(Json& section, const SaveContext& /*context*/) const
{
    section.Set("nextId", static_cast<int>(m_nextId));
    Json areas = Json::MakeArray();
    for (const CommercialArea& area : m_areas)
    {
        Json item = Json::MakeObject();
        item.Set("id", static_cast<int>(area.id));
        item.Set("minX", area.rect.minX);
        item.Set("minZ", area.rect.minZ);
        item.Set("maxX", area.rect.maxX);
        item.Set("maxZ", area.rect.maxZ);
        item.Set("published", area.published);
        item.Set("publishedWeek", area.publishedWeek);
        item.Set("plannedCapacity", area.plannedCapacity);
        item.Set("achieved", static_cast<int>(area.achieved));
        Json history = Json::MakeArray();
        for (const AreaHistoryEntry& entry : area.history)
        {
            Json h = Json::MakeObject();
            h.Set("week", entry.week);
            h.Set("kind", static_cast<int>(entry.kind));
            history.Push(std::move(h));
        }
        item.Set("history", std::move(history));
        areas.Push(std::move(item));
    }
    section.Set("areas", std::move(areas));

    Json withdrawn = Json::MakeArray();
    for (const WithdrawnAreaRecord& record : m_withdrawn)
    {
        Json item = Json::MakeObject();
        item.Set("areaId", static_cast<int>(record.areaId));
        item.Set("publishedWeek", record.publishedWeek);
        item.Set("withdrawnWeek", record.withdrawnWeek);
        item.Set("plannedCapacity", record.plannedCapacity);
        item.Set("achieved", static_cast<int>(record.achieved));
        withdrawn.Push(std::move(item));
    }
    section.Set("withdrawn", std::move(withdrawn));
}

void CommercialAreaManager::Load(const Json& section, int /*version*/, const LoadContext& context)
{
    ResetToDefault();
    // 場所・大きさを持つ値(矩形)は、保存した時と今の長さの基準の比(spaceScale)で、今の大きさに合わせる(道路・建物と同じ扱い)。
    const float scale = context.spaceScale > 0.0f ? context.spaceScale : 1.0f;
    uint32_t maxId = 0;
    if (const Json* areas = section.Find("areas"))
    {
        if (areas->IsArray())
        {
            for (size_t i = 0; i < areas->Size(); ++i)
            {
                const Json& item = areas->At(i);
                if (!item.IsObject())
                {
                    continue;
                }
                CommercialArea area;
                area.id = static_cast<uint32_t>((std::max)(0, item.GetInt("id", 0)));
                area.rect.minX = item.GetFloat("minX", 0.0f) * scale;
                area.rect.minZ = item.GetFloat("minZ", 0.0f) * scale;
                area.rect.maxX = item.GetFloat("maxX", 0.0f) * scale;
                area.rect.maxZ = item.GetFloat("maxZ", 0.0f) * scale;
                area.published = item.GetBool("published", false);
                area.publishedWeek = item.GetInt("publishedWeek", 0);
                area.plannedCapacity = (std::max)(0, item.GetInt("plannedCapacity", m_settings.plannedCapacity));
                area.achieved = ReadEnum<AreaMilestone>(item, "achieved", static_cast<int>(AreaMilestone::Complete), AreaMilestone::None);
                if (const Json* history = item.Find("history"))
                {
                    if (history->IsArray())
                    {
                        for (size_t h = 0; h < history->Size(); ++h)
                        {
                            const Json& entry = history->At(h);
                            AreaHistoryEntry e;
                            e.week = entry.GetInt("week", 0);
                            e.kind = ReadEnum<AreaHistoryKind>(entry, "kind", static_cast<int>(AreaHistoryKind::Withdrawn), AreaHistoryKind::Published);
                            area.history.push_back(e);
                        }
                    }
                }
                // 壊れた区画(IDが0・重複・面積が無い・他の区画と重なる)は、読み飛ばす(重なる区画・二重所属を作らない)。
                if (area.id == 0 || !(area.rect.Area() > 0.0f) || Find(area.id) != nullptr
                    || !CanPlaceAreaRect(area.rect, m_areas, 0, m_settings.placementEpsilon))
                {
                    continue;
                }
                maxId = (std::max)(maxId, area.id);
                m_areas.push_back(std::move(area));
            }
        }
    }
    if (const Json* withdrawn = section.Find("withdrawn"))
    {
        if (withdrawn->IsArray())
        {
            for (size_t i = 0; i < withdrawn->Size(); ++i)
            {
                const Json& item = withdrawn->At(i);
                WithdrawnAreaRecord record;
                record.areaId = static_cast<uint32_t>((std::max)(0, item.GetInt("areaId", 0)));
                record.publishedWeek = item.GetInt("publishedWeek", 0);
                record.withdrawnWeek = item.GetInt("withdrawnWeek", 0);
                record.plannedCapacity = (std::max)(0, item.GetInt("plannedCapacity", 0));
                record.achieved = ReadEnum<AreaMilestone>(item, "achieved", static_cast<int>(AreaMilestone::Complete), AreaMilestone::None);
                m_withdrawn.push_back(record);
                maxId = (std::max)(maxId, record.areaId);
            }
        }
    }
    // 次のIDは、保存した値と、読んだIDの最大+1の、大きい方(撤回した区画のIDも、再利用しない)。
    m_nextId = (std::max)(static_cast<uint32_t>((std::max)(1, section.GetInt("nextId", 1))), maxId + 1);
}

void CommercialAreaManager::ResetToDefault()
{
    m_areas.clear();
    m_withdrawn.clear();
    m_members.clear();
    m_nextId = 1;
}
