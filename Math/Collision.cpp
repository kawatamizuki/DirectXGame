#include <cfloat>
#include <cmath>
#include <algorithm>
#include "Collision.h"
#include"Transform.h"

using namespace DirectX;

// Ray と Sphere の当たり判定
// ray        : 判定に使うRay
// center     : Sphere中心座標
// radius     : Sphere半径
// distance   : Ray始点からHit位置までの距離
bool IntersectRaySphere(
    const Ray& ray,
    const DirectX::XMFLOAT3& center,
    float radius,
    float& distance
)
{
    // Ray始点
    XMVECTOR rayOrigin =
        XMLoadFloat3(&ray.origin);

    // Ray方向ベクトル
    XMVECTOR rayDir =
        XMLoadFloat3(&ray.direction);

    // Sphere中心
    XMVECTOR sphereCenter =
        XMLoadFloat3(&center);

    // Ray始点 → Sphere中心へのベクトル
    XMVECTOR toSphere =
        sphereCenter - rayOrigin;

    float projection;

    // Sphere方向ベクトルをRay方向へ射影
    // Ray上で最もSphere中心へ近い位置を求めるために使用
    XMStoreFloat(
        &projection,
        XMVector3Dot(toSphere, rayDir)
    );

    // SphereがRayの後方にある場合
    if (projection < 0.0f)
    {
        return false;
    }

    // Ray上の最近接点
    XMVECTOR closestPoint =
        rayOrigin + rayDir * projection;

    // Sphere中心と最近接点の差分
    XMVECTOR distanceVec =
        sphereCenter - closestPoint;

    float distanceSq;

    // 最近接点とSphere中心の距離?
    XMStoreFloat(
        &distanceSq,
        XMVector3LengthSq(distanceVec)
    );

    // Sphere半径以内ならHit
    if (distanceSq <= radius * radius)
    {
        distance = projection;
        return true;
    }

    return false;
}

// Ray と AABB(Axis Aligned Bounding Box) の当たり判定
// min        : Box最小座標
// max        : Box最大座標
// distance   : Ray始点からHit位置までの距離
bool IntersectRayAABB(
    const Ray& ray,
    const DirectX::XMFLOAT3& min,
    const DirectX::XMFLOAT3& max,
    float& distance)
{
    // RayがBoxへ入る距離
    float tMin = 0.0f;

    // RayがBoxから出る距離
    float tMax = FLT_MAX;

    // Ray始点
    const float origin[3] =
    {
        ray.origin.x,
        ray.origin.y,
        ray.origin.z
    };

    // Ray方向
    const float dir[3] =
    {
        ray.direction.x,
        ray.direction.y,
        ray.direction.z
    };

    // Box最小座標
    const float boxMin[3] =
    {
        min.x,
        min.y,
        min.z
    };

    // Box最大座標
    const float boxMax[3] =
    {
        max.x,
        max.y,
        max.z
    };

    // XYZ軸ごとに判定
    for (int i = 0; i < 3; ++i)
    {
        // Rayが軸にほぼ平行な場合
        if (fabsf(dir[i]) < 0.0001f)
        {
            // Box範囲外ならHitしない
            if (origin[i] < boxMin[i] ||
                origin[i] > boxMax[i])
            {
                return false;
            }
        }
        else
        {
            // 逆数を使って除算回数を減らす
            float invDir = 1.0f / dir[i];

            // Box面との交差距離
            float t1 = (boxMin[i] - origin[i]) * invDir;
            float t2 = (boxMax[i] - origin[i]) * invDir;

            // t1 が近い側になるよう入れ替え
            if (t1 > t2)
            {
                std::swap(t1, t2);
            }

            // Boxへ入る距離を更新
            tMin = std::max(tMin, t1);

            // Boxから出る距離を更新
            tMax = std::min(tMax, t2);

            // 入る前に出てしまうならHitしない
            if (tMin > tMax)
            {
                return false;
            }
        }
    }

    // 最初にHitした距離
    distance = tMin;

    return true;
}

// Ray と OBB(Oriented Bounding Box) の当たり判定
// worldRay   : ワールド空間のRay
// transform  : ObjectのTransform
// localMin   : ローカル空間Box最小座標
// localMax   : ローカル空間Box最大座標
// distance   : Ray始点からHit位置までの距離
bool IntersectRayOBB(
    const Ray& worldRay,
    const Transform& transform,
    const DirectX::XMFLOAT3& localMin,
    const DirectX::XMFLOAT3& localMax,
    float& distance)
{
    using namespace DirectX;

    // ObjectのWorld行列
    XMMATRIX world =
        transform.GetWorldMatrix();

    // World行列の逆行列
    // ワールド空間 → ローカル空間へ変換するために使用
    XMMATRIX invWorld =
        XMMatrixInverse(nullptr, world);

    // ワールド空間Ray始点
    XMVECTOR worldOrigin =
        XMLoadFloat3(&worldRay.origin);

    // ワールド空間Ray方向
    XMVECTOR worldDirection =
        XMLoadFloat3(&worldRay.direction);

    // Ray始点をローカル空間へ変換
    XMVECTOR localOrigin =
        XMVector3TransformCoord(
            worldOrigin,
            invWorld
        );

    // Ray方向をローカル空間へ変換
    // directionなのでTransformNormalを使用
    XMVECTOR localDirection =
        XMVector3TransformNormal(
            worldDirection,
            invWorld
        );

    // 方向ベクトルを正規化
    localDirection =
        XMVector3Normalize(localDirection);

    // ローカル空間Ray作成
    Ray localRay;

    XMStoreFloat3(
        &localRay.origin,
        localOrigin
    );

    XMStoreFloat3(
        &localRay.direction,
        localDirection
    );

    // ローカル空間AABB判定
    // OBB判定の本体
    return IntersectRayAABB(
        localRay,
        localMin,
        localMax,
        distance
    );
}