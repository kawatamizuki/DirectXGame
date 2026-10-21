#pragma once
#include <string>
#include <DirectXMath.h>
#include "Transform.h"

struct GameContext;
class Field;
class Model;

// Assetsパネルのモデルをゲームの画面へドラッグして、飾り(ObjectKind::Prop)として置く。
//
// ・ドラッグ中(GameContext::assetDrag.active)で、カーソルがゲームの画面の上にある間、地面にレイを飛ばして、
//   置く場所に半透明のゴーストと1マスの枠を出す(Draw)。
// ・ゲームの画面の上でボタンを離した(assetDrag.dropped)フレームに、その位置へ置く(Update)。
// ・置く大きさは「最大の辺が1マス」に自動で収める(BuildControllerの自動フィットと同じ考え方)。足元が地面の高さに付く。
//   向き・大きさ・位置の調整は、既存のHierarchy/Inspector/ギズモ(Editingモード)で行う。
// ・マスを占有しない(道路・建物と重なって置ける飾り・目印用)。セーブ対象(モデルのパスで保存)。
// ・モデルは GameContext::modelCache から借りる(住所が変わらないので、Propが持ち続けても安全)。
class PropPlacer
{
public:
    // contextとfieldは呼び出し側が所有する(借りるだけ)。
    void Initialize(GameContext* context, Field* field);

    // 今フレームの assetDrag を見て、ゲームの画面の上でドロップされていたら置く。
    void Update();

    // ドラッグ中のゴーストと枠を描く。
    void Draw();

    // 画面の(screenX, screenY)(クライアント座標のピクセル)の下の地面へ置く。置けたらtrue。
    bool PlaceAtScreenPoint(const std::string& modelPath, float screenX, float screenY);

    // 地面の点groundPointへ置く。モデルを読めない・Field の外ならfalse。
    bool PlaceAt(const std::string& modelPath, const DirectX::XMFLOAT3& groundPoint);

    // modelを、groundPoint(地面の点)の上に置く時の Transform(最大の辺が1マス、足元が地面)。単体テストできるよう公開。
    static Transform ComputePlacement(const Model& model, const DirectX::XMFLOAT3& groundPoint, float groundHeight, float cellSize);

private:
    // 画面の点の下の地面を求める。Fieldの外・地面に当たらない時はfalse。
    bool RaycastScreenPoint(float screenX, float screenY, DirectX::XMFLOAT3& outPoint) const;

    GameContext* m_context = nullptr;
    Field* m_field = nullptr;
};
