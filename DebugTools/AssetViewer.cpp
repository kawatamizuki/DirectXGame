#include "AssetViewer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include "imgui.h"
#include "Renderer.h"
#include "DayNightCycle.h"
#include "Debug.h"

using namespace DirectX;

namespace
{
    constexpr float kSpacing = 2.2f;          // 並べる間隔(1つぶんの大きさ1.6に、余白0.6)
    constexpr float kLabelHeightOffset = 0.95f; // 名前の札を出す、モデルの中心からの高さ
    constexpr const char* kWindowName = "Asset Viewer";
    constexpr float kPi = 3.14159265f;

    ImTextureRef ToTexture(ID3D11ShaderResourceView* srv)
    {
        return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(srv)));
    }
}

void AssetViewer::Initialize(Renderer* renderer, const DayNightCycle* dayNightCycle)
{
    m_renderer = renderer;
    m_dayNightCycle = dayNightCycle;
    m_preview.Initialize(renderer);
}

size_t AssetViewer::GetLoadedCount() const
{
    size_t count = 0;
    for (const Item& item : m_items)
    {
        if (item.state == Item::State::Loaded) { ++count; }
    }
    return count;
}

size_t AssetViewer::GetFailedCount() const
{
    size_t count = 0;
    for (const Item& item : m_items)
    {
        if (item.state == Item::State::Failed) { ++count; }
    }
    return count;
}

size_t AssetViewer::GetSkippedLargeCount() const
{
    size_t count = 0;
    for (const Item& item : m_items)
    {
        if (item.state == Item::State::SkippedLarge) { ++count; }
    }
    return count;
}

void AssetViewer::Release()
{
    m_items.clear();      // Modelのデストラクタが、GPUのバッファ・テクスチャを解放する
    m_items.shrink_to_fit();
    m_source.clear();
    m_source.shrink_to_fit();
    m_target.Release();
    m_requested = 0;
    m_loadIndex = 0;
    m_totalLoadMs = 0.0f;
    m_selected = -1;
    m_selectedLicense.clear();
}

void AssetViewer::Open(const std::vector<AssetEntry>& models, const std::string& title)
{
    // 今の内容は捨てて、新しい内容に入れ替える。
    m_items.clear();
    m_title = title;
    m_source = models;
    m_requested = models.size();
    m_loadIndex = 0;
    m_totalLoadMs = 0.0f;
    m_selected = -1;
    m_selectedLicense.clear();

    const size_t count = (std::min)(models.size(), static_cast<size_t>((std::max)(1, m_maxModels)));
    m_items.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        m_items[i].entry = models[i];
    }

    Layout();
    FitCamera();
    *m_openFlag = true;
}

void AssetViewer::Layout()
{
    const int count = static_cast<int>(m_items.size());
    m_cols = (std::max)(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>((std::max)(1, count))))));
    m_rows = (std::max)(1, (count + m_cols - 1) / m_cols);

    for (int i = 0; i < count; ++i)
    {
        const int column = i % m_cols;
        const int row = i / m_cols;
        // 手前(+Z側)から見て、左から右・上から下の順に読めるよう並べる(左手座標系では、+Z側から見ると+Xが左になる)。
        m_items[i].slot.x = -(static_cast<float>(column) - (m_cols - 1) * 0.5f) * kSpacing;
        m_items[i].slot.y = 0.0f;
        m_items[i].slot.z = (static_cast<float>(row) - (m_rows - 1) * 0.5f) * kSpacing;
        UpdateTransform(m_items[i]);
    }
}

void AssetViewer::UpdateTransform(Item& item)
{
    if (item.model)
    {
        item.transform = ModelPreviewRenderer::MakeFittedTransform(*item.model, item.slot, m_flipAll ? kPi : 0.0f);
    }
}

