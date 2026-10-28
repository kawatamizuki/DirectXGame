#include "EditorUi.h"
#include <algorithm>
#include <cfloat>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "ShortcutTable.h"

namespace
{
    // パネルのウィンドウ名(ImGuiのウィンドウ名、設定の保存キー、初期配置の指定で共通)。
    // 名前を変えると imgui.ini の配置も別物になるので、変えない。
    constexpr const char* kPanelHierarchy = "Hierarchy";
    constexpr const char* kPanelInspector = "Inspector";
    constexpr const char* kPanelEditorSettings = "Editor Settings";
    constexpr const char* kPanelDebug = "Debug";
    constexpr const char* kPanelBuild = "Build";
    constexpr const char* kPanelTime = "Time";
    constexpr const char* kPanelEconomy = "Economy";
    constexpr const char* kPanelScenario = "Scenario";
    constexpr const char* kPanelCityStats = "City Stats";
    constexpr const char* kPanelProfiler = "Profiler";
    constexpr const char* kPanelLog = "Log";
    constexpr const char* kPanelAssets = "Assets";
    constexpr const char* kPanelAssetViewer = "Asset Viewer";
    constexpr const char* kPanelAudio = "Audio";
    constexpr const char* kPanelShortcuts = "Shortcuts";

    // メニューに出す順番と、初めて起動した時に開いているか。
    struct PanelInfo
    {
        const char* name;
        bool defaultOpen;
        const char* menuShortcut; // メニューに薄く出すキー(無ければnullptr)
        bool persist = true;      // 開閉を設定ファイルに保存するか(中身が空では意味が無いウィンドウはfalse)
    };

    const PanelInfo kPanels[] =
    {
        { kPanelHierarchy,      true,  nullptr },
        { kPanelInspector,      true,  nullptr },
        { kPanelEditorSettings, true,  nullptr },
        { kPanelDebug,          true,  nullptr },
        { kPanelBuild,          true,  nullptr },
        { kPanelTime,           true,  nullptr },
        { kPanelEconomy,        true,  nullptr },
        { kPanelScenario,       true,  nullptr },
        { kPanelCityStats,      true,  nullptr },
        { kPanelProfiler,       true,  nullptr },
        { kPanelLog,            true,  nullptr },
        { kPanelAssets,         false, "F2"    }, // 開くまでは何も処理しない(素材を探すのも、初めて開いた時)
        { kPanelAssetViewer,    false, nullptr, false }, // 普段は閉じていて、何も読み込まない(Assetsからダブルクリック/Vキーで開く)。開いたまま終了しても、次は閉じて始める
        { kPanelAudio,          false, nullptr }, // 音の一覧と音量の調整。開くまでは何も処理しない
        { kPanelShortcuts,      false, nullptr },
    };

    // ヒントを出し始めるまでの待ち時間(秒)。通り過ぎる時にちらつかないように。
    constexpr double kHintDelaySeconds = 0.3;

    // 設定の保存を確かめる間隔(フレーム)。
    constexpr int kSaveCheckIntervalFrames = 120;
}

void EditorUi::Initialize()
{
    for (const PanelInfo& panel : kPanels)
    {
        m_settings.AddPanel(panel.name, panel.defaultOpen, panel.persist);
    }
    m_shortcutsPanel = m_settings.FindPanel(kPanelShortcuts);
}

void EditorUi::LoadSettings()
{
    m_settings.Load(kSettingsPath);
    m_lastSavedText = m_settings.Serialize();
}

bool* EditorUi::PanelFlag(const char* name)
{
    return m_settings.FindPanel(name);
}

bool EditorUi::GetGameViewRect(float& x, float& y, float& width, float& height) const
{
    if (!m_dockId)
    {
        return false;
    }
    const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(m_dockId);
    if (!central || central->Size.x < 1.0f || central->Size.y < 1.0f)
    {
        return false;
    }
    x = central->Pos.x;
    y = central->Pos.y;
    width = central->Size.x;
    height = central->Size.y;
    return true;
}

