#include "RoadMeshBuilder.h"

using namespace DirectX;

namespace
{
    // 地面(y=0)とのZファイティングを避けるための、ごくわずかな底上げ。
    constexpr float kRoadHeightOffset = 0.02f;

    // 同一点とみなす距離(RoadNodeIndexの吸着誤差と揃えている)。
    constexpr float kSamePointEpsilon = 0.001f;

    bool IsNearPoint(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        XMVECTOR va = XMLoadFloat3(&a);
        XMVECTOR vb = XMLoadFloat3(&b);
        float distSq = XMVectorGetX(XMVector3LengthSq(va - vb));
        return distSq <= kSamePointEpsilon * kSamePointEpsilon;
    }

    // 区間の「自前の」縁ベクトル(進行方向に垂直、長さ=道幅の半分)を計算する。長さ0の区間ならゼロベクトル。
    XMVECTOR ComputeNaturalPerpendicular(const RoadSegment& segment)
    {
        XMVECTOR start = XMLoadFloat3(&segment.start);
        XMVECTOR end = XMLoadFloat3(&segment.end);
        XMVECTOR dir = end - start;
        float length = XMVectorGetX(XMVector3Length(dir));

        if (length < 0.0001f)
        {
            return XMVectorZero();
        }

        dir = XMVector3Normalize(dir);
        XMVECTOR perp = XMVectorSet(-XMVectorGetZ(dir), 0.0f, XMVectorGetX(dir), 0.0f);
        return XMVector3Normalize(perp) * (segment.width * 0.5f);
    }

    // 継ぎ目で繋がる2つの縁ベクトル(perpA, perpB。どちらも「向き×半幅」)の向きを
    // 平均化し、継ぎ目の角度に合わせた縁の「向き」だけを返す(長さ=道幅は変えない。
    // 下の関数本体のコメントも参照)。
    XMVECTOR ComputeMiterPerpendicular(XMVECTOR perpA, XMVECTOR perpB)
    {
        float halfWidth = (XMVectorGetX(XMVector3Length(perpA)) + XMVectorGetX(XMVector3Length(perpB))) * 0.5f;
        if (halfWidth < 0.0001f)
        {
            return perpA;
        }

        XMVECTOR dirA = XMVector3Normalize(perpA);
        XMVECTOR dirB = XMVector3Normalize(perpB);

        XMVECTOR sum = dirA + dirB;
        float sumLength = XMVectorGetX(XMVector3Length(sum));

        // ほぼ逆向き(急な折り返し)の場合は平均化が破綻するので、片方をそのまま使う
        if (sumLength < 0.01f)
        {
            return perpA;
        }

        XMVECTOR miterDir = sum / sumLength;

        // 以前はcosHalfAngleで割って長さを伸ばし、隙間ができないようにしていたが、
        // 鋭い角度で交わる継ぎ目(直線モードが斜めに引けるようになり、既存道路と
        // 鋭角で接続するケースが増えた)では、区間の片端だけ幅が広がり、区間全体が
        // 広い方から狭い方への「くさび形」に歪んで見える問題があった。
        // 「道幅は常に一定であるべき」という要求を優先し、ミターは縁の向き(角度に
        // 合わせる)だけを調整し、長さは常に元の半幅のまま変えないことにする。
        // (トレードオフ: 非常に鋭い角度の継ぎ目では、わずかな隙間/重なりが
        // 再び目立つことがあるが、道幅が変わって見えるよりは軽微とみなす。)
        return miterDir * halfWidth;
    }

    // 1区間ぶんの帯状クアッド(2枚の三角形)をverticesに積み足す。
    // startPerp/endPerpは、ミター済みならその値、そうでなければ区間自前の縁ベクトルを渡す。
    void AppendSegmentQuad(
        std::vector<Vertex>& vertices,
        const RoadSegment& segment,
        XMVECTOR startPerp, XMVECTOR endPerp,
        const XMFLOAT4& color)
    {
        XMVECTOR start = XMLoadFloat3(&segment.start);
        XMVECTOR end = XMLoadFloat3(&segment.end);

        float length = XMVectorGetX(XMVector3Length(end - start));
        if (length < 0.0001f)
        {
            return;
        }

        XMVECTOR offsetY = XMVectorSet(0.0f, kRoadHeightOffset, 0.0f, 0.0f);

        XMFLOAT3 p0, p1, p2, p3;
        XMStoreFloat3(&p0, start - startPerp + offsetY);
        XMStoreFloat3(&p1, start + startPerp + offsetY);
        XMStoreFloat3(&p2, end + endPerp + offsetY);
        XMStoreFloat3(&p3, end - endPerp + offsetY);

        // UV: 幅方向は0〜1、長さ方向は区間の長さぶん進める
        // (将来タイル状のアスファルトテクスチャに差し替えた時、自然にタイリングされるように)
        float vLen = length;

        Vertex v0{ p0.x, p0.y, p0.z, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,  color.x, color.y, color.z, color.w };
        Vertex v1{ p1.x, p1.y, p1.z, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,  color.x, color.y, color.z, color.w };
        Vertex v2{ p2.x, p2.y, p2.z, 0.0f, 1.0f, 0.0f, 1.0f, vLen,  color.x, color.y, color.z, color.w };
        Vertex v3{ p3.x, p3.y, p3.z, 0.0f, 1.0f, 0.0f, 0.0f, vLen,  color.x, color.y, color.z, color.w };

        // 2枚の三角形を両方の巻き順で積む(表裏どちらから見ても描画されるようにする)。
        // 上空から見下ろすカメラでは常に表側しか見えないため片面カリングでも実害は無い想定だが、
        // 巻き順(LH座標系+デフォルトのカリング設定)を1発で確実に当てるのが難しいため、
        // 安全側に倒して両面分の三角形を生成する(頂点数は2倍になるがこの規模では無視できる)。
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v0);
        vertices.push_back(v2);
        vertices.push_back(v3);

