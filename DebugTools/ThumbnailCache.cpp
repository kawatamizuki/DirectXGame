#include "ThumbnailCache.h"
#include <Windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include "Renderer.h"
#include "Model.h"
#include "ModelPreviewRenderer.h"
#include "WICTextureLoader11.h"
#include "Profiler.h"
#include "Debug.h"

#pragma comment(lib, "windowscodecs.lib")

namespace fs = std::filesystem;

namespace
{
    // FNV-1a 64ビット。同じ入力なら、いつ・どのPCでも同じ値になる(std::hashは実装依存なので使わない)。
    uint64_t Fnv1a64(const std::string& text)
    {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char c : text)
        {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    // ファイル名に使える文字だけにする(英数字・ハイフン以外は「_」)。サムネイルのPNGを、エクスプローラーで見て
    // どのモデルか分かるように、名前を入れるため。長すぎる名前は切る。
    std::string SanitizeForFileName(const std::string& name)
    {
        std::string result;
        for (unsigned char c : name)
        {
            result.push_back((std::isalnum(c) || c == '-') ? static_cast<char>(c) : '_');
        }
        if (result.size() > 48)
        {
            result.resize(48);
        }
        return result.empty() ? std::string("model") : result;
    }

    std::wstring ToWide(const std::string& text)
    {
        if (text.empty()) { return std::wstring(); }
        int length = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
        std::wstring result(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, &result[0], length);
        if (!result.empty() && result.back() == L'\0') { result.pop_back(); }
        return result;
    }

    double ElapsedMs(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
}

std::string ThumbnailCache::MakeKey(const AssetEntry& entry)
{
    std::string material = entry.path + "|" + std::to_string(entry.fileSize) + "|" + std::to_string(entry.modifiedTime) + "|v" + std::to_string(kVersion);
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(Fnv1a64(material)));
    return std::string(buffer);
}

std::string ThumbnailCache::GetCachePath(const AssetEntry& entry) const
{
    return m_cacheDirectory + "/" + SanitizeForFileName(entry.name) + "_" + MakeKey(entry) + ".png";
}

std::vector<uint8_t> ThumbnailCache::DownscaleHalf(const std::vector<uint8_t>& rgba, UINT width, UINT height)
{
    const UINT outWidth = width / 2;
    const UINT outHeight = height / 2;
    std::vector<uint8_t> out(static_cast<size_t>(outWidth) * outHeight * 4);

    for (UINT y = 0; y < outHeight; ++y)
    {
        for (UINT x = 0; x < outWidth; ++x)
        {
            for (int channel = 0; channel < 4; ++channel)
            {
                // 2x2の4画素の平均(四捨五入)。
                const size_t i00 = (static_cast<size_t>(y * 2) * width + (x * 2)) * 4 + channel;
                const size_t i10 = i00 + 4;
                const size_t i01 = i00 + static_cast<size_t>(width) * 4;
                const size_t i11 = i01 + 4;
                out[(static_cast<size_t>(y) * outWidth + x) * 4 + channel] =
                    static_cast<uint8_t>((rgba[i00] + rgba[i10] + rgba[i01] + rgba[i11] + 2) / 4);
            }
        }
    }
    return out;
}

bool ThumbnailCache::WritePng(const std::string& path, const std::vector<uint8_t>& rgba, UINT width, UINT height)
{
    if (rgba.size() != static_cast<size_t>(width) * height * 4 || width == 0 || height == 0)
    {
        return false;
    }

    // PNGエンコーダーは、B,G,R,Aの並びが標準。RとBを入れ替えた並びを作る。
    std::vector<uint8_t> bgra(rgba.size());
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        bgra[i + 0] = rgba[i + 2];
        bgra[i + 1] = rgba[i + 1];
        bgra[i + 2] = rgba[i + 0];
        bgra[i + 3] = rgba[i + 3];
    }

    // 途中で失敗しても、既にあるファイルを壊さないよう、一時ファイルに書いてから置き換える。
    const std::string tempPath = path + ".tmp";
    {
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICStream> stream;
        if (FAILED(factory->CreateStream(stream.GetAddressOf())) ||
            FAILED(stream->InitializeFromFilename(ToWide(tempPath).c_str(), GENERIC_WRITE)))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
        Microsoft::WRL::ComPtr<IPropertyBag2> options;
        if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf())) ||
            FAILED(frame->Initialize(options.Get())) ||
            FAILED(frame->SetSize(width, height)))
        {
            return false;
        }

        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA)
        {
            return false;
        }

        if (FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data())) ||
            FAILED(frame->Commit()) ||
            FAILED(encoder->Commit()))
        {
            return false;
        }
    } // ここでCOMオブジェクトが解放され、一時ファイルが閉じる

    if (!MoveFileExA(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileA(tempPath.c_str());
        return false;
    }
    return true;
}

