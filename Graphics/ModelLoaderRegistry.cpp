#include "ModelLoaderRegistry.h"
#include <cctype>
#include "ObjModelLoader.h"
#include "FbxModelLoader.h"
#include "Debug.h"

std::string ModelLoaderRegistry::GetLowerExtension(const std::string& filePath)
{
    size_t slash = filePath.find_last_of("/\\");
    size_t dot = filePath.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    {
        return std::string();
    }

    std::string extension = filePath.substr(dot);
    for (char& c : extension)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension;
}

void ModelLoaderRegistry::Register(std::unique_ptr<IModelLoader> loader)
{
    if (loader)
    {
        m_loaders.push_back(std::move(loader));
    }
}

IModelLoader* ModelLoaderRegistry::FindLoader(const std::string& filePath) const
{
    std::string extension = GetLowerExtension(filePath);
    // 後から登録したものを優先する。
    for (auto it = m_loaders.rbegin(); it != m_loaders.rend(); ++it)
    {
        if ((*it)->SupportsExtension(extension))
        {
            return it->get();
        }
    }
    return nullptr;
}

bool ModelLoaderRegistry::CanLoad(const std::string& filePath) const
{
    return FindLoader(filePath) != nullptr;
}

bool ModelLoaderRegistry::LoadFromFile(const std::string& filePath, ModelData& out)
{
    out.Clear();

    IModelLoader* loader = FindLoader(filePath);
    if (!loader)
    {
        Debug::Error("ModelLoaderRegistry: 対応していない拡張子です : " + filePath);
        return false;
    }

    if (!loader->LoadFromFile(filePath, out))
    {
        Debug::Error(std::string("ModelLoaderRegistry: ") + loader->Name() + " での読み込みに失敗しました : " + filePath);
        return false;
    }
    return true;
}

ModelLoaderRegistry& ModelLoaderRegistry::Default()
{
    static ModelLoaderRegistry registry = []()
    {
        ModelLoaderRegistry r;
        r.Register(std::make_unique<ObjModelLoader>());
        r.Register(std::make_unique<FbxModelLoader>());
        return r;
    }();
    return registry;
}
