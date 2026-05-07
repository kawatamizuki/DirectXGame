#pragma once
#include <d3d11.h>
#include <string>
class Model
{
public:
    Model();
    ~Model();

    bool LoadFromObj(ID3D11Device* device, const std::string& filePath);

    ID3D11Buffer* GetVertexBuffer() const;
    UINT GetVertexCount() const;
    const std::string& GetTexturePath() const;
    ID3D11ShaderResourceView* GetTextureView() const;

private:
    ID3D11Buffer* m_vertexBuffer;
    UINT m_vertexCount;
    std::string m_texturePath;
    ID3D11ShaderResourceView* m_textureView;
};
