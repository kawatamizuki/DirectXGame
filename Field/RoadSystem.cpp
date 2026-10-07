#include <cmath>
#include <cfloat>
#include <algorithm>
#include <sstream>
#include "RoadSystem.h"
#include "GameContext.h"
#include "Field.h"
#include "IOccupancyGrid.h"
#include "IRoadPlacementStrategy.h"
#include "IRoadMeshGenerator.h"
#include "Renderer.h"
#include "InputManager.h"
#include "InputAction.h"
#include "DebugRenderer.h"
#include "Debug.h"
#include "RoadProximity.h"
#include "RoadMeshBuilder.h"
#include "Transform.h"
#include "Camera.h"
#include "GameObject.h"
#include "GridOrientationRegistry.h"
#include "OrientedRectangleOverlap.h"
#include "imgui.h"

using namespace DirectX;

namespace
{
    // 道路⇔建物の向き合わせ(GridOrientationRegistryの主張、近くの建物角度の検索)で
    // 「近く」とみなす距離。セルサイズの2倍程度(調整可能)。
    constexpr float kOrientationAlignDistance = 2.0f;

    // segmentの道路面を表す向き付き矩形(中心線を道幅で膨らませた帯)を返す。
    // endExtensionだけ始点・終点側にも延長できる(行き止まりの見た目の延長に合わせる用)。
    OrientedRect MakeRoadRect(const RoadSegment& segment, float endExtension)
    {
        XMVECTOR dir = XMLoadFloat3(&segment.end) - XMLoadFloat3(&segment.start);
        float length = XMVectorGetX(XMVector3Length(dir));

        OrientedRect rect;
        if (length < 0.0001f)
        {
            // 長さのない区間は、始点を中心にした道幅×道幅の正方形として扱う。
            rect.origin = XMFLOAT3(segment.start.x - segment.width * 0.5f, 0.0f, segment.start.z - segment.width * 0.5f);
            rect.yaw = 0.0f;
            rect.width = segment.width;
            rect.depth = segment.width;
            return rect;
        }

        // 道路のyaw規約(ローカル+Z→ワールド(sin(yaw),0,cos(yaw)))は、RoadSystem内の
        // 他の箇所(ClaimOrientationNearSegmentなど)と共通。ローカル+Xは(cos,-sin)。
        float yaw = atan2f(XMVectorGetX(dir), XMVectorGetZ(dir));
        float c = cosf(yaw);
        float s = sinf(yaw);

        // 始点から、道幅の半分だけローカル-X側へ、延長ぶんだけローカル-Z側へずらした点が最小角。
        float halfWidth = segment.width * 0.5f;
        rect.origin = XMFLOAT3(
            segment.start.x - c * halfWidth - s * endExtension,
            0.0f,
            segment.start.z + s * halfWidth - c * endExtension);
        rect.yaw = yaw;
        rect.width = segment.width;
        rect.depth = length + endExtension * 2.0f;
        return rect;
    }

    // segmentの実際の道幅を考慮した、帯状の矩形が触れるワールドセルを列挙する。
    // ワールドのマス単位の占有マーク(Profilerの占有マス数の表示用)にだけ使う。
    // 道路・建物の空き判定は、このマス単位の情報ではなく矩形どうしの厳密な重なりで行う
    // ([IOccupancyGrid::IsRectFree]、[RoadSystem::IsSegmentPlaceable])。
    std::vector<GridCoord> SampleCellsForSegmentFootprint(const RoadSegment& segment, float cellSize)
    {
        XMVECTOR dir = XMLoadFloat3(&segment.end) - XMLoadFloat3(&segment.start);
        if (XMVectorGetX(XMVector3Length(dir)) < 0.0001f)
        {
            return { WorldToGrid(segment.start, cellSize) };
        }

        return SampleOrientedRectangleCells(MakeRoadRect(segment, 0.0f), cellSize);
    }

    // 点pointから、点lineStart-lineEndを通る直線(線分ではなく無限直線)までの距離。
    float PerpendicularDistanceToLine(const XMFLOAT3& point, const XMFLOAT3& lineStart, const XMFLOAT3& lineEnd)
    {
        XMVECTOR p = XMLoadFloat3(&point);
        XMVECTOR a = XMLoadFloat3(&lineStart);
        XMVECTOR b = XMLoadFloat3(&lineEnd);
        XMVECTOR ab = b - a;

        float abLenSq = XMVectorGetX(XMVector3LengthSq(ab));
        if (abLenSq < 0.0001f)
        {
            return XMVectorGetX(XMVector3Length(p - a));
        }

        float t = XMVectorGetX(XMVector3Dot(p - a, ab)) / abLenSq;
        XMVECTOR closestOnLine = a + ab * t;
        return XMVectorGetX(XMVector3Length(p - closestOnLine));
    }

    // pointsの中間点(先頭・末尾以外)すべてが、先頭→末尾を結ぶ直線からepsilon以内に
    // 収まっているか。収まっていれば「直線のつもりで引いた」とみなせる。
    // Douglas-Peuckerのような間引きだと、実際に丸くカーブさせた区間まで少ない直線の
    // 集まり(多角形)に単純化されてカクついて見えてしまうため、ここでは「全体として
    // 直線とみなせるかどうか」を一括判定するだけに留め、そうでなければ点は一切
    // 動かさない(平滑化された滑らかな形をそのまま保つ)。
    bool IsPathNearlyStraight(const std::vector<XMFLOAT3>& points, float epsilon)
    {
        for (size_t i = 1; i + 1 < points.size(); ++i)
        {
            if (PerpendicularDistanceToLine(points[i], points.front(), points.back()) > epsilon)
            {
                return false;
            }
        }
        return true;
    }
}

void RoadSystem::Initialize(
    GameContext* context,
    Field* field,
    IOccupancyGrid* occupancy,
    IRoadPlacementStrategy* straightStrategy,
    IRoadPlacementStrategy* curveStrategy,
    IRoadMeshGenerator* meshGenerator,
    GridOrientationRegistry* orientationRegistry)
{
    m_context = context;
    m_field = field;
    m_occupancy = occupancy;
    m_straightStrategy = straightStrategy;
    m_curveStrategy = curveStrategy;
    m_meshGenerator = meshGenerator;
    m_orientationRegistry = orientationRegistry;
}

size_t RoadSystem::GetMeshVertexCount() const
{
    return m_meshGenerator ? m_meshGenerator->GetVertexCount() : 0;
}

IRoadPlacementStrategy* RoadSystem::GetActiveStrategy() const
{
    return (m_context && m_context->roadDrawMode == RoadDrawMode::Curve) ? m_curveStrategy : m_straightStrategy;
}