        vertices.push_back(v0);
        vertices.push_back(v2);
        vertices.push_back(v1);
        vertices.push_back(v0);
        vertices.push_back(v3);
        vertices.push_back(v2);
    }

    // ノード(継ぎ目・T字路・交差点)の位置に、道幅を一辺とする正方形の「キャップ」を
    // 敷く。ミターは角度差が小さいCurve同士の継ぎ目にしか使えず、それ以外
    // (T字路・交差点・直線モードが絡む継ぎ目)は各区間が自前の縁のまま描画されるため、
    // 継ぎ目の角度によっては地面が見えるV字/隙間ができてしまう。
    // 区間の縁は必ずノード位置から道幅の半分(halfWidth)以内に収まるため、
    // 一辺=道幅(半径halfWidthの円を包含するサイズ)の正方形をノード中心に置けば、
    // 角度に関わらずその隙間を必ず覆い隠せる(v1では見た目の美しい面取りより
    // 「隙間ゼロ」を優先する簡易対策)。道路本体よりわずかに高く置き、
    // 同じ高さの三角形同士のZファイティングを避ける。
    void AppendNodeCap(
        std::vector<Vertex>& vertices,
        const RoadNode& node,
        const std::vector<RoadSegment>& segments,
        const XMFLOAT4& color)
    {
        float maxHalfWidth = 0.0f;
        for (size_t segIndex : node.connectedSegmentIndices)
        {
            if (segIndex < segments.size())
            {
                float halfWidth = segments[segIndex].width * 0.5f;
                if (halfWidth > maxHalfWidth)
                {
                    maxHalfWidth = halfWidth;
                }
            }
        }

        if (maxHalfWidth < 0.0001f)
        {
            return;
        }

        constexpr float kNodeCapHeightOffset = kRoadHeightOffset + 0.005f;
        XMVECTOR center = XMLoadFloat3(&node.position) + XMVectorSet(0.0f, kNodeCapHeightOffset, 0.0f, 0.0f);
        float half = maxHalfWidth;

        XMFLOAT3 p0, p1, p2, p3;
        XMStoreFloat3(&p0, center + XMVectorSet(-half, 0.0f, -half, 0.0f));
        XMStoreFloat3(&p1, center + XMVectorSet(half, 0.0f, -half, 0.0f));
        XMStoreFloat3(&p2, center + XMVectorSet(half, 0.0f, half, 0.0f));
        XMStoreFloat3(&p3, center + XMVectorSet(-half, 0.0f, half, 0.0f));

        Vertex v0{ p0.x, p0.y, p0.z, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,  color.x, color.y, color.z, color.w };
        Vertex v1{ p1.x, p1.y, p1.z, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,  color.x, color.y, color.z, color.w };
        Vertex v2{ p2.x, p2.y, p2.z, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,  color.x, color.y, color.z, color.w };
        Vertex v3{ p3.x, p3.y, p3.z, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  color.x, color.y, color.z, color.w };

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v0);
        vertices.push_back(v2);
        vertices.push_back(v3);

        vertices.push_back(v0);
        vertices.push_back(v2);
        vertices.push_back(v1);
        vertices.push_back(v0);
        vertices.push_back(v3);
        vertices.push_back(v2);
    }
}