void AssetViewer::FitCamera()
{
    m_view.target = { 0.0f, 0.0f, 0.0f };
    m_view.yaw = 0.0f;
    m_view.pitch = 0.8f;

    // 格子全体が、絵の幅にも高さにも収まる距離を求める。縦の視野角は40度(ModelPreviewRenderer::MakeOrbitCamera)。
    // 奥行き方向は、上から見下ろす角度(pitch)の分だけ縮んで見えるので、sin(pitch)を掛ける。
    const float aspect = m_target.IsValid() ? static_cast<float>(m_target.GetWidth()) / static_cast<float>(m_target.GetHeight()) : 1.6f;
    const float halfFovTan = std::tan(XMConvertToRadians(40.0f) * 0.5f);
    const float gridWidth = static_cast<float>(m_cols) * kSpacing + 1.0f;
    const float gridDepth = static_cast<float>(m_rows) * kSpacing * std::sin(m_view.pitch) + 2.0f;
    const float distanceForWidth = gridWidth / (2.0f * halfFovTan * aspect);
    const float distanceForDepth = gridDepth / (2.0f * halfFovTan);
    m_view.distance = (std::max)(4.0f, (std::max)(distanceForWidth, distanceForDepth) * 1.05f);
}

void AssetViewer::LoadOne(Item& item)
{
    const auto start = std::chrono::steady_clock::now();

    // 複数のモデルを開く時は、大きいファイル(街全体のFBXなど)を自動では読まない(数秒、画面が止まるため)。
    if (!m_loadLarge && m_items.size() > 1 && item.entry.fileSize > kLargeFileBytes)
    {
        item.state = Item::State::SkippedLarge;
        return;
    }

    auto model = std::make_unique<Model>();
    bool ok = false;
    {
        Debug::ScopedInfoSuppress quiet; // 大量に読む時に、Logパネルが「読み込んだ」の行で埋まらないように
        ok = model->LoadFromFile(m_renderer->GetDevice(), item.entry.path);
    }
    item.loadMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_totalLoadMs += item.loadMs;

    if (ok && model->GetVertexCount() > 0)
    {
        item.model = std::move(model);
        item.state = Item::State::Loaded;
        UpdateTransform(item);
    }
    else
    {
        item.state = Item::State::Failed;
    }
}

void AssetViewer::Update()
{
    if (!*m_openFlag)
    {
        // 閉じている間は、モデルを持たない(メモリを残さない)。
        if (!m_items.empty() || m_target.IsValid())
        {
            Release();
        }
        return;
    }

    if (!m_renderer || m_loadIndex >= m_items.size())
    {
        return;
    }

    // 時間枠(8ms)を使い切るまで、順に読み込む。最低1つは進める(1つが枠より長くても、止まらずに進む)。
    const auto start = std::chrono::steady_clock::now();
    do
    {
        LoadOne(m_items[m_loadIndex]);
        ++m_loadIndex;
    } while (m_loadIndex < m_items.size() &&
             std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count() < kFrameBudgetMs);
}

void AssetViewer::EnsureTarget(UINT width, UINT height)
{
    if (!m_renderer)
    {
        return;
    }
    if (m_target.IsValid() && m_target.GetWidth() == width && m_target.GetHeight() == height)
    {
        return;
    }
    m_target.Initialize(m_renderer->GetDevice(), width, height);
}

void AssetViewer::DrawDebugUI()
{
    if (!*m_openFlag)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(300.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kWindowName, m_openFlag))
    {
        ImGui::End();
        return;
    }

    DrawControls();
    ImGui::Separator();

    const float infoHeight = 150.0f;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float imageHeight = (std::max)(64.0f, available.y - infoHeight - ImGui::GetStyle().ItemSpacing.y);
    DrawSceneImage(available.x, imageHeight);

    DrawInfoPanel();

    ImGui::End();
}

