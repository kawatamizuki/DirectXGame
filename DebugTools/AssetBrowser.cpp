#include "AssetBrowser.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <Windows.h>
#include <shellapi.h>
#include "imgui.h"
#include "Renderer.h"
#include "BakedModelCache.h"
#include "DayNightCycle.h"
#include "Debug.h"

namespace
{
    constexpr float kCellPadding = 6.0f;
    constexpr float kFrameBudgetMs = 8.0f;   // サムネイル生成に使う、1フレームあたりの時間枠
    constexpr float kTreeWidth = 230.0f;     // フォルダの木の幅
    constexpr const char* kFolderPopupName = "フォルダ名##assetFolderName";
    constexpr const char* kFolderPayload = "ASSET_FOLDER"; // 木のフォルダをドラッグして移動する時の、ImGuiのペイロードの種類
    constexpr const char* kModelPayload = AssetBrowser::kModelPayloadType; // モデルをドラッグしてゲームの画面へ置く時の種類

    ImTextureRef ToTexture(ID3D11ShaderResourceView* srv)
    {
        return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(srv)));
    }

    std::string ToLower(std::string text)
    {
        for (char& c : text)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    // 幅maxWidthに収まるよう、末尾を「...」で切った文字列を返す(ImGuiの文字幅で測る)。
    std::string FitText(const std::string& text, float maxWidth)
    {
        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
        {
            return text;
        }
        std::string result = text;
        while (!result.empty() && ImGui::CalcTextSize((result + "...").c_str()).x > maxWidth)
        {
            result.pop_back();
        }
        return result + "...";
    }

    // エクスプローラーで開く。ファイルなら、そのファイルを選んだ状態で親フォルダを開く。
    void OpenInExplorer(const std::string& relativePath, bool selectFile)
    {
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(std::filesystem::path(relativePath), ec);
        if (ec)
        {
            return;
        }
        absolute.make_preferred();
        if (selectFile)
        {
            const std::wstring arguments = L"/select,\"" + absolute.wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
        }
        else
        {
            ShellExecuteW(nullptr, L"open", absolute.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    // path が folder の下(folder/...)または folder そのものなら、先頭の folder を newFolder に置き換えた物を返す。違えばそのまま。
    std::string RemapPath(const std::string& path, const std::string& folder, const std::string& newFolder)
    {
        if (path == folder)
        {
            return newFolder;
        }
        if (path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/')
        {
            return newFolder + path.substr(folder.size());
        }
        return path;
    }
}

void AssetBrowser::Initialize(Renderer* renderer, const DayNightCycle* dayNightCycle)
{
    m_renderer = renderer;
    m_dayNightCycle = dayNightCycle;
    m_preview.Initialize(renderer);
}

void AssetBrowser::EnsureInitialized()
{
    if (m_initialized || !m_renderer)
    {
        return;
    }
    m_initialized = true;

    // サムネイルは、ゲーム内の時刻とは無関係に、昼の標準の光で描く(いつ生成しても同じ見た目になる)。
    LightingState noon;
    if (m_dayNightCycle)
    {
        noon = m_dayNightCycle->Evaluate(12.0f);
    }
    m_thumbnails.Initialize(m_renderer, &m_preview, noon);

    // 素材の置き場を用意する(無ければ作る)。Incomingは、これから見る素材を置く場所(Gitに入れない)。
    {
        std::error_code ec;
        std::filesystem::create_directories("Assets/Models", ec);
        std::filesystem::create_directories("Assets/Incoming", ec);
    }

    m_catalog.SetRoots(AssetCatalog::DefaultRoots());
    Rescan();
}

void AssetBrowser::Rescan()
{
    if (!m_initialized)
    {
        EnsureInitialized(); // 初回の EnsureInitialized が、この関数を呼び直す
        return;
    }

    m_catalog.Scan();

    // 探し直しても、選んでいたフォルダ・モデルが残っていれば、そのまま保つ。
    if (!m_selectedFolder.empty() && m_catalog.FindFolder(m_selectedFolder) < 0)
    {
        m_selectedFolder.clear();
    }
    if (!m_selectedPath.empty())
    {
        bool found = false;
        for (const AssetEntry& entry : m_catalog.GetEntries())
        {
            if (entry.path == m_selectedPath)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            m_selectedPath.clear();
        }
    }

    m_visibleDirty = true;
    RebuildVisibleList();
    EnqueueVisibleThumbnails();
}

void AssetBrowser::ShowFolder(const std::string& folder)
{
    *m_openFlag = true;
    EnsureInitialized();
    Rescan();
    if (m_catalog.FindFolder(folder) >= 0)
    {
        SelectFolder(folder);
        m_expandPath = folder;
    }
}

void AssetBrowser::ShowImportResult(const std::string& message, const std::string& folder)
{
    if (!folder.empty())
    {
        ShowFolder(folder); // パネルを開き、探し直して、取り込んだフォルダを選ぶ
    }
    else
    {
        *m_openFlag = true;
    }
    m_importMessage = message;
    m_importFolder = folder;
    m_importPopupRequested = true;
    m_wasDrawn = false; // 他のタブに隠れていても、前面に出す(隠れていると、結果のウィンドウが出ない)
}

void AssetBrowser::EnqueueVisibleThumbnails()
{
    // 全ての項目(重複は除く)を、空いた時間に順に用意する。今見えている項目は、Get()の予約で先に処理される。
    std::vector<AssetEntry> toPrepare;
    for (const AssetEntry& entry : m_catalog.GetEntries())
    {
        if (!entry.duplicate)
        {
            toPrepare.push_back(entry);
        }
    }
    m_thumbnails.EnqueueBackground(toPrepare);
}

void AssetBrowser::SelectFolder(const std::string& path)
{
    if (m_selectedFolder != path)
    {
        m_selectedFolder = path;
        m_visibleDirty = true;
    }
}

void AssetBrowser::RebuildVisibleList()
{
    m_visible.clear();
    const std::vector<AssetEntry>& entries = m_catalog.GetEntries();
    const std::string search = ToLower(m_searchText);

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const AssetEntry& entry = entries[i];
        if (entry.duplicate && !m_showDuplicates)
        {
            continue;
        }
        if (!AssetCatalog::IsInFolder(entry, m_selectedFolder, m_includeSubfolders))
        {
            continue;
        }
        if (!search.empty() && ToLower(entry.name).find(search) == std::string::npos && ToLower(entry.pack).find(search) == std::string::npos)
        {
            continue;
        }
        m_visible.push_back(i);
    }
    m_visibleDirty = false;
}

const AssetEntry* AssetBrowser::GetSelectedEntry() const
{
    if (m_selectedPath.empty())
    {
        return nullptr;
    }
    for (const AssetEntry& entry : m_catalog.GetEntries())
    {
        if (entry.path == m_selectedPath)
        {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<AssetEntry> AssetBrowser::GetVisibleEntries() const
{
    std::vector<AssetEntry> result;
    result.reserve(m_visible.size());
    for (size_t index : m_visible)
    {
        result.push_back(m_catalog.GetEntries()[index]);
    }
    return result;
}

void AssetBrowser::OpenViewerForSelection()
{
    if (!m_openViewer)
    {
        return;
    }
    if (const AssetEntry* selected = GetSelectedEntry())
    {
        m_openViewer(std::vector<AssetEntry>{ *selected }, selected->name);
        return;
    }
    OpenViewerForVisible(m_selectedFolder.empty() ? std::string("すべて") : m_selectedFolder);
}

void AssetBrowser::OpenViewerForVisible(const std::string& title)
{
    if (m_openViewer)
    {
        m_openViewer(GetVisibleEntries(), title);
    }
}

void AssetBrowser::Update()
{
    if (!*m_openFlag)
    {
        return; // 閉じている間は何もしない
    }

    EnsureInitialized();
    if (!m_initialized)
    {
        return;
    }

    m_thumbnails.Update(kFrameBudgetMs);
}

void AssetBrowser::DrawDebugUI()
{
    if (!*m_openFlag)
    {
        m_wasDrawn = false;
        return;
    }

    // 閉じていたものを開いた時(F2やEditorメニュー)は、他のタブに隠れていても、前面に出す。
    if (!m_wasDrawn)
    {
        ImGui::SetNextWindowFocus();
        m_wasDrawn = true;
        m_focusFrame = ImGui::GetFrameCount();
    }

    ImGui::SetNextWindowPos(ImVec2(40.0f, 90.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Assets", m_openFlag))
    {
        ImGui::End();
        return;
    }

    if (!m_initialized)
    {
        ImGui::TextUnformatted("読み込み中...");
        ImGui::End();
        return;
    }

    // Vキー: ビューワーで開く(このパネルにカーソルがある時、または操作中。文字を入力している間は除く)。
    const bool active = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    if (active && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_V, false))
    {
        OpenViewerForSelection();
    }

    DrawToolbar();
    ImGui::Separator();

    const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##folders", ImVec2(kTreeWidth, -footerHeight), true);
    DrawFolderTree();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##gridArea", ImVec2(0.0f, -footerHeight), false);
    DrawBreadcrumb();
    ImGui::BeginChild("##grid", ImVec2(0.0f, 0.0f), true);
    DrawThumbnailGrid();
    ImGui::EndChild();
    ImGui::EndChild();

    DrawStatusLine();
    DrawFolderPopup();
    DrawImportPopup();

    ImGui::End();

    // 木の描画が終わってから、ドラッグでの移動を行う(描画中に木を作り直さない)。
    if (!m_pendingMoveSource.empty())
    {
        MoveFolderTo(m_pendingMoveSource, m_pendingMoveTarget);
        m_pendingMoveSource.clear();
        m_pendingMoveTarget.clear();
    }
}

void AssetBrowser::DrawToolbar()
{
    bool changed = false;

    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputTextWithHint("##search", "検索(名前・パック)", m_searchText, sizeof(m_searchText)))
    {
        changed = true;
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("サブフォルダも含める", &m_includeSubfolders))
    {
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("重複も表示", &m_showDuplicates))
    {
        changed = true;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("大きさ", &m_thumbnailScale, 64.0f, 192.0f, "%.0f");

    if (changed)
    {
        m_visibleDirty = true;
    }
    if (m_visibleDirty)
    {
        RebuildVisibleList();
    }

    // 2行目: 操作。
    if (ImGui::Button("再スキャン"))
    {
        Rescan();
    }

    ImGui::SameLine();
    if (ImGui::Button("サムネイルを作り直す"))
    {
        m_thumbnails.Regenerate(GetVisibleEntries());
    }

    ImGui::SameLine();
    bool paused = m_thumbnails.IsPaused();
    if (ImGui::Checkbox("生成を一時停止", &paused))
    {
        m_thumbnails.SetPaused(paused);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!m_openViewer);
    if (ImGui::Button("ビューワーで開く(V)"))
    {
        OpenViewerForSelection();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("選んだモデルを、別ウィンドウ(Asset Viewer)で大きく見ます。\nモデルを選んでいない時は、今見えているモデル全部を開きます。");
    }

    ImGui::SameLine();
    DrawCacheButton();
}

void AssetBrowser::DrawCacheButton()
{
    // モデルキャッシュ(一度読んだモデルを、ディスクに焼いて保存したもの)を消すボタン。状態は、下の状況行に出す。
    if (ImGui::Button("モデルキャッシュを消す"))
    {
        BakedModelCache::ClearAll();
        m_cacheMeasureCounter = 120; // 次のフレームで測り直す
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Cache/Models の .mesh を全て消します(消しても、次の読み込みで作り直されるだけです)。\nモデルの結果が古い気がする時(テクスチャを後から足した時など)に使います。");
    }
}

void AssetBrowser::DrawBreadcrumb()
{
    // 「すべて > Assets > Models」のように、選んでいるフォルダまでの道を出す。クリックで、その階層へ戻れる。
    if (ImGui::SmallButton("すべて"))
    {
        SelectFolder(std::string());
    }

    std::string accumulated;
    size_t position = 0;
    while (position < m_selectedFolder.size())
    {
        const size_t slash = m_selectedFolder.find('/', position);
        const std::string segment = m_selectedFolder.substr(position, slash == std::string::npos ? std::string::npos : slash - position);
        accumulated = accumulated.empty() ? segment : accumulated + "/" + segment;

        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        ImGui::PushID(accumulated.c_str());
        if (ImGui::SmallButton(segment.c_str()))
        {
            SelectFolder(accumulated);
        }
        ImGui::PopID();

        if (slash == std::string::npos)
        {
            break;
        }
        position = slash + 1;
    }
}

void AssetBrowser::DrawFolderTree()
{
    const std::vector<FolderNode>& folders = m_catalog.GetFolders();
    if (folders.empty())
    {
        return;
    }
    DrawFolderNode(0);
    m_expandPath.clear(); // 開くのは一度だけ
}

void AssetBrowser::DrawFolderNode(int index)
{
    const FolderNode& node = m_catalog.GetFolders()[index];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.children.empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (node.path == m_selectedFolder)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (index == 0 || node.path == "Assets")
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    // 作成・移動・取り込みの結果のフォルダまでの親を、一度だけ開く。
    if (!m_expandPath.empty() && !node.children.empty() &&
        (node.path.empty() || m_expandPath == node.path ||
         (m_expandPath.size() > node.path.size() && m_expandPath.compare(0, node.path.size(), node.path) == 0 && m_expandPath[node.path.size()] == '/')))
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    // 表示は「名前 (モデル数)」。IDは、数が変わっても開閉の状態が変わらないよう、パスだけで決める。
    char label[512];
    // (ルートのパスは空なので、「dir:」を付ける。空のIDは、ImGuiが禁止している。)
    std::snprintf(label, sizeof(label), "%s (%d)###dir:%s", node.name.c_str(), node.modelCount, node.path.c_str());
    const bool open = ImGui::TreeNodeEx(label, flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        SelectFolder(node.path);
    }

    // 右クリックのメニュー。ポップアップのIDは、直前の項目(このフォルダの節)のIDから作る(nullptr)。
    // 文字列のIDを渡すと、全ての節が同じIDになり、全ての節の分のメニューが重なって出てしまう。
    if (ImGui::BeginPopupContextItem(nullptr))
    {
        const bool canCreate = AssetCatalog::CanCreateIn(node.path);
        const bool canModify = AssetCatalog::CanModify(node.path);

        if (ImGui::MenuItem("新しいフォルダ", nullptr, false, canCreate))
        {
            BeginFolderPopup(1, node.path);
        }
        if (ImGui::MenuItem("名前の変更", nullptr, false, canModify))
        {
            BeginFolderPopup(2, node.path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("このフォルダをビューワーで開く", nullptr, false, static_cast<bool>(m_openViewer) && node.modelCount > 0))
        {
            // 見えている物ではなく、このフォルダ(サブフォルダ込み)のモデル全部を渡す。
            std::vector<AssetEntry> models;
            for (const AssetEntry& entry : m_catalog.GetEntries())
            {
                if (!entry.duplicate && AssetCatalog::IsInFolder(entry, node.path, true))
                {
                    models.push_back(entry);
                }
            }
            m_openViewer(models, node.path.empty() ? std::string("すべて") : node.path);
        }
        if (ImGui::MenuItem("エクスプローラーで開く", nullptr, false, !node.path.empty()))
        {
            OpenInExplorer(node.path, false);
        }
        if (!canCreate)
        {
            ImGui::Separator();
            ImGui::TextDisabled("フォルダの作成・名前の変更・移動は、\nAssets/Models と Assets/Incoming の下だけです。");
        }
        ImGui::EndPopup();
    }

    // ドラッグで、フォルダごと別のフォルダの中へ移動する(フォルダの中のモデル・MTL・テクスチャの相対位置が保たれる)。
    if (AssetCatalog::CanModify(node.path) && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload(kFolderPayload, node.path.c_str(), node.path.size() + 1);
        ImGui::Text("フォルダを移動: %s", node.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderPayload))
        {
            m_pendingMoveSource = static_cast<const char*>(payload->Data);
            m_pendingMoveTarget = node.path;
        }
        ImGui::EndDragDropTarget();
    }

    if (open)
    {
        for (int child : node.children)
        {
            DrawFolderNode(child);
        }
        ImGui::TreePop();
    }
}

void AssetBrowser::DrawEntryContextMenu(const AssetEntry& entry)
{
    if (ImGui::MenuItem("ビューワーで開く", "V", false, static_cast<bool>(m_openViewer)))
    {
        m_openViewer(std::vector<AssetEntry>{ entry }, entry.name);
    }
    if (ImGui::MenuItem("シーンの中央に置く", nullptr, false, static_cast<bool>(m_placeAtCenter)))
    {
        m_placeAtCenter(entry);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("エクスプローラーで場所を開く"))
    {
        OpenInExplorer(entry.path, true);
    }
    if (ImGui::MenuItem("パスをコピー"))
    {
        ImGui::SetClipboardText(entry.path.c_str());
    }
}

void AssetBrowser::DrawThumbnailGrid()
{
    if (m_visible.empty())
    {
        ImGui::TextWrapped("このフォルダにはモデルがありません。Assets/Incoming/<パック名>/ に素材を置いて「再スキャン」を押すか、エクスプローラーからフォルダをこのウィンドウへドロップしてください。");
        return;
    }

    const float thumbnail = m_thumbnailScale;
    const float cellWidth = thumbnail + kCellPadding * 2.0f;
    const float labelHeight = ImGui::GetTextLineHeightWithSpacing();
    const float cellHeight = thumbnail + labelHeight + kCellPadding * 2.0f + ImGui::GetStyle().ItemSpacing.y;

    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = (std::max)(1, static_cast<int>(available / (cellWidth + ImGui::GetStyle().ItemSpacing.x)));
    const int rowCount = (static_cast<int>(m_visible.size()) + columns - 1) / columns;

    // 見えている行だけを描く(ListClipper)。見えている項目だけが、Get()でサムネイルを予約するので、生成も見えている物が優先される。
    ImGuiListClipper clipper;
    clipper.Begin(rowCount, cellHeight + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                const int visibleIndex = row * columns + column;
                if (visibleIndex >= static_cast<int>(m_visible.size()))
                {
                    break;
                }

                const size_t entryIndex = m_visible[visibleIndex];
                const AssetEntry& entry = m_catalog.GetEntries()[entryIndex];

                if (column > 0)
                {
                    ImGui::SameLine();
                }

                ImGui::PushID(static_cast<int>(entryIndex));
                ImGui::BeginGroup();

                ID3D11ShaderResourceView* srv = m_thumbnails.Get(entry);
                const ImVec2 imageSize(thumbnail, thumbnail);
                bool clicked = false;
                if (srv)
                {
                    clicked = ImGui::ImageButton("##thumb", ToTexture(srv), imageSize);
                }
                else
                {
                    // まだ用意できていない(失敗した、または大きいので自動では作らない)項目の場所取り。
                    const ImVec2 placeholderSize(thumbnail + ImGui::GetStyle().FramePadding.x * 2.0f, thumbnail + ImGui::GetStyle().FramePadding.y * 2.0f);
                    if (m_thumbnails.IsLargeSkipped(entry))
                    {
                        char text[64];
                        std::snprintf(text, sizeof(text), "大きいファイル\n%.1f MB\nクリックで作成", static_cast<double>(entry.fileSize) / (1024.0 * 1024.0));
                        clicked = ImGui::Button(text, placeholderSize);
                        if (clicked)
                        {
                            m_thumbnails.RequestAnyway(entry); // 数秒かかることがある(クリックした時だけ)
                        }
                    }
                    else
                    {
                        clicked = ImGui::Button(m_thumbnails.HasFailed(entry) ? "x" : "...", placeholderSize);
                    }
                }

                // 選択中の枠。
                if (entry.path == m_selectedPath)
                {
                    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                        IM_COL32(255, 180, 40, 255), 0.0f, 0, 3.0f);
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s\nパック: %s\n形式: %s%s", entry.path.c_str(), entry.pack.c_str(), entry.extension.c_str(), entry.duplicate ? "(重複)" : "");
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_openViewer)
                    {
                        m_openViewer(std::vector<AssetEntry>{ entry }, entry.name);
                    }
                }

                // ドラッグして、ゲームの画面へ落とすと、飾り(Prop)として置ける。ペイロードは、モデルのパス。
                // (ゴーストの表示と配置は、Game側のPropPlacerが GameContext::assetDrag を見て行う)
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    ImGui::SetDragDropPayload(kModelPayload, entry.path.c_str(), entry.path.size() + 1);
                    if (srv)
                    {
                        ImGui::Image(ToTexture(srv), ImVec2(64.0f, 64.0f));
                        ImGui::SameLine();
                    }
                    ImGui::Text("%s\nゲームの画面へドロップで置く", entry.name.c_str());
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginPopupContextItem("##entryContext"))
                {
                    m_selectedPath = entry.path;
                    DrawEntryContextMenu(entry);
                    ImGui::EndPopup();
                }

                ImGui::TextUnformatted(FitText(entry.name, cellWidth - kCellPadding).c_str());
                ImGui::EndGroup();
                ImGui::PopID();

                if (clicked)
                {
                    m_selectedPath = entry.path;
                }
            }
        }
    }
    clipper.End();

    // 項目の無い所での右クリック: 今見えているモデル全部をビューワーで開く。
    if (ImGui::BeginPopupContextWindow("##gridContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::MenuItem("見えているモデルをビューワーで開く", nullptr, false, static_cast<bool>(m_openViewer)))
        {
            OpenViewerForVisible(m_selectedFolder.empty() ? std::string("すべて") : m_selectedFolder);
        }
        ImGui::EndPopup();
    }
}

void AssetBrowser::DrawStatusLine()
{
    // モデルキャッシュ(一度読んだモデルを、ディスクに焼いて保存したもの)の状態。ディスクの大きさは、時々だけ測り直す。
    ++m_cacheMeasureCounter;
    if (m_cacheMeasureCounter >= 120)
    {
        BakedModelCache::MeasureDisk(m_cacheFiles, m_cacheBytes);
        m_cacheMeasureCounter = 0;
    }
    const BakedModelCache::Stats cache = BakedModelCache::GetStats();

    ImGui::Text("表示 %zu / 全%zu件  サムネイル: 待ち %zu・作成 %zu(平均 %.1fms)・キャッシュ読込 %zu・失敗 %zu  |  モデルキャッシュ%s: ヒット %zu・ミス %zu・破棄 %zu、%zu 個 %.1f MB",
        m_visible.size(), m_catalog.GetEntries().size(), m_thumbnails.GetPendingCount(),
        m_thumbnails.GetGeneratedCount(),
        m_thumbnails.GetGeneratedCount() > 0 ? m_thumbnails.GetTotalGenerateMs() / static_cast<float>(m_thumbnails.GetGeneratedCount()) : 0.0f,
        m_thumbnails.GetLoadedFromDiskCount(), m_thumbnails.GetFailedCount(),
        BakedModelCache::IsEnabled() ? "" : "(無効)", cache.hits, cache.misses, cache.invalidated,
        m_cacheFiles, static_cast<double>(m_cacheBytes) / (1024.0 * 1024.0));
    if (ImGui::IsItemHovered())
    {
        const double avgHit = cache.hits > 0 ? cache.hitMs / static_cast<double>(cache.hits) : 0.0;
        const double avgMiss = cache.misses > 0 ? cache.missLoadMs / static_cast<double>(cache.misses) : 0.0;
        ImGui::SetTooltip("サムネイルの最長 %.0f ms\nモデルキャッシュ: ヒットの平均 %.2f ms / ミス(ローダー)の平均 %.2f ms", m_thumbnails.GetSlowestGenerateMs(), avgHit, avgMiss);
    }
    if (m_thumbnails.GetSlowestGenerateMs() > 200.0f)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(最長: %s)", m_thumbnails.GetSlowestPath().c_str());
    }
    if (!m_folderError.empty() && m_folderPopupMode == 0)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_folderError.c_str());
    }
}