bool ThumbnailCache::Initialize(Renderer* renderer, ModelPreviewRenderer* preview, const LightingState& lighting,
    const std::string& cacheDirectory)
{
    m_renderer = renderer;
    m_preview = preview;
    m_lighting = lighting;
    m_cacheDirectory = cacheDirectory;

    std::error_code ec;
    fs::create_directories(fs::path(m_cacheDirectory), ec);
    if (ec)
    {
        Debug::Warning("ThumbnailCache: キャッシュフォルダを作れませんでした : " + m_cacheDirectory);
    }

    if (!renderer || !preview)
    {
        return false;
    }
    return m_target.Initialize(renderer->GetDevice(), kRenderSize, kRenderSize);
}

ID3D11ShaderResourceView* ThumbnailCache::Get(const AssetEntry& entry)
{
    const std::string key = MakeKey(entry);

    auto found = m_resident.find(key);
    if (found != m_resident.end())
    {
        found->second.lastUse = ++m_useCounter;
        return found->second.srv.Get();
    }

    if (m_failed.find(key) == m_failed.end() && !IsLargeSkipped(entry))
    {
        Enqueue(entry, key, true); // 見えている項目なので、先頭に割り込ませる
    }
    return nullptr;
}

bool ThumbnailCache::IsLargeSkipped(const AssetEntry& entry) const
{
    return entry.fileSize > kLargeFileBytes && m_forced.find(MakeKey(entry)) == m_forced.end();
}

void ThumbnailCache::RequestAnyway(const AssetEntry& entry)
{
    const std::string key = MakeKey(entry);
    m_forced.insert(key);
    if (m_resident.find(key) == m_resident.end())
    {
        m_failed.erase(key);
        Enqueue(entry, key, true);
    }
}

bool ThumbnailCache::HasFailed(const AssetEntry& entry) const
{
    return m_failed.find(MakeKey(entry)) != m_failed.end();
}

void ThumbnailCache::Enqueue(const AssetEntry& entry, const std::string& key, bool front)
{
    if (m_queued.find(key) != m_queued.end())
    {
        // 既に予約済み。見えている項目なら、先頭に移す(スクロールで見えた物を優先するため)。
        if (front)
        {
            auto it = std::find_if(m_queue.begin(), m_queue.end(), [&key](const Request& r) { return r.key == key; });
            if (it != m_queue.end() && it != m_queue.begin())
            {
                Request moved = std::move(*it);
                m_queue.erase(it);
                m_queue.push_front(std::move(moved));
            }
        }
        return;
    }

    m_queued.insert(key);
    Request request{ entry, key };
    if (front) { m_queue.push_front(std::move(request)); }
    else { m_queue.push_back(std::move(request)); }
}

void ThumbnailCache::EnqueueBackground(const std::vector<AssetEntry>& entries)
{
    for (const AssetEntry& entry : entries)
    {
        const std::string key = MakeKey(entry);
        if (m_resident.find(key) == m_resident.end() && m_failed.find(key) == m_failed.end() && !IsLargeSkipped(entry))
        {
            Enqueue(entry, key, false);
        }
    }
}

