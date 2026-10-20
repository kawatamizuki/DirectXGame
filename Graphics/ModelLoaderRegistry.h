#pragma once
#include <memory>
#include <string>
#include <vector>
#include "IModelLoader.h"

// 拡張子からローダーを選んでモデルを読む入口。
// 形式(OBJ/FBX)の違いはここで吸収し、Modelなど呼び出し側は拡張子を気にしなくてよい。
class ModelLoaderRegistry
{
public:
    // ローダーを登録する(後から登録したものが先に使われる。標準のローダーを差し替えたい時用)。
    void Register(std::unique_ptr<IModelLoader> loader);

    // filePathの拡張子に合うローダーで読む。拡張子に合うローダーが無い、または読み込みに失敗したらfalse。
    bool LoadFromFile(const std::string& filePath, ModelData& out);

    // filePathの拡張子を読めるローダーがあるか。
    bool CanLoad(const std::string& filePath) const;

    // 標準のローダー(OBJ、FBX)を登録済みの共有インスタンス。
    static ModelLoaderRegistry& Default();

    // filePathの拡張子を小文字・ドット付きで返す(無ければ空)。
    static std::string GetLowerExtension(const std::string& filePath);

private:
    IModelLoader* FindLoader(const std::string& filePath) const;

    std::vector<std::unique_ptr<IModelLoader>> m_loaders;
};
