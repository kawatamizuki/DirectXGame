#include "ModelCache.h"
#include "Renderer.h"
#include "Debug.h"

Model* ModelCache::Get(const std::string& path)
{
    auto found = m_models.find(path);
    if (found != m_models.end())
    {
        return found->second.get();
    }
    if (!m_renderer || m_failed.find(path) != m_failed.end())
    {
        return nullptr;
    }

    auto model = std::make_unique<Model>();
    if (!model->LoadFromFile(m_renderer->GetDevice(), path) || model->GetVertexCount() == 0)
    {
        Debug::Warning("ModelCache: モデルを読み込めませんでした : " + path);
        m_failed.insert(path);
        return nullptr;
    }

    Model* raw = model.get();
    m_models.emplace(path, std::move(model));
    return raw;
}
