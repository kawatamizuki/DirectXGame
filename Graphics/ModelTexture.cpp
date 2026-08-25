#include "Model.h"

#include <Windows.h>
#include "Debug.h"
#include "WICTextureLoader11.h"

namespace
{
    std::wstring ToWideStringForTexture(const std::string& value)
    {
        if (value.empty())
        {
            return L"";
        }

        const int size = MultiByteToWideChar(
            CP_ACP, 0, value.c_str(), -1, nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(
            CP_ACP, 0, value.c_str(), -1, &result[0], size);
        if (!result.empty() && result.back() == L'\0')
        {
            result.pop_back();
        }
        return result;
    }
}

bool Model::LoadTextureOverride(ID3D11Device* device, const std::string& texturePath)
{
    if (!device || texturePath.empty())
    {
        return false;
    }

    ID3D11ShaderResourceView* textureView = nullptr;
    const std::wstring widePath = ToWideStringForTexture(texturePath);
    const HRESULT result = DirectX::CreateWICTextureFromFile(
        device, widePath.c_str(), nullptr, &textureView);
    if (FAILED(result))
    {
        Debug::Error("Texture override load failed : " + texturePath);
        return false;
    }

    m_material.SetTexturePath(texturePath);
    m_material.SetTextureView(textureView);
    textureView->Release();
    return true;
}
