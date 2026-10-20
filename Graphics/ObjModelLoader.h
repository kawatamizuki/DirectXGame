#pragma once
#include "IModelLoader.h"

// OBJ形式のローダー。既存のObjLoader(自作のパーサー)をIModelLoaderの形に包んだもの。
class ObjModelLoader : public IModelLoader
{
public:
    const char* Name() const override { return "OBJ"; }
    bool SupportsExtension(const std::string& extension) const override { return extension == ".obj"; }
    bool LoadFromFile(const std::string& filePath, ModelData& out) override;
};