RoadSystem::SnapResult RoadSystem::TryFindSnapPoint(const XMFLOAT3& point, RoadDrawMode mode, const XMFLOAT3* pendingStart) const
{
    SnapResult result;
    result.point = point;

    if (!m_field)
    {
        return result;
    }

    bool widthAware = (mode == RoadDrawMode::Curve);

    // セルサイズの6割程度を吸着半径にする(0.4だと3D視点からのクリックでは
    // 正確に狙うのが難しく、意図して繋げたい時に繋がらない=孤立した道路に
    // 見えてしまう問題があったため拡大した)。
    // widthAware(カーブモードのみtrue)の時は、区間ごとの道幅の半分をさらに加える。
    // 太い道路(Large等)は中心線から離れた道路面の上にカーソルがあっても、
    // 基準距離だけでは届かず吸着しないことがあったため。直線モードは吸着感が
    // 変わらないよう、従来通りwidthAware=falseのまま(道幅を考慮しない)にしている。
    float snapRadius = m_field->GetCellSize() * 0.6f;
    float widthFactor = widthAware ? 0.5f : 0.0f;

    // ①既存の端点への吸着(優先。そのまま繋がるだけで区間の分割は不要)
    float bestDist = FLT_MAX;
    bool foundEndpoint = false;
    XMFLOAT3 bestEndpoint{};

    XMVECTOR pointVec = XMLoadFloat3(&point);

    for (const RoadSegment& segment : m_segments)
    {
        float acceptRadius = snapRadius + segment.width * widthFactor;
        const XMFLOAT3* candidates[2] = { &segment.start, &segment.end };
        for (const XMFLOAT3* candidate : candidates)
        {
            XMVECTOR candidateVec = XMLoadFloat3(candidate);
            float dist = XMVectorGetX(XMVector3Length(candidateVec - pointVec));

            if (dist <= acceptRadius && dist < bestDist)
            {
                bestDist = dist;
                bestEndpoint = *candidate;
                foundEndpoint = true;
            }
        }
    }

    if (foundEndpoint)
    {
        result.point = bestEndpoint;
        result.snapped = true;
        return result;
    }

    // ②既存区間の途中への吸着(T字路分岐。確定時にSplitSegmentAtが必要)
    std::optional<NearestRoadPoint> nearestOnRoad = FindNearestPointOnRoad(m_segments, point, snapRadius, widthFactor);
    if (nearestOnRoad.has_value())
    {
        const RoadSegment& targetSegment = m_segments[nearestOnRoad->segmentIndex];

        if (mode == RoadDrawMode::Straight)
        {
            // 直線モードでは、区間の途中への分岐点をカーソルの生の投影位置にせず、
            // その区間の始点からの距離をセル単位に丸めた位置に揃える。区間の始点・
            // 長さは元々セル単位(始点はセル中心、長さはセルの整数倍)で作られているため、
            // この丸めた点は必ずその区間上のセルの区切り/中心と一致し、道路を引いた時と
            // 同じグリッドに乗る(区間の中点に固定する案も試したが、区間が偶数セル長とは
            // 限らないためグリッドから外れることがあり、かえって不自然になったためやめた)。
            XMVECTOR segStart = XMLoadFloat3(&targetSegment.start);
            XMVECTOR segEnd = XMLoadFloat3(&targetSegment.end);
            XMVECTOR segDir = segEnd - segStart;
            float segLength = XMVectorGetX(XMVector3Length(segDir));

            if (segLength > 0.0001f)
            {
                segDir = XMVector3Normalize(segDir);
                float projected = XMVectorGetX(XMVector3Dot(pointVec - segStart, segDir));

                float cellSize = m_field->GetCellSize();
                float snappedDistance = roundf(projected / cellSize) * cellSize;
                if (snappedDistance < 0.0f) { snappedDistance = 0.0f; }
                if (snappedDistance > segLength) { snappedDistance = segLength; }

                XMVECTOR snappedPoint = segStart + segDir * snappedDistance;
                XMStoreFloat3(&result.point, snappedPoint);
            }
            else
            {
                result.point = nearestOnRoad->point;
            }
        }
        else
        {
            result.point = nearestOnRoad->point;
        }

        result.snapped = true;
        result.requiresSplit = true;
        result.splitSegmentIndex = nearestOnRoad->segmentIndex;
        return result;
    }

    // ③既存道路のどちらにも当てはまらなかった場合のフォールバック(直線モードのみ)。
    // カーブモードはここで何もせず生の点のまま返す(自由描画を保つため)。
    if (mode == RoadDrawMode::Straight)
    {
        if (!pendingStart)
        {
            // 1点目(起点)を探している: 最も近いセルの中心にスナップする
            // (建物もセルの中心に配置されるため、基準点を合わせることで道路と建物の
            // 位置関係が直感的になる)。
            float cellSize = m_field->GetCellSize();
            result.point = GridToWorld(WorldToGrid(point, cellSize), cellSize);
        }
        else
        {
            // 2点目(終点)を、*pendingStartから見て探している: 近くに建物があればその
            // 向きに角度を合わせ、無ければ15度刻みに丸める。どちらの場合も距離は
            // セル単位(cellSizeの整数倍)に丸める。距離を自由にしていると、区間の長さが
            // 半端になり1点目(セル中心)基準の位置関係がズレて見えるため、
            // グリッド単位できっちり引けるように距離も揃える。
            XMVECTOR startVec = XMLoadFloat3(pendingStart);
            XMVECTOR toPoint = pointVec - startVec;

            XMVECTOR dir;
            std::optional<float> nearbyBuildingYaw = FindNearbyBuildingYaw(point);
            if (nearbyBuildingYaw.has_value())
            {
                // 建物のyaw(Transform::rotation.y)は、ローカル+Z(前方)が
                // ワールド(sin(yaw), 0, cos(yaw))に対応する規約(BuildControllerと同じ)。
                // 建物の「正面」軸だけを使うと、建物の横方向(正面と直角)に向かって
                // ドラッグした時、実際のドラッグ方向とほぼ直角になってしまい、
                // 投影距離がほぼ0(=道路が置けない)になる問題があった。
                // そのため正面軸と、その90度回転(側面軸)の両方を候補にし、
                // 実際にドラッグしている方向により近い方を採用する。
                float yaw = *nearbyBuildingYaw;
                XMVECTOR forwardAxis = XMVectorSet(sinf(yaw), 0.0f, cosf(yaw), 0.0f);
                XMVECTOR sideAxis = XMVectorSet(cosf(yaw), 0.0f, -sinf(yaw), 0.0f);

                float forwardDot = fabsf(XMVectorGetX(XMVector3Dot(toPoint, forwardAxis)));
                float sideDot = fabsf(XMVectorGetX(XMVector3Dot(toPoint, sideAxis)));

                dir = (forwardDot >= sideDot) ? forwardAxis : sideAxis;
            }
            else
            {
                float rawAngle = atan2f(XMVectorGetZ(toPoint), XMVectorGetX(toPoint));
                float step = XMConvertToRadians(15.0f);
                float snappedAngle = roundf(rawAngle / step) * step;
                dir = XMVectorSet(cosf(snappedAngle), 0.0f, sinf(snappedAngle), 0.0f);
            }

            float projected = XMVectorGetX(XMVector3Dot(toPoint, dir));
            if (projected < 0.0f)
            {
                // 建物の向きは前後どちらでもよい「軸」として扱うため、カーソルの
                // 実際の方向に合わせてdirを反転する(15度刻みの場合はrawAngleが
                // 元々カーソル方向由来なのでここに来ることは通常無い)。
                dir = -dir;
                projected = -projected;
            }

            float cellSize = m_field->GetCellSize();
            float snappedDistance = roundf(projected / cellSize) * cellSize;

            XMVECTOR snappedPoint = startVec + dir * snappedDistance;
            XMStoreFloat3(&result.point, snappedPoint);
        }
    }

    return result;
}

void RoadSystem::RegisterSegmentEndpoint(const XMFLOAT3& point, size_t segmentIndex)
{
    int nodeId = m_nodeIndex.FindNode(point, m_nodes);
    if (nodeId == -1)
    {
        RoadNode newNode;
        newNode.position = point;
        nodeId = static_cast<int>(m_nodes.size());
        m_nodes.push_back(newNode);
        m_nodeIndex.RegisterNode(point, nodeId);
    }

    m_nodes[nodeId].connectedSegmentIndices.push_back(segmentIndex);
}

