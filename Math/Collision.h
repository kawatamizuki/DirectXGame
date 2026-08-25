#pragma once
#include "Ray.h"
#include <DirectXMath.h>

class Transform;

// Sphere を使ったRay判定
// 実装が簡単だが判定は少し大雑把
bool IntersectRaySphere(
    const Ray& ray,
    const DirectX::XMFLOAT3& center,
    float radius,
    float& distance
);

// AABB(Axis Aligned Bounding Box)判定
// ワールド軸固定の箱判定
// Sphereより正確だがObject回転には弱い
bool IntersectRayAABB(
    const Ray& ray,
    const DirectX::XMFLOAT3& min,
    const DirectX::XMFLOAT3& max,
    float& distance
);

// OBB(Oriented Bounding Box)判定
// Object回転・Scaleに対応
// Rayをローカル空間へ変換してAABB判定を行う
bool IntersectRayOBB(
    const Ray& worldRay,
    const Transform& transform,
    const DirectX::XMFLOAT3& localMin,
    const DirectX::XMFLOAT3& localMax,
    float& distance
);