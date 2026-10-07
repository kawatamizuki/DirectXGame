#include <cmath>
#include <algorithm>
#include "BuildController.h"
#include "GameContext.h"
#include "Field.h"
#include "IOccupancyGrid.h"
#include "GameObjectFactory.h"
#include "Model.h"
#include "Renderer.h"
#include "InputManager.h"
#include "InputAction.h"
#include "DebugRenderer.h"
#include "RoadSystem.h"
#include "RoadProximity.h"
#include "GridOrientationRegistry.h"
#include "OrientedRectangleOverlap.h"
#include "imgui.h"

using namespace DirectX;

namespace
{
    // 近くの道路/GridOrientationRegistryを「近い」とみなす距離。RoadSystem.cppの
    // kOrientationAlignDistanceと同じ考え方の値(セルサイズの2倍程度、調整可能)。
    constexpr float kRoadAlignDistance = 2.0f;
}

void BuildController::Initialize(
    GameContext* context, Field* field, IOccupancyGrid* occupancy,
    RoadSystem* roadSystem, GridOrientationRegistry* orientationRegistry,
    Model* houseModel, Model* officeModel, Model* shopModel)
{
    m_context = context;
    m_field = field;
    m_occupancy = occupancy;
    m_roadSystem = roadSystem;
    m_orientationRegistry = orientationRegistry;
    m_houseModel = houseModel;
    m_officeModel = officeModel;
    m_shopModel = shopModel;
}

Model* BuildController::GetModelForType(BuildingType type) const
{
    switch (type)
    {
    case BuildingType::House:  return m_houseModel;
    case BuildingType::Office: return m_officeModel;
    case BuildingType::Shop:   return m_shopModel;
    }

    return m_houseModel;
}

std::vector<GridCoord> BuildController::GetFootprintCells(const GridCoord& cell, int widthCells, int depthCells) const
{
    std::vector<GridCoord> cells;
    cells.reserve(static_cast<size_t>(widthCells) * static_cast<size_t>(depthCells));

    for (int dz = 0; dz < depthCells; ++dz)
    {
        for (int dx = 0; dx < widthCells; ++dx)
        {
            cells.push_back(GridCoord{ cell.x + dx, cell.z + dz });
        }
    }

    return cells;
}

void BuildController::RotateSelection()
{
    m_selectedRotationStep = (m_selectedRotationStep + 1) % 4;
}

BuildController::PlacementFrame BuildController::ComputePlacementFrame(const XMFLOAT3& hitPoint) const
{
    PlacementFrame frame; // 既定: yaw=0, origin=(0,0,0)(今まで通りのワールド軸グリッド)

    if (!m_orientationRegistry || !m_field)
    {
        return frame;
    }

    float cellSize = m_field->GetCellSize();

    // ①既にこのチャンクの向きが確定していれば、それを使う(早い者勝ちで固定済み)。
    std::optional<GridOrientationRegistry::OrientedCell> locked = m_orientationRegistry->GetOrientation(hitPoint, cellSize);
    if (locked.has_value())
    {
        frame.yaw = locked->yaw;
        frame.origin = locked->origin;
        return frame;
    }

    // ②まだ未確定なら、近くの道路を探してその向きを提案する
    // (ここではまだ確定しない。実際に建物を置いた時だけClaimOrientationする)。
    if (m_roadSystem)
    {
        std::optional<NearestRoadPoint> nearest =
            FindNearestPointOnRoad(m_roadSystem->GetSegments(), hitPoint, kRoadAlignDistance);
        if (nearest.has_value())
        {
            const RoadSegment& segment = m_roadSystem->GetSegments()[nearest->segmentIndex];
            XMVECTOR dir = XMLoadFloat3(&segment.end) - XMLoadFloat3(&segment.start);
            if (XMVectorGetX(XMVector3LengthSq(dir)) > 0.0001f)
            {
                dir = XMVector3Normalize(dir);
                // 建物のyaw(Transform::rotation.y)は、ローカル+Z(前方)が
                // ワールド(sin(yaw), 0, cos(yaw))に対応する規約(RoadSystemと同じ)。
                frame.yaw = atan2f(XMVectorGetX(dir), XMVectorGetZ(dir));
                // 原点は道路区間自身の始点(=中心線上の点)を基準に求める。hitPoint
                // ではなくsegment.startを使うことで、実際に配置を確定してClaimOrientation
                // する時([Update]内)と全く同じ原点になり、プレビューと結果がズレない。
                frame.origin = GridOrientationRegistry::ComputeChunkOrigin(segment.start, frame.yaw, cellSize);
            }
        }
    }

    // ③どちらも無ければ、既定のワールド軸(yaw=0)のまま。
    return frame;
}