void RoadSystem::SplitSegmentAt(size_t segmentIndex, const XMFLOAT3& splitPoint)
{
    if (segmentIndex >= m_segments.size())
    {
        return;
    }

    RoadSegment original = m_segments[segmentIndex];

    RoadSegment firstHalf = original;
    firstHalf.end = splitPoint;

    RoadSegment secondHalf = original;
    secondHalf.start = splitPoint;

    m_segments[segmentIndex] = firstHalf;
    size_t secondHalfIndex = m_segments.size();
    m_segments.push_back(secondHalf);

    // 元のB端点ノードは、分割前はsegmentIndex(A→B)に繋がっていたが、
    // 分割後はsecondHalfIndex(splitPoint→B)に繋がる形に変わるため、参照を付け替える。
    int nodeB = m_nodeIndex.FindNode(original.end, m_nodes);
    if (nodeB != -1)
    {
        std::vector<size_t>& connections = m_nodes[nodeB].connectedSegmentIndices;
        connections.erase(std::remove(connections.begin(), connections.end(), segmentIndex), connections.end());
        connections.push_back(secondHalfIndex);
    }

    // 分割点(既存ノードがあればそこに追加、無ければ新規ノードを作る)にfirstHalf/secondHalfを登録する。
    RegisterSegmentEndpoint(splitPoint, segmentIndex);
    RegisterSegmentEndpoint(splitPoint, secondHalfIndex);
}

std::vector<XMFLOAT3> RoadSystem::CollectConnectionPoints(
    const XMFLOAT3& start, bool startRequiresSplit,
    const XMFLOAT3& end, bool endConnects) const
{
    std::vector<XMFLOAT3> points;

    // 始点が既存の道路に繋がっているのは、既存区間の途中への吸着(分割待ち)か、
    // 既存の端点ノードの上にある場合(自由描画の途中の小区間の始点も、直前の小区間の
    // 終点ノードの上にあるのでここに含まれる)。
    if (startRequiresSplit || m_nodeIndex.FindNode(start, m_nodes) != -1)
    {
        points.push_back(start);
    }

    // 終点は、既存の道路に吸着した時だけ接続点として扱う(何もない場所で誤って
    // 既存の道路に重ねた場合は、そのまま拒否したい)。
    if (endConnects)
    {
        points.push_back(end);
    }

    return points;
}

bool RoadSystem::IsSegmentPlaceable(const RoadSegment& segment, const std::vector<XMFLOAT3>& connectionPoints) const
{
    if (!m_field || !m_occupancy)
    {
        return false;
    }

    float cellSize = m_field->GetCellSize();

    // ①建物には重ならない。道路面(行き止まりの見た目の延長ぶんも含む)を、建物の実際の
    // 形と厳密に比較する。ぴったり隣接するのはOK。以前はワールドのマス単位の占有と
    // 照合していたため、斜め道路のそばでは実際には空いている場所まで拒否されていた。
    if (!m_occupancy->IsRectFree(MakeRoadRect(segment, cellSize * 0.5f), IOccupancyGrid::kOccupantBuilding))
    {
        return false;
    }

    // ②他の道路は、接続先以外を横切らない。新しい区間の中心線(ごく細い矩形)が、既存区間の
    // 路面と重なっていたら拒否する。ただし、接続点(始点・終点)を含む既存区間は
    // 「今まさに繋がる相手」なので対象外にする(T字路・端点・分割点のどれでも、
    // 接続点がその区間の路面上にあるかどうかで一律に判定できる)。
    // 道路どうしは路面が重なるのが当たり前の関係(接続部分)なので、建物と違い
    // 路面全体ではなく中心線で見る。
    RoadSegment centerline = segment;
    centerline.width = cellSize * 0.02f;
    OrientedRect centerlineRect = MakeRoadRect(centerline, 0.0f);

    constexpr float kEpsilon = 0.0001f;
    constexpr float kContainTolerance = 0.01f;

    for (const RoadSegment& existing : m_segments)
    {
        OrientedRect existingRect = MakeRoadRect(existing, 0.0f);

        bool isConnectionTarget = false;
        for (const XMFLOAT3& point : connectionPoints)
        {
            if (OrientedRectContainsPoint(existingRect, point.x, point.z, kContainTolerance))
            {
                isConnectionTarget = true;
                break;
            }
        }
        if (isConnectionTarget)
        {
            continue;
        }

        if (OrientedRectsOverlap(centerlineRect, existingRect, kEpsilon))
        {
            return false;
        }
    }

    return true;
}

void RoadSystem::ClaimOrientationNearSegment(const RoadSegment& segment, size_t ownSegmentIndex) const
{
    if (!m_orientationRegistry || !m_field)
    {
        return;
    }

    XMVECTOR dir = XMLoadFloat3(&segment.end) - XMLoadFloat3(&segment.start);
    float length = XMVectorGetX(XMVector3Length(dir));
    if (length < 0.0001f)
    {
        return;
    }
    float yaw = atan2f(XMVectorGetX(dir), XMVectorGetZ(dir));

    float cellSize = m_field->GetCellSize();

    // このマス群の原点は区間全体で共通(区間自身の始点を基準にする)。
    // ComputeChunkOriginは始点(道路の中心線上)をローカルマス(0,0)の中心に置く原点を返すため、
    // この原点で作るローカルグリッドは必ずこの区間の実際の位置に揃い、
    // ローカルマスの列番号0が道路の中心線を挟んだ列、±mが道路の両脇の列になる。
    XMFLOAT3 origin = GridOrientationRegistry::ComputeChunkOrigin(segment.start, yaw, cellSize);

    // 建物が建つ「道路の縁に接する最初の列」の番号m。列0の中心が中心線上にあるので、
    // 道路の縁は中心線から道幅/2、列mの手前の辺は(m - 0.5)*cellSize。縁以降から始まる
    // 最小のmを選ぶ(幅1.0以下ならm=1、Large(1.6)ならm=2)。以前は常に±1だったため、
    // Largeでは道路自身に食い込んだマスが青く表示されてしまっていた。
    // 道路自身の列(0)は主張しない(建物を置けない場所を「建てられる場所」の目印として
    // 水色にしないため)。
    float edgeFromCenterline = segment.width * 0.5f;
    int sideColumn = static_cast<int>(std::ceil((edgeFromCenterline + cellSize * 0.5f) / cellSize - 0.0001f));
    if (sideColumn < 1)
    {
        sideColumn = 1;
    }

    // 長さ方向のマス番号。始点は列の中心(ローカルz=cellSize/2)にあり、終点はそこからlengthの位置。
    // 以前はワールドのチャンクへ向けてサンプル点を取っていたため、斜めではサンプル点同士が
    // 同じワールドチャンクに入って歯抜けになった。ローカルのマス番号を整数で直接全部
    // 回すので、斜めでも隙間は構造的に生じない。終点がマスの境界にちょうど乗る場合は、
    // 隣のマスは触れるだけなので含めない(-0.0001f)。
    int lastRow = static_cast<int>(std::ceil((cellSize * 0.5f + length) / cellSize - 0.0001f)) - 1;
    if (lastRow < 0)
    {
        lastRow = 0;
    }

    // 他の道路の路面と重なるマスは作らない(交差部分に水色のマスが乗らないように)。
    // 同じ道の隣の区間とも、折れ曲がりの内側では重なる分は作らないのが正しい。
    std::vector<OrientedRect> otherRoadRects;
    otherRoadRects.reserve(m_segments.size());
    for (size_t i = 0; i < m_segments.size(); ++i)
    {
        if (i != ownSegmentIndex)
        {
            otherRoadRects.push_back(MakeRoadRect(m_segments[i], 0.0f));
        }
    }

    float overlapTolerance = cellSize * 0.05f;

    for (int row = 0; row <= lastRow; ++row)
    {
        for (int side = -1; side <= 1; side += 2)
        {
            int column = side * sideColumn;

            GridOrientationRegistry::OrientedCell cell;
            cell.yaw = yaw;
            cell.origin = origin;
            cell.localCellX = column;
            cell.localCellZ = row;
            OrientedRect cellRect = GridOrientationRegistry::MakeCellRect(cell, cellSize);

            bool overlapsOtherRoad = false;
            for (const OrientedRect& roadRect : otherRoadRects)
            {
                if (OrientedRectsOverlap(cellRect, roadRect, overlapTolerance))
                {
                    overlapsOtherRoad = true;
                    break;
                }
            }
            if (overlapsOtherRoad)
            {
                continue;
            }

            m_orientationRegistry->TryClaimCell(cellSize, yaw, origin, column, row);
        }
    }
}

