#pragma once
#include <vector>
#include "GameObject.h"
#include "Transform.h"

// GameObjectの生成/削除/idによる検索をここに集約する。
// BuildController/RoadSystemはGameContext::objectsに直接push_backせず、
// 必ずこの経由で生成する。
namespace GameObjectFactory
{
    GameObject& Spawn(
        std::vector<GameObject>& objects,
        Model* model,
        const Transform& transform,
        ObjectKind kind);

    // v1: swap-and-popで削除する。
    // 呼び出し側はDespawn呼び出しをまたいでGameObject&/ポインタを保持しないこと
    // (末尾要素と入れ替わるため、他インデックスの参照先がずれる)。
    void Despawn(std::vector<GameObject>& objects, size_t index);

    // idから現在のGameObjectを探す(線形探索)。見つからなければnullptr。
    // 複数フレームにまたがってオブジェクトを参照する時は、インデックスではなくidを保持し、
    // 使う直前にこの関数で解決すること(Despawnでインデックスがずれても安全なため)。
    // 数が増えてボトルネックになったらid→indexの対応表への最適化を検討する。
    GameObject* FindById(std::vector<GameObject>& objects, uint32_t id);
    const GameObject* FindById(const std::vector<GameObject>& objects, uint32_t id);
}
