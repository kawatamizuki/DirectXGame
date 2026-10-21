#include "OffscreenTarget.h"
#include <cstring>
#include "Debug.h"

bool OffscreenTarget::Initialize(ID3D11Device* device, UINT width, UINT height)
{
    Release();

    if (!device || width == 0 || height == 0)
    {
        Debug::Error("OffscreenTarget::Initialize failed : invalid arguments");
        return false;
    }

    m_device = device;
    m_width = width;
    m_height = height;

    // 色: 描画先(RTV)として使い、ImGuiなどで画像として表示する(SRV)ため、両方のフラグを付ける。
    D3D11_TEXTURE2D_DESC colorDesc = {};
    colorDesc.Width = width;
    colorDesc.Height = height;
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&colorDesc, nullptr, m_colorTexture.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(m_colorTexture.Get(), nullptr, m_rtv.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(m_colorTexture.Get(), nullptr, m_srv.GetAddressOf())))
    {
        Debug::Error("OffscreenTarget::Initialize failed : color target creation failed");
        Release();
        return false;
    }

    // 深度: 手前の物が奥の物を隠すために必要。
    D3D11_TEXTURE2D_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(device->CreateTexture2D(&depthDesc, nullptr, m_depthTexture.GetAddressOf())) ||
        FAILED(device->CreateDepthStencilView(m_depthTexture.Get(), nullptr, m_dsv.GetAddressOf())))
    {
        Debug::Error("OffscreenTarget::Initialize failed : depth target creation failed");
        Release();
        return false;
    }

    return true;
}

void OffscreenTarget::Release()
{
    m_savedRtv.Reset();
    m_savedDsv.Reset();
    m_staging.Reset();
    m_dsv.Reset();
    m_depthTexture.Reset();
    m_srv.Reset();
    m_rtv.Reset();
    m_colorTexture.Reset();
    m_device.Reset();
    m_width = 0;
    m_height = 0;
    m_savedViewportCount = 0;
    m_inScope = false;
}

void OffscreenTarget::Begin(ID3D11DeviceContext* context, const float clearColor[4])
{
    if (!IsValid() || !context || m_inScope)
    {
        return;
    }

    // 元の描画先とビューポートを覚えておく(Endで戻す)。
    m_savedRtv.Reset();
    m_savedDsv.Reset();
    context->OMGetRenderTargets(1, m_savedRtv.GetAddressOf(), m_savedDsv.GetAddressOf());
    m_savedViewportCount = 1;
    context->RSGetViewports(&m_savedViewportCount, &m_savedViewport);

    ID3D11RenderTargetView* rtv = m_rtv.Get();
    context->OMSetRenderTargets(1, &rtv, m_dsv.Get());
    context->ClearRenderTargetView(m_rtv.Get(), clearColor);
    context->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    m_inScope = true;
}

void OffscreenTarget::End(ID3D11DeviceContext* context)
{
    if (!m_inScope || !context)
    {
        return;
    }

    ID3D11RenderTargetView* rtv = m_savedRtv.Get();
    context->OMSetRenderTargets(1, &rtv, m_savedDsv.Get());
    if (m_savedViewportCount > 0)
    {
        context->RSSetViewports(m_savedViewportCount, &m_savedViewport);
    }

    m_savedRtv.Reset();
    m_savedDsv.Reset();
    m_inScope = false;
}

bool OffscreenTarget::ReadPixels(ID3D11DeviceContext* context, std::vector<uint8_t>& outRgba)
{
    if (!IsValid() || !context)
    {
        return false;
    }

    if (!m_staging)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        m_colorTexture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, m_staging.GetAddressOf())))
        {
            Debug::Error("OffscreenTarget::ReadPixels failed : staging texture creation failed");
            return false;
        }
    }

    context->CopyResource(m_staging.Get(), m_colorTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    {
        Debug::Error("OffscreenTarget::ReadPixels failed : Map failed");
        return false;
    }

    // GPUのメモリは行ごとに隙間(RowPitch)があるので、隙間なしに詰め直す。
    outRgba.resize(static_cast<size_t>(m_width) * m_height * 4);
    for (UINT y = 0; y < m_height; ++y)
    {
        const uint8_t* source = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        std::memcpy(&outRgba[static_cast<size_t>(y) * m_width * 4], source, static_cast<size_t>(m_width) * 4);
    }

    context->Unmap(m_staging.Get(), 0);
    return true;
}