std::optional<float> RoadSystem::FindNearbyBuildingYaw(const XMFLOAT3& point) const
{
    if (!m_context || !m_context->objects)
    {
        return std::nullopt;
    }

    XMVECTOR pointVec = XMLoadFloat3(&point);
    float bestDistSq = kOrientationAlignDistance * kOrientationAlignDistance;
    bool found = false;
    float bestYaw = 0.0f;

    for (const GameObject& obj : *m_context->objects)
    {
        if (obj.kind != ObjectKind::Building)
        {
            continue;
        }

        XMVECTOR objVec = XMLoadFloat3(&obj.transform.position);
        float distSq = XMVectorGetX(XMVector3LengthSq(objVec - pointVec));
        if (distSq <= bestDistSq)
        {
            bestDistSq = distSq;
            bestYaw = obj.transform.rotation.y;
            found = true;
        }
    }

    if (!found)
    {
        return std::nullopt;
    }

    return bestYaw;
}

bool RoadSystem::IsPerpendicularEnoughForSplit(const RoadSegment& approachSegment, size_t targetSegmentIndex) const
{
    if (targetSegmentIndex >= m_segments.size())
    {
        return true;
    }

    const RoadSegment& target = m_segments[targetSegmentIndex];

    XMVECTOR approachDir = XMLoadFloat3(&approachSegment.end) - XMLoadFloat3(&approachSegment.start);
    XMVECTOR targetDir = XMLoadFloat3(&target.end) - XMLoadFloat3(&target.start);

    float approachLenSq = XMVectorGetX(XMVector3LengthSq(approachDir));
    float targetLenSq = XMVectorGetX(XMVector3LengthSq(targetDir));
    if (approachLenSq < 0.0001f || targetLenSq < 0.0001f)
    {
        // どちらかの長さがほぼ0だと角度を評価できないので、判定せず許可する。
        return true;
    }

    approachDir = XMVector3Normalize(approachDir);
    targetDir = XMVector3Normalize(targetDir);

    float dot = XMVectorGetX(XMVector3Dot(approachDir, targetDir));

    // 直角(dot=0)から±30度程度までを許可する(cos(60度)=0.5)。
    constexpr float kMaxDeviationFromPerpendicular = 0.5f;
    return fabsf(dot) <= kMaxDeviationFromPerpendicular;
}

bool RoadSystem::CommitSegment(
    RoadSegment segment,
    bool startRequiresSplit, size_t startSplitIndex,
    bool endConnects, bool endRequiresSplit, size_t endSplitIndex,
    const std::vector<XMFLOAT3>& chainConnectionPoints)
{
    float cellSize = m_field->GetCellSize();

    // 建物には重ならず、接続先以外の道路は横切らない(道路だけが相手なら接続できる)。
    // 接続点は、この区間自身の始点・終点に加えて、呼び出し側が渡す自由描画の連鎖全体の
    // 接続点(ループ状に戻ってくる軌跡でも、出発した道路のそばを通れるように)も使う。
    std::vector<XMFLOAT3> connectionPoints = chainConnectionPoints;
    std::vector<XMFLOAT3> ownPoints = CollectConnectionPoints(segment.start, startRequiresSplit, segment.end, endConnects);
    connectionPoints.insert(connectionPoints.end(), ownPoints.begin(), ownPoints.end());

    if (!IsSegmentPlaceable(segment, connectionPoints))
    {
        return false;
    }

    // ワールドのマス単位の占有マーク(Profilerの占有マス数の表示用。空き判定には使わない)。
    std::vector<GridCoord> occupiedCells = SampleCellsForSegmentFootprint(segment, cellSize);
    m_occupancy->SetOccupiedRange(occupiedCells, true);

    // 空き判定用に、道路面そのものの形(矩形)を登録する。始点・終点側は、
    // 行き止まりの見た目の延長(RebuildMeshのcellSize/2)と同じだけ延ばした形にする
    // (軸に揃った道路では、延長した先がちょうどマスの境界になる)。
    // 後で区間が分割されても、和集合の形は変わらないので登録は1回で足りる。
    m_occupancy->OccupyRect(MakeRoadRect(segment, cellSize * 0.5f), IOccupancyGrid::kOccupantRoad);

    // 分割が必要な吸着(始点/終点)より先に、この区間自体をノード登録しておく。
    // 分割対象の区間はまだ分割されていないため、分割点にはまだノードが無い。
    // ここで先に作っておくことで、直後のSplitSegmentAt側がそれを見つけて使い回せる。
    size_t newIndex = m_segments.size();
    m_segments.push_back(segment);

    RegisterSegmentEndpoint(segment.start, newIndex);
    RegisterSegmentEndpoint(segment.end, newIndex);

    // 新しい道路が覆った既存の青マスは消し(道路の上にマスが残らないように)、
    // そのうえでこの区間の両脇のマスを登録する。既に他の道路/建物のマスがある場所には
    // 登録されない(先着優先=元々あった方のマスを残す)。建物配置がこれを参照して
    // グリッドの基準角度を道路に揃える。
    if (m_orientationRegistry)
    {
        m_orientationRegistry->RemoveCellsOverlapping(MakeRoadRect(segment, 0.0f), cellSize);
    }
    ClaimOrientationNearSegment(segment, newIndex);

    // 既知の制約: 始点・終点が同じ区間の途中に吸着した場合は正しく分割されない(v1の割り切り)。
    if (startRequiresSplit)
    {
        SplitSegmentAt(startSplitIndex, segment.start);
    }
    if (endRequiresSplit)
    {
        SplitSegmentAt(endSplitIndex, segment.end);
    }

    // 区間の内容が変わったので見た目のメッシュも作り直す(頻度は道路配置と同程度で、毎フレームではない)。
    // 行き止まりの見た目は、セル半分ぶん伸ばして隣接セルの端までしっかり届くようにする
    // (直線モードの端点がセルの中心に来るため、伸ばさないと行き止まりがマス目の
    // 半分で途切れて見えてしまう)。
    if (m_meshGenerator && m_context && m_context->renderer && m_field)
    {
        m_meshGenerator->RebuildMesh(m_context->renderer->GetDevice(), m_segments, m_nodes, m_field->GetCellSize() * 0.5f);
    }

    return true;
}