void ThumbnailCache::Update(float budgetMs)
{
    if (m_paused || m_queue.empty())
    {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    bool first = true;
    while (!m_queue.empty())
    {
        // 最低1件は進める(予算が極端に小さくても、いつかは終わるように)。
        if (!first && ElapsedMs(start) >= budgetMs)
        {
            break;
        }
        first = false;

        Request request = std::move(m_queue.front());
        m_queue.pop_front();
        m_queued.erase(request.key);

        if (m_resident.find(request.key) != m_resident.end() || m_failed.find(request.key) != m_failed.end())
        {
            continue;
        }
        Process(request);
    }

    Evict();
}

void ThumbnailCache::Process(const Request& request)
{
    // 大量の読み込みでログが埋まらないよう、この中のLog/Infoは止める(警告・エラーは出る)。
    Debug::ScopedInfoSuppress quiet;

    const std::string path = GetCachePath(request.entry);

    std::error_code ec;
    if (fs::is_regular_file(fs::path(path), ec))
    {
        if (LoadFromDisk(path, request.key))
        {
            ++m_loadedFromDiskCount;
            return;
        }
        // 壊れたPNGは消して、作り直す。
        fs::remove(fs::path(path), ec);
    }

    if (!Generate(request))
    {
        m_failed.insert(request.key);
    }
}

bool ThumbnailCache::LoadFromDisk(const std::string& path, const std::string& key)
{
    if (!m_renderer)
    {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    // WIC_LOADER_IGNORE_SRGB: PNGに「sRGB」の印が付いていても、色を生の値のまま読む。
    // (sRGBとして読むと、表示する時に色が暗く解釈され、生成した直後の画像と見た目が変わってしまう。)
    HRESULT hr = DirectX::CreateWICTextureFromFileEx(
        m_renderer->GetDevice(), ToWide(path).c_str(), 0,
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0,
        DirectX::WIC_LOADER_IGNORE_SRGB, nullptr, srv.GetAddressOf());
    if (FAILED(hr))
    {
        return false;
    }

    Resident resident;
    resident.srv = srv;
    resident.lastUse = ++m_useCounter;
    m_resident[key] = std::move(resident);
    return true;
}

bool ThumbnailCache::StoreFromPixels(const std::string& key, const std::vector<uint8_t>& rgba, UINT width, UINT height)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = rgba.data();
    data.SysMemPitch = width * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    ID3D11Device* device = m_renderer->GetDevice();
    if (FAILED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf())))
    {
        return false;
    }

    Resident resident;
    resident.srv = srv;
    resident.lastUse = ++m_useCounter;
    m_resident[key] = std::move(resident);
    return true;
}

bool ThumbnailCache::Generate(const Request& request)
{
    PROFILE_SCOPE("AssetBrowser::Thumbnail");
    const auto start = std::chrono::steady_clock::now();

    if (!m_renderer || !m_preview || !m_target.IsValid())
    {
        return false;
    }

    // モデルを1回だけ読んで描き、すぐ捨てる(メモリに溜めない)。
    Model model;
    if (!model.LoadFromFile(m_renderer->GetDevice(), request.entry.path))
    {
        Debug::Warning("ThumbnailCache: モデルを読めませんでした : " + request.entry.path);
        return false;
    }

    m_preview->Render(model, m_target, PreviewView(), m_lighting);

    std::vector<uint8_t> pixels;
    if (!m_target.ReadPixels(m_renderer->GetContext(), pixels))
    {
        return false;
    }

    std::vector<uint8_t> reduced = DownscaleHalf(pixels, kRenderSize, kRenderSize);

    // 保存に失敗しても(読み取り専用の場所など)、今回の表示にはメモリ上の画像を使う。
    if (!WritePng(GetCachePath(request.entry), reduced, kThumbnailSize, kThumbnailSize))
    {
        Debug::Warning("ThumbnailCache: PNGを保存できませんでした : " + GetCachePath(request.entry));
    }

    if (!StoreFromPixels(request.key, reduced, kThumbnailSize, kThumbnailSize))
    {
        return false;
    }

    m_lastGenerateMs = static_cast<float>(ElapsedMs(start));
    m_totalGenerateMs += m_lastGenerateMs;
    if (m_lastGenerateMs > m_slowestMs)
    {
        m_slowestMs = m_lastGenerateMs;
        m_slowestPath = request.entry.path;
    }
    ++m_generatedCount;
    return true;
}

void ThumbnailCache::Evict()
{
    while (m_resident.size() > kMaxResident)
    {
        auto oldest = m_resident.begin();
        for (auto it = m_resident.begin(); it != m_resident.end(); ++it)
        {
            if (it->second.lastUse < oldest->second.lastUse)
            {
                oldest = it;
            }
        }
        m_resident.erase(oldest);
    }
}

void ThumbnailCache::Regenerate(const std::vector<AssetEntry>& entries)
{
    for (const AssetEntry& entry : entries)
    {
        const std::string key = MakeKey(entry);
        m_resident.erase(key);
        m_failed.erase(key);

        std::error_code ec;
        fs::remove(fs::path(GetCachePath(entry)), ec);
    }

    EnqueueBackground(entries);
}
