#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

class Material
{
public:
    Material();
    ~Material();

    void SetTexturePath(const std::string& path);
    const std::string& GetTexturePath() const;

    void SetTextureView(ID3D11ShaderResourceView* textureView);
    ID3D11ShaderResourceView* GetTextureView() const;

    bool HasTexture() const;

    void Clear();

private:
    std::string m_texturePath;
    // ComPtrがReleaseを自動で行ってくれる
    // そのため、デストラクタで手動Releaseする必要がなくなる
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_textureView;
};