void RoadSystem::Update()
{
    m_hasLastHit = false;

    if (!m_context || !m_field || !m_occupancy || !m_straightStrategy || !m_curveStrategy)
    {
        return;
    }

    if (m_context->input->IsActionPressed(InputAction::Cancel))
    {
        GetActiveStrategy()->Reset();
        m_pendingStartRequiresSplit = false;
        m_curveDragActive = false;
        CancelPendingCurve();
    }

    // Buildingツールが選択されている間は道路配置を行わない(BuildControllerにクリックを譲る)
    if (m_context->placementTool != PlacementTool::Road)
    {
        return;
    }

    // 確認アイコン表示中は、確定/キャンセルされるまで新しいドラッグを始めさせない
    // (アイコンのImGuiボタンが通常WantCaptureMouseを占有するはずだが、念のため明示的にガードする)。
    if (m_curveAwaitingConfirm)
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

    // 既存の道路の端点/途中が近くにあれば、そこに吸着させて確実に繋がるようにする
    // (曲線モードではドラッグの始点・終点にのみ使う。ドラッグ中の生の軌跡には使わない)。
    // 直線モードでは、2点目(終点)を探している時だけ角度スナップの基準として
    // pendingStart(1点目)を渡す(1点目自身を探している時はnullptrのまま)。
    bool isCurveMode = (m_context->roadDrawMode == RoadDrawMode::Curve);
    XMFLOAT3 pendingStart{};
    bool hasPendingStart = !isCurveMode && m_straightStrategy->HasPendingStart(pendingStart);
    SnapResult snap = TryFindSnapPoint(
        hitPoint,
        isCurveMode ? RoadDrawMode::Curve : RoadDrawMode::Straight,
        hasPendingStart ? &pendingStart : nullptr);

    if (isCurveMode)
    {
        UpdateCurveMode(hitPoint, snap);
    }
    else
    {
        UpdateStraightMode(hitPoint, snap);
    }
}

void RoadSystem::UpdateStraightMode(const XMFLOAT3& /*hitPoint*/, const SnapResult& snap)
{
    XMFLOAT3 effectivePoint = snap.point;

    m_lastHitPoint = effectivePoint;
    m_hasLastHit = true;
    m_lastHitIsSnapped = snap.snapped;

    // カーソル位置(または置きかけの区間全体)が配置可能かどうかを判定する。
    // 置きかけの始点があれば「始点→今のカーソル位置」の区間全体で、
    // 無ければ今のカーソル位置の1マスだけで判定する。
    // CommitSegmentと同じ判定(IsSegmentPlaceable)を使うことで、
    // 「繋がる場所なのにマーカーが赤く見える」という表示と実際の判定のズレを防ぐ。
    {
        XMFLOAT3 pendingStartForHover;
        float cellSize = m_field->GetCellSize();
        if (m_straightStrategy->HasPendingStart(pendingStartForHover))
        {
            RoadSegment hoverSegment;
            hoverSegment.start = pendingStartForHover;
            hoverSegment.end = effectivePoint;
            hoverSegment.width = GetRoadTypeDefinition(m_context->roadType).width;

            std::vector<XMFLOAT3> connectionPoints = CollectConnectionPoints(
                pendingStartForHover, m_pendingStartRequiresSplit, effectivePoint, snap.snapped);
            m_hoverValid = IsSegmentPlaceable(hoverSegment, connectionPoints);
        }
        else
        {
            // これから引く1点目。既存の道路に吸着していれば、そこから伸ばせることを示すため
            // 常に「置ける」表示にする(実際の判定は2点目確定時にCommitSegmentが行う)。
            // 吸着していなければ、そのマス1つ分が建物・道路のどちらにも重ならないことを見る。
            OrientedRect hoverCellRect;
            hoverCellRect.origin = GridToWorldCorner(WorldToGrid(effectivePoint, cellSize), cellSize);
            hoverCellRect.yaw = 0.0f;
            hoverCellRect.width = cellSize;
            hoverCellRect.depth = cellSize;
            m_hoverValid = snap.snapped || m_occupancy->IsRectFree(hoverCellRect);
        }
    }

    if (!m_context->input->IsActionPressed(InputAction::Decide))
    {
        return;
    }

    std::optional<RoadSegment> result = m_straightStrategy->OnPlacementPoint(effectivePoint);
    if (!result.has_value())
    {
        // 1点目が確定した(まだ2点目待ち)。この点が既存区間の途中への吸着だったら、
        // 2点目が確定した時に使うため分割対象を覚えておく。
        m_pendingStartRequiresSplit = snap.requiresSplit;
        m_pendingStartSplitSegmentIndex = snap.splitSegmentIndex;

        m_context->inputConsumed = true;
        return;
    }

    RoadSegment segment = *result;
    segment.type = m_context->roadType;
    segment.width = GetRoadTypeDefinition(m_context->roadType).width;
    segment.drawMode = RoadDrawMode::Straight;

    bool committed = CommitSegment(
        segment,
        m_pendingStartRequiresSplit, m_pendingStartSplitSegmentIndex,
        snap.snapped, snap.requiresSplit, snap.splitSegmentIndex);

    if (!committed)
    {
        // 拒否されたことが分かるようログに残す(見た目のフィードバックは今後の課題)。
        // ストラテジーもリセットしておかないと、次のクリックが「2点目」として
        // 扱われず新しい道路の1点目として扱われてしまい紛らわしいため明示的に揃える。
        Debug::Log("RoadSystem: 経路上に既存の建物/道路があるため道路の配置を拒否しました");
        m_straightStrategy->Reset();
    }

    m_pendingStartRequiresSplit = false;
    m_context->inputConsumed = true;
}

