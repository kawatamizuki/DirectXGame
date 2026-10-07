#include "GameFlags.h"
#include <cmath>

void GameFlags::Set(const std::string& name, double value)
{
    // "version"は保存データの区画の版番号と同じ名前になるので、フラグ名には使えない(無視する)。
    if (name == "version")
    {
        return;
    }
    m_values[name] = value;
}

double GameFlags::Get(const std::string& name, double defaultValue) const
{
    auto it = m_values.find(name);
    return it == m_values.end() ? defaultValue : it->second;
}

bool GameFlags::Has(const std::string& name) const
{
    return m_values.find(name) != m_values.end();
}

void GameFlags::Save(Json& section, const SaveContext& /*context*/) const
{
    for (const auto& entry : m_values)
    {
        section.Set(entry.first, entry.second);
    }
}

void GameFlags::Load(const Json& section, int /*version*/, const LoadContext& /*context*/)
{
    Clear();
    for (const auto& member : section.Members())
    {
        // "version"は区画の版番号(ScenarioManagerが入れたもの)でフラグではない。数値以外の項目も無視する。
        if (member.first == "version" || !member.second.IsNumber() || !std::isfinite(member.second.AsNumber()))
        {
            continue;
        }
        m_values[member.first] = member.second.AsNumber();
    }
}