// ---------------------------------------------------------------------------
// フォルダの作成・名前の変更・移動
// ---------------------------------------------------------------------------
void AssetBrowser::BeginFolderPopup(int mode, const std::string& target)
{
    m_folderPopupMode = mode;
    m_folderPopupTarget = target;
    m_folderError.clear();
    m_folderPopupOpenRequested = true;

    std::memset(m_folderNameBuffer, 0, sizeof(m_folderNameBuffer));
    if (mode == 2)
    {
        const size_t slash = target.find_last_of('/');
        const std::string leaf = (slash == std::string::npos) ? target : target.substr(slash + 1);
        std::snprintf(m_folderNameBuffer, sizeof(m_folderNameBuffer), "%s", leaf.c_str());
    }
}

void AssetBrowser::DrawFolderPopup()
{
    if (m_folderPopupOpenRequested)
    {
        ImGui::OpenPopup(kFolderPopupName);
        m_folderPopupOpenRequested = false;
    }

    if (!ImGui::BeginPopupModal(kFolderPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    if (m_folderPopupMode == 1)
    {
        ImGui::Text("「%s」の中に、新しいフォルダを作ります。", m_folderPopupTarget.c_str());
    }
    else
    {
        ImGui::Text("「%s」の名前を変えます。", m_folderPopupTarget.c_str());
    }
    ImGui::TextDisabled("使える文字: 英数字・「_」・「-」・スペース");

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    const bool enter = ImGui::InputText("##folderName", m_folderNameBuffer, sizeof(m_folderNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

    if (!m_folderError.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_folderError.c_str());
    }

    bool close = false;
    if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)) || enter)
    {
        close = ApplyFolderPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        m_folderError.clear();
        close = true;
    }

    if (close)
    {
        m_folderPopupMode = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetBrowser::DrawImportPopup()
{
    constexpr const char* kImportPopupName = "取り込み結果##assetImport";
    // パネルを前面に出した直後のフレームでは、まだ開かない: 前面に出す処理(ドッキングのタブの切り替え)が数フレームかけて落ち着く間、
    // 親のウィンドウへのフォーカスが繰り返され、その度に、開いたばかりのポップアップが閉じられてしまうため。
    if (m_importPopupRequested && ImGui::GetFrameCount() > m_focusFrame + 3)
    {
        ImGui::OpenPopup(kImportPopupName);
        m_importPopupRequested = false;
    }

    if (!ImGui::BeginPopupModal(kImportPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
    ImGui::TextUnformatted(m_importMessage.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
    }
    if (!m_importFolder.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button("エクスプローラーで開く"))
        {
            OpenInExplorer(m_importFolder, false);
        }
    }
    ImGui::EndPopup();
}

bool AssetBrowser::ApplyFolderPopup()
{
    const std::string name = m_folderNameBuffer;
    std::string outPath;
    std::string error;
    bool ok = false;

    if (m_folderPopupMode == 1)
    {
        ok = AssetCatalog::CreateFolder(m_folderPopupTarget, name, outPath, error);
    }
    else if (m_folderPopupMode == 2)
    {
        ok = AssetCatalog::RenameFolder(m_folderPopupTarget, name, outPath, error, m_lockCheck);
    }

    if (!ok)
    {
        m_folderError = error;
        return false;
    }

    m_folderError.clear();
    if (m_folderPopupMode == 2)
    {
        // 名前を変えたフォルダを選んでいた(またはその中のモデルを選んでいた)なら、新しいパスへ付け替える。
        m_selectedFolder = RemapPath(m_selectedFolder, m_folderPopupTarget, outPath);
        m_selectedPath = RemapPath(m_selectedPath, m_folderPopupTarget, outPath);
    }
    Rescan();
    if (m_folderPopupMode == 1)
    {
        SelectFolder(outPath);
    }
    m_expandPath = outPath;
    return true;
}

void AssetBrowser::MoveFolderTo(const std::string& source, const std::string& newParent)
{
    std::string outPath;
    std::string error;
    if (!AssetCatalog::MoveFolder(source, newParent, outPath, error, m_lockCheck))
    {
        m_folderError = error;
        return;
    }

    m_folderError.clear();
    m_selectedFolder = RemapPath(m_selectedFolder, source, outPath);
    m_selectedPath = RemapPath(m_selectedPath, source, outPath);
    Rescan();
    m_expandPath = outPath;
    Debug::Info("AssetBrowser: フォルダを移動しました: " + source + " -> " + outPath);
}