XMFLOAT3 BuildController::LocalToWorld(const XMFLOAT3& localPoint, const PlacementFrame& frame) const
{
    XMVECTOR localVec = XMLoadFloat3(&localPoint);
    XMVECTOR rotationQuat = XMQuaternionRotationRollPitchYaw(0.0f, frame.yaw, 0.0f);
    XMVECTOR worldVec = XMVector3Rotate(localVec, rotationQuat) + XMLoadFloat3(&frame.origin);

    XMFLOAT3 result;
    XMStoreFloat3(&result, worldVec);
    return result;
}

OrientedRect BuildController::MakeFootprintRect(
    const GridCoord& cell, int widthCells, int depthCells,
    const PlacementFrame& frame, float cellSize) const
{
    XMFLOAT3 localOrigin = GridToWorldCorner(cell, cellSize); // frameのローカル座標系での矩形の最小角

    OrientedRect rect;
    rect.origin = LocalToWorld(localOrigin, frame);
    rect.yaw = frame.yaw;
    rect.width = static_cast<float>(widthCells) * cellSize;
    rect.depth = static_cast<float>(depthCells) * cellSize;
    return rect;
}

BuildController::PlacementPreview BuildController::ComputePlacement(const XMFLOAT3& hitPoint) const
{
    PlacementPreview result;

    if (!m_field || !m_occupancy)
    {
        return result;
    }

    const BuildingDefinition& def = GetBuildingDefinition(m_selectedType);
    float cellSize = m_field->GetCellSize();

    result.frame = ComputePlacementFrame(hitPoint);

    // hitPointをframeのローカル座標に変換してからセルを求める
    // (frame.yaw==0かつorigin=(0,0,0)の時は、今までのWorldToGridと完全に同じ結果になる)。
    XMVECTOR hitVec = XMLoadFloat3(&hitPoint);
    XMVECTOR originVec = XMLoadFloat3(&result.frame.origin);
    XMVECTOR inverseRotationQuat = XMQuaternionRotationRollPitchYaw(0.0f, -result.frame.yaw, 0.0f);
    XMVECTOR localHitVec = XMVector3Rotate(hitVec - originVec, inverseRotationQuat);
    XMFLOAT3 localHit;
    XMStoreFloat3(&localHit, localHitVec);

    GridCoord cell = WorldToGrid(localHit, cellSize);

    // 90/270度回転時は、グリッド上で占有する幅と奥行きが入れ替わる
    // (モデル自身の縮尺には影響しない。あくまでグリッド上の占有範囲の話)。
    bool footprintSwapped = (m_selectedRotationStep % 2) != 0;
    int effectiveWidthCells = footprintSwapped ? def.footprintDepthCells : def.footprintWidthCells;
    int effectiveDepthCells = footprintSwapped ? def.footprintWidthCells : def.footprintDepthCells;

    result.localCell = cell;
    result.localWidthCells = effectiveWidthCells;
    result.localDepthCells = effectiveDepthCells;
    result.footprintRect = MakeFootprintRect(cell, effectiveWidthCells, effectiveDepthCells, result.frame, cellSize);

    // ワールドのマス単位の占有セルは、フィールド範囲内かの判定と、道路描画側の
    // (保守的な)占有判定への登録にだけ使う。
    result.footprintCells = SampleOrientedRectangleCells(result.footprintRect, cellSize);

    bool allInBounds = true;
    for (const GridCoord& footprintCell : result.footprintCells)
    {
        if (!m_field->IsInBounds(footprintCell))
        {
            allInBounds = false;
            break;
        }
    }

    // 空いているかの判定は、ワールドのマス単位ではなく矩形どうしの厳密な重なりで行う。
    // 斜めの道路のすぐ隣に回転して置く建物は、ワールドのマス単位で見ると道路と同じマスに
    // 触れてしまうため(以前はこれが原因で斜め道路沿いに全く置けなかった)、実際の形で比較する。
    result.isValid = allInBounds && m_occupancy->IsRectFree(result.footprintRect);

    XMFLOAT3 localCorner = GridToWorldCorner(cell, cellSize);
    float worldFootprintWidth = static_cast<float>(effectiveWidthCells) * cellSize;
    float worldFootprintDepth = static_cast<float>(effectiveDepthCells) * cellSize;
    XMFLOAT3 localFootprintCenter(localCorner.x + worldFootprintWidth * 0.5f, 0.0f, localCorner.z + worldFootprintDepth * 0.5f);
    XMFLOAT3 footprintCenter = LocalToWorld(localFootprintCenter, result.frame);

    Model* model = GetModelForType(m_selectedType);

    // 90度刻みの回転は、この場所の基準角度(frame.yaw)からの相対値になる。
    float yawRadians = result.frame.yaw + XMConvertToRadians(static_cast<float>(m_selectedRotationStep) * 90.0f);
    result.transform.scale = { 1.0f, 1.0f, 1.0f };
    result.transform.position = footprintCenter;
    result.transform.SetRotationEuler({ 0.0f, yawRadians, 0.0f });

    if (model)
    {
        // モデルの実測バウンディングボックスから、原点位置に関わらずfootprintの中心・
        // 地面基準(GetHeightAt、将来の高低差地形にも自動追従)に揃える。
        // Ground配置(Core/Game.cpp)と全く同じ考え方の一般化。
        XMFLOAT3 boundsMin = model->GetBoundsMin();
        XMFLOAT3 boundsMax = model->GetBoundsMax();
        float modelWidth = boundsMax.x - boundsMin.x;
        float modelDepth = boundsMax.z - boundsMin.z;

        if (modelWidth > 0.0001f && modelDepth > 0.0001f)
        {
            // フィットスケールはモデル自身のローカル座標系の話であり、回転(向き)の影響を受けない。
            // そのため常に「回転前の」footprintWidthCells/DepthCellsと比較する。
            // Windows.hのminマクロと衝突するためstd::minは使わない(RoadSystem.cppと同じ理由)。
            float rawFootprintWidth = static_cast<float>(def.footprintWidthCells) * cellSize;
            float rawFootprintDepth = static_cast<float>(def.footprintDepthCells) * cellSize;
            float scaleForWidth = rawFootprintWidth / modelWidth;
            float scaleForDepth = rawFootprintDepth / modelDepth;
            float fitScale = (scaleForWidth < scaleForDepth) ? scaleForWidth : scaleForDepth;

            // モデルのローカル中心(XZ)を、実際に描画で使う回転量(基準角度+90度刻み)で
            // 回してからfootprint中心に合わせる。自前の符号計算をせず、エンジンの
            // 既存のクォータニオン演算を使うことで、どんな原点位置のモデルでも
            // (将来footprintが正方形でない建物でも)正しく中心が合う。
            float centerX = (boundsMin.x + boundsMax.x) * 0.5f;
            float centerZ = (boundsMin.z + boundsMax.z) * 0.5f;
            XMVECTOR localCenterVec = XMVectorSet(centerX * fitScale, 0.0f, centerZ * fitScale, 0.0f);
            XMVECTOR rotationQuat = XMQuaternionRotationRollPitchYaw(0.0f, yawRadians, 0.0f);
            XMVECTOR rotatedCenterVec = XMVector3Rotate(localCenterVec, rotationQuat);
            XMFLOAT3 rotatedCenter;
            XMStoreFloat3(&rotatedCenter, rotatedCenterVec);

            result.transform.scale = { fitScale, fitScale, fitScale };
            result.transform.position =
            {
                footprintCenter.x - rotatedCenter.x,
                // Y軸回転はYを変えないため、Y方向のオフセットは回転前と同じ計算でよい
                m_field->GetHeightAt(footprintCenter.x, footprintCenter.z) - boundsMin.y * fitScale,
                footprintCenter.z - rotatedCenter.z
            };
        }
    }

    return result;
}

