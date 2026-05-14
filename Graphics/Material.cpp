#include "Material.h"

Material::Material()
    : m_texturePath("")
    , m_textureView(nullptr)
{
}

Material::~Material()
{
    // ComPtrが自動でReleaseしてくれるので何もしない
}

void Material::SetTexturePath(const std::string& path)
{
    m_texturePath = path;
}

const std::string& Material::GetTexturePath() const
{
    return m_texturePath;
}

void Material::SetTextureView(ID3D11ShaderResourceView* textureView)
{
   /* if (m_textureView)
    {
        m_textureView->Release();
        m_textureView = nullptr;
    }

    m_textureView = textureView;

    if (m_textureView)
    {
        m_textureView->AddRef();
    }*/

    // ComPtrに代入するとAddRefされる
  // 以前のテクスチャがあれば、ComPtrが自動でReleaseしてくれる
    m_textureView = textureView;
}

ID3D11ShaderResourceView* Material::GetTextureView() const
{
    return m_textureView.Get();
}

bool Material::HasTexture() const
{
    return m_textureView != nullptr;
}

void Material::Clear()
{
    m_texturePath.clear();

    // 保持しているDirectXリソースを解放する
    m_textureView.Reset();
}