void RoadSystem::UpdateCurveMode(const XMFLOAT3& hitPoint, const SnapResult& snap)
{
    // ドラッグ開始前(まだ押していない)は、直線モードの1点目と同じく吸着後の点を
    // カーソル/マーカーの見た目にも使う。これにより「＝」マーカーや丸カーソルが
    // 実際にドラッグを始めた時の始点と同じ位置に出るようになる(今までは生のhitPointを
    // 使っていたため、吸着していてもマーカーは別の位置に出ており、実際に押すと
    // 始点がズレて見える原因になっていた)。ドラッグ中は生のカーソル位置を使う
    // (ドラッグ中の軌跡そのものは吸着しない設計のため)。
    XMFLOAT3 effectivePoint = m_curveDragActive ? hitPoint : snap.point;

    m_lastHitPoint = effectivePoint;
    m_hasLastHit = true;
    m_lastHitIsSnapped = snap.snapped;

    // ホバー表示: ドラッグ中は「直前に確定した点→今のカーソル位置」の範囲で判定する。
    // CommitSegmentと同じ判定(IsSegmentPlaceable)を使う
    // (ドラッグ中の終点は常に生の点=接続点ではないのでendConnects=false)。
    {
        XMFLOAT3 pendingStartForHover;
        float cellSize = m_field->GetCellSize();
        if (m_curveDragActive && m_curveStrategy->HasPendingStart(pendingStartForHover))
        {
            RoadSegment hoverSegment;
            hoverSegment.start = pendingStartForHover;
            hoverSegment.end = hitPoint;
            hoverSegment.width = GetRoadTypeDefinition(m_context->roadType).width;

            // 既存区間の途中から始めたドラッグは、最初の小区間の始点だけがその分割点。
            bool startIsSplitPoint = m_pendingStartRequiresSplit && m_curvePendingSegments.empty();
            std::vector<XMFLOAT3> connectionPoints = CollectConnectionPoints(
                pendingStartForHover, startIsSplitPoint, hitPoint, false);
            m_hoverValid = IsSegmentPlaceable(hoverSegment, connectionPoints);
        }
        else
        {
            // これからドラッグを始める点。既存の道路に吸着していれば、そこから伸ばせることを
            // 示すため常に「置ける」表示にする。吸着していなければ、そのマス1つ分が
            // 建物・道路のどちらにも重ならないことを見る。
            OrientedRect hoverCellRect;
            hoverCellRect.origin = GridToWorldCorner(WorldToGrid(effectivePoint, cellSize), cellSize);
            hoverCellRect.yaw = 0.0f;
            hoverCellRect.width = cellSize;
            hoverCellRect.depth = cellSize;
            m_hoverValid = snap.snapped || m_occupancy->IsRectFree(hoverCellRect);
        }
    }

    // ドラッグ開始: 始点だけは既存道路への吸着を効かせる
    if (m_context->input->IsActionPressed(InputAction::Decide))
    {
        m_curveStrategy->Reset();
        m_curveStrategy->OnPlacementPoint(snap.point); // 始点として記録するだけ(戻り値は常にnullopt)

        m_pendingStartRequiresSplit = snap.requiresSplit;
        m_pendingStartSplitSegmentIndex = snap.splitSegmentIndex;
        m_curveDragActive = true;
        m_curvePendingSegments.clear();

        // TEMP DEBUG: ドラッグ開始点で本当に吸着判定できているかを見るための一時ログ。
        {
            std::ostringstream oss;
            oss << "[TEMP] カーブ開始 snapped=" << (snap.snapped ? "true" : "false")
                << " requiresSplit=" << (snap.requiresSplit ? "true" : "false")
                << " hitPoint=(" << hitPoint.x << "," << hitPoint.z << ")"
                << " snapPoint=(" << snap.point.x << "," << snap.point.z << ")";
            Debug::Log(oss.str());
        }

        m_context->inputConsumed = true;
        return;
    }

    // ドラッグ継続: 生のカーソル位置をそのまま渡す(吸着はしない。ストラテジー内部で平滑化される)。
    // ここでは確定(CommitSegment)せず、バッファに積むだけにする(離した時にまとめて検証・確定する)。
    if (m_curveDragActive && m_context->input->IsActionDown(InputAction::Decide))
    {
        std::optional<RoadSegment> result = m_curveStrategy->OnPlacementPoint(hitPoint);
        if (result.has_value())
        {
            RoadSegment segment = *result;
            segment.type = m_context->roadType;
            segment.width = GetRoadTypeDefinition(m_context->roadType).width;
            segment.drawMode = RoadDrawMode::Curve;
            m_curvePendingSegments.push_back(segment);
        }

        RebuildCurveGhostMesh();

        m_context->inputConsumed = true;
        return;
    }

    // ドラッグ終了: まずドラッグ中と同じ経路で最後の小区間をバッファに積み(ここで捨てない)、
    // その上で「直前の確定点→吸着後の終点」の残りわずかな距離だけを最後に繋ぐ。
    // (以前はここで「ドラッグ開始点→離した点」を一直線で繋いでしまい、ドラッグ中に
    // 描いた経路が丸ごと無視されて直線1本が即置かれたように見えるバグがあった)
    // まだ何もCommitSegmentしない: バッファが確定するのはユーザーが確認アイコンを押した時。
    if (m_curveDragActive && m_context->input->IsActionReleased(InputAction::Decide))
    {
        std::optional<RoadSegment> interiorResult = m_curveStrategy->OnPlacementPoint(hitPoint);
        if (interiorResult.has_value())
        {
            RoadSegment interiorSegment = *interiorResult;
            interiorSegment.type = m_context->roadType;
            interiorSegment.width = GetRoadTypeDefinition(m_context->roadType).width;
            interiorSegment.drawMode = RoadDrawMode::Curve;
            m_curvePendingSegments.push_back(interiorSegment);
        }

        XMFLOAT3 lastConfirmed;
        if (m_curveStrategy->HasPendingStart(lastConfirmed))
        {
            XMVECTOR startVec = XMLoadFloat3(&lastConfirmed);
            XMVECTOR endVec = XMLoadFloat3(&snap.point);
            float distance = XMVectorGetX(XMVector3Length(endVec - startVec));

            // ほぼ動かさずに離した場合は何もしない(点1つだけの道路を作らない)
            if (distance > 0.001f)
            {
                RoadSegment segment;
                segment.start = lastConfirmed;
                segment.end = snap.point;
                segment.type = m_context->roadType;
                segment.width = GetRoadTypeDefinition(m_context->roadType).width;
                segment.drawMode = RoadDrawMode::Curve;
                m_curvePendingSegments.push_back(segment);

                m_curvePendingEndConnects = snap.snapped;
                m_curvePendingEndRequiresSplit = snap.requiresSplit;
                m_curvePendingEndSplitIndex = snap.splitSegmentIndex;

                // TEMP DEBUG: 離した時点で終点が吸着したかを見るための一時ログ。
                {
                    std::ostringstream oss;
                    oss << "[TEMP] カーブ終了 snapped=" << (snap.snapped ? "true" : "false")
                        << " requiresSplit=" << (snap.requiresSplit ? "true" : "false")
                        << " hitPoint=(" << hitPoint.x << "," << hitPoint.z << ")"
                        << " snapPoint=(" << snap.point.x << "," << snap.point.z << ")";
                    Debug::Log(oss.str());
                }
            }
        }

        m_curveStrategy->Reset();
        m_curveDragActive = false;

        if (m_curvePendingSegments.empty())
        {
            // 何も積まれなかった(ドラッグせずクリックだけだった等)。確定するものが無いのでそのまま終える。
            // カーブモードは直線モードと違い、押した状態でマウスを動かして離す「ドラッグ」操作が
            // 必要(2回クリックするだけでは区間が1つも生成されない)。
            Debug::Log("[TEMP] カーブ: ドラッグ量が無かったため何も生成されませんでした(クリックだけでなく押したままドラッグしてください)");
            m_pendingStartRequiresSplit = false;
        }
        else
        {
            // ここではまだ確定しない。ゴーストを最新化し、確認アイコンの表示に切り替える。
            // 手ブレ対策(直線のつもりの時だけ均す)はGetEffectiveCurvePendingSegments側で
            // 毎フレーム計算しており、ここでm_curvePendingSegments自体を書き換える必要はない
            // (ドラッグ中のゴーストと確定結果が常に同じ計算式を通るようにするため)。
            RebuildCurveGhostMesh();
            m_curveAwaitingConfirm = true;
        }

        m_context->inputConsumed = true;
    }
}

std::vector<RoadSegment> RoadSystem::GetEffectiveCurvePendingSegments() const
{
    if (m_curvePendingSegments.size() < 2)
    {
        return m_curvePendingSegments;
    }

    std::vector<XMFLOAT3> points;
    points.reserve(m_curvePendingSegments.size() + 1);
    points.push_back(m_curvePendingSegments.front().start);
    for (const RoadSegment& seg : m_curvePendingSegments)
    {
        points.push_back(seg.end);
    }

    // 始点→終点を結ぶ直線から見て、全ての中間点がこの範囲内に収まっていれば
    // 「直線のつもりで引いた」とみなし、手ブレを完全に均した1本の直線にする。
    constexpr float kStraightnessEpsilon = 0.12f;
    if (!IsPathNearlyStraight(points, kStraightnessEpsilon))
    {
        // 実際に曲げている場合は何もせず、平滑化済みの滑らかな形をそのまま返す。
        return m_curvePendingSegments;
    }

    RoadSegment straight = m_curvePendingSegments.front();
    straight.end = m_curvePendingSegments.back().end;
    return { straight };
}