std::vector<Vertex> BuildRoadMeshVertices(
    const std::vector<RoadSegment>& segments,
    const std::vector<RoadNode>& nodes,
    const XMFLOAT4& color,
    size_t emitStartIndex,
    float deadEndExtension)
{
    size_t count = segments.size();

    // まず各区間「自前」の縁ベクトルを求めておく
    std::vector<XMVECTOR> naturalPerp(count);
    for (size_t i = 0; i < count; ++i)
    {
        naturalPerp[i] = ComputeNaturalPerpendicular(segments[i]);
    }

    // 各区間の始点側/終点側で実際に使う縁ベクトル(初期値は自前のもの)
    std::vector<XMVECTOR> startPerp = naturalPerp;
    std::vector<XMVECTOR> endPerp = naturalPerp;

    // 次数2の素直な連続点(行き止まり・T字路・十字路ではない)だけミターする。
    // nodesが空(プレビュー用途)ならこのループは何もしない。
    for (const RoadNode& node : nodes)
    {
        if (node.connectedSegmentIndices.size() != 2)
        {
            continue;
        }

        size_t idxA = node.connectedSegmentIndices[0];
        size_t idxB = node.connectedSegmentIndices[1];
        if (idxA >= count || idxB >= count)
        {
            continue;
        }

        const RoadSegment& segA = segments[idxA];
        const RoadSegment& segB = segments[idxB];

        bool aViaStart = IsNearPoint(segA.start, node.position);
        bool aViaEnd = IsNearPoint(segA.end, node.position);
        bool bViaStart = IsNearPoint(segB.start, node.position);
        bool bViaEnd = IsNearPoint(segB.end, node.position);

        // 「片方がこのノードで終わり(end)、もう片方がこのノードから始まる(start)」という
        // 素直な連続(チェーン)の場合だけミターする。両方start同士/end同士(分岐)は対象外にする
        // (向きが逆になりミター計算が破綻するため)。
        bool isSimpleChain = (aViaEnd && bViaStart) || (aViaStart && bViaEnd);
        if (!isSimpleChain)
        {
            continue;
        }

        // ミターは、Freehand(Curve)同士の継ぎ目、つまり「元々小さい角度差しかない
        // 滑らかな曲線の一部」にだけ適用する。直線モードはグリッド角度スナップにより
        // 任意の(時に鋭い)角度で折れることがあり、そこにミターをかけると幅は一定でも
        // 逆に継ぎ目に大きな隙間が目立ってしまうため、ミターせず各区間自前の縁のまま
        // 描画する(継ぎ目はそのまま重なる/隙間が空くが、直線区間同士の交差点としては
        // 自然な見た目になる)。
        bool bothCurve = (segA.drawMode == RoadDrawMode::Curve) && (segB.drawMode == RoadDrawMode::Curve);
        if (!bothCurve)
        {
            continue;
        }

        XMVECTOR miter = ComputeMiterPerpendicular(naturalPerp[idxA], naturalPerp[idxB]);

        if (aViaEnd) { endPerp[idxA] = miter; }
        else { startPerp[idxA] = miter; }

        if (bViaEnd) { endPerp[idxB] = miter; }
        else { startPerp[idxB] = miter; }
    }

    // 行き止まり(次数1のノード)の見た目だけを、区間自身の方向にdeadEndExtensionぶん伸ばす。
    // segments自体は変更せず、描画用の始点/終点だけをここで作る。
    std::vector<XMFLOAT3> renderStart(count);
    std::vector<XMFLOAT3> renderEnd(count);
    for (size_t i = 0; i < count; ++i)
    {
        renderStart[i] = segments[i].start;
        renderEnd[i] = segments[i].end;
    }

    if (deadEndExtension > 0.0001f)
    {
        for (const RoadNode& node : nodes)
        {
            if (node.connectedSegmentIndices.size() != 1)
            {
                continue;
            }

            size_t idx = node.connectedSegmentIndices[0];
            if (idx >= count)
            {
                continue;
            }

            const RoadSegment& segment = segments[idx];
            bool viaStart = IsNearPoint(segment.start, node.position);
            bool viaEnd = IsNearPoint(segment.end, node.position);
            if (!viaStart && !viaEnd)
            {
                continue;
            }

            XMVECTOR segStart = XMLoadFloat3(&segment.start);
            XMVECTOR segEnd = XMLoadFloat3(&segment.end);
            XMVECTOR dir = segEnd - segStart;
            float len = XMVectorGetX(XMVector3Length(dir));
            if (len < 0.0001f)
            {
                continue;
            }
            dir = XMVector3Normalize(dir);

            if (viaStart)
            {
                XMVECTOR extended = segStart - dir * deadEndExtension;
                XMStoreFloat3(&renderStart[idx], extended);
            }
            else
            {
                XMVECTOR extended = segEnd + dir * deadEndExtension;
                XMStoreFloat3(&renderEnd[idx], extended);
            }
        }
    }

    std::vector<Vertex> vertices;
    vertices.reserve((count > emitStartIndex ? count - emitStartIndex : 0) * 12);

    for (size_t i = emitStartIndex; i < count; ++i)
    {
        RoadSegment renderSegment = segments[i];
        renderSegment.start = renderStart[i];
        renderSegment.end = renderEnd[i];
        AppendSegmentQuad(vertices, renderSegment, startPerp[i], endPerp[i], color);
    }

    // 継ぎ目・T字路・交差点の隙間キャップは、新規に出力する区間が関わるノードにだけ
    // 敷けばよい(それ以外は既に確定済みメッシュ側で敷き済みのため)。
    for (const RoadNode& node : nodes)
    {
        if (node.connectedSegmentIndices.size() < 2)
        {
            continue;
        }

        bool involvesNewSegment = false;
        for (size_t segIndex : node.connectedSegmentIndices)
        {
            if (segIndex >= emitStartIndex)
            {
                involvesNewSegment = true;
                break;
            }
        }

        if (involvesNewSegment)
        {
            AppendNodeCap(vertices, node, segments, color);
        }
    }

    return vertices;
}