void AssetViewer::DrawControls()
{
    // 見出しと、読み込みの進み具合。
    const size_t finished = m_loadIndex;
    ImGui::Text("%s", m_title.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  読み込み %zu / %zu(成功 %zu、失敗 %zu、大きいので省略 %zu)  合計 %.0f ms",
        finished, m_items.size(), GetLoadedCount(), GetFailedCount(), GetSkippedLargeCount(), m_totalLoadMs);

    if (m_requested > m_items.size())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%zu 件のうち、最初の %zu 件だけを読み込んでいます(上限を変えて「再読み込み」で増やせます)。",
            m_requested, m_items.size());
    }

    if (ImGui::Button("全体表示"))
    {
        FitCamera();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("全部を背面から", &m_flipAll))
    {
        for (Item& item : m_items) { UpdateTransform(item); }
    }
    ImGui::SameLine();
    ImGui::Checkbox("名前の札", &m_showLabels);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::SliderFloat("背景", &m_backgroundBrightness, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("時刻", &m_timeOfDay, 0.0f, 24.0f, "%.1f 時");

    // 上限と、大きいファイル。変えた時は「再読み込み」で反映する。
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("上限", &m_maxModels, 8, 32);
    m_maxModels = (std::max)(1, (std::min)(kMaxModelsLimit, m_maxModels));
    ImGui::SameLine();
    ImGui::Checkbox("大きいファイルも読む", &m_loadLarge);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("4MBを超えるファイル(街全体のFBXなど)は、複数を開く時は省略します。\n読み込みに数秒かかり、その間、画面が止まります。");
    }
    ImGui::SameLine();
    if (ImGui::Button("再読み込み") && !m_source.empty())
    {
        const std::vector<AssetEntry> source = m_source;
        const std::string title = m_title;
        Open(source, title);
    }
}