bool EditorUi::GetGameViewCenter(float& x, float& y) const
{
    if (!m_dockId)
    {
        return false;
    }
    const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(m_dockId);
    if (!central || central->Size.x <= 0.0f || central->Size.y <= 0.0f)
    {
        return false;
    }
    x = central->Pos.x + central->Size.x * 0.5f;
    y = central->Pos.y + central->Size.y * 0.5f;
    return true;
}

void EditorUi::SaveSettingsIfChanged(bool force)
{
    if (!force)
    {
        ++m_framesSinceSaveCheck;
        if (m_framesSinceSaveCheck < kSaveCheckIntervalFrames)
        {
            return;
        }
    }
    m_framesSinceSaveCheck = 0;

    std::string text = m_settings.Serialize();
    if (text == m_lastSavedText)
    {
        return;
    }
    if (m_settings.Save(kSettingsPath))
    {
        m_lastSavedText = text;
    }
}

void EditorUi::BeginFrame(const FrameInfo& info)
{
    DrawMenuBar(info);
    DrawDockSpace(info);
}

void EditorUi::EndFrame(const FrameInfo& info)
{
    DrawShortcutsWindow();
    DrawHoverHint(info);
}

// ---------------------------------------------------------------------------
// Editorメニュー
// ---------------------------------------------------------------------------
void EditorUi::DrawMenuBar(const FrameInfo& info)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float barHeight = ImGui::GetFrameHeight();

    // ImGuiのBeginMainMenuBarは画面の最上端に出てゲームの上部バー(HUD)を隠すので、使わない。
    // 代わりに、HUDの直下に、メニューバー付きの細いウィンドウを置く。
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + info.hudTopInset));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, barHeight));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    // ImGuiは、ウィンドウをWindowMinSize(既定32px)より低くしない。メニューバーの高さ(約24px)に合わせるため、下げておく
    // (下げないと、メニューバーがドックスペースに数px重なって、タブの上端を隠す)。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar;

    if (ImGui::Begin("##EditorMenuBar", nullptr, flags))
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Editor"))
            {
                if (ImGui::BeginMenu("パネル"))
                {
                    for (const PanelInfo& panel : kPanels)
                    {
                        bool* open = m_settings.FindPanel(panel.name);
                        if (open)
                        {
                            ImGui::MenuItem(panel.name, panel.menuShortcut, open);
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("レイアウトを初期化"))
                {
                    m_layoutResetRequested = true;
                    m_settings.ResetPanels();
                }

                ImGui::Separator();

                ImGui::MenuItem("ヒントを表示", nullptr, &m_settings.showHints);

                if (ImGui::BeginMenu("エディタ機能のON/OFF"))
                {
                    if (m_settings.Toggles().empty())
                    {
                        ImGui::TextDisabled("(登録された機能はありません)");
                    }
                    for (const EditorSettings::Toggle& toggle : m_settings.Toggles())
                    {
                        ImGui::MenuItem(toggle.label.c_str(), nullptr, toggle.flag);
                        if (!toggle.description.empty() && ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s", toggle.description.c_str());
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("ショートカット一覧..."))
                {
                    if (m_shortcutsPanel)
                    {
                        *m_shortcutsPanel = true;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(4);

    m_menuBarBottom = info.hudTopInset + barHeight;
}

// ---------------------------------------------------------------------------
// ドックスペース(パネルを吸着させる土台)と初期配置
// ---------------------------------------------------------------------------
void EditorUi::DrawDockSpace(const FrameInfo& info)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float top = m_menuBarBottom;
    float width = viewport->Size.x;
    float height = std::max(1.0f, viewport->Size.y - top);

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + top));
    ImGui::SetNextWindowSize(ImVec2(width, height));

    // 背景を描かない・動かせない・手前に出てこない土台のウィンドウ。パネルはこの上のドックに吸着する。
    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##EditorDockHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    m_dockId = ImGui::GetID("EditorDockSpace");

    // 配置の情報が無い時(初回、またはimgui.iniを消した時)と、初期化を頼まれた時だけ、初期配置を作る。
    // 配置の情報があれば、ImGuiが imgui.ini から復元した物をそのまま使う(ユーザーが動かした配置を壊さない)。
    if (m_layoutResetRequested || ImGui::DockBuilderGetNode(m_dockId) == nullptr)
    {
        BuildDefaultLayout(m_dockId, width, height);
        m_layoutResetRequested = false;
    }

    // 中央は透過(ゲームの画面がそのまま見える)で、マウスもゲームに届く。中央にはパネルを吸着させない。
    ImGui::DockSpace(
        m_dockId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode);

    ImGui::End();
    (void)info;
}

void EditorUi::BuildDefaultLayout(unsigned int dockId, float width, float height)
{
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockId, ImVec2(width, height));

    // 中央(ゲームの画面)を残して、左・右・下を順に切り出す。
    //  ┌ 左(Hierarchy / ツール) ┬ 中央(ゲームの画面) ┬ 右(Inspector) ┐
    //  │                       ├ 下(Assets / Log / Profiler / City Stats) ┤
    ImGuiID center = dockId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.36f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.52f, nullptr, &left);

    ImGui::DockBuilderDockWindow(kPanelHierarchy, left);

    ImGui::DockBuilderDockWindow(kPanelDebug, leftBottom);
    ImGui::DockBuilderDockWindow(kPanelBuild, leftBottom);
    ImGui::DockBuilderDockWindow(kPanelTime, leftBottom);
    ImGui::DockBuilderDockWindow(kPanelEconomy, leftBottom);
    ImGui::DockBuilderDockWindow(kPanelScenario, leftBottom);

    ImGui::DockBuilderDockWindow(kPanelInspector, right);
    ImGui::DockBuilderDockWindow(kPanelEditorSettings, right);

    ImGui::DockBuilderDockWindow(kPanelAssets, bottom);
    ImGui::DockBuilderDockWindow(kPanelLog, bottom);
    ImGui::DockBuilderDockWindow(kPanelProfiler, bottom);
    ImGui::DockBuilderDockWindow(kPanelCityStats, bottom);
    ImGui::DockBuilderDockWindow(kPanelAudio, bottom);

    ImGui::DockBuilderFinish(dockId);

    // 下のタブは、Assets を最初に見えるタブにする(Unityのプロジェクトウィンドウのように)。
    // Assets は閉じた状態で始まるので、開いた時にこのタブが前面に出る。
    if (ImGuiDockNode* bottomNode = ImGui::DockBuilderGetNode(bottom))
    {
        bottomNode->SelectedTabId = ImHashStr(kPanelAssets);
    }
}

// ---------------------------------------------------------------------------
// ホバーのヒント
// ---------------------------------------------------------------------------
void EditorUi::DrawHoverHint(const FrameInfo& info)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext* context = ImGui::GetCurrentContext();

    // カーソルの下にあるウィンドウから、どの場面のヒントを出すかを決める。
    std::string hintContext;
    ImVec2 anchor(0.0f, 0.0f); // ヒントの箱の右下の基準点
    std::vector<const ShortcutEntry*> lines;

    ImGuiWindow* hovered = context ? context->HoveredWindow : nullptr;
    if (hovered && hovered->RootWindow)
    {
        // 子ウィンドウの上でも、親(最上位)のウィンドウの名前で判定する。
        hintContext = ShortcutTable::ContextForWindowName(hovered->RootWindow->Name);
        if (!hintContext.empty())
        {
            lines = ShortcutTable::ForContext(hintContext);
            anchor = ImVec2(hovered->RootWindow->Pos.x + hovered->RootWindow->Size.x,
                            hovered->RootWindow->Pos.y + hovered->RootWindow->Size.y);
        }
    }
    else if (!hovered && info.inGameScene && !info.mouseOverGameHud && !io.WantCaptureMouse)
    {
        // どのウィンドウの上でもない=中央の透過部分(ゲームの画面)の上。
        hintContext = info.playing ? "Game:Playing" : "Game:Editing";

        // 今のモード(Playing / Editing)の操作。長い時は、先頭の8行だけ(残りは、ショートカット一覧で見る)。
        lines = ShortcutTable::ForContext(info.playing ? "Playing" : "Editing");
        constexpr size_t kMaxHintLines = 8;
        if (lines.size() > kMaxHintLines)
        {
            lines.resize(kMaxHintLines);
        }

        ImGuiDockNode* central = m_dockId ? ImGui::DockBuilderGetCentralNode(m_dockId) : nullptr;
        if (central)
        {
            anchor = ImVec2(central->Pos.x + central->Size.x, central->Pos.y + central->Size.y);
        }
        else
        {
            anchor = ImVec2(io.DisplaySize.x, io.DisplaySize.y);
        }
    }

    double now = ImGui::GetTime();
    if (hintContext != m_hoverContext)
    {
        m_hoverContext = hintContext;
        m_hoverSince = now;
    }

    if (!m_settings.showHints || hintContext.empty() || lines.empty() || (now - m_hoverSince) < kHintDelaySeconds)
    {
        return;
    }

    // 小さな文字で、[キー] 説明 を行ごとに並べる。ImGuiの入力は奪わない(描画だけ)。
    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize() * 0.8f;
    const float pad = 6.0f;
    const float lineGap = 2.0f;
    const float keyGap = 8.0f;

    float keyWidth = 0.0f;
    float descWidth = 0.0f;
    for (const ShortcutEntry* entry : lines)
    {
        std::string key = std::string("[") + entry->keys + "]";
        keyWidth = std::max(keyWidth, font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, key.c_str()).x);
        descWidth = std::max(descWidth, font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, entry->description).x);
    }

    float lineHeight = fontSize + lineGap;
    float boxWidth = pad * 2.0f + keyWidth + keyGap + descWidth;
    float boxHeight = pad * 2.0f + lineHeight * static_cast<float>(lines.size()) - lineGap;

    // 基準点の少し内側の右下に置く。画面の外にはみ出さない。
    ImVec2 boxMin(anchor.x - boxWidth - 10.0f, anchor.y - boxHeight - 10.0f);
    boxMin.x = std::max(4.0f, std::min(boxMin.x, io.DisplaySize.x - boxWidth - 4.0f));
    boxMin.y = std::max(4.0f, std::min(boxMin.y, io.DisplaySize.y - boxHeight - 4.0f));
    ImVec2 boxMax(boxMin.x + boxWidth, boxMin.y + boxHeight);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(15, 15, 20, 200), 4.0f);
    drawList->AddRect(boxMin, boxMax, IM_COL32(120, 120, 140, 160), 4.0f);

    float y = boxMin.y + pad;
    for (const ShortcutEntry* entry : lines)
    {
        std::string key = std::string("[") + entry->keys + "]";
        drawList->AddText(font, fontSize, ImVec2(boxMin.x + pad, y), IM_COL32(255, 220, 120, 255), key.c_str());
        drawList->AddText(font, fontSize, ImVec2(boxMin.x + pad + keyWidth + keyGap, y), IM_COL32(230, 230, 230, 255), entry->description);
        y += lineHeight;
    }
}

// ---------------------------------------------------------------------------
// ショートカット一覧ウィンドウ
// ---------------------------------------------------------------------------
void EditorUi::DrawShortcutsWindow()
{
    if (!m_shortcutsPanel || !*m_shortcutsPanel)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kPanelShortcuts, m_shortcutsPanel))
    {
        ImGui::TextDisabled("ウィンドウの上にカーソルを合わせると、そのウィンドウのキーが小さく出ます(Editor > ヒントを表示 でON/OFF)。");
        ImGui::Separator();

        for (const std::string& context : ShortcutTable::Contexts())
        {
            if (ImGui::CollapsingHeader(ShortcutTable::ContextTitle(context).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable(("##shortcuts_" + context).c_str(), 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                    ImGui::TableSetupColumn("description", ImGuiTableColumnFlags_WidthStretch);
                    for (const ShortcutEntry* entry : ShortcutTable::ForContext(context))
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.47f, 1.0f), "%s", entry->keys);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(entry->description);
                    }
                    ImGui::EndTable();
                }
            }
        }
    }
    ImGui::End();
}
