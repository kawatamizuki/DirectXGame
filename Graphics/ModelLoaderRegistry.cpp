#include "ModelLoaderRegistry.h"
#include <cctype>
#include <chrono>
#include "BakedModelCache.h"
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

    // 一度読んで焼いて(保存して)ある物があれば、それを読むだけにする(OBJ/FBXの解析・座標系の変換を飛ばす)。
    if (BakedModelCache::TryLoad(filePath, out))
    {
        return true;
    }
    out.Clear();

    const auto start = std::chrono::steady_clock::now();
    if (!loader->LoadFromFile(filePath, out))
    {
        Debug::Error(std::string("ModelLoaderRegistry: ") + loader->Name() + " での読み込みに失敗しました : " + filePath);
        return false;
    }
    const double loaderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    // 次回から軽くするために保存する。失敗しても、この読み込み自体は成功として続ける。
    BakedModelCache::Store(filePath, out, loaderMs);
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