bool RoadSystem::IsPendingCurvePathValid() const
{
    if (m_curvePendingSegments.empty())
    {
        return false;
    }

    std::vector<RoadSegment> effectiveSegments = GetEffectiveCurvePendingSegments();

    // 既存道路の「途中」への接続(分割が必要な接続)は、ほぼ直角に交わる時だけ許可する。
    // 浅い角度の枝分かれはT字路の継ぎ目がミターされず隙間/めくれが目立つ見た目になるため。
    // 行き止まり(端点)への接続はこの制限を受けない(そちらは次数2でミターされるため問題ない)。
    if (m_pendingStartRequiresSplit &&
        !IsPerpendicularEnoughForSplit(effectiveSegments.front(), m_pendingStartSplitSegmentIndex))
    {
        return false;
    }
    if (m_curvePendingEndConnects && m_curvePendingEndRequiresSplit &&
        !IsPerpendicularEnoughForSplit(effectiveSegments.back(), m_curvePendingEndSplitIndex))
    {
        return false;
    }

    // 始点・終点それぞれが既存道路に接続する場合、その接続先の道路は「今まさに繋がる相手」
    // なので、横切り扱いで弾かないようにする。ループ状に戻ってくる軌跡だと、出発した道路の
    // そばを最初の区間以外でも通ることがあるため、この接続点は全区間の判定に共通で使う。
    std::vector<XMFLOAT3> connectionPoints = CollectConnectionPoints(
        effectiveSegments.front().start, m_pendingStartRequiresSplit,
        effectiveSegments.back().end, m_curvePendingEndConnects);

    for (const RoadSegment& segment : effectiveSegments)
    {
        if (!IsSegmentPlaceable(segment, connectionPoints))
        {
            return false;
        }
    }

    return true;
}

void RoadSystem::RebuildCurveGhostMesh()
{
    if (!m_context || !m_context->renderer)
    {
        return;
    }

    if (m_curvePendingSegments.empty())
    {
        // まだ小区間が1つも確定していない(ドラッグ直後のごく短い間)。
        // 既に空ならCreateFromVerticesを毎フレーム呼んでログを増やさないようにする。
        if (m_curveGhostModel.GetVertexCount() != 0)
        {
            m_curveGhostModel.CreateFromVertices(m_context->renderer->GetDevice(), {});
        }
        return;
    }

    bool valid = IsPendingCurvePathValid();
    XMFLOAT4 color = valid
        ? XMFLOAT4(0.3f, 0.9f, 0.4f, 1.0f)
        : XMFLOAT4(0.9f, 0.25f, 0.25f, 1.0f);

    // 表示・有効判定・確定のすべてで同じ形を使うため、手ブレ補正後の実効的な区間列を使う
    // (直線のつもりで引いた時だけ均され、実際に曲げた場合はそのままの滑らかな形になる)。
    std::vector<RoadSegment> effectiveSegments = GetEffectiveCurvePendingSegments();

    // ゴーストの継ぎ目もミターする。カーブは細かい小区間の連続なので、ミター無しだと
    // 「前の細かい分割状態」がそのまま見えてしまい、繋がって見えない/ガタガタに見える。
    // そのため確定済みのm_segments/m_nodesに未確定のeffectiveSegmentsを仮想的に
    // つなげた一時リストを作り、境界(既存道路との継ぎ目)・内部(小区間同士の継ぎ目)の
    // 両方をミター対象にする。ただし実際に頂点を出力するのはpending分だけにして、
    // 確定済みの道路を二重に(半透明で上書き)描画しないようにする(emitStartIndex)。
    std::vector<RoadSegment> combinedSegments = m_segments;
    size_t pendingStartIndex = combinedSegments.size();
    combinedSegments.insert(combinedSegments.end(), effectiveSegments.begin(), effectiveSegments.end());

    std::vector<RoadNode> combinedNodes = m_nodes;

    // 内部の継ぎ目: 小区間i→i+1は常にi.end==(i+1).startで繋がっているので、
    // その点を合成ノードとして登録する(次数2の素直な連続=ミター対象)。
    for (size_t i = 0; i + 1 < effectiveSegments.size(); ++i)
    {
        RoadNode chainNode;
        chainNode.position = effectiveSegments[i].end;
        chainNode.connectedSegmentIndices = { pendingStartIndex + i, pendingStartIndex + i + 1 };
        combinedNodes.push_back(chainNode);
    }

    // 始点が既存道路の「端点」に吸着していた場合(途中への吸着=分割待ちの場合は
    // まだ実ノードが無いので対象外)、その既存ノードに先頭区間も繋げて境界もミターする。
    if (!effectiveSegments.empty() && !m_pendingStartRequiresSplit)
    {
        int existingNodeId = m_nodeIndex.FindNode(effectiveSegments.front().start, m_nodes);
        if (existingNodeId != -1)
        {
            combinedNodes[existingNodeId].connectedSegmentIndices.push_back(pendingStartIndex);
        }
    }

    // 終点が既存道路の「端点」に吸着していた場合も同様に境界をミターする。
    if (!effectiveSegments.empty() && m_curvePendingEndConnects && !m_curvePendingEndRequiresSplit)
    {
        int existingNodeId = m_nodeIndex.FindNode(effectiveSegments.back().end, m_nodes);
        if (existingNodeId != -1)
        {
            combinedNodes[existingNodeId].connectedSegmentIndices.push_back(combinedSegments.size() - 1);
        }
    }

    // 確定後(QuadRoadMeshGenerator側)と同じ行き止まり伸長を適用し、ゴーストと確定結果の
    // 見た目がズレないようにする。
    float deadEndExtension = m_field ? m_field->GetCellSize() * 0.5f : 0.0f;
    std::vector<Vertex> vertices = BuildRoadMeshVertices(combinedSegments, combinedNodes, color, pendingStartIndex, deadEndExtension);

    // vertices.empty()のまま(既にゴーストが空の状態)なら、CreateFromVerticesの
    // 「vertices is empty」警告ログを毎フレーム増やさないよう呼び出し自体をスキップする。
    if (vertices.empty() && m_curveGhostModel.GetVertexCount() == 0)
    {
        return;
    }

    m_curveGhostModel.CreateFromVertices(m_context->renderer->GetDevice(), vertices);
}