void AssetViewer::DrawSceneImage(float width, float height)
{
    const UINT targetWidth = static_cast<UINT>((std::max)(64.0f, std::floor(width / 16.0f) * 16.0f));
    const UINT targetHeight = static_cast<UINT>((std::max)(64.0f, std::floor(height / 16.0f) * 16.0f));
    EnsureTarget(targetWidth, targetHeight);
    if (!m_target.IsValid())
    {
        ImGui::TextUnformatted("描画先を作れませんでした。");
        return;
    }

    // 光は、時刻スライダーの時刻(ゲーム内の時刻とは無関係)。
    LightingState lighting;
    if (m_dayNightCycle)
    {
        lighting = m_dayNightCycle->Evaluate(m_timeOfDay);
    }

    std::vector<ModelPreviewRenderer::SceneModel> models;
    models.reserve(m_items.size());
    for (const Item& item : m_items)
    {
        if (item.state == Item::State::Loaded && item.model)
        {
            ModelPreviewRenderer::SceneModel sceneModel;
            sceneModel.model = item.model.get();
            sceneModel.transform = item.transform;
            models.push_back(sceneModel);
        }
    }
    m_preview.RenderScene(models, m_target, m_view, lighting, m_backgroundBrightness);

    // 描いた絵を、等倍で表示する。
    const ImVec2 imageSize(static_cast<float>(m_target.GetWidth()), static_cast<float>(m_target.GetHeight()));
    const ImVec2 imagePos = ImGui::GetCursorScreenPos();
    ImGui::Image(ToTexture(m_target.GetSRV()), imageSize);

    // 画像の上に見えない領域を重ねて、ドラッグ・ホイール・クリックを受け取る。
    ImGui::SetCursorScreenPos(imagePos);
    ImGui::InvisibleButton("##viewerInput", imageSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    const Camera camera = ModelPreviewRenderer::MakeOrbitCamera(m_view, imageSize.x / imageSize.y);
    const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;

    if (active)
    {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            // 左ドラッグ: 回転。カーソルを右へ動かすと、手前の面が右へ動く向き(つかんで回す感覚)。
            // 左手座標系では、+Z側から見るとカメラが+Xへ回ると、物は右へ回って見える。yawを増やすとカメラは+Xへ動く。
            m_view.yaw += mouseDelta.x * 0.01f;
            m_view.pitch += mouseDelta.y * 0.01f;
            m_view.pitch = (std::max)(-1.5f, (std::min)(1.5f, m_view.pitch));
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))
        {
            // 中/右ドラッグ: 平行移動(カメラの右方向と、真上方向へ)。距離が遠いほど大きく動く。
            const XMFLOAT3 right = camera.GetRight();
            const float scale = m_view.distance * 0.0016f;
            m_view.target.x -= right.x * mouseDelta.x * scale;
            m_view.target.y -= right.y * mouseDelta.x * scale;
            m_view.target.z -= right.z * mouseDelta.x * scale;
            m_view.target.y += mouseDelta.y * scale;
        }
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        // ホイール: ズーム(カメラの距離)。
        m_view.distance *= std::pow(0.9f, ImGui::GetIO().MouseWheel);
        m_view.distance = (std::max)(0.8f, (std::min)(400.0f, m_view.distance));
    }

    // 名前の札(3Dの位置を画面の位置に直して、ImGuiで描く)。ほぼ動かさずにクリックして離した時は、一番近い札を選ぶ。
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y), true);

    const bool clicked = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x +
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y < 16.0f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    // カーソルに一番近い札(これより遠い札は選ばない。60ピクセル)。クリックでの選択と、混んでいる時の札の表示に使う。
    int nearest = -1;
    if (hovered)
    {
        float nearestDistance = 60.0f;
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            const Item& item = m_items[i];
            POINT screen{};
            if (!camera.WorldToScreen(XMFLOAT3(item.slot.x, item.slot.y + kLabelHeightOffset, item.slot.z), m_target.GetWidth(), m_target.GetHeight(), screen))
            {
                continue;
            }
            const float dx = mouse.x - (imagePos.x + static_cast<float>(screen.x));
            const float dy = mouse.y - (imagePos.y + static_cast<float>(screen.y));
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = static_cast<int>(i);
            }
        }
    }
    const int nearestAtFrameStart = nearest;

    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const Item& item = m_items[i];
        const XMFLOAT3 labelWorld(item.slot.x, item.slot.y + kLabelHeightOffset, item.slot.z);
        POINT screen{};
        if (!camera.WorldToScreen(labelWorld, m_target.GetWidth(), m_target.GetHeight(), screen))
        {
            continue;
        }
        const ImVec2 position(imagePos.x + static_cast<float>(screen.x), imagePos.y + static_cast<float>(screen.y));

        const bool selected = (static_cast<int>(i) == m_selected);
        if (!m_showLabels && !selected)
        {
            continue;
        }

        // となりのモデルまでの画面上の幅。札はこの幅に収める(重なって読めなくならないように)。
        // 幅が狭すぎる時(遠くから見ている時)は、選んだ札と、カーソルに一番近い札だけを出す。
        float cellWidth = 1000.0f;
        {
            POINT neighbor{};
            const XMFLOAT3 neighborWorld(labelWorld.x - kSpacing, labelWorld.y, labelWorld.z);
            if (camera.WorldToScreen(neighborWorld, m_target.GetWidth(), m_target.GetHeight(), neighbor))
            {
                cellWidth = std::fabs(static_cast<float>(neighbor.x - screen.x));
            }
        }
        const bool crowded = cellWidth < 28.0f;
        if (crowded && !selected && static_cast<int>(i) != nearestAtFrameStart)
        {
            continue;
        }

        // 状態の印: 読み込み前「...」、失敗、省略。
        std::string text = item.entry.name;
        if (item.state == Item::State::Pending) { text += " ..."; }
        else if (item.state == Item::State::Failed) { text += " [失敗]"; }
        else if (item.state == Item::State::SkippedLarge) { text += " [大きいので省略]"; }
        if (!crowded && !selected)
        {
            // 幅に収まるよう、末尾を「...」で切る。
            const float maxWidth = (std::max)(24.0f, cellWidth - 8.0f);
            while (text.size() > 1 && ImGui::CalcTextSize(text.c_str()).x > maxWidth)
            {
                text.pop_back();
                // 日本語(UTF-8)の途中で切らない: 継続バイトと、その先頭のバイトまで削る。
                while (text.size() > 1 && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) { text.pop_back(); }
                if (text.size() > 1 && static_cast<unsigned char>(text.back()) >= 0xC0) { text.pop_back(); }
            }
        }

        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 boxMin(position.x - textSize.x * 0.5f - 3.0f, position.y - textSize.y - 3.0f);
        const ImVec2 boxMax(position.x + textSize.x * 0.5f + 3.0f, position.y + 1.0f);
        drawList->AddRectFilled(boxMin, boxMax, selected ? IM_COL32(255, 180, 40, 230) : IM_COL32(20, 20, 30, 170), 3.0f);
        drawList->AddText(ImVec2(boxMin.x + 3.0f, boxMin.y + 2.0f), selected ? IM_COL32(20, 20, 20, 255) : IM_COL32(240, 240, 240, 255), text.c_str());
    }
    drawList->PopClipRect();

    if (clicked)
    {
        m_selected = nearest;
        m_selectedLicense = (nearest >= 0) ? AssetCatalog::FindLicenseFile(m_items[nearest].entry) : std::string();
    }
}

