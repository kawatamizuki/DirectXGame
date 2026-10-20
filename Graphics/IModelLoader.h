#pragma once
#include <cstddef>
#include <string>
#include "ModelData.h"

// 1つのモデル形式(OBJ、FBXなど)を読むローダーのインターフェース。
// 形式を増やす時は、これを実装したクラスを作ってModelLoaderRegistryに登録するだけで、
// 呼び出し側(Model)は変更しなくてよい。
class IModelLoader
{
public:
    virtual ~IModelLoader() = default;

    // ローダーの名前(ログ用)。
    virtual const char* Name() const = 0;

    // このローダーが読める拡張子か。extensionは小文字でドット付き(例: ".fbx")。
    virtual bool SupportsExtension(const std::string& extension) const = 0;

    // ファイルから読む。成功したらtrueで、outに結果が入る(失敗時のoutの中身は不定)。
    virtual bool LoadFromFile(const std::string& filePath, ModelData& out) = 0;

    // メモリ上のバイト列から読む(将来、素材をexeに組み込んだ時に、ファイルを介さず読むために用意)。
    // baseDirectoryは、テクスチャなどの相対参照を解決する基準のフォルダ(末尾の区切りは有っても無くてもよい)。
    // この形式がメモリからの読み込みに対応していなければfalseを返す(既定)。
    virtual bool LoadFromMemory(const void* data, size_t size, const std::string& baseDirectory, ModelData& out)
    {
        (void)data; (void)size; (void)baseDirectory; (void)out;
        return false;
    }
};
