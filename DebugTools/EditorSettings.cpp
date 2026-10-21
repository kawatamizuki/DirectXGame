#include "EditorSettings.h"
#include <filesystem>
#include <fstream>
#include <iterator>

bool* EditorSettings::AddPanel(const std::string& name, bool defaultOpen, bool persist)
{
    if (bool* existing = FindPanel(name))
    {
        return existing;
    }

    Panel panel;
    panel.name = name;
    panel.open = defaultOpen;
    panel.defaultOpen = defaultOpen;
    panel.persist = persist;
    m_panels.push_back(panel);
    return &m_panels.back().open;
}

bool* EditorSettings::FindPanel(const std::string& name)
{
    for (Panel& panel : m_panels)
    {
        if (panel.name == name)
        {
            return &panel.open;
        }
    }
    return nullptr;
}

void EditorSettings::RegisterToggle(const std::string& id, const std::string& label, bool* flag, const std::string& description)
{
    if (!flag)
    {
        return;
    }
    for (const Toggle& toggle : m_toggles)
    {
        if (toggle.id == id)
        {
            return;
        }
    }

    Toggle toggle;
    toggle.id = id;
    toggle.label = label;
    toggle.description = description;
    toggle.flag = flag;
    toggle.defaultValue = *flag;
    m_toggles.push_back(toggle);
}

void EditorSettings::ResetPanels()
{
    for (Panel& panel : m_panels)
    {
        panel.open = panel.defaultOpen;
    }
}

Json EditorSettings::ToJson() const
{
    Json root = Json::MakeObject();
    root.Set("version", kFormatVersion);
    root.Set("showHints", showHints);

    Json panels = Json::MakeObject();
    for (const Panel& panel : m_panels)
    {
        if (panel.persist)
        {
            panels.Set(panel.name, panel.open);
        }
    }
    root.Set("panels", panels);

    Json toggles = Json::MakeObject();
    for (const Toggle& toggle : m_toggles)
    {
        toggles.Set(toggle.id, toggle.flag ? *toggle.flag : toggle.defaultValue);
    }
    root.Set("toggles", toggles);
    return root;
}

void EditorSettings::ApplyJson(const Json& json)
{
    if (!json.IsObject())
    {
        return;
    }

    if (const Json* hints = json.Find("showHints"))
    {
        if (hints->IsBool())
        {
            showHints = hints->AsBool(showHints);
        }
    }

    if (const Json* panels = json.Find("panels"))
    {
        if (panels->IsObject())
        {
            for (Panel& panel : m_panels)
            {
                const Json* value = panels->Find(panel.name);
                if (panel.persist && value && value->IsBool())
                {
                    panel.open = value->AsBool(panel.open);
                }
            }
        }
    }

    if (const Json* toggles = json.Find("toggles"))
    {
        if (toggles->IsObject())
        {
            for (Toggle& toggle : m_toggles)
            {
                const Json* value = toggles->Find(toggle.id);
                if (value && value->IsBool() && toggle.flag)
                {
                    *toggle.flag = value->AsBool(*toggle.flag);
                }
            }
        }
    }
}

bool EditorSettings::Save(const std::string& path) const
{
    std::error_code ec;
    std::filesystem::path target(path);
    if (target.has_parent_path())
    {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec)
        {
            return false;
        }
    }

    // 一時ファイルに書き切ってから置き換える(書き込み中に落ちても、前の設定が壊れない)。
    std::string text = Serialize();
    std::string tempPath = path + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file)
        {
            file.close();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    std::filesystem::rename(tempPath, target, ec);
    if (ec)
    {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}

bool EditorSettings::Load(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Json json;
    std::string error;
    if (!Json::Parse(text, json, error) || !json.IsObject())
    {
        return false;
    }

    ApplyJson(json);
    return true;
}