void AssetViewer::DrawInfoPanel()
{
    ImGui::BeginChild("##viewerInfo", ImVec2(0.0f, 0.0f), true);
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size()))
    {
        ImGui::TextWrapped("名前の札の近くをクリックすると、そのモデルの情報が出ます。左ドラッグで回転、中/右ドラッグで平行移動、ホイールでズームできます。");
        ImGui::EndChild();
        return;
    }

    const Item& item = m_items[m_selected];
    ImGui::TextUnformatted(item.entry.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", item.entry.path.c_str());

    switch (item.state)
    {
    case Item::State::Pending:
        ImGui::TextUnformatted("読み込み待ちです。");
        break;
    case Item::State::Failed:
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "このモデルを読み込めませんでした。");
        break;
    case Item::State::SkippedLarge:
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "大きいファイル(%.1f MB)なので省略しました。「大きいファイルも読む」→「再読み込み」で読めます。",
            static_cast<double>(item.entry.fileSize) / (1024.0 * 1024.0));
        break;
    case Item::State::Loaded:
    {
        const XMFLOAT3 boundsMin = item.model->GetBoundsMin();
        const XMFLOAT3 boundsMax = item.model->GetBoundsMax();
        const float width = boundsMax.x - boundsMin.x;
        const float height = boundsMax.y - boundsMin.y;
        const float depth = boundsMax.z - boundsMin.z;
        // 1x1マスに収める時の倍率(BuildControllerと同じ: 幅と奥行きのうち、きつい方に合わせる)。
        const float fitToCell = (std::min)(width > 1e-4f ? 1.0f / width : 1e6f, depth > 1e-4f ? 1.0f / depth : 1e6f);

        ImGui::Text("形式: %s%s   パック: %s", item.entry.extension.c_str(), item.entry.duplicate ? "(重複)" : "", item.entry.pack.c_str());
        ImGui::Text("頂点数: %u(三角形 %u)   大きさ: 幅 %.2f x 高さ %.2f x 奥行き %.2f   1x1マスに収める倍率: %.3f",
            item.model->GetVertexCount(), item.model->GetVertexCount() / 3, width, height, depth, fitToCell);
        ImGui::Text("ファイルサイズ: %.1f KB   読み込み時間: %.1f ms", static_cast<double>(item.entry.fileSize) / 1024.0, item.loadMs);
        const std::string& texturePath = item.model->GetMaterial().GetTexturePath();
        ImGui::TextWrapped("テクスチャ: %s", texturePath.empty() ? "(なし)" : texturePath.c_str());
        break;
    }
    }

    if (m_selectedLicense.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "ライセンス文書: 見つかりません(取得元で確認して THIRD_PARTY_LICENSES.md に記録)");
    }
    else
    {
        ImGui::TextWrapped("ライセンス文書: %s", m_selectedLicense.c_str());
    }
    ImGui::EndChild();
}
