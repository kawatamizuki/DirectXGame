#include "GameObjectFactory.h"

namespace GameObjectFactory
{
    namespace
    {
        // 生成した全GameObjectで重複しない連番id。0は「未割当」の意味で使うため1から始める。
        uint32_t s_nextId = 1;
    }

    GameObject& Spawn(
        std::vector<GameObject>& objects,
        Model* model,
        const Transform& transform,
        ObjectKind kind)
    {
        GameObject obj;
        obj.id = s_nextId++;
        obj.model = model;
        obj.transform = transform;
        obj.kind = kind;

        objects.push_back(obj);
        return objects.back();
    }

    void Despawn(std::vector<GameObject>& objects, size_t index)
    {
        if (index >= objects.size())
        {
            return;
        }

        objects[index] = objects.back();
        objects.pop_back();
    }

    GameObject* FindById(std::vector<GameObject>& objects, uint32_t id)
    {
        for (GameObject& obj : objects)
        {
            if (obj.id == id)
            {
                return &obj;
            }
        }
        return nullptr;
    }

    const GameObject* FindById(const std::vector<GameObject>& objects, uint32_t id)
    {
        for (const GameObject& obj : objects)
        {
            if (obj.id == id)
            {
                return &obj;
            }
        }
        return nullptr;
    }
}