void BuildController::Update()
{
    m_hasHoverPoint = false;

    if (!m_context || !m_field || !m_occupancy)
    {
        return;
    }

    // Roadツールが選択されている間は建物配置を行わない(RoadSystemにクリックを譲る)
    if (m_context->placementTool != PlacementTool::Building)
    {
        return;
    }

    if (ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    POINT mousePos = m_context->input->GetMousePosition();
    Ray ray = m_context->camera->ScreenPointToRay(
        mousePos,
        m_context->renderer->GetWindowWidth(),
        m_context->renderer->GetWindowHeight());

    XMFLOAT3 hitPoint;
    if (!m_field->RaycastGround(ray, hitPoint))
    {
        return;
    }

    m_hoverPoint = hitPoint;
    m_hasHoverPoint = true;

    if (m_context->input->IsActionPressed(InputAction::Rotate))
    {
        RotateSelection();
    }

    if (m_context->input->IsActionPressed(InputAction::Decide))
    {
        PlacementPreview placement = ComputePlacement(hitPoint);
        if (!placement.isValid)
        {
            return;
        }

        Model* model = GetModelForType(m_selectedType);
        GameObject& spawned = GameObjectFactory::Spawn(*m_context->objects, model, placement.transform, ObjectKind::Building);
        spawned.buildingType = m_selectedType;
        m_occupancy->OccupyRect(placement.footprintRect, IOccupancyGrid::kOccupantBuilding);
        m_occupancy->SetOccupiedRange(placement.footprintCells, true);

        // 建物が実際に占有したローカルマスを、この向き・この原点の格子として登録する
        // (既に別の道路/建物のマスと重なる分は登録されず、先に存在した方が優先される)。
        // これにより、次にこの付近へ配置する建物が同じ向き・同じ格子で揃う。
        // originはComputePlacementFrameで実際に使ったものをそのまま渡す(ここで別途
        // 計算し直すと、プレビューと確定後でズレる可能性があるため)。
        if (m_orientationRegistry && m_field)
        {
            for (int dz = 0; dz < placement.localDepthCells; ++dz)
            {
                for (int dx = 0; dx < placement.localWidthCells; ++dx)
                {
                    m_orientationRegistry->TryClaimCell(
                        m_field->GetCellSize(), placement.frame.yaw, placement.frame.origin,
                        placement.localCell.x + dx, placement.localCell.z + dz);
                }
            }
        }

        m_context->inputConsumed = true;
    }
}

void BuildController::Draw()
{
    // 建物種別・配置ツールの選択UI。
    // 本格的なin-game UIに置き換える前提の仮のImGuiボタン。
    // 他のデバッグパネル(Debug/Hierarchy/Inspector)と重ならない位置に固定表示する
    // (重なると初回起動時に他パネルの下に隠れて見えなくなることがあるため)。
    ImGui::SetNextWindowPos(ImVec2(310.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Build");

    if (m_context)
    {
        int tool = static_cast<int>(m_context->placementTool);
        ImGui::Text("Tool");
        ImGui::RadioButton("Building", &tool, static_cast<int>(PlacementTool::Building));
        ImGui::SameLine();
        ImGui::RadioButton("Road", &tool, static_cast<int>(PlacementTool::Road));
        m_context->placementTool = static_cast<PlacementTool>(tool);
        ImGui::Separator();
    }

    // GridOrientationRegistryで既に基準角度が確定しているチャンクを可視化する
    // (道路/建物のツール選択によらず常に切り替えられるようにここに置く)。
    ImGui::Checkbox("Show orientation chunks (debug)", &m_showOrientationDebug);

    if (m_context && m_context->placementTool == PlacementTool::Road)
    {
        int roadType = static_cast<int>(m_context->roadType);
        ImGui::RadioButton(GetRoadTypeDefinition(RoadType::Narrow).name, &roadType, static_cast<int>(RoadType::Narrow));
        ImGui::RadioButton(GetRoadTypeDefinition(RoadType::Normal).name, &roadType, static_cast<int>(RoadType::Normal));
        ImGui::RadioButton(GetRoadTypeDefinition(RoadType::Large).name, &roadType, static_cast<int>(RoadType::Large));
        m_context->roadType = static_cast<RoadType>(roadType);

        ImGui::Separator();

        // 直線モード: グリッド(セル中心)基準の2点クリックで引く、街の骨格用のメインモード。
        // フリーハンドモード(内部的にはRoadDrawMode::Curve): ドラッグで自由に曲線を引く、
        // 細道・獣道など複雑な道の作り込み用(表示名は「Freehand」だが型名は変更していない)。
        int drawMode = static_cast<int>(m_context->roadDrawMode);
        ImGui::RadioButton("Straight", &drawMode, static_cast<int>(RoadDrawMode::Straight));
        ImGui::SameLine();
        ImGui::RadioButton("Freehand", &drawMode, static_cast<int>(RoadDrawMode::Curve));
        m_context->roadDrawMode = static_cast<RoadDrawMode>(drawMode);
    }
    else
    {
        int selected = static_cast<int>(m_selectedType);
        ImGui::RadioButton(GetBuildingDefinition(BuildingType::House).name, &selected, static_cast<int>(BuildingType::House));
        ImGui::RadioButton(GetBuildingDefinition(BuildingType::Office).name, &selected, static_cast<int>(BuildingType::Office));
        ImGui::RadioButton(GetBuildingDefinition(BuildingType::Shop).name, &selected, static_cast<int>(BuildingType::Shop));
        m_selectedType = static_cast<BuildingType>(selected);

        // Rボタン(InputAction::Rotate)と同じRotateSelection()を呼ぶだけの仮ボタン。
        // 将来ImGuiパネルを本番のin-game UIに置き換えても、呼び出し先のロジックはそのまま使える。
        if (ImGui::Button("Rotate"))
        {
            RotateSelection();
        }
        ImGui::SameLine();
        ImGui::Text("%d degrees", m_selectedRotationStep * 90);
    }
    ImGui::End();

    if (m_showOrientationDebug && m_orientationRegistry && m_field && m_context && m_context->debugRenderer)
    {
        m_orientationRegistry->DrawDebugOverlay(*m_context->debugRenderer, m_field->GetCellSize());
    }

    if (!m_context || !m_field || !m_hasHoverPoint || m_context->placementTool != PlacementTool::Building)
    {
        return;
    }

    // Update()の確定配置と全く同じ計算をここでも使い、ゴーストプレビューと
    // 実際の配置結果がズレないようにする。
    PlacementPreview placement = ComputePlacement(m_hoverPoint);

    // 実際に建つ建物モデルを半透明(ゴースト)で表示する
    Model* model = GetModelForType(m_selectedType);
    if (model)
    {
        m_context->renderer->DrawModel(*model, placement.transform, *m_context->camera, 0.5f);
    }

    // footprint(占有セル範囲)全体を枠として描画する(1x1に限らず何マスでも対応)。
    // 基準座標系(placement.frame)がワールド軸と異なる場合、枠自体もその向きに回転させる。
    float cellSize = m_field->GetCellSize();
    XMVECTOR hitVec = XMLoadFloat3(&m_hoverPoint);
    XMVECTOR originVec = XMLoadFloat3(&placement.frame.origin);
    XMVECTOR inverseRotationQuat = XMQuaternionRotationRollPitchYaw(0.0f, -placement.frame.yaw, 0.0f);
    XMVECTOR localHitVec = XMVector3Rotate(hitVec - originVec, inverseRotationQuat);
    XMFLOAT3 localHit;
    XMStoreFloat3(&localHit, localHitVec);
    GridCoord hoverCell = WorldToGrid(localHit, cellSize);

    bool footprintSwapped = (m_selectedRotationStep % 2) != 0;
    const BuildingDefinition& def = GetBuildingDefinition(m_selectedType);
    float footprintWidth = static_cast<float>(footprintSwapped ? def.footprintDepthCells : def.footprintWidthCells) * cellSize;
    float footprintDepth = static_cast<float>(footprintSwapped ? def.footprintWidthCells : def.footprintDepthCells) * cellSize;

    XMFLOAT3 localCorner = GridToWorldCorner(hoverCell, cellSize);
    XMFLOAT3 localP0(localCorner.x, 0.0f, localCorner.z);
    XMFLOAT3 localP1(localCorner.x + footprintWidth, 0.0f, localCorner.z);
    XMFLOAT3 localP2(localCorner.x + footprintWidth, 0.0f, localCorner.z + footprintDepth);
    XMFLOAT3 localP3(localCorner.x, 0.0f, localCorner.z + footprintDepth);

    XMFLOAT3 p0 = LocalToWorld(localP0, placement.frame);
    XMFLOAT3 p1 = LocalToWorld(localP1, placement.frame);
    XMFLOAT3 p2 = LocalToWorld(localP2, placement.frame);
    XMFLOAT3 p3 = LocalToWorld(localP3, placement.frame);

    XMFLOAT4 color = placement.isValid
        ? XMFLOAT4(0.2f, 1.0f, 0.2f, 1.0f)
        : XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f);

    m_context->debugRenderer->AddLine(p0, p1, color);
    m_context->debugRenderer->AddLine(p1, p2, color);
    m_context->debugRenderer->AddLine(p2, p3, color);
    m_context->debugRenderer->AddLine(p3, p0, color);
}