void RoadSystem::ConfirmPendingCurve()
{
    if (m_curvePendingSegments.empty() || !IsPendingCurvePathValid())
    {
        return;
    }

    // ゴースト表示・有効判定と全く同じ実効的な区間列を確定にも使う(直線のつもりの時だけ
    // 手ブレを均した形になる。ここがズレるとプレビューと実際の確定結果が食い違う)。
    std::vector<RoadSegment> effectiveSegments = GetEffectiveCurvePendingSegments();

    // TEMP DEBUG: 吸着が本当に効いているか(始点/終点が既存ノードと同じ扱いになるか)を
    // 確定の前後で比較して調べるための一時ログ。原因特定後に削除する。
    XMFLOAT3 dragStartPoint = effectiveSegments.front().start;
    XMFLOAT3 dragEndPoint = effectiveSegments.back().end;
    int startNodeIdBefore = m_nodeIndex.FindNode(dragStartPoint, m_nodes);
    int endNodeIdBefore = m_curvePendingEndConnects ? m_nodeIndex.FindNode(dragEndPoint, m_nodes) : -1;
    {
        std::ostringstream oss;
        oss << "[TEMP] ConfirmPendingCurve開始: segments=" << effectiveSegments.size()
            << " startRequiresSplit=" << (m_pendingStartRequiresSplit ? "true" : "false")
            << " startNodeIdBefore=" << startNodeIdBefore
            << " endConnects=" << (m_curvePendingEndConnects ? "true" : "false")
            << " endRequiresSplit=" << (m_curvePendingEndRequiresSplit ? "true" : "false")
            << " endNodeIdBefore=" << endNodeIdBefore;
        Debug::Log(oss.str());
    }

    size_t lastIndex = effectiveSegments.size() - 1;

    // 始点・終点それぞれの接続先の道路は、横切り扱いで拒否しない
    // (IsPendingCurvePathValidと全く同じ組み立て方。ここが揃っていないと
    // 「プレビューは緑(確定できる)なのに実際は拒否される」というズレが起きるため、
    // 全区間に対して同じ接続点を使う。確定の途中で区間が分割されても、接続点は
    // 分割後のどちらの半分の路面上にもあるので、そのまま使える)。
    std::vector<XMFLOAT3> chainConnectionPoints = CollectConnectionPoints(
        dragStartPoint, m_pendingStartRequiresSplit, dragEndPoint, m_curvePendingEndConnects);

    for (size_t i = 0; i < effectiveSegments.size(); ++i)
    {
        bool isFirst = (i == 0);
        bool isLast = (i == lastIndex);

        bool startRequiresSplit = isFirst && m_pendingStartRequiresSplit;
        size_t startSplitIndex = isFirst ? m_pendingStartSplitSegmentIndex : 0;

        bool endConnects = isLast && m_curvePendingEndConnects;
        bool endRequiresSplit = isLast && m_curvePendingEndRequiresSplit;
        size_t endSplitIndex = isLast ? m_curvePendingEndSplitIndex : 0;

        bool committed = CommitSegment(
            effectiveSegments[i],
            startRequiresSplit, startSplitIndex,
            endConnects, endRequiresSplit, endSplitIndex,
            chainConnectionPoints);

        if (!committed)
        {
            // IsPendingCurvePathValidで事前検証済みのため通常起こらないはずだが、
            // 念のためログだけ残してこれ以上は確定しない。
            Debug::Log("RoadSystem: カーブの確定中に予期しない配置拒否が発生しました");
            break;
        }
    }

    // TEMP DEBUG: 確定後、始点/終点が実際にどのノードに属しているか(次数=接続本数)を確認する。
    // 次数が1のまま(自分の区間だけ)なら、見た目は繋がっていても内部的には孤立している。
    {
        int startNodeIdAfter = m_nodeIndex.FindNode(dragStartPoint, m_nodes);
        int endNodeIdAfter = m_nodeIndex.FindNode(dragEndPoint, m_nodes);
        size_t startDegree = (startNodeIdAfter != -1) ? m_nodes[startNodeIdAfter].connectedSegmentIndices.size() : 0;
        size_t endDegree = (endNodeIdAfter != -1) ? m_nodes[endNodeIdAfter].connectedSegmentIndices.size() : 0;

        std::ostringstream oss;
        oss << "[TEMP] ConfirmPendingCurve完了: startNodeId " << startNodeIdBefore << "->" << startNodeIdAfter
            << " degree=" << startDegree
            << " / endNodeId " << endNodeIdBefore << "->" << endNodeIdAfter
            << " degree=" << endDegree
            << " (degree>=2なら既存ノードと共有、1なら孤立)";
        Debug::Log(oss.str());
    }

    CancelPendingCurve();
}

void RoadSystem::CancelPendingCurve()
{
    m_curvePendingSegments.clear();
    m_curveAwaitingConfirm = false;
    m_pendingStartRequiresSplit = false;
    m_curvePendingEndConnects = false;
    m_curvePendingEndRequiresSplit = false;
    m_curvePendingEndSplitIndex = 0;

    // 既に空なら(CreateFromVerticesの「vertices is empty」警告を増やさないよう)何もしない。
    if (m_context && m_context->renderer && m_curveGhostModel.GetVertexCount() != 0)
    {
        m_curveGhostModel.CreateFromVertices(m_context->renderer->GetDevice(), {});
    }
}

void RoadSystem::Draw()
{
    if (!m_context || !m_meshGenerator)
    {
        return;
    }

    m_meshGenerator->Draw(*m_context->renderer, *m_context->camera);

    // カーソル位置に置ける/置けないかを丸マーカーで表示する(緑=置ける、赤=置けない)。
    // 本格的なゲームらしい見た目のカーソル表現は今後の課題。
    if (m_hasLastHit)
    {
        constexpr int kCircleSegments = 16;
        float radius = m_field ? m_field->GetCellSize() * 0.25f : 0.25f;
        XMFLOAT4 markerColor = m_hoverValid
            ? XMFLOAT4(0.2f, 1.0f, 0.2f, 1.0f)
            : XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f);

        XMFLOAT3 prevPoint(
            m_lastHitPoint.x + radius,
            0.0f,
            m_lastHitPoint.z);

        for (int i = 1; i <= kCircleSegments; ++i)
        {
            float angle = (static_cast<float>(i) / static_cast<float>(kCircleSegments)) * XM_2PI;
            XMFLOAT3 nextPoint(
                m_lastHitPoint.x + cosf(angle) * radius,
                0.0f,
                m_lastHitPoint.z + sinf(angle) * radius);

            m_context->debugRenderer->AddLine(prevPoint, nextPoint, markerColor);
            prevPoint = nextPoint;
        }
    }

    // 吸着先の端点をマーカーで表示(繋がることが視覚的にわかるように)。
    // 「X」だと設置不可のバツ印と誤解されやすいため、「=」(平行な2本線)にしている。
    if (m_hasLastHit && m_lastHitIsSnapped)
    {
        float markerSize = m_field ? m_field->GetCellSize() * 0.15f : 0.15f;
        XMFLOAT3 c = m_lastHitPoint;
        XMFLOAT4 markerColor(0.2f, 0.8f, 1.0f, 1.0f);

        m_context->debugRenderer->AddLine(
            XMFLOAT3(c.x - markerSize, 0.0f, c.z - markerSize * 0.4f),
            XMFLOAT3(c.x + markerSize, 0.0f, c.z - markerSize * 0.4f),
            markerColor);
        m_context->debugRenderer->AddLine(
            XMFLOAT3(c.x - markerSize, 0.0f, c.z + markerSize * 0.4f),
            XMFLOAT3(c.x + markerSize, 0.0f, c.z + markerSize * 0.4f),
            markerColor);
    }

    // 置きかけの始点→カーソルの黄色い線は直線モードのみで表示する
    // (曲線モードは下のゴーストメッシュがこの役割を兼ねる)。
    if (m_context->roadDrawMode == RoadDrawMode::Straight)
    {
        XMFLOAT3 pendingStart;
        IRoadPlacementStrategy* activeStrategy = GetActiveStrategy();
        if (activeStrategy && activeStrategy->HasPendingStart(pendingStart) && m_hasLastHit)
        {
            m_context->debugRenderer->AddLine(
                pendingStart,
                m_lastHitPoint,
                XMFLOAT4(1.0f, 1.0f, 0.2f, 1.0f));
        }
    }

    // カーブドラッグ中〜確認待ちの間、確定前の道路を半透明のゴーストとして表示する
    // (建物配置のゴーストと同じalpha=0.5の描画経路を再利用する)。
    if ((m_curveDragActive || m_curveAwaitingConfirm) && m_curveGhostModel.GetVertexCount() > 0)
    {
        Transform ghostTransform;
        m_context->renderer->DrawModel(m_curveGhostModel, ghostTransform, *m_context->camera, 0.5f);
    }

    if (m_curveAwaitingConfirm)
    {
        DrawConfirmIcon();
    }
}

void RoadSystem::DrawConfirmIcon()
{
    if (m_curvePendingSegments.empty())
    {
        return;
    }

    POINT screenPos;
    XMFLOAT3 anchor = m_curvePendingSegments.back().end;
    bool onScreen = m_context->camera->WorldToScreen(
        anchor,
        m_context->renderer->GetWindowWidth(),
        m_context->renderer->GetWindowHeight(),
        screenPos);

    if (!onScreen)
    {
        return;
    }

    // アイコンがカーソル/道路の真上に重ならないよう、少し横にずらして表示する。
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(screenPos.x) + 24.0f, static_cast<float>(screenPos.y) - 16.0f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("##RoadCurveConfirm", nullptr, flags);

    bool valid = IsPendingCurvePathValid();

    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("確定"))
    {
        ConfirmPendingCurve();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("取消"))
    {
        CancelPendingCurve();
    }

    if (!valid)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "既存の道路/建物と重なっています");
    }

    ImGui::End();
}
