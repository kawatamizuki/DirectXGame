#pragma once
#include "Model.h"
#include "Transform.h"
#include "BuildingType.h"

// GameObjectの種類タグ。
// コンポーネント化などの大掛かりな仕組みを導入する前段階として、
// まずはこのフラットな1フィールドで種類を判別できるようにする。
enum class ObjectKind
{
    Environment,
    Building,
    Road,
    Agent // 代表住民などの個体シミュレーション対象の見た目用オブジェクト
};

class GameObject
{
public:
    // 生成時に割り当てられる一意なID。0は未割当。
    // vector内のインデックスは削除(Despawn)で変わりうるため、
    // 複数フレームにまたがって特定のオブジェクトを参照する時は
    // インデックスではなくこのidを使うこと(GameObjectFactory::FindById)。
    uint32_t id = 0;

    Model* model = nullptr;
    Transform transform;
    ObjectKind kind = ObjectKind::Environment;

    // kind==Buildingの時だけ意味を持つ、建物の種類。
    BuildingType buildingType = BuildingType::House;

    // falseの間はRenderer::DrawModelを呼ばない(描画しない)。
    // 主にkind==Agentのオブジェクトが、House/Officeなど施設利用中(内部にいる間)は
    // モデルを表示しないようにするために使う。
    bool visible = true;

private:

};
