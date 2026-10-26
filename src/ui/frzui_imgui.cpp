// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frzui.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <share.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

std::vector<Slot> g_slots;

namespace {

struct LogFile {
    CRITICAL_SECTION cs;
    FILE* f = nullptr;
    LogFile()
    {
        InitializeCriticalSection(&cs);
        std::wstring dir = UiRootDir() + L"\\logs";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring path = dir + L"\\ramstasis.log";
        f = _wfsopen(path.c_str(), L"ab", _SH_DENYNO);
    }
    ~LogFile()
    {
        if (f) fclose(f);
        DeleteCriticalSection(&cs);
    }
};

LogFile& Log()
{
    static LogFile g;
    return g;
}

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string s((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], need, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring w((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], need);
    return w;
}

void FormatMb(unsigned long long bytes, char* out, size_t n)
{
    double mb = (double)bytes / 1048576.0;
    if (mb >= 1024.0) sprintf_s(out, n, "%.2f GB", mb / 1024.0);
    else sprintf_s(out, n, "%.0f MB", mb);
}

void FormatMbField(unsigned long long bytes, char* out, size_t n)
{
    sprintf_s(out, n, "%llu MB", bytes / 1048576ULL);
}

std::string LowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)::tolower(c); });
    return s;
}

bool ContainsNoCase(const std::string& hay, const std::string& needleLower)
{
    return LowerAscii(hay).find(needleLower) != std::string::npos;
}

std::string TrimWs(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string ShortError(const EngineResult& r, const std::string& jsonError)
{
    if (!jsonError.empty()) return jsonError;
    if (r.err.empty() && r.out.empty() && !r.ran) {
        // the engine process never ran at all - report why instead of a bare "-1"
        DWORD w = (DWORD)r.spawnErr;
        if (w == 2 || w == 3) return "engine not found beside RamStasis.exe";
        if (w == 5)           return "engine launch denied (error 5) - antivirus or permissions";
        if (w == 1260)        return "engine blocked by policy or antivirus (error 1260)";
        char buf[80];
        sprintf_s(buf, sizeof(buf), "cannot start engine (win32 %lu)", (unsigned long)w);
        return buf;
    }
    std::string s = r.err;
    size_t pos = 0;
    std::string fallback;
    while (pos <= s.size()) {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        std::string line = TrimWs(s.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty()) continue;
        if (line.find("===") != std::string::npos) continue;
        if (line.find("ERROR") != std::string::npos ||
            line.find("REFUS") != std::string::npos ||
            line.find("error") != std::string::npos)
            return line;
        if (fallback.empty()) fallback = line;
        if (eol >= s.size()) break;
    }
    if (!fallback.empty()) return fallback;
    char buf[64];
    sprintf_s(buf, sizeof(buf), "engine exit code %ld", r.exitCode);
    return buf;
}

float g_dpiScale = 1.0f;
inline float Dpi(float val) { return (float)(int)(val * g_dpiScale + 0.5f); }

const ImVec4 kColBg      (0.0588f, 0.0706f, 0.0902f, 1.00f);
const ImVec4 kColCard    (0.0902f, 0.1098f, 0.1412f, 1.00f);
const ImVec4 kColCard2   (0.1098f, 0.1294f, 0.1647f, 1.00f);
const ImVec4 kColBorder  (0.1490f, 0.1804f, 0.2392f, 1.00f);
const ImVec4 kColText    (0.9020f, 0.9176f, 0.9490f, 1.00f);
const ImVec4 kColDim     (0.5451f, 0.6039f, 0.6902f, 1.00f);
const ImVec4 kColFaint   (0.3686f, 0.4157f, 0.4941f, 1.00f);
const ImVec4 kColEmerald (0.0627f, 0.7255f, 0.5059f, 1.00f);
const ImVec4 kColCyan    (0.0235f, 0.7137f, 0.8314f, 1.00f);
const ImVec4 kColBlue    (0.2314f, 0.5098f, 0.9647f, 1.00f);
const ImVec4 kColAmber   (0.9608f, 0.6196f, 0.0431f, 1.00f);
const ImVec4 kColRed     (0.9373f, 0.2667f, 0.2667f, 1.00f);

ImU32 Col(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
ImU32 ColAlpha(const ImVec4& c, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * a)); }

ID3D11Device*           g_pd3dDevice = nullptr;
ID3D11DeviceContext*    g_pd3dContext = nullptr;
IDXGISwapChain*         g_swapChain = nullptr;
ID3D11RenderTargetView* g_mainRTV = nullptr;

void CreateRenderTarget()
{
    ID3D11Texture2D* bb = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (!bb) return;
    g_pd3dDevice->CreateRenderTargetView(bb, nullptr, &g_mainRTV);
    bb->Release();
}

bool CreateDeviceD3D(HWND hWnd, bool& usedWarp)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL fl[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                                D3D11_CREATE_DEVICE_SINGLETHREADED, fl, 2,
                                                D3D11_SDK_VERSION, &sd,
                                                &g_swapChain, &g_pd3dDevice, &got,
                                                &g_pd3dContext);
    usedWarp = false;
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                            D3D11_CREATE_DEVICE_SINGLETHREADED, fl, 2,
                                            D3D11_SDK_VERSION, &sd, &g_swapChain,
                                            &g_pd3dDevice, &got, &g_pd3dContext);
        usedWarp = SUCCEEDED(hr);
    }
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return g_mainRTV != nullptr;
}

void CleanupDeviceD3D()
{
    if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; }
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_pd3dContext) { g_pd3dContext->Release(); g_pd3dContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

bool SaveBackbufferBmp(const std::wstring& path, std::string& err)
{
    err.clear();
    if (!g_swapChain || !g_pd3dDevice || !g_pd3dContext) {
        err = "no D3D device";
        return false;
    }
    ID3D11Texture2D* back = nullptr;
    HRESULT hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(hr) || !back) {
        err = "GetBuffer failed";
        return false;
    }
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC sdesc = desc;
    sdesc.Usage = D3D11_USAGE_STAGING;
    sdesc.BindFlags = 0;
    sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sdesc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    hr = g_pd3dDevice->CreateTexture2D(&sdesc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        back->Release();
        err = "staging texture failed";
        return false;
    }
    g_pd3dContext->CopyResource(staging, back);
    back->Release();

    D3D11_MAPPED_SUBRESOURCE map = {};
    hr = g_pd3dContext->Map(staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) {
        staging->Release();
        err = "map failed";
        return false;
    }

    int w = (int)desc.Width;
    int h = (int)desc.Height;
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + (DWORD)w * (DWORD)h * 4;
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = -h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)w * (DWORD)h * 4;
    ih.biXPelsPerMeter = 2835;
    ih.biYPelsPerMeter = 2835;

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        g_pd3dContext->Unmap(staging, 0);
        staging->Release();
        err = "cannot create output file";
        return false;
    }
    DWORD wrote = 0;
    bool ok = WriteFile(f, &fh, sizeof(fh), &wrote, nullptr) != FALSE;
    if (ok) ok = WriteFile(f, &ih, sizeof(ih), &wrote, nullptr) != FALSE;
    std::vector<BYTE> row((size_t)w * 4);
    for (int y = 0; y < h && ok; ++y) {
        const BYTE* src = static_cast<const BYTE*>(map.pData) + (size_t)y * map.RowPitch;
        for (int x = 0; x < w; ++x) {
            row[(size_t)x * 4 + 0] = src[(size_t)x * 4 + 2];
            row[(size_t)x * 4 + 1] = src[(size_t)x * 4 + 1];
            row[(size_t)x * 4 + 2] = src[(size_t)x * 4 + 0];
            row[(size_t)x * 4 + 3] = 255;
        }
        ok = WriteFile(f, row.data(), (DWORD)row.size(), &wrote, nullptr) != FALSE;
    }
    g_pd3dContext->Unmap(staging, 0);
    staging->Release();
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(path.c_str());
        err = "write failed";
        return false;
    }
    return true;
}

ID3D11ShaderResourceView* MakeIconTexture(HICON hIcon, int size)
{
    if (!hIcon || !g_pd3dDevice) return nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    if (!dc) { ReleaseDC(nullptr, screen); return nullptr; }

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) { DeleteDC(dc); ReleaseDC(nullptr, screen); return nullptr; }
    HGDIOBJ old = SelectObject(dc, dib);
    RECT all = { 0, 0, size, size };
    FillRect(dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
    DrawIconEx(dc, 0, 0, hIcon, size, size, 0, nullptr, DI_NORMAL);

    uint32_t* px = (uint32_t*)bits;
    bool hasAlpha = false;
    for (int p = 0; p < size * size; ++p) {
        if ((px[p] & 0xFF000000) != 0) { hasAlpha = true; break; }
    }
    if (!hasAlpha) {
        for (int p = 0; p < size * size; ++p) {
            if ((px[p] & 0x00FFFFFF) != 0) px[p] |= 0xFF000000;
        }
    }

    ID3D11ShaderResourceView* srv = nullptr;
    D3D11_TEXTURE2D_DESC td;
    ZeroMemory(&td, sizeof(td));
    td.Width = (UINT)size;
    td.Height = (UINT)size;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA srd;
    ZeroMemory(&srd, sizeof(srd));
    srd.pSysMem = bits;
    srd.SysMemPitch = (UINT)size * 4;
    ID3D11Texture2D* tex = nullptr;
    if (SUCCEEDED(g_pd3dDevice->CreateTexture2D(&td, &srd, &tex)) && tex) {
        g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
    }
    SelectObject(dc, old);
    DeleteObject(dib);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return srv;
}

std::unordered_map<std::wstring, ID3D11ShaderResourceView*> g_iconCache;

ID3D11ShaderResourceView* GetOrLoadAppIconSrv(const std::wstring& image)
{
    if (image.empty() || !g_pd3dDevice) return nullptr;
    std::unordered_map<std::wstring, ID3D11ShaderResourceView*>::const_iterator it =
        g_iconCache.find(image);
    if (it != g_iconCache.end()) return it->second;

    ID3D11ShaderResourceView* srv = nullptr;
    SHFILEINFOW sfi;
    ZeroMemory(&sfi, sizeof(sfi));
    if (SHGetFileInfoW(image.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES) && sfi.hIcon) {
        srv = MakeIconTexture(sfi.hIcon, 48);
        DestroyIcon(sfi.hIcon);
    }
    g_iconCache[image] = srv;
    return srv;
}

void ReleaseIconCache()
{
    for (std::unordered_map<std::wstring, ID3D11ShaderResourceView*>::iterator it = g_iconCache.begin();
         it != g_iconCache.end(); ++it) {
        if (it->second) it->second->Release();
    }
    g_iconCache.clear();
}

ID3D11ShaderResourceView* LoadPngResourceTexture(HINSTANCE hInst, int resourceId, int maxSize)
{
    if (!g_pd3dDevice) return nullptr;
    HRSRC res = FindResourceW(hInst, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
    if (!res) return nullptr;
    HGLOBAL loaded = LoadResource(hInst, res);
    DWORD size = SizeofResource(hInst, res);
    const BYTE* data = static_cast<const BYTE*>(LockResource(loaded));
    if (!data || size == 0) return nullptr;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return nullptr;

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;

    if (SUCCEEDED(factory->CreateStream(&stream))) {
        hr = stream->InitializeFromMemory(const_cast<BYTE*>(data), size);
    }
    if (SUCCEEDED(hr) && stream) {
        hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand,
                                              &decoder);
    }
    if (SUCCEEDED(hr) && decoder) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr) && frame) {
        hr = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(hr) && converter) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                   nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }

    IWICBitmapSource* source = converter;
    UINT outW = 0;
    UINT outH = 0;
    if (SUCCEEDED(hr) && converter) hr = converter->GetSize(&outW, &outH);
    if (SUCCEEDED(hr) && maxSize > 0 && (outW > (UINT)maxSize || outH > (UINT)maxSize)) {
        UINT scaleW = outW;
        UINT scaleH = outH;
        if (outW >= outH) {
            scaleW = (UINT)maxSize;
            scaleH = outH * (UINT)maxSize / outW;
        } else {
            scaleH = (UINT)maxSize;
            scaleW = outW * (UINT)maxSize / outH;
        }
        if (scaleW == 0) scaleW = 1;
        if (scaleH == 0) scaleH = 1;
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr) && scaler) {
            hr = scaler->Initialize(converter, scaleW, scaleH, WICBitmapInterpolationModeFant);
            if (SUCCEEDED(hr)) {
                source = scaler;
                outW = scaleW;
                outH = scaleH;
            }
        }
    }

    if (SUCCEEDED(hr) && source && outW > 0 && outH > 0) {
        std::vector<BYTE> pixels((size_t)outW * (size_t)outH * 4);
        hr = source->CopyPixels(nullptr, outW * 4, (UINT)pixels.size(), pixels.data());
        if (SUCCEEDED(hr)) {
            D3D11_TEXTURE2D_DESC td;
            ZeroMemory(&td, sizeof(td));
            td.Width = outW;
            td.Height = outH;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA srd;
            ZeroMemory(&srd, sizeof(srd));
            srd.pSysMem = pixels.data();
            srd.SysMemPitch = outW * 4;
            if (SUCCEEDED(g_pd3dDevice->CreateTexture2D(&td, &srd, &texture)) && texture) {
                g_pd3dDevice->CreateShaderResourceView(texture, nullptr, &srv);
            }
        }
    }

    if (texture) texture->Release();
    if (scaler) scaler->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    factory->Release();
    (void)outW;
    (void)outH;
    return srv;
}

const wchar_t* kHudClass = L"RamStasisPickHud";
HWND  g_hudWnd = nullptr;
HFONT g_hudFontSmall = nullptr;
HFONT g_hudFontBold = nullptr;
HFONT g_hudFontTitle = nullptr;

std::wstring g_hudTitle = L"";
std::wstring g_hudApp = L"";
std::wstring g_hudMeta = L"";
std::wstring g_hudHint = L"";
bool g_hudProtected = false;

HFONT MakeUiFont(int height, int weight)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void HudCreateFonts()
{
    if (g_hudFontSmall) DeleteObject(g_hudFontSmall);
    if (g_hudFontBold) DeleteObject(g_hudFontBold);
    if (g_hudFontTitle) DeleteObject(g_hudFontTitle);
    g_hudFontSmall = MakeUiFont((int)Dpi(13), FW_NORMAL);
    g_hudFontBold = MakeUiFont((int)Dpi(16), FW_SEMIBOLD);
    g_hudFontTitle = MakeUiFont((int)Dpi(18), FW_BOLD);
    if (g_hudWnd) InvalidateRect(g_hudWnd, nullptr, TRUE);
}

void HudPaintTo(HDC dc, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    RECT rc = { 0, 0, width, height };

    HBRUSH bg = CreateSolidBrush(RGB(23, 28, 36));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    RECT accent = { 0, 0, (LONG)Dpi(3), rc.bottom };
    HBRUSH accentBrush = CreateSolidBrush(g_hudProtected ? RGB(239, 68, 68) : RGB(16, 185, 129));
    FillRect(dc, &accent, accentBrush);
    DeleteObject(accentBrush);

    HPEN border = CreatePen(PS_SOLID, 1, RGB(38, 46, 61));
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, rc.right, rc.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(border);

    SetBkMode(dc, TRANSPARENT);

    const int labelX = (int)Dpi(16);
    const int textX = (int)Dpi(78);
    const int rightPad = (int)Dpi(14);

    RECT label = { labelX, (int)Dpi(9), textX - (int)Dpi(6), (int)Dpi(32) };
    SelectObject(dc, g_hudFontSmall);
    SetTextColor(dc, RGB(6, 182, 212));
    DrawTextW(dc, L"WINDOW", -1, &label, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    RECT title = { textX, (int)Dpi(7), rc.right - rightPad, (int)Dpi(33) };
    SelectObject(dc, g_hudFontTitle);
    SetTextColor(dc, RGB(231, 234, 242));
    DrawTextW(dc, g_hudTitle.c_str(), -1, &title,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT appLabel = { labelX, (int)Dpi(37), textX - (int)Dpi(6), (int)Dpi(58) };
    SelectObject(dc, g_hudFontSmall);
    SetTextColor(dc, RGB(139, 152, 173));
    DrawTextW(dc, L"APP", -1, &appLabel, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    RECT app = { textX, (int)Dpi(35), rc.right - rightPad, (int)Dpi(59) };
    SelectObject(dc, g_hudFontBold);
    SetTextColor(dc, RGB(150, 200, 255));
    DrawTextW(dc, g_hudApp.c_str(), -1, &app,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT meta = { labelX, (int)Dpi(62), rc.right - rightPad, (int)Dpi(84) };
    SelectObject(dc, g_hudFontSmall);
    SetTextColor(dc, g_hudProtected ? RGB(239, 68, 68) : RGB(16, 185, 129));
    DrawTextW(dc, g_hudMeta.c_str(), -1, &meta,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT hint = { labelX, (int)Dpi(86), rc.right - rightPad, (int)Dpi(108) };
    SelectObject(dc, g_hudFontSmall);
    SetTextColor(dc, RGB(139, 152, 173));
    DrawTextW(dc, g_hudHint.c_str(), -1, &hint,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void HudPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HudPaintTo(dc, rc.right, rc.bottom);
    EndPaint(hwnd, &ps);
}

bool SaveHudBmp(const std::wstring& path, std::string& err)
{
    err.clear();
    int w = (int)Dpi(430);
    int h = (int)Dpi(116);
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    if (!dc) {
        ReleaseDC(nullptr, screen);
        err = "no dc";
        return false;
    }
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) {
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        err = "no dib";
        return false;
    }
    HGDIOBJ old = SelectObject(dc, dib);
    HudPaintTo(dc, w, h);
    GdiFlush();

    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + (DWORD)w * (DWORD)h * 4;
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = -h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)w * (DWORD)h * 4;
    ih.biXPelsPerMeter = 2835;
    ih.biYPelsPerMeter = 2835;

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        SelectObject(dc, old);
        DeleteObject(dib);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        err = "cannot create output file";
        return false;
    }
    DWORD wrote = 0;
    bool ok = WriteFile(f, &fh, sizeof(fh), &wrote, nullptr) != FALSE;
    if (ok) ok = WriteFile(f, &ih, sizeof(ih), &wrote, nullptr) != FALSE;
    std::vector<BYTE> row((size_t)w * 4);
    const BYTE* src = static_cast<const BYTE*>(bits);
    for (int y = 0; y < h && ok; ++y) {
        for (int x = 0; x < w; ++x) {
            row[(size_t)x * 4 + 0] = src[(size_t)y * w * 4 + (size_t)x * 4 + 2];
            row[(size_t)x * 4 + 1] = src[(size_t)y * w * 4 + (size_t)x * 4 + 1];
            row[(size_t)x * 4 + 2] = src[(size_t)y * w * 4 + (size_t)x * 4 + 0];
            row[(size_t)x * 4 + 3] = 255;
        }
        ok = WriteFile(f, row.data(), (DWORD)row.size(), &wrote, nullptr) != FALSE;
    }
    CloseHandle(f);
    SelectObject(dc, old);
    DeleteObject(dib);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    if (!ok) {
        DeleteFileW(path.c_str());
        err = "write failed";
        return false;
    }
    return true;
}

LRESULT CALLBACK HudWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        HudPaint(hwnd);
        return 0;
    case WM_PRINTCLIENT: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        HudPaintTo((HDC)wParam, rc.right, rc.bottom);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void HudCreate(HINSTANCE hInst)
{
    if (g_hudWnd) return;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = HudWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32515));
    wc.lpszClassName = kHudClass;
    RegisterClassExW(&wc);

    g_hudWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kHudClass, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hInst, nullptr);
    if (g_hudWnd) {
        SetLayeredWindowAttributes(g_hudWnd, 0, 238, LWA_ALPHA);
        SetWindowRgn(g_hudWnd,
                     CreateRoundRectRgn(0, 0, (int)Dpi(430) + 1, (int)Dpi(116) + 1,
                                        (int)Dpi(12), (int)Dpi(12)),
                     TRUE);
    }
    HudCreateFonts();
}

void HudDestroy(HINSTANCE hInst)
{
    if (g_hudWnd) {
        DestroyWindow(g_hudWnd);
        g_hudWnd = nullptr;
    }
    if (g_hudFontSmall) { DeleteObject(g_hudFontSmall); g_hudFontSmall = nullptr; }
    if (g_hudFontBold) { DeleteObject(g_hudFontBold); g_hudFontBold = nullptr; }
    if (g_hudFontTitle) { DeleteObject(g_hudFontTitle); g_hudFontTitle = nullptr; }
    UnregisterClassW(kHudClass, hInst);
}

void HudShow(bool show)
{
    if (!g_hudWnd) return;
    ShowWindow(g_hudWnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void HudSetText(const std::wstring& title, const std::wstring& app,
                const std::wstring& meta, const std::wstring& hint, bool protectedHit)
{
    g_hudTitle = title.empty() ? L"(untitled window)" : title;
    g_hudApp = app;
    g_hudMeta = meta;
    g_hudHint = hint;
    g_hudProtected = protectedHit;
    if (g_hudWnd) {
        int w = (int)Dpi(430);
        int h = (int)Dpi(116);
        SetWindowRgn(g_hudWnd, CreateRoundRectRgn(0, 0, w + 1, h + 1,
                                                  (int)Dpi(12), (int)Dpi(12)), TRUE);
        InvalidateRect(g_hudWnd, nullptr, TRUE);
    }
}

void HudPlaceAt(POINT pt)
{
    if (!g_hudWnd) return;
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (mon && GetMonitorInfoW(mon, &mi)) work = mi.rcWork;
    else { work.left = 0; work.top = 0; work.right = GetSystemMetrics(SM_CXSCREEN); work.bottom = GetSystemMetrics(SM_CYSCREEN); }

    int w = (int)Dpi(430);
    int h = (int)Dpi(116);
    int x = pt.x + (int)Dpi(20);
    int y = pt.y + (int)Dpi(20);
    if (x + w > work.right) x = pt.x - w - (int)Dpi(20);
    if (y + h > work.bottom) y = pt.y - h - (int)Dpi(20);
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    if (x + w > work.right) x = work.right - w;
    if (y + h > work.bottom) y = work.bottom - h;
    SetWindowPos(g_hudWnd, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

}

void LogUi(const char* fmt, ...)
{
    LogFile& lg = Log();
    if (!lg.f) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    EnterCriticalSection(&lg.cs);
    fprintf(lg.f, "%02u:%02u:%02u.%03u %s\n", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, buf);
    fflush(lg.f);
    LeaveCriticalSection(&lg.cs);
}

namespace {

HWND  g_hwnd = nullptr;
HINSTANCE g_hInst = nullptr;
bool  g_quit = false;
bool  g_forceQuit = false;
bool  g_dirty = true;
bool  g_hidden = false;
bool  g_trayAdded = false;
bool  g_warpDevice = false;
HANDLE g_wakeEvent = nullptr;
HANDLE g_cmdEvent = nullptr;
HANDLE g_workerStop = nullptr;
HANDLE g_instanceMutex = nullptr;
HICON g_appIconLarge = nullptr;
HICON g_appIconSmall = nullptr;
HICON g_trayIcon = nullptr;
ID3D11ShaderResourceView* g_logoTexture = nullptr;
bool  g_isAdmin = false;
UINT  g_trayCbMsg = 0;
UINT  g_wakeMsg = 0;
std::wstring g_trayTipLast;
const UINT_PTR kTimerId = 1;
const int  kHotkeyId = 0x0BEE;
unsigned g_idleTicks = 0;

const wchar_t* kRamStasisVersion = L"1.0.0";
const wchar_t* kRamStasisWakeMessage = L"WM_RAMSTASIS_WAKE";
const wchar_t* kRamStasisMutexName = L"Local\\RamStasis.SingleInstance";

Telemetry g_tel;
bool      g_telValid = false;
ULONGLONG g_lastTelMs = 0;
ULONGLONG g_lastFgMs = 0;
ULONGLONG g_lastSwapMs = 0;

std::vector<GuardComp> g_guards;
ULONGLONG g_lastGuardMs = 0;

std::vector<PickRow> g_appRows;
ULONGLONG g_appRowsMs = 0;

std::string g_statusLine;
std::string g_recoveryNote;

bool g_exitModal = false;
int  g_frozenCountModal = 0;

bool g_hotkeyRegistered = false;
bool g_captureHotkey = false;

bool  g_pickMode = false;
bool  g_pickArmed = false;
int   g_pickSlot = -1;
ULONGLONG g_hudLastMs = 0;

PagefileStatus g_pf;
bool g_pfScanned = false;
bool g_pfApplying = false;
int  g_pfDrive = -1;
unsigned long long g_pfInitialMb = 0;
unsigned long long g_pfMaxMb = 0;
bool g_pfSystemManaged = false;
bool g_pfResultValid = false;
bool g_pfResultOk = false;
bool g_pfResultReboot = false;
std::string g_pfResultMessage;
ULONGLONG g_lastPfScanMs = 0;

enum UiPage { PAGE_WORKSPACE = 0, PAGE_EXPLORER, PAGE_SETTINGS, PAGE_COUNT };
int g_page = PAGE_WORKSPACE;

bool g_qaMode = false;
bool g_shotMode = false;
std::wstring g_shotPath;
bool g_qaPopup = false;
bool g_qaHotkey = false;
int  g_qaExpandRow = -1;
int  g_qaPfPreset = -1;
int  g_qaPfResult = 0;
bool g_qaPfClamp = false;
bool g_qaHud = false;
int  g_hotkeyRetriesLeft = 0;
ULONGLONG g_hotkeyRetryAt = 0;

bool g_settingsDirty = false;
ULONGLONG g_settingsDirtyMs = 0;

char g_appFilter[128] = "";
int  g_appFilterTab = 0;
std::unordered_set<unsigned long long> g_expandedRows;

enum CmdKind { CMD_FREEZE = 0, CMD_THAW, CMD_SWAP, CMD_PANIC, CMD_PAGEFILE };

struct UiCommand {
    int kind = CMD_FREEZE;
    int slotA = -1;
    int slotB = -1;
    unsigned long long pidA = 0;
    unsigned long long pidB = 0;
    bool treeA = true;
    bool clearOnSuccess = false;

    PagefileStatus pfStatus;
    int pfDrive = -1;
    unsigned long long pfInitialMb = 0;
    unsigned long long pfMaxMb = 0;
    bool pfSystemManaged = false;
};

struct UiResult {
    int kind = CMD_FREEZE;
    int slotA = -1;
    int slotB = -1;
    bool ok = false;
    bool refused = false;
    long exitCode = -1;
    std::string error;
    FreezeInfo freeze;
    ThawInfo thaw;
    int thawedRoots = 0;
    int orphanRoots = 0;
    unsigned long long orphanSuspendsReleased = 0;
    int orphanSkipped = 0;
    bool clearOnSuccess = false;
    bool swapContents = false;
    PagefileApplyResult pf;
    bool pfRan = false;
};

CRITICAL_SECTION g_cmdLock;
CRITICAL_SECTION g_resLock;
std::vector<UiCommand> g_cmdQ;
std::vector<UiResult>  g_resQ;

void RunGuardScan()
{
    EngineResult r = RunEngine(L"guard --json");
    std::vector<GuardComp> comps;
    if (ParseGuardOut(r.out, comps)) {
        EnterCriticalSection(&g_resLock);
        g_guards = comps;
        g_lastGuardMs = GetTickCount64();
        LeaveCriticalSection(&g_resLock);
        LogUi("guard: %u component(s)", (unsigned)comps.size());
    } else {
        LogUi("guard: no JSON (exit=%ld)", r.exitCode);
    }
}

UiResult DoFreeze(const UiCommand& cmd)
{
    UiResult res;
    res.kind = CMD_FREEZE;
    res.slotA = cmd.slotA;

    if (cmd.pidA == 0 || cmd.pidA == 4 || cmd.pidA == GetCurrentProcessId() ||
        IsCoreSystemProcess(cmd.pidA)) {
        res.ok = false;
        res.refused = true;
        res.error = "Refused: system/shell processes cannot be frozen";
        LogUi("freeze pid=%llu REFUSED by UI core-process guard", cmd.pidA);
        return res;
    }

    std::wstring args = L"freeze " + std::to_wstring(cmd.pidA);
    if (!cmd.treeA) args += L" --no-tree";
    args += L" --json";
    int baselineProcs = 0;
    unsigned long long baselineWs = 0;
    unsigned long long baselineCommit = 0;
    TreePrivateWorkingSetUi(cmd.pidA, cmd.treeA, baselineProcs, baselineWs, baselineCommit);
    EngineResult r = RunEngine(args.c_str());
    res.exitCode = r.exitCode;
    res.refused = false;
    if (r.exitCode == 8) {
        res.refused = true;
        res.ok = false;
        res.error = "REFUSED by anti-cheat guard: " + ShortError(r, std::string());
        LogUi("freeze pid=%llu REFUSED by anti-cheat guard", cmd.pidA);
        return res;
    }
    FreezeInfo fi;
    fi.exitCode = r.exitCode;
    if (!ParseFreezeOut(r.out, fi)) {
        res.ok = false;
        res.error = ShortError(r, std::string());
        LogUi("freeze pid=%llu FAILED exit=%ld err=%s", cmd.pidA, r.exitCode, res.error.c_str());
        return res;
    }
    res.freeze = fi;
    res.ok = fi.ok;
    res.freeze.privateWsBefore = baselineWs;
    if (!fi.ok) {
        res.error = ShortError(r, fi.error);
        LogUi("freeze pid=%llu refused by engine: %s", cmd.pidA, res.error.c_str());
    } else {
        LogUi("freeze pid=%llu ok: freedWs=%llu procs=%d windows=%d",
              cmd.pidA, fi.freedWs, fi.procCount, fi.windowCount);
    }
    return res;
}

UiResult DoThaw(const UiCommand& cmd)
{
    UiResult res;
    res.kind = CMD_THAW;
    res.slotA = cmd.slotA;
    res.clearOnSuccess = cmd.clearOnSuccess;
    std::wstring args = L"thaw " + std::to_wstring(cmd.pidA) + L" --json";
    EngineResult r = RunEngine(args.c_str());
    res.exitCode = r.exitCode;
    ThawInfo ti;
    if (!ParseThawOut(r.out, ti)) {
        res.ok = false;
        res.error = ShortError(r, std::string());
        LogUi("thaw pid=%llu FAILED exit=%ld err=%s", cmd.pidA, r.exitCode, res.error.c_str());
        return res;
    }
    res.thaw = ti;
    res.ok = ti.ok;
    if (!ti.ok) {
        res.error = ShortError(r, ti.error);
        LogUi("thaw pid=%llu FAILED: %s", cmd.pidA, res.error.c_str());
    } else {
        LogUi("thaw pid=%llu ok: resumed=%d threads=%d wake=%.2fms",
              cmd.pidA, ti.processesResumed, ti.threadsVerified, ti.wakeMs);
    }
    return res;
}

UiResult DoSwap(const UiCommand& cmd)
{
    UiResult res;
    res.kind = CMD_SWAP;
    res.slotA = cmd.slotA;
    res.slotB = cmd.slotB;
    res.swapContents = (cmd.slotB >= 0);

    UiCommand fz = cmd;
    fz.kind = CMD_FREEZE;
    UiResult fr = DoFreeze(fz);
    if (!fr.ok) {
        res.ok = false;
        res.error = fr.error.empty() ? std::string("freeze step failed") : fr.error;
        res.refused = fr.refused;
        res.exitCode = fr.exitCode;
        return res;
    }
    res.freeze = fr.freeze;

    if (cmd.slotB >= 0) {
        UiCommand tw = cmd;
        tw.kind = CMD_THAW;
        tw.pidA = cmd.pidB;
        tw.slotA = cmd.slotB;
        UiResult tr = DoThaw(tw);
        if (!tr.ok) {
            res.ok = false;
            res.error = "freeze ok, thaw failed: " + tr.error;
            res.exitCode = tr.exitCode;
            return res;
        }
        res.thaw = tr.thaw;
    }
    res.ok = true;
    return res;
}

UiResult DoPanic()
{
    UiResult res;
    res.kind = CMD_PANIC;
    std::vector<unsigned long long> pids;
    std::vector<StateWinRec> wins;
    std::vector<StateFileInfo> infos;
    ReadStateDirFrozen(pids, wins, &infos);
    int n = 0;
    for (size_t i = 0; i < pids.size(); ++i) {
        std::wstring args = L"thaw " + std::to_wstring(pids[i]) + L" --json";
        EngineResult r = RunEngine(args.c_str());
        ThawInfo ti;
        if (ParseThawOut(r.out, ti) && ti.ok) {
            ++n;
            LogUi("panic: thawed pid=%llu", pids[i]);
        } else {
            LogUi("panic: thaw pid=%llu FAILED exit=%ld", pids[i], r.exitCode);
            char eb[64];
            sprintf_s(eb, sizeof(eb), "pid %llu did not thaw", pids[i]);
            res.error = eb;
        }
    }
    res.thawedRoots = n;

    {
        EngineResult r = RunEngine(L"thaw --orphans --json");
        LogUi("panic: orphan sweep exit=%ld", r.exitCode);
        std::string body = r.out;
        size_t at = body.find("{\"command\": \"thaw_orphans\"");
        if (at != std::string::npos) {
            size_t end = body.rfind("}");
            if (end != std::string::npos && end > at) {
                std::string obj = body.substr(at, end - at + 1);
                unsigned long long orphans = 0, released = 0, skipped = 0;
                const char* keys[3] = { "\"orphans_resumed\":", "\"thread_suspends_released\":", "\"skipped\":" };
                unsigned long long* dst[3] = { &orphans, &released, &skipped };
                for (int k = 0; k < 3; ++k) {
                    size_t p2 = obj.find(keys[k]);
                    if (p2 != std::string::npos)
                        *dst[k] = strtoull(obj.c_str() + p2 + strlen(keys[k]), nullptr, 10);
                }
                res.orphanRoots = (int)orphans;
                res.orphanSuspendsReleased = released;
                res.orphanSkipped = (int)skipped;
                LogUi("panic: orphans resumed=%llu thread-suspends=%llu skipped=%llu",
                      orphans, released, skipped);
            }
        }
    }

    res.ok = (n == (int)pids.size());
    return res;
}

UiResult DoPagefile(const UiCommand& cmd)
{
    UiResult res;
    res.kind = CMD_PAGEFILE;
    res.pf = ApplyPagefileConfig(cmd.pfStatus, cmd.pfDrive, cmd.pfInitialMb,
                                 cmd.pfMaxMb, cmd.pfSystemManaged);
    res.pfRan = true;
    res.ok = res.pf.ok;
    res.error = ToUtf8(res.pf.message);
    Telemetry probe;
    PollTelemetry(probe);
    return res;
}

void WorkerMain()
{
    for (;;) {
        UiCommand cmd;
        bool have = false;
        EnterCriticalSection(&g_cmdLock);
        if (!g_cmdQ.empty()) {
            cmd = g_cmdQ.front();
            g_cmdQ.erase(g_cmdQ.begin());
            have = true;
        }
        LeaveCriticalSection(&g_cmdLock);
        if (!have) {
            DWORD w = WaitForSingleObject(g_cmdEvent, 500);
            if (WaitForSingleObject(g_workerStop, 0) == WAIT_OBJECT_0) break;
            if (w == WAIT_TIMEOUT) {
                ULONGLONG now = GetTickCount64();
                ULONGLONG last = 0;
                EnterCriticalSection(&g_resLock);
                last = g_lastGuardMs;
                LeaveCriticalSection(&g_resLock);
                if (now - last > 60000) RunGuardScan();
            }
            continue;
        }
        UiResult res;
        switch (cmd.kind) {
        case CMD_FREEZE:   res = DoFreeze(cmd);   break;
        case CMD_THAW:     res = DoThaw(cmd);     break;
        case CMD_SWAP:     res = DoSwap(cmd);     break;
        case CMD_PANIC:    res = DoPanic();       break;
        case CMD_PAGEFILE: res = DoPagefile(cmd); break;
        default: break;
        }
        EnterCriticalSection(&g_resLock);
        g_resQ.push_back(res);
        LeaveCriticalSection(&g_resLock);
        SetEvent(g_wakeEvent);
    }
}

void PostCommand(const UiCommand& cmd)
{
    EnterCriticalSection(&g_cmdLock);
    g_cmdQ.push_back(cmd);
    LeaveCriticalSection(&g_cmdLock);
    SetEvent(g_cmdEvent);
}

std::wstring VkName(UINT vk)
{
    wchar_t buf[32];
    if (vk >= 'A' && vk <= 'Z') { buf[0] = (wchar_t)vk; buf[1] = 0; return buf; }
    if (vk >= '0' && vk <= '9') { buf[0] = (wchar_t)vk; buf[1] = 0; return buf; }
    if (vk >= VK_F1 && vk <= VK_F24) {
        swprintf_s(buf, L"F%u", vk - VK_F1 + 1);
        return buf;
    }
    switch (vk) {
    case VK_SPACE:    return L"Space";
    case VK_RETURN:   return L"Enter";
    case VK_TAB:      return L"Tab";
    case VK_BACK:     return L"Backspace";
    case VK_INSERT:   return L"Insert";
    case VK_DELETE:   return L"Delete";
    case VK_HOME:     return L"Home";
    case VK_END:      return L"End";
    case VK_PRIOR:    return L"Page Up";
    case VK_NEXT:     return L"Page Down";
    case VK_UP:       return L"Up";
    case VK_DOWN:     return L"Down";
    case VK_LEFT:     return L"Left";
    case VK_RIGHT:    return L"Right";
    case VK_OEM_1:    return L";";
    case VK_OEM_2:    return L"/";
    case VK_OEM_3:    return L"`";
    case VK_OEM_4:    return L"[";
    case VK_OEM_5:    return L"\\";
    case VK_OEM_6:    return L"]";
    case VK_OEM_7:    return L"'";
    case VK_OEM_PLUS: return L"=";
    case VK_OEM_MINUS:return L"-";
    case VK_OEM_COMMA:return L",";
    case VK_OEM_PERIOD:return L".";
    default: break;
    }
    wchar_t name[64];
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (sc != 0 && GetKeyNameTextW((LONG)(sc << 16), name, 64) > 0) return name;
    swprintf_s(buf, L"VK 0x%02X", (unsigned)vk);
    return buf;
}

std::wstring HotkeyLabel()
{
    std::wstring out;
    if (g_cfg.swapHotkeyModifiers & MOD_CONTROL) out += L"Ctrl + ";
    if (g_cfg.swapHotkeyModifiers & MOD_ALT) out += L"Alt + ";
    if (g_cfg.swapHotkeyModifiers & MOD_SHIFT) out += L"Shift + ";
    if (g_cfg.swapHotkeyModifiers & MOD_WIN) out += L"Win + ";
    out += VkName(g_cfg.swapHotkeyVk);
    return out;
}

bool RegisterConfiguredHotkey()
{
    if (g_hwnd && g_hotkeyRegistered) {
        UnregisterHotKey(g_hwnd, kHotkeyId);
        g_hotkeyRegistered = false;
    }
    if (!g_hwnd) return false;
    UINT mods = g_cfg.swapHotkeyModifiers;
    if (mods == 0) mods = MOD_CONTROL | MOD_ALT;
    if (RegisterHotKey(g_hwnd, kHotkeyId, mods | MOD_NOREPEAT, g_cfg.swapHotkeyVk)) {
        g_hotkeyRegistered = true;
        LogUi("hotkey registered: mods=%u vk=%u", mods, g_cfg.swapHotkeyVk);
        return true;
    }
    LogUi("RegisterHotKey FAILED mods=%u vk=%u win32=%lu",
          mods, g_cfg.swapHotkeyVk, (unsigned long)GetLastError());
    return false;
}

bool SlotTree(const Slot& s)
{
    return s.treeOverride ? s.tree : g_cfg.defaultFreezeTree;
}

bool SlotAutoThaw(const Slot& s)
{
    return s.autoThawOverride ? s.autoThaw : g_cfg.defaultAutoThawOnFocus;
}

std::wstring SlotDisplayName(const Slot& s)
{
    if (!s.friendlyName.empty()) return s.friendlyName;
    if (!s.exe.empty()) return s.exe;
    return L"Unassigned";
}

void RefreshSlotLive(size_t i)
{
    if (i >= g_slots.size()) return;
    Slot& s = g_slots[i];
    if (s.state == SLOT_EMPTY || s.pid == 0 || s.state == SLOT_BUSY) return;
    int n = 0;
    unsigned long long ws = 0, com = 0;
    if (TreePrivateWorkingSetUi(s.pid, SlotTree(s), n, ws, com)) {
        s.procCount = n;
        s.wsNow = ws;
        s.commitNow = com;
    }
}

void ApplyFreezeOk(size_t i, const FreezeInfo& fi)
{
    if (i >= g_slots.size()) return;
    Slot& s = g_slots[i];
    s.state = SLOT_FROZEN;
    s.busyAction.clear();
    if (!fi.exe.empty()) s.exe = fi.exe;
    if (!fi.image.empty()) {
        s.image = fi.image;
        s.friendlyName = FriendlyExeName(fi.image);
    }
    if (fi.procCount > 0) s.procCount = fi.procCount;
    s.windowCount = fi.windowCount;
    s.lastError.clear();

    StateFileInfo info;
    unsigned long long privateBaseline = 0;
    if (ReadStateFile(s.pid, info)) {
        s.treePids.clear();
        for (size_t k = 0; k < info.procs.size(); ++k) {
            s.treePids.push_back(info.procs[k].pid);
            unsigned long long bytes = 0;
            if (ProcessPrivateWorkingSetUi(info.procs[k].pid, bytes)) privateBaseline += bytes;
            if (info.procs[k].pid == s.pid && s.createTime == 0)
                s.createTime = info.procs[k].createTime;
        }
        s.procCount = (int)info.procs.size();
    } else {
        s.treePids.clear();
    }
    s.baselineWs = fi.privateWsBefore ? fi.privateWsBefore : privateBaseline;
    RefreshSlotLive(i);
    if (s.baselineWs == 0 && s.wsNow > 0) s.baselineWs = s.wsNow + fi.freedWs;
    if (s.baselineWs < s.wsNow) s.baselineWs = s.wsNow;
    if (s.createTime == 0) s.createTime = ProcessCreateTimeUi(s.pid);
    std::wstring title = PrimaryWindowTitleForPid(s.pid);
    if (!title.empty()) s.windowTitle = title;
    if (s.friendlyName.empty()) s.friendlyName = FriendlyExeName(s.image);
}

void ApplyThawOk(size_t i)
{
    if (i >= g_slots.size()) return;
    Slot& s = g_slots[i];
    s.state = SLOT_RUNNING;
    s.busyAction.clear();
    s.baselineWs = 0;
    s.treePids.clear();
    s.lastError.clear();
    RefreshSlotLive(i);
}

void ClearSlot(size_t i)
{
    if (i >= g_slots.size()) return;
    g_slots[i] = Slot();
}

void AddSlot()
{
    g_slots.push_back(Slot());
    g_dirty = true;
}

int PickTargetSlot(int preferred)
{
    if (preferred >= 0 && (size_t)preferred < g_slots.size()) return preferred;
    for (size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].state == SLOT_EMPTY) return (int)i;
    }
    g_slots.push_back(Slot());
    return (int)g_slots.size() - 1;
}

void AssignRowToSlot(int slot, const PickRow& row)
{
    int target = PickTargetSlot(slot);
    if (target < 0 || (size_t)target >= g_slots.size()) return;
    Slot& s = g_slots[target];
    s = Slot();
    s.state = SLOT_RUNNING;
    s.pid = row.rootPid;
    s.createTime = row.createTime ? row.createTime : ProcessCreateTimeUi(row.rootPid);
    s.exe = row.exe;
    s.image = row.image;
    s.friendlyName = row.friendlyName;
    if (s.friendlyName.empty()) s.friendlyName = FriendlyExeName(row.image);
    if (s.friendlyName.empty()) s.friendlyName = row.exe;
    s.windowTitle = row.primaryWindowTitle;
    s.treeOverride = false;
    s.autoThawOverride = false;
    s.tree = g_cfg.defaultFreezeTree;
    s.autoThaw = g_cfg.defaultAutoThawOnFocus;
    s.protectedHit = row.protectedHit;
    s.protectedBy = row.protectedBy;
    s.windowCount = (int)row.windows.size();
    RefreshSlotLive((size_t)target);
    char buf[320];
    sprintf_s(buf, sizeof(buf), "Assigned %s to Slot %c",
              ToUtf8(s.friendlyName).c_str(), 'A' + (char)(target % 26));
    g_statusLine = buf;
    g_dirty = true;
}

void QueueFreeze(int slotIdx);
void QueueThaw(int slotIdx, bool clearOnSuccess);

void RemoveSlot(size_t i)
{
    if (i >= g_slots.size()) return;
    if (g_slots[i].state == SLOT_BUSY) {
        g_statusLine = "Wait for the current operation before removing this card.";
        g_dirty = true;
        return;
    }
    if (g_slots[i].state == SLOT_FROZEN) {
        QueueThaw((int)i, true);
        g_statusLine = "Thawing process before removing card...";
        g_dirty = true;
        return;
    }
    g_slots.erase(g_slots.begin() + i);
    g_statusLine = "Removed process card";
    g_dirty = true;
}

void QueueClear(int slotIdx)
{
    if (slotIdx < 0 || (size_t)slotIdx >= g_slots.size()) return;
    Slot& s = g_slots[slotIdx];
    if (s.state == SLOT_BUSY) return;
    if (s.state == SLOT_FROZEN) {
        QueueThaw(slotIdx, true);
        return;
    }
    ClearSlot((size_t)slotIdx);
    g_statusLine = "Slot selection cleared";
    g_dirty = true;
}

void QueueFreeze(int slotIdx)
{
    if (slotIdx < 0 || (size_t)slotIdx >= g_slots.size()) return;
    Slot& s = g_slots[slotIdx];
    if (s.pid == 0 || s.state == SLOT_BUSY || s.state == SLOT_FROZEN) return;
    UiCommand c;
    c.kind = CMD_FREEZE;
    c.slotA = slotIdx;
    c.pidA = s.pid;
    c.treeA = SlotTree(s);
    PostCommand(c);
    s.state = SLOT_BUSY;
    s.busyAction = "Freezing...";
    s.lastError.clear();
    g_dirty = true;
}

void QueueThaw(int slotIdx, bool clearOnSuccess)
{
    if (slotIdx < 0 || (size_t)slotIdx >= g_slots.size()) return;
    Slot& s = g_slots[slotIdx];
    if (s.pid == 0 || s.state == SLOT_BUSY) return;
    UiCommand c;
    c.kind = CMD_THAW;
    c.slotA = slotIdx;
    c.pidA = s.pid;
    c.clearOnSuccess = clearOnSuccess;
    PostCommand(c);
    s.state = SLOT_BUSY;
    s.busyAction = "Thawing...";
    s.lastError.clear();
    g_dirty = true;
}

bool SwapPossible()
{
    int assigned = 0;
    for (size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].state != SLOT_EMPTY) ++assigned;
        if (g_slots[i].state == SLOT_BUSY) return false;
    }
    return assigned >= 1;
}

void QueueSwap()
{
    ULONGLONG now = GetTickCount64();
    if (now - g_lastSwapMs < 300) return;
    g_lastSwapMs = now;

    int running = -1, frozen = -1, assigned = 0;
    for (size_t i = 0; i < g_slots.size(); ++i) {
        SlotState st = g_slots[i].state;
        if (st == SLOT_BUSY) return;
        if (st == SLOT_EMPTY) continue;
        ++assigned;
        if (st == SLOT_FROZEN && frozen < 0) frozen = (int)i;
        else if (st == SLOT_RUNNING && running < 0) running = (int)i;
    }
    if (assigned == 0) return;

    if (assigned == 1) {
        int i = (running >= 0) ? running : frozen;
        if (g_slots[i].state == SLOT_FROZEN) QueueThaw(i, false);
        else QueueFreeze(i);
        return;
    }

    if (running >= 0 && frozen >= 0) {
        UiCommand c;
        c.kind = CMD_SWAP;
        c.slotA = running;
        c.slotB = frozen;
        c.pidA = g_slots[running].pid;
        c.pidB = g_slots[frozen].pid;
        c.treeA = SlotTree(g_slots[running]);
        PostCommand(c);
        g_slots[running].state = SLOT_BUSY;
        g_slots[running].busyAction = "Swapping...";
        g_slots[running].lastError.clear();
        g_slots[frozen].state = SLOT_BUSY;
        g_slots[frozen].busyAction = "Thawing...";
        g_slots[frozen].lastError.clear();
        g_statusLine = "Swapping applications...";
        g_dirty = true;
        return;
    }

    if (running >= 0 && frozen < 0) {
        int toFreeze = running;
        QueueFreeze(toFreeze);
        g_statusLine = "Froze the running selection";
        return;
    }
    g_statusLine = "Nothing to swap: every assigned selection is already frozen.";
    g_dirty = true;
}

void QueuePanic()
{
    for (size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].state == SLOT_BUSY) {
            g_statusLine = "Another operation is still running.";
            g_dirty = true;
            return;
        }
    }
    UiCommand c;
    c.kind = CMD_PANIC;
    PostCommand(c);
    for (size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].state == SLOT_FROZEN) {
            g_slots[i].state = SLOT_BUSY;
            g_slots[i].busyAction = "Thawing all...";
        }
    }
    g_statusLine = "Emergency thaw: unwinding every frozen tree...";
    g_dirty = true;
}

void QueuePagefileApply()
{
    if (g_pfApplying) return;
    if (g_pfDrive < 0 || (size_t)g_pfDrive >= g_pf.drives.size()) {
        g_statusLine = "Select a fixed drive before applying a pagefile.";
        g_dirty = true;
        return;
    }
    UiCommand c;
    c.kind = CMD_PAGEFILE;
    c.pfStatus = g_pf;
    c.pfDrive = g_pfDrive;
    c.pfInitialMb = g_pfInitialMb;
    c.pfMaxMb = g_pfMaxMb;
    c.pfSystemManaged = g_pfSystemManaged;
    PostCommand(c);
    g_pfApplying = true;
    g_pfResultValid = false;
    g_statusLine = "Applying pagefile configuration...";
    g_dirty = true;
}

void DrainResults()
{
    for (;;) {
        UiResult res;
        {
            EnterCriticalSection(&g_resLock);
            if (g_resQ.empty()) { LeaveCriticalSection(&g_resLock); break; }
            res = g_resQ.front();
            g_resQ.erase(g_resQ.begin());
            LeaveCriticalSection(&g_resLock);
        }
        g_dirty = true;
        switch (res.kind) {
        case CMD_FREEZE:
            if (res.slotA >= 0 && (size_t)res.slotA < g_slots.size()) {
                Slot& s = g_slots[res.slotA];
                s.busyAction.clear();
                if (res.ok) {
                    ApplyFreezeOk((size_t)res.slotA, res.freeze);
                    char buf[256];
                    sprintf_s(buf, sizeof(buf), "Froze %s", ToUtf8(SlotDisplayName(s)).c_str());
                    g_statusLine = buf;
                } else {
                    s.state = SLOT_RUNNING;
                    s.lastError = res.error;
                    g_statusLine = "Freeze failed: " + res.error;
                }
            }
            break;
        case CMD_THAW:
            if (res.slotA >= 0 && (size_t)res.slotA < g_slots.size()) {
                Slot& s = g_slots[res.slotA];
                s.busyAction.clear();
                if (res.ok) {
                    std::string who = ToUtf8(SlotDisplayName(s));
                    ApplyThawOk((size_t)res.slotA);
                    g_statusLine = "Thawed " + who;
                    if (res.clearOnSuccess) {
                        g_slots.erase(g_slots.begin() + res.slotA);
                        g_statusLine = "Thawed and removed " + who;
                    }
                } else {
                    s.state = SLOT_FROZEN;
                    s.lastError = res.error;
                    g_statusLine = "Thaw failed: " + res.error;
                }
            }
            break;
        case CMD_SWAP:
            if (res.slotA >= 0 && (size_t)res.slotA < g_slots.size()) g_slots[res.slotA].busyAction.clear();
            if (res.slotB >= 0 && (size_t)res.slotB < g_slots.size()) g_slots[res.slotB].busyAction.clear();
            if (!res.ok) {
                if (res.slotA >= 0 && (size_t)res.slotA < g_slots.size()) {
                    g_slots[res.slotA].state = SLOT_RUNNING;
                    g_slots[res.slotA].lastError = res.error;
                }
                if (res.slotB >= 0 && (size_t)res.slotB < g_slots.size())
                    g_slots[res.slotB].state = SLOT_FROZEN;
                g_statusLine = "Swap failed: " + res.error;
                break;
            }
            if (res.slotA >= 0 && (size_t)res.slotA < g_slots.size())
                ApplyFreezeOk((size_t)res.slotA, res.freeze);
            if (res.swapContents && res.slotB >= 0 && (size_t)res.slotB < g_slots.size()) {
                ApplyThawOk((size_t)res.slotB);
                std::swap(g_slots[res.slotA], g_slots[res.slotB]);
            }
            g_statusLine = "Application swap complete";
            break;
        case CMD_PANIC:
            for (size_t i = 0; i < g_slots.size(); ++i) {
                if (g_slots[i].state != SLOT_BUSY) continue;
                g_slots[i].busyAction.clear();
                StateFileInfo info;
                if (ReadStateFile(g_slots[i].pid, info)) {
                    g_slots[i].state = SLOT_FROZEN;
                } else {
                    g_slots[i].state = SLOT_RUNNING;
                    g_slots[i].baselineWs = 0;
                    g_slots[i].treePids.clear();
                }
            }
            {
                char buf[192];
                sprintf_s(buf, sizeof(buf),
                          "Emergency thaw complete: %d tree(s) restored, %d orphan(s) resumed",
                          res.thawedRoots, res.orphanRoots);
                g_statusLine = buf;
            }
            break;
        case CMD_PAGEFILE:
            g_pfApplying = false;
            if (res.pfRan) {
                g_pfResultValid = true;
                g_pfResultOk = res.pf.ok;
                g_pfResultReboot = res.pf.rebootRequired;
                g_pfResultMessage = ToUtf8(res.pf.message);
                g_statusLine = res.pf.ok ? "Pagefile configuration saved." : "Pagefile update failed.";
                PollTelemetry(g_tel);
                g_telValid = true;
                PagefileStatus fresh;
                if (ScanPagefileDrives(fresh)) {
                    g_pf = fresh;
                    g_pfScanned = true;
                    g_lastPfScanMs = GetTickCount64();
                }
            }
            break;
        default:
            break;
        }
    }
}

void EnsureAppRows(bool force)
{
    ULONGLONG now = GetTickCount64();
    if (!force && g_appRowsMs != 0 && now - g_appRowsMs < 2000) return;
    g_appRowsMs = now;

    std::vector<GuardComp> guards;
    EnterCriticalSection(&g_resLock);
    guards = g_guards;
    LeaveCriticalSection(&g_resLock);

    std::vector<PickRow> tmp;
    if (!SnapshotApps(tmp, guards)) return;
    for (size_t i = 0; i < tmp.size(); ++i) {
        std::wstring image = tmp[i].image.empty() ? tmp[i].exe : tmp[i].image;
        tmp[i].iconSrv = GetOrLoadAppIconSrv(image);
    }
    g_appRows.swap(tmp);
}

const PickRow* FindRowByRoot(unsigned long long rootPid)
{
    for (size_t i = 0; i < g_appRows.size(); ++i) {
        if (g_appRows[i].rootPid == rootPid) return &g_appRows[i];
    }
    return nullptr;
}

const PickRow* FindRowForWindow(HWND hwnd, unsigned long long pid)
{
    for (size_t i = 0; i < g_appRows.size(); ++i) {
        const PickRow& r = g_appRows[i];
        for (size_t k = 0; k < r.windows.size(); ++k) {
            if ((hwnd && r.windows[k].hwnd == hwnd) || r.windows[k].pid == pid) return &r;
        }
    }
    return nullptr;
}

DWORD ForegroundPid()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid;
}

void CheckAutoThaw()
{
    if (!g_cfg.watchForegroundWake) return;
    DWORD fg = ForegroundPid();
    if (fg == 0 || fg == GetCurrentProcessId()) return;
    for (size_t i = 0; i < g_slots.size(); ++i) {
        Slot& s = g_slots[i];
        if (s.state != SLOT_FROZEN || !SlotAutoThaw(s) || s.pid == 0) continue;
        bool member = false;
        for (size_t k = 0; k < s.treePids.size(); ++k) {
            if (s.treePids[k] == fg) { member = true; break; }
        }
        if (!member) member = IsTreeMember(s.pid, fg);
        if (!member) continue;
        LogUi("auto-thaw: foreground pid=%lu belongs to slot %zu", fg, i);
        QueueThaw((int)i, false);
        return;
    }
}

void StartupRecovery()
{
    int swept = CleanupStaleFrozenStateFiles();
    if (swept > 0) LogUi("startup cleanup: %d stale state file(s) removed", swept);

    std::vector<unsigned long long> pids;
    std::vector<StateWinRec> wins;
    std::vector<StateFileInfo> infos;
    if (!ReadStateDirFrozen(pids, wins, &infos)) return;
    int placed = 0;
    for (size_t i = 0; i < infos.size(); ++i) {
        int slot = -1;
        for (size_t k = 0; k < g_slots.size(); ++k) {
            if (g_slots[k].state == SLOT_EMPTY) { slot = (int)k; break; }
        }
        if (slot < 0) {
            slot = (int)g_slots.size();
            g_slots.push_back(Slot());
        }
        Slot& s = g_slots[(size_t)slot];
        s.state = SLOT_FROZEN;
        s.pid = infos[i].rootPid;
        s.createTime = 0;
        for (size_t k = 0; k < infos[i].procs.size(); ++k) {
            if (infos[i].procs[k].pid == infos[i].rootPid) {
                s.createTime = infos[i].procs[k].createTime;
                break;
            }
        }
        s.image = infos[i].image;
        s.baselineWs = infos[i].wsBeforeSum;
        s.procCount = (int)infos[i].procs.size();
        s.treePids.clear();
        for (size_t k = 0; k < infos[i].procs.size(); ++k) s.treePids.push_back(infos[i].procs[k].pid);
        for (size_t k = 0; k < infos[i].procs.size(); ++k) {
            if (!infos[i].procs[k].exe.empty()) { s.exe = infos[i].procs[k].exe; break; }
        }
        s.friendlyName = FriendlyExeName(s.image);
        if (s.friendlyName.empty()) s.friendlyName = s.exe;
        s.windowTitle = PrimaryWindowTitleForPid(s.pid);
        s.fromRecovery = true;
        s.windowCount = (int)infos[i].wins.size();
        RefreshSlotLive((size_t)slot);
        ++placed;
    }
    if (placed > 0) {
        char buf[160];
        sprintf_s(buf, sizeof(buf),
                  "%d application tree(s) still frozen from a prior session were recovered.",
                  placed);
        g_recoveryNote = buf;
        LogUi("startup recovery: %d frozen tree(s) found on disk", placed);
    }
}

void EnsureAppRows(bool force);
void TrayUpdate();

bool SlotTree(const Slot& s);
bool SlotAutoThaw(const Slot& s);
void RefreshSlotLive(size_t i);

void PromoteOrDropDeadSlot(size_t i)
{
    if (i >= g_slots.size()) return;
    Slot& s = g_slots[i];
    unsigned long long oldPid = s.pid;

    if (s.state == SLOT_FROZEN) {
        LogUi("slot %zu root pid=%llu exited; sweeping orphaned members", i, oldPid);
        SweepThawStateFilesInProcess(std::vector<unsigned long long>(1, oldPid));
        StateFileInfo info;
        if (ReadStateFile(oldPid, info)) {
            bool anyAlive = false;
            for (size_t k = 0; k < info.procs.size(); ++k) {
                if (IsProcessInstanceAlive(info.procs[k].pid, info.procs[k].createTime)) {
                    anyAlive = true;
                    break;
                }
            }
            if (!anyAlive) DeleteFileW(info.path.c_str());
        }
    }

    EnsureAppRows(true);
    const PickRow* promoted = nullptr;
    for (size_t k = 0; k < g_appRows.size(); ++k) {
        const PickRow& row = g_appRows[k];
        bool member = false;
        if (row.rootPid == oldPid) member = true;
        if (!member) {
            for (size_t m = 0; m < s.treePids.size(); ++m) {
                if (s.treePids[m] != row.rootPid) continue;
                member = true;
                break;
            }
        }
        if (!member) continue;
        if (!IsProcessInstanceAlive(row.rootPid, row.createTime)) continue;
        if (IsCoreSystemProcess(row.rootPid)) continue;
        if (!promoted || row.treeWs > promoted->treeWs) promoted = &row;
    }

    if (promoted) {
        bool tree = SlotTree(s);
        bool autoThaw = SlotAutoThaw(s);
        bool treeOverride = s.treeOverride;
        bool autoThawOverride = s.autoThawOverride;
        Slot fresh;
        fresh.state = SLOT_RUNNING;
        fresh.pid = promoted->rootPid;
        fresh.createTime = promoted->createTime ? promoted->createTime
                                                : ProcessCreateTimeUi(promoted->rootPid);
        fresh.exe = promoted->exe;
        fresh.image = promoted->image;
        fresh.friendlyName = promoted->friendlyName.empty()
            ? FriendlyExeName(promoted->image) : promoted->friendlyName;
        fresh.windowTitle = promoted->primaryWindowTitle;
        fresh.tree = tree;
        fresh.autoThaw = autoThaw;
        fresh.treeOverride = treeOverride;
        fresh.autoThawOverride = autoThawOverride;
        fresh.protectedHit = promoted->protectedHit;
        fresh.protectedBy = promoted->protectedBy;
        fresh.windowCount = (int)promoted->windows.size();
        g_slots[i] = fresh;
        RefreshSlotLive(i);
        char buf[256];
        sprintf_s(buf, sizeof(buf), "Slot %c promoted surviving child pid %llu",
                  'A' + (char)(i % 26), g_slots[i].pid);
        g_statusLine = buf;
        LogUi("%s", buf);
        g_dirty = true;
        return;
    }

    g_statusLine = "Removed a ghost card: its process instance exited.";
    LogUi("slot %zu removed: pid=%llu exited with no surviving members", i, oldPid);
    g_slots.erase(g_slots.begin() + i);
    g_dirty = true;
}

void TickSlotsAndTelemetry()
{
    PollTelemetry(g_tel);
    g_telValid = true;

    for (size_t i = 0; i < g_slots.size();) {
        Slot& s = g_slots[i];
        if (s.state == SLOT_EMPTY || s.state == SLOT_BUSY || s.pid == 0) {
            ++i;
            continue;
        }
        if (s.createTime == 0) s.createTime = ProcessCreateTimeUi(s.pid);
        if (!IsProcessInstanceAlive(s.pid, s.createTime)) {
            size_t before = g_slots.size();
            PromoteOrDropDeadSlot(i);
            if (g_slots.size() < before) continue;
            ++i;
            continue;
        }
        if (s.state == SLOT_FROZEN && s.baselineWs == 0) {
            unsigned long long sum = 0;
            for (size_t k = 0; k < s.treePids.size(); ++k) {
                unsigned long long bytes = 0;
                if (ProcessPrivateWorkingSetUi(s.treePids[k], bytes)) sum += bytes;
            }
            s.baselineWs = sum;
        }
        RefreshSlotLive(i);
        ++i;
    }
    TrayUpdate();
}

std::wstring TrayTipText()
{
    int frozen = 0;
    unsigned long long freed = 0;
    for (size_t i = 0; i < g_slots.size(); ++i) {
        const Slot& s = g_slots[i];
        if (s.state != SLOT_FROZEN) continue;
        ++frozen;
        if (s.baselineWs > s.wsNow) freed += s.baselineWs - s.wsNow;
    }
    wchar_t buf[128];
    if (frozen > 0) {
        swprintf_s(buf, L"RamStasis \u2014 %d App(s) in Stasis (+%.2f GB Freed)",
                   frozen, (double)freed / 1073741824.0);
    } else {
        wcscpy_s(buf, L"RamStasis \u2014 Process & RAM Stasis Manager");
    }
    return buf;
}

void TrayUpdate()
{
    if (!g_trayAdded || !g_hwnd) return;
    std::wstring tip = TrayTipText();
    if (tip == g_trayTipLast) return;
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    g_trayTipLast = tip;
}

bool TrayAdd()
{
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = g_trayCbMsg;
    nid.hIcon = g_trayIcon ? g_trayIcon : LoadRamStasisAppIcon(g_hInst, 32);
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    std::wstring tip = TrayTipText();
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    NOTIFYICONDATAW v = nid;
    v.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &v);
    g_trayAdded = true;
    g_trayTipLast = tip;
    return true;
}

void TrayRemove()
{
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

void ForceForegroundWindow(HWND hwnd)
{
    if (!hwnd) return;
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD thisThread = GetCurrentThreadId();
    bool attached = fgThread != 0 && fgThread != thisThread;
    if (attached) AttachThreadInput(thisThread, fgThread, TRUE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    if (attached) AttachThreadInput(thisThread, fgThread, FALSE);
}

void ShowWindowFromTray()
{
    if (!g_hwnd) return;
    if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
    else ShowWindow(g_hwnd, SW_SHOW);
    SetWindowPos(g_hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    g_hidden = false;
    g_dirty = true;
    ForceForegroundWindow(g_hwnd);
    TrayUpdate();
    LogUi("window restored from tray");
}

void ExitPickMode(bool);

void HideToTray()
{
    if (!g_hwnd) return;
    if (g_pickMode) ExitPickMode(true);
    ShowWindow(g_hwnd, SW_HIDE);
    g_hidden = true;
    g_dirty = true;
    LogUi("hidden to tray");
}

void ThawEverythingForExit();

void TrayMenu(const POINT* at = nullptr)
{
    int frozen = 0;
    for (size_t i = 0; i < g_slots.size(); ++i)
        if (g_slots[i].state == SLOT_FROZEN) ++frozen;

    wchar_t status[128];
    swprintf_s(status, L"RamStasis \u2014 %d app(s) in stasis", frozen);
    std::wstring swapLabel = L"Swap Applications\t" + HotkeyLabel();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_GRAYED, 90, status);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 102, g_hidden ? L"Show RamStasis" : L"Hide RamStasis");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 100, swapLabel.c_str());
    AppendMenuW(m, MF_STRING, 101, L"Thaw All");
    AppendMenuW(m, MF_STRING, 104, L"Settings && Pagefile");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 103, L"Exit RamStasis");
    POINT pt;
    if (at) pt = *at;
    else GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    UINT cmd = (UINT)TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                    pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    switch (cmd) {
    case 100: PostMessageW(g_hwnd, WM_HOTKEY, (WPARAM)kHotkeyId, 0); break;
    case 101: QueuePanic(); break;
    case 102:
        if (g_hidden || !IsWindowVisible(g_hwnd)) ShowWindowFromTray();
        else HideToTray();
        break;
    case 103:

        ThawEverythingForExit();
        g_forceQuit = true;
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        break;
    case 104:
        ShowWindowFromTray();
        g_page = PAGE_SETTINGS;
        break;
    default: break;
    }
}

void ThawEverythingForExit()
{
    std::vector<unsigned long long> roots;
    for (size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].state == SLOT_FROZEN && g_slots[i].pid != 0 &&
            std::find(roots.begin(), roots.end(), g_slots[i].pid) == roots.end())
            roots.push_back(g_slots[i].pid);
    }
    std::vector<unsigned long long> statePids;
    std::vector<StateWinRec> wins;
    ReadStateDirFrozen(statePids, wins, nullptr);
    for (size_t i = 0; i < statePids.size(); ++i) {
        if (std::find(roots.begin(), roots.end(), statePids[i]) == roots.end())
            roots.push_back(statePids[i]);
    }

    bool spawnFailed = false;
    std::vector<unsigned long long> remaining;
    for (size_t i = 0; i < roots.size(); ++i) {
        if (spawnFailed) {
            remaining.push_back(roots[i]);
            continue;
        }
        std::wstring args = L"thaw " + std::to_wstring(roots[i]) + L" --json";
        EngineResult r = RunEngine(args.c_str());
        LogUi("shutdown thaw pid=%llu exit=%ld ran=%d", roots[i], r.exitCode, (int)r.ran);
        if (!r.ran) {
            spawnFailed = true;
            remaining.push_back(roots[i]);
        }
    }
    if (!remaining.empty()) {
        LogUi("shutdown fallback: spawning engine child failed; sweeping %u state file(s) in-process",
              (unsigned)remaining.size());
        SweepThawStateFilesInProcess(remaining);
    }
}

void ExitPickMode(bool)
{
    if (g_pickMode) {
        if (GetCapture() == g_hwnd) ReleaseCapture();
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
    }
    g_pickMode = false;
    g_pickArmed = false;
    g_pickSlot = -1;
    HudShow(false);
    g_dirty = true;
}

void UpdatePickHud()
{
    POINT pt;
    GetCursorPos(&pt);
    HWND under = WindowFromPoint(pt);
    HWND rootWnd = under ? GetAncestor(under, GA_ROOT) : nullptr;
    DWORD pid = 0;
    if (rootWnd) GetWindowThreadProcessId(rootWnd, &pid);

    if (!rootWnd || pid == 0 || pid == GetCurrentProcessId()) {
        HudShow(false);
        return;
    }

    ULONGLONG now = GetTickCount64();
    if (now - g_hudLastMs >= 120) {
        g_hudLastMs = now;
        wchar_t title[512] = L"";
        GetWindowTextW(rootWnd, title, (int)(sizeof(title) / sizeof(title[0])));
        const PickRow* row = FindRowForWindow(rootWnd, pid);
        unsigned long long rootPid = row ? row->rootPid : ResolveAppRootPid(pid);
        if (!row && rootPid != 0) row = FindRowByRoot(rootPid);

        std::wstring app;
        std::wstring meta;
        bool prot = false;
        if (row) {
            app = row->friendlyName.empty() ? row->exe : row->friendlyName;
            if (!row->exe.empty() && _wcsicmp(app.c_str(), row->exe.c_str()) != 0)
                app += L"  (" + row->exe + L")";
            wchar_t mb[320];
            char ws[64], cm[64];
            FormatMb(row->treeWs, ws, sizeof(ws));
            FormatMb(row->treeCommit, cm, sizeof(cm));
            swprintf_s(mb, L"ROOT %llu  •  %u process(es)  •  RAM %hs  •  Commit %hs",
                       row->rootPid, (unsigned)(row->childProcs.size() + 1), ws, cm);
            meta = mb;
            prot = row->protectedHit && g_cfg.guardAntiCheat;
        } else {
            wchar_t exeFallback[64];
            swprintf_s(exeFallback, L"PID %lu", (unsigned long)pid);
            app = exeFallback;
            wchar_t mb[256];
            swprintf_s(mb, L"ROOT %llu  •  not in current app snapshot", rootPid);
            meta = mb;
        }
        std::wstring hint = prot ? L"Protected by anti-cheat guard — click refused"
                                 : L"Click to assign  •  Esc to cancel";
        HudSetText(title, app, meta, hint, prot);
    }
    HudPlaceAt(pt);
    HudShow(true);
}

void PickWindowAtCursor()
{
    POINT pt;
    GetCursorPos(&pt);
    HWND under = WindowFromPoint(pt);
    HWND rootWnd = under ? GetAncestor(under, GA_ROOT) : nullptr;
    DWORD pid = 0;
    if (rootWnd) GetWindowThreadProcessId(rootWnd, &pid);
    if (!rootWnd || pid == 0 || pid == GetCurrentProcessId()) return;

    EnsureAppRows(true);
    unsigned long long rootPid = ResolveAppRootPid(pid);
    const PickRow* row = rootPid ? FindRowByRoot(rootPid) : nullptr;
    if (!row) row = FindRowForWindow(rootWnd, pid);
    if (rootPid == 0 && row) rootPid = row->rootPid;

    if (rootPid == 0 || rootPid == 4 || IsCoreSystemProcess(rootPid)) {
        g_statusLine = "Refused: that window belongs to a system/shell process.";
        ExitPickMode(true);
        return;
    }
    if (!row) {
        g_statusLine = "Refused: could not inspect the target process.";
        ExitPickMode(true);
        return;
    }
    if (row->protectedHit && g_cfg.guardAntiCheat) {
        char buf[256];
        sprintf_s(buf, sizeof(buf), "Refused: %s is protected by the anti-cheat guard.",
                  ToUtf8(row->friendlyName.empty() ? row->exe : row->friendlyName).c_str());
        g_statusLine = buf;
        ExitPickMode(true);
        return;
    }

    int target = g_pickSlot;
    AssignRowToSlot(target, *row);
    ExitPickMode(false);
}

void EnterPickMode(int slot)
{
    g_pickSlot = slot;
    g_pickMode = true;
    g_pickArmed = false;
    SetCapture(g_hwnd);
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32515)));
    g_hudLastMs = 0;
    g_statusLine = "Pick mode: left-click a target window (Esc cancels)";
    HudShow(true);
    g_dirty = true;
}

void UpdatePickMode()
{
    if (!g_pickMode || g_hidden) return;
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
        g_statusLine = "Pick cancelled";
        ExitPickMode(true);
        return;
    }
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        g_pickArmed = true;
    } else if (g_pickArmed) {
        PickWindowAtCursor();
        return;
    }
    UpdatePickHud();
}

void RequestClose()
{
    if (!g_forceQuit && g_cfg.minimizeToTrayOnClose) {
        HideToTray();
        return;
    }
    int frozen = 0;
    for (size_t i = 0; i < g_slots.size(); ++i)
        if (g_slots[i].state == SLOT_FROZEN) ++frozen;
    if (frozen > 0 && !g_forceQuit) {
        g_frozenCountModal = frozen;
        g_exitModal = true;
        g_dirty = true;
        return;
    }
    g_quit = true;
    PostQuitMessage(0);
}

void RequestSwapFromHotkey()
{
    if (g_hidden) ShowWindowFromTray();
    QueueSwap();
}

}

namespace {

ImFont* g_fontBody = nullptr;
ImFont* g_fontBold = nullptr;
ImFont* g_fontHeader = nullptr;
ImFont* g_fontSmall = nullptr;

void PushBold()   { if (g_fontBold) ImGui::PushFont(g_fontBold, g_fontBold->LegacySize); }
void PopBold()    { if (g_fontBold) ImGui::PopFont(); }
void PushHeader() { if (g_fontHeader) ImGui::PushFont(g_fontHeader, g_fontHeader->LegacySize); }
void PopHeader()  { if (g_fontHeader) ImGui::PopFont(); }
void PushSmall()  { if (g_fontSmall) ImGui::PushFont(g_fontSmall, g_fontSmall->LegacySize); }
void PopSmall()   { if (g_fontSmall) ImGui::PopFont(); }

void ApplyUiStyle()
{
    ImGuiStyle& st = ImGui::GetStyle();
    st.FontSizeBase = 16.0f;
    st.FontScaleDpi = g_dpiScale;
    st.FontScaleMain = 1.0f;
    st.WindowPadding = ImVec2(Dpi(0), Dpi(0));
    st.FramePadding = ImVec2(Dpi(12), Dpi(7));
    st.CellPadding = ImVec2(Dpi(8), Dpi(6));
    st.ItemSpacing = ImVec2(Dpi(10), Dpi(8));
    st.ItemInnerSpacing = ImVec2(Dpi(8), Dpi(6));
    st.TouchExtraPadding = ImVec2(0, 0);
    st.IndentSpacing = Dpi(18);
    st.ScrollbarSize = Dpi(10);
    st.GrabMinSize = Dpi(10);
    st.WindowBorderSize = Dpi(1);
    st.ChildBorderSize = Dpi(1);
    st.PopupBorderSize = Dpi(1);
    st.FrameBorderSize = Dpi(1);
    st.WindowRounding = Dpi(8);
    st.ChildRounding = Dpi(8);
    st.FrameRounding = Dpi(6);
    st.PopupRounding = Dpi(8);
    st.ScrollbarRounding = Dpi(6);
    st.GrabRounding = Dpi(4);
    st.TabRounding = Dpi(6);
    st.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    st.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    st.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    st.DisabledAlpha = 0.42f;
    st.AntiAliasedLines = true;
    st.AntiAliasedLinesUseTex = true;
    st.AntiAliasedFill = true;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                  = kColText;
    c[ImGuiCol_TextDisabled]          = kColDim;
    c[ImGuiCol_WindowBg]              = kColBg;
    c[ImGuiCol_ChildBg]               = kColCard;
    c[ImGuiCol_PopupBg]               = ImVec4(0.0706f, 0.0863f, 0.1137f, 0.99f);
    c[ImGuiCol_Border]                = kColBorder;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = ImVec4(0.0706f, 0.0863f, 0.1098f, 1.0f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.1020f, 0.1255f, 0.1608f, 1.0f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.1333f, 0.1647f, 0.2118f, 1.0f);
    c[ImGuiCol_TitleBg]               = kColBg;
    c[ImGuiCol_TitleBgActive]         = kColBg;
    c[ImGuiCol_TitleBgCollapsed]      = kColBg;
    c[ImGuiCol_MenuBarBg]             = kColCard;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.2196f, 0.2549f, 0.3294f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.2902f, 0.3373f, 0.4314f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.3608f, 0.4196f, 0.5294f, 1.0f);
    c[ImGuiCol_CheckMark]             = kColEmerald;
    c[ImGuiCol_SliderGrab]            = kColBlue;
    c[ImGuiCol_SliderGrabActive]      = kColCyan;
    c[ImGuiCol_Button]                = ImVec4(0.1020f, 0.1255f, 0.1608f, 1.0f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.1490f, 0.1843f, 0.2353f, 1.0f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.1922f, 0.2353f, 0.2980f, 1.0f);
    c[ImGuiCol_Header]                = ImVec4(0.1020f, 0.1255f, 0.1608f, 1.0f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.1490f, 0.1843f, 0.2353f, 1.0f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.1922f, 0.2353f, 0.2980f, 1.0f);
    c[ImGuiCol_Separator]             = ImVec4(0.1490f, 0.1804f, 0.2392f, 0.70f);
    c[ImGuiCol_SeparatorHovered]      = kColCyan;
    c[ImGuiCol_SeparatorActive]       = kColCyan;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = kColBorder;
    c[ImGuiCol_ResizeGripActive]      = kColCyan;
    c[ImGuiCol_TableHeaderBg]         = ImVec4(0.0706f, 0.0863f, 0.1098f, 1.0f);
    c[ImGuiCol_TableBorderStrong]     = kColBorder;
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.1490f, 0.1804f, 0.2392f, 0.45f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1, 1, 1, 0.018f);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(0.2314f, 0.5098f, 0.9647f, 0.35f);
    c[ImGuiCol_NavCursor]             = kColCyan;
    c[ImGuiCol_PlotHistogram]         = kColEmerald;
    c[ImGuiCol_PlotHistogramHovered]  = kColCyan;
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.01f, 0.015f, 0.02f, 0.72f);
}

std::string Ellipsize(const std::string& text, float maxW)
{
    if (text.empty()) return text;
    if (ImGui::CalcTextSize(text.c_str()).x <= maxW) return text;
    const char* dots = "...";
    float dotsW = ImGui::CalcTextSize(dots).x;
    std::string out = text;
    while (!out.empty() && ImGui::CalcTextSize(out.c_str()).x + dotsW > maxW) {
        out.pop_back();
        while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80) out.pop_back();
    }
    out += dots;
    return out;
}

void DrawEllipsisText(ImDrawList* dl, ImVec2 pos, ImU32 color,
                      const std::string& text, float maxW)
{
    std::string shown = Ellipsize(text, maxW);
    dl->AddText(pos, color, shown.c_str());
}

float DrawPill(ImDrawList* dl, ImVec2 pos, const char* text, const ImVec4& color,
               float height = 0.0f, bool strong = false)
{
    if (height <= 0.0f) height = Dpi(24);
    float padX = Dpi(9);
    float tw = ImGui::CalcTextSize(text).x;
    float w = tw + padX * 2.0f;
    ImVec2 a = pos;
    ImVec2 b = ImVec2(pos.x + w, pos.y + height);
    ImU32 bg = ColAlpha(color, strong ? 0.22f : 0.10f);
    ImU32 bd = ColAlpha(color, strong ? 0.55f : 0.32f);
    dl->AddRectFilled(a, b, bg, height * 0.5f);
    dl->AddRect(a, b, bd, height * 0.5f, 0, Dpi(1));
    dl->AddText(ImVec2(a.x + padX, a.y + (height - ImGui::GetFontSize()) * 0.5f + Dpi(1)),
                Col(color), text);
    return w;
}

void DrawDashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col,
                    float dash, float gap, float thickness)
{
    float step = dash + gap;
    for (float x = a.x; x < b.x; x += step) {
        float x2 = x + dash;
        if (x2 > b.x) x2 = b.x;
        dl->AddLine(ImVec2(x, a.y), ImVec2(x2, a.y), col, thickness);
        dl->AddLine(ImVec2(x, b.y), ImVec2(x2, b.y), col, thickness);
    }
    for (float y = a.y; y < b.y; y += step) {
        float y2 = y + dash;
        if (y2 > b.y) y2 = b.y;
        dl->AddLine(ImVec2(a.x, y), ImVec2(a.x, y2), col, thickness);
        dl->AddLine(ImVec2(b.x, y), ImVec2(b.x, y2), col, thickness);
    }
}

void TextCentered(const char* text, float width, const ImVec4& color)
{
    float tw = ImGui::CalcTextSize(text).x;
    float x = (width - tw) * 0.5f;
    if (x < 0.0f) x = 0.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
    ImGui::TextColored(color, "%s", text);
}

enum BtnKind { BTN_PRIMARY = 0, BTN_SUCCESS, BTN_DANGER, BTN_GHOST, BTN_SUBTLE };

bool UiButton(const char* label, const ImVec2& size, BtnKind kind, bool enabled,
              const char* tooltip, const char* busyLabel = nullptr)
{
    bool busy = (busyLabel && busyLabel[0] != '\0');
    bool disabled = !enabled || busy;

    ImVec4 bg, hover, active, text, border;
    switch (kind) {
    case BTN_PRIMARY:
        bg = ImVec4(0.1098f, 0.2784f, 0.6078f, 1.0f);
        hover = ImVec4(0.1490f, 0.3608f, 0.7608f, 1.0f);
        active = ImVec4(0.1882f, 0.4353f, 0.8784f, 1.0f);
        text = ImVec4(0.9412f, 0.9647f, 1.0f, 1.0f);
        border = ImVec4(0.2314f, 0.5098f, 0.9647f, 0.55f);
        break;
    case BTN_SUCCESS:
        bg = ImVec4(0.0392f, 0.3529f, 0.2588f, 1.0f);
        hover = ImVec4(0.0510f, 0.4667f, 0.3373f, 1.0f);
        active = ImVec4(0.0627f, 0.5804f, 0.4196f, 1.0f);
        text = ImVec4(0.9412f, 1.0f, 0.9725f, 1.0f);
        border = ImVec4(0.0627f, 0.7255f, 0.5059f, 0.55f);
        break;
    case BTN_DANGER:
        bg = ImVec4(0.3922f, 0.1020f, 0.1020f, 1.0f);
        hover = ImVec4(0.5451f, 0.1333f, 0.1333f, 1.0f);
        active = ImVec4(0.7059f, 0.1647f, 0.1647f, 1.0f);
        text = ImVec4(1.0f, 0.9412f, 0.9412f, 1.0f);
        border = ImVec4(0.9373f, 0.2667f, 0.2667f, 0.55f);
        break;
    case BTN_GHOST:
        bg = ImVec4(0.0706f, 0.0863f, 0.1098f, 0.55f);
        hover = ImVec4(0.1176f, 0.1451f, 0.1882f, 1.0f);
        active = ImVec4(0.1569f, 0.1922f, 0.2471f, 1.0f);
        text = kColDim;
        border = ImVec4(0.1490f, 0.1804f, 0.2392f, 0.75f);
        break;
    case BTN_SUBTLE:
    default:
        bg = ImVec4(0.0902f, 0.1098f, 0.1412f, 1.0f);
        hover = ImVec4(0.1294f, 0.1569f, 0.2000f, 1.0f);
        active = ImVec4(0.1686f, 0.2039f, 0.2588f, 1.0f);
        text = kColText;
        border = kColBorder;
        break;
    }

    if (disabled) {

        bg = ImVec4(0.0706f, 0.0863f, 0.1098f, 1.0f);
        hover = bg;
        active = bg;
        text = ImVec4(kColDim.x, kColDim.y, kColDim.z, 0.60f);
        border = ImVec4(kColBorder.x, kColBorder.y, kColBorder.z, 0.60f);
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
        ImGui::BeginDisabled();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Dpi(1));
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    bool clicked = ImGui::Button(busy ? busyLabel : label, size);
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();
    if (disabled) {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (busy) ImGui::SetTooltip("Operation in progress. Please wait...");
        else if (tooltip && tooltip[0]) ImGui::SetTooltip("%s", tooltip);
    }
    return clicked && !disabled;
}

bool UiToggle(const char* id, const char* label, const char* desc, bool* value,
              bool enabled, const char* disabledTip)
{
    ImGui::PushID(id);
    float rowH = (desc && desc[0]) ? Dpi(46) : Dpi(32);
    float availW = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##row", ImVec2(availW > 0 ? availW : Dpi(100), rowH));
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float sw = Dpi(40);
    float sh = Dpi(22);
    float sy = p.y + (rowH - sh) * 0.5f;
    ImU32 trackOff = ColAlpha(kColBorder, 1.0f);
    ImU32 trackOn = ColAlpha(kColEmerald, enabled ? 0.92f : 0.40f);
    ImU32 track = *value ? trackOn : trackOff;
    if (!enabled) track = ColAlpha(kColBorder, 0.65f);
    ImVec2 ta = ImVec2(p.x, sy);
    ImVec2 tb = ImVec2(p.x + sw, sy + sh);
    dl->AddRectFilled(ta, tb, track, sh * 0.5f);
    if (hovered && enabled) dl->AddRect(ta, tb, ColAlpha(kColCyan, 0.65f), sh * 0.5f, 0, Dpi(1));
    float knobR = Dpi(8);
    float knobX = *value ? (tb.x - knobR - Dpi(3)) : (ta.x + knobR + Dpi(3));
    dl->AddCircleFilled(ImVec2(knobX, sy + sh * 0.5f), knobR,
                        enabled ? IM_COL32(245, 248, 255, 255) : IM_COL32(150, 158, 172, 180));

    ImU32 labelCol = enabled ? Col(kColText) : Col(kColFaint);
    dl->AddText(ImVec2(p.x + sw + Dpi(12), p.y + Dpi(2)), labelCol, label);
    if (desc && desc[0]) {
        dl->AddText(ImVec2(p.x + sw + Dpi(12), p.y + Dpi(23)), Col(kColDim), desc);
    }

    bool changed = false;
    if (clicked && enabled) { *value = !*value; changed = true; }
    if (hovered && !enabled && disabledTip) ImGui::SetTooltip("%s", disabledTip);
    ImGui::PopID();
    return changed;
}

bool GearButton(const char* id, const ImVec2& size, const char* tooltip)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##gear", size);
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c = ImVec2(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    ImU32 col = hovered ? Col(kColCyan) : Col(kColDim);
    if (hovered) dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                                   ColAlpha(kColBorder, 0.55f), Dpi(6));
    float r = Dpi(7);
    dl->AddCircle(c, r, col, 16, Dpi(1.5f));
    dl->AddCircleFilled(c, Dpi(3), col, 12);
    for (int i = 0; i < 8; ++i) {
        static const float dirs[8][2] = {
            { 1.0f, 0.0f }, { 0.7071f, 0.7071f }, { 0.0f, 1.0f }, { -0.7071f, 0.7071f },
            { -1.0f, 0.0f }, { -0.7071f, -0.7071f }, { 0.0f, -1.0f }, { 0.7071f, -0.7071f }
        };
        ImVec2 a = ImVec2(c.x + dirs[i][0] * r, c.y + dirs[i][1] * r);
        ImVec2 b = ImVec2(c.x + dirs[i][0] * (r + Dpi(2.5f)), c.y + dirs[i][1] * (r + Dpi(2.5f)));
        dl->AddLine(a, b, col, Dpi(2));
    }
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked;
}

bool DrawLogoImage(ImVec2 p, float s)
{
    if (!g_logoTexture) return false;
    ImGui::SetCursorScreenPos(p);
    ImGui::Image(ImTextureRef((ImTextureID)(UINT_PTR)g_logoTexture), ImVec2(s, s));
    return true;
}

void DrawLogo(ImDrawList* dl, ImVec2 p, float s)
{
    ImVec2 c = ImVec2(p.x + s * 0.5f, p.y + s * 0.5f);
    float r = s * 0.46f;
    dl->AddNgonFilled(c, r, Col(ImVec4(0.0275f, 0.0627f, 0.1098f, 1.0f)), 6);
    dl->AddNgon(c, r, Col(kColCyan), 6, Dpi(1.6f));
    float chip = s * 0.40f;
    ImVec2 a = ImVec2(c.x - chip * 0.5f, c.y - chip * 0.5f);
    ImVec2 b = ImVec2(c.x + chip * 0.5f, c.y + chip * 0.5f);
    dl->AddRectFilled(a, b, Col(ImVec4(0.0667f, 0.1098f, 0.1686f, 1.0f)), Dpi(3));
    dl->AddRect(a, b, Col(kColBlue), Dpi(3), 0, Dpi(1.1f));
    float barW = s * 0.085f;
    float barH = s * 0.24f;
    float gap = s * 0.065f;
    ImU32 core = Col(kColEmerald);
    dl->AddRectFilled(ImVec2(c.x - gap * 0.5f - barW, c.y - barH * 0.5f),
                      ImVec2(c.x - gap * 0.5f, c.y + barH * 0.5f), core, Dpi(1.5f));
    dl->AddRectFilled(ImVec2(c.x + gap * 0.5f, c.y - barH * 0.5f),
                      ImVec2(c.x + gap * 0.5f + barW, c.y + barH * 0.5f), core, Dpi(1.5f));
}

void DrawGauge(ImDrawList* dl, ImVec2 p, float w, const char* label, double frac,
               const char* value, const ImVec4& color)
{
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    dl->AddText(p, Col(kColFaint), label);
    float vw = ImGui::CalcTextSize(value).x;
    dl->AddText(ImVec2(p.x + w - vw, p.y), Col(color), value);
    ImVec2 a = ImVec2(p.x, p.y + Dpi(14));
    ImVec2 b = ImVec2(p.x + w, p.y + Dpi(18));
    dl->AddRectFilled(a, b, ColAlpha(kColBorder, 0.55f), Dpi(2));
    if (frac > 0.0) {
        ImVec2 f = ImVec2(a.x + (b.x - a.x) * (float)frac, b.y);
        dl->AddRectFilled(a, f, Col(color), Dpi(2));
    }
}

void DrawWorkspacePage(float w, float h);
void DrawExplorerPage(float w, float h);
void DrawSettingsPage(float w, float h);
void DrawExitModal();
void DrawSlotSettingsPopup();

int g_slotSettingsId = -1;
int g_slotSettingsToOpen = -1;

const float kHeaderH = 92.0f;
const float kNavH = 46.0f;
const float kFooterH = 32.0f;

void SlotTotals(int& frozen, int& active, unsigned long long& freed)
{
    frozen = 0;
    active = 0;
    freed = 0;
    for (size_t i = 0; i < g_slots.size(); ++i) {
        const Slot& s = g_slots[i];
        if (s.state == SLOT_FROZEN) {
            ++frozen;
            if (s.baselineWs > s.wsNow) freed += s.baselineWs - s.wsNow;
        } else if (s.state == SLOT_RUNNING) {
            ++active;
        }
    }
}

void ToggleMaximize()
{
    if (!g_hwnd) return;
    ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    g_dirty = true;
}

bool WindowControlButton(const char* id, int kind, const ImVec2& size, const char* tooltip)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##wc", size);
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (hovered) {
        ImU32 bg = (kind == 2) ? IM_COL32(196, 43, 43, 235) : ColAlpha(kColBorder, 0.75f);
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, Dpi(5));
    }
    ImU32 col = hovered ? IM_COL32(255, 255, 255, 245) : Col(kColDim);
    ImVec2 c = ImVec2(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    float s = Dpi(5);
    if (kind == 0) {
        dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, Dpi(1.4f));
    } else if (kind == 1) {
        if (IsZoomed(g_hwnd)) {
            dl->AddRect(ImVec2(c.x - s, c.y - s + Dpi(1.5f)),
                        ImVec2(c.x + s - Dpi(1.5f), c.y + s), col, 0, 0, Dpi(1.2f));
            dl->AddRect(ImVec2(c.x - s + Dpi(1.5f), c.y - s),
                        ImVec2(c.x + s, c.y + s - Dpi(1.5f)), col, 0, 0, Dpi(1.2f));
        } else {
            dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, 0, 0, Dpi(1.2f));
        }
    } else {
        dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, Dpi(1.4f));
        dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), col, Dpi(1.4f));
    }
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked;
}

void DrawHeader(float w)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float h = Dpi(kHeaderH);
    dl->AddRectFilled(wp, ImVec2(wp.x + w, wp.y + h), Col(kColBg));
    dl->AddLine(ImVec2(wp.x, wp.y + h - Dpi(1)), ImVec2(wp.x + w, wp.y + h - Dpi(1)),
                Col(kColBorder), Dpi(1));

    float logo = Dpi(40);
    ImVec2 logoPos = ImVec2(wp.x + Dpi(16), wp.y + Dpi(12));
    if (!DrawLogoImage(logoPos, logo)) DrawLogo(dl, logoPos, logo);

    PushHeader();
    dl->AddText(ImVec2(wp.x + Dpi(68), wp.y + Dpi(6)), Col(kColText), "RAMSTASIS");
    PopHeader();
    PushSmall();
    dl->AddText(ImVec2(wp.x + Dpi(68), wp.y + Dpi(38)), Col(kColCyan),
                "ZERO-CPU PAGEFILE PROCESS STASIS");
    PopSmall();

    float btnW = Dpi(34);
    float btnH = Dpi(28);
    float btnY = Dpi(12);
    float btnX = w - Dpi(16) - btnW * 3.0f - Dpi(4) * 2.0f;
    ImGui::SetCursorPos(ImVec2(btnX, btnY));
    if (WindowControlButton("min", 0, ImVec2(btnW, btnH), "Minimize to tray")) HideToTray();
    ImGui::SetCursorPos(ImVec2(btnX + btnW + Dpi(4), btnY));
    if (WindowControlButton("max", 1, ImVec2(btnW, btnH),
                            IsZoomed(g_hwnd) ? "Restore window" : "Maximize window"))
        ToggleMaximize();
    ImGui::SetCursorPos(ImVec2(btnX + (btnW + Dpi(4)) * 2.0f, btnY));
    if (WindowControlButton("close", 2, ImVec2(btnW, btnH),
                            g_cfg.minimizeToTrayOnClose ? "Minimize to tray" : "Close RamStasis"))
        RequestClose();

    dl->AddLine(ImVec2(wp.x + Dpi(16), wp.y + Dpi(61)),
                ImVec2(wp.x + w - Dpi(16), wp.y + Dpi(61)),
                ColAlpha(kColBorder, 0.55f), Dpi(1));

    int frozen = 0, active = 0;
    unsigned long long freed = 0;
    SlotTotals(frozen, active, freed);
    char badge[64];
    sprintf_s(badge, sizeof(badge), "%d IN STASIS", frozen);
    float pillY = Dpi(66);
    float pillH = Dpi(21);
    ImVec4 badgeCol = frozen > 0 ? kColCyan : kColFaint;
    float bx = wp.x + Dpi(16);
    DrawPill(dl, ImVec2(bx, wp.y + pillY), badge, badgeCol, pillH, frozen > 0);
    if (frozen > 0) {
        char freedText[64];
        char size[48];
        FormatMb(freed, size, sizeof(size));
        sprintf_s(freedText, sizeof(freedText), "%s FREED", size);
        bx += ImGui::CalcTextSize(badge).x + Dpi(18) + Dpi(8);
        DrawPill(dl, ImVec2(bx, wp.y + pillY), freedText, kColEmerald, pillH, false);
    }

    float gaugeW = Dpi(148);
    float gaugeGap = Dpi(16);
    float gx2 = w - Dpi(16) - gaugeW;
    float gx1 = gx2 - gaugeGap - gaugeW;
    float gaugeY = wp.y + Dpi(65);
    char ramVal[32] = "--";
    char comVal[32] = "--";
    double ramFrac = 0.0;
    double comFrac = 0.0;
    ImVec4 comCol = kColEmerald;
    if (g_telValid && g_tel.ramTotal > 0) {
        ramFrac = (double)g_tel.ramUsed / (double)g_tel.ramTotal;
        comFrac = g_tel.commitFrac;
        sprintf_s(ramVal, sizeof(ramVal), "%d%%", (int)(ramFrac * 100.0));
        sprintf_s(comVal, sizeof(comVal), "%d%%", (int)(comFrac * 100.0));
        if (comFrac >= 0.90) comCol = kColRed;
        else if (comFrac >= 0.75) comCol = kColAmber;
    }
    if (gx1 > bx + Dpi(12)) {
        PushSmall();
        DrawGauge(dl, ImVec2(wp.x + gx1, gaugeY), gaugeW, "PHYSICAL RAM", ramFrac,
                  ramVal, kColCyan);
        DrawGauge(dl, ImVec2(wp.x + gx2, gaugeY), gaugeW, "COMMIT", comFrac,
                  comVal, comCol);
        PopSmall();
    }

    float dragRight = btnX - Dpi(8);
    if (ImGui::IsMouseHoveringRect(wp, ImVec2(wp.x + dragRight, wp.y + Dpi(56))) &&
        !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ToggleMaximize();
        } else {
            ReleaseCapture();
            SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, (WPARAM)HTCAPTION, 0);
        }
    }
}

void DrawNav(float w)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float y = Dpi(kHeaderH);
    float h = Dpi(kNavH);
    dl->AddLine(ImVec2(wp.x, wp.y + y + h - Dpi(1)),
                ImVec2(wp.x + w, wp.y + y + h - Dpi(1)), Col(kColBorder), Dpi(1));

    static const char* kNames[PAGE_COUNT] = {
        "Workspace", "Explorer", "Settings"
    };
    static const char* kTips[PAGE_COUNT] = {
        "Manage stasis slots and swap parked applications",
        "Browse running apps grouped by their visible windows",
        "Global behaviour, hotkey and pagefile configuration"
    };

    float x = Dpi(16);
    for (int i = 0; i < PAGE_COUNT; ++i) {
        float tw = ImGui::CalcTextSize(kNames[i]).x + Dpi(28);
        ImVec2 p = ImVec2(wp.x + x, wp.y + y + Dpi(8));
        ImVec2 q = ImVec2(p.x + tw, p.y + Dpi(30));
        ImGui::SetCursorPos(ImVec2(x, y + Dpi(8)));
        ImGui::PushID(i);
        ImGui::InvisibleButton("##navtab", ImVec2(tw, Dpi(30)));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();
        ImGui::PopID();

        if (g_page == i) {
            dl->AddRectFilled(p, q, Col(kColCard2), Dpi(7));
            dl->AddRect(p, q, ColAlpha(kColBorder, 0.9f), Dpi(7), 0, Dpi(1));
            dl->AddRectFilled(ImVec2(p.x + Dpi(9), q.y - Dpi(2)),
                              ImVec2(q.x - Dpi(9), q.y), Col(kColCyan), Dpi(1));
        } else if (hovered) {
            dl->AddRectFilled(p, q, ColAlpha(kColCard, 0.85f), Dpi(7));
        }
        ImU32 textCol = (g_page == i) ? Col(kColText) : (hovered ? Col(kColText) : Col(kColDim));
        dl->AddText(ImVec2(p.x + Dpi(14), p.y + Dpi(6)), textCol, kNames[i]);
        if (hovered) ImGui::SetTooltip("%s", kTips[i]);
        if (clicked) { g_page = i; g_dirty = true; }
        x += tw + Dpi(6);
    }

    float pickW = Dpi(68);
    float thawW = Dpi(94);
    float swapW = Dpi(86);
    float gap = Dpi(7);
    float actionX = w - Dpi(16) - pickW - gap - thawW - gap - swapW;
    if (actionX > x + Dpi(8)) {
        ImGui::SetCursorPos(ImVec2(actionX, y + Dpi(9)));
        if (UiButton("Swap", ImVec2(swapW, Dpi(28)), BTN_PRIMARY, SwapPossible(),
                     "Freeze the running selection and restore the parked one")) {
            QueueSwap();
        }
        ImGui::SetCursorPos(ImVec2(actionX + swapW + gap, y + Dpi(9)));
        if (UiButton("Thaw All", ImVec2(thawW, Dpi(28)), BTN_DANGER, true,
                     "Sweep every state file and resume all suspended processes")) {
            QueuePanic();
        }
        ImGui::SetCursorPos(ImVec2(actionX + swapW + gap + thawW + gap, y + Dpi(9)));
        if (UiButton("Pick", ImVec2(pickW, Dpi(28)), BTN_GHOST, true,
                     "Crosshair-pick a running desktop window")) {
            EnterPickMode(-1);
        }
    }
}

float DrawCommitBanner(float w, float y)
{
    bool warn = false;
    int freePct = 0;
    if (g_telValid && g_tel.commitLimit > 0) {
        freePct = (int)((double)g_tel.freeCommit * 100.0 / (double)g_tel.commitLimit);
        if (g_cfg.warnOnLowCommit && freePct < g_cfg.lowCommitWarnPct) warn = true;
        if (g_tel.commitFrac >= 0.90) warn = true;
    }
    if (!warn) return 0.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float h = Dpi(44);
    ImVec2 a = ImVec2(wp.x + Dpi(16), wp.y + y);
    ImVec2 b = ImVec2(wp.x + w - Dpi(16), wp.y + y + h);
    dl->AddRectFilled(a, b, ColAlpha(kColAmber, 0.07f), Dpi(8));
    dl->AddRect(a, b, ColAlpha(kColAmber, 0.32f), Dpi(8), 0, Dpi(1));
    dl->AddRectFilled(ImVec2(a.x, a.y + Dpi(8)), ImVec2(a.x + Dpi(3), b.y - Dpi(8)),
                      ColAlpha(kColAmber, 0.9f), Dpi(2));

    float btnW = Dpi(190);
    ImGui::SetCursorPos(ImVec2(w - Dpi(30) - btnW, y + Dpi(7)));
    if (UiButton("Open Pagefile Manager", ImVec2(btnW, Dpi(30)), BTN_SUBTLE, true,
                 "Open Settings & Pagefile to expand the commit limit")) {
        g_page = PAGE_SETTINGS;
        g_dirty = true;
    }

    char head[160];
    if (g_telValid) {
        char freeMb[64];
        FormatMb(g_tel.freeCommit, freeMb, sizeof(freeMb));
        sprintf_s(head, sizeof(head),
                  "Commit headroom is low: %d%% free (%s of %.1f GB).",
                  freePct, freeMb, (double)g_tel.commitLimit / 1073741824.0);
    } else {
        sprintf_s(head, sizeof(head), "Commit headroom is low.");
    }
    PushBold();
    dl->AddText(ImVec2(a.x + Dpi(16), a.y + Dpi(5)), Col(kColAmber), head);
    PopBold();
    dl->AddText(ImVec2(a.x + Dpi(16), a.y + Dpi(24)), Col(kColDim),
                "Expanding the pagefile prevents allocation failures while frozen apps are parked.");
    return h + Dpi(8);
}

void DrawFooter(float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float barH = Dpi(kFooterH);
    float y = h - barH;
    dl->AddLine(ImVec2(wp.x, wp.y + y), ImVec2(wp.x + w, wp.y + y),
                Col(kColBorder), Dpi(1));

    std::string left = g_statusLine;
    ImVec4 leftCol = kColDim;
    if (left.empty() && !g_recoveryNote.empty()) {
        left = g_recoveryNote;
        leftCol = kColAmber;
    }

    char version[64];
    sprintf_s(version, sizeof(version), "RamStasis v%ls", kRamStasisVersion);
    const char* badge = g_isAdmin ? "ADMIN" : "STANDARD";
    ImVec4 badgeCol = g_isAdmin ? kColEmerald : kColFaint;
    float verW = ImGui::CalcTextSize(version).x;
    float badgeW = ImGui::CalcTextSize(badge).x + Dpi(18);
    float rightEdge = w - Dpi(16);
    float verX = rightEdge - verW;
    float badgeX = verX - Dpi(10) - badgeW;

    DrawPill(dl, ImVec2(wp.x + badgeX, wp.y + y + Dpi(7)), badge, badgeCol, Dpi(18), false);
    dl->AddText(ImVec2(wp.x + verX, wp.y + y + Dpi(7)), Col(kColFaint), version);

    std::string hk = "SWAP  " + ToUtf8(HotkeyLabel());
    float hkW = ImGui::CalcTextSize(hk.c_str()).x;
    float hkX = badgeX - Dpi(14) - hkW;
    if (hkX > Dpi(360)) {
        dl->AddText(ImVec2(wp.x + hkX, wp.y + y + Dpi(7)), Col(kColFaint), hk.c_str());
    }

    if (!left.empty()) {
        float maxW = (hkX > Dpi(360) ? hkX : badgeX) - Dpi(24);
        if (maxW < Dpi(120)) maxW = Dpi(120);
        float maxRight = wp.x + Dpi(16) + maxW;
        if (maxRight > wp.x + w - Dpi(16)) maxRight = wp.x + w - Dpi(16);
        dl->PushClipRect(ImVec2(wp.x + Dpi(16), wp.y + y),
                         ImVec2(maxRight, wp.y + h), true);
        dl->AddText(ImVec2(wp.x + Dpi(16), wp.y + y + Dpi(7)), Col(leftCol),
                    Ellipsize(left, maxW).c_str());
        dl->PopClipRect();
    }
}

bool BeginCard(const char* id, float height, bool border, bool dim)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Dpi(16), Dpi(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Dpi(8));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          dim ? ImVec4(0.0706f, 0.0863f, 0.1098f, 0.55f) : kColCard);
    ImGui::PushStyleColor(ImGuiCol_Border, kColBorder);
    ImGuiChildFlags flags = border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
    return ImGui::BeginChild(id, ImVec2(0, height), flags);
}

void EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

std::string SlotStatusText(const Slot& s)
{
    switch (s.state) {
    case SLOT_RUNNING: return "ACTIVE";
    case SLOT_FROZEN: return "FROZEN";
    case SLOT_BUSY: return "WORKING";
    default: return "EMPTY";
    }
}

ImVec4 SlotStatusColor(const Slot& s)
{
    switch (s.state) {
    case SLOT_RUNNING: return kColEmerald;
    case SLOT_FROZEN: return kColCyan;
    case SLOT_BUSY: return kColAmber;
    default: return kColFaint;
    }
}

void DrawBusyStripe(const ImVec2& a, const ImVec2& b, float time)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = b.x - a.x;
    float seg = width * 0.28f;
    float travel = (b.x - a.x + seg);
    float x = a.x - seg + (float)fmod(time * 120.0, (double)travel);
    ImVec2 c = ImVec2(x < a.x ? a.x : x, a.y);
    ImVec2 d = ImVec2(x + seg, b.y);
    if (d.x > b.x) d.x = b.x;
    if (c.x < b.x) dl->AddRectFilled(c, d, ColAlpha(kColAmber, 0.8f), Dpi(1));
}

void DrawSlotCard(size_t i, float w, float h)
{
    if (i >= g_slots.size()) return;
    Slot& s = g_slots[i];
    char letter = 'A' + (char)(i % 26);
    char id[48];
    sprintf_s(id, sizeof(id), "slot_%u", (unsigned)i);

    ImGui::PushID((int)i);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    bool border = (s.state != SLOT_EMPTY);
    BeginCard(id, h, border, s.state == SLOT_EMPTY);

    if (s.state == SLOT_EMPTY) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(ImVec2(p0.x + w * 0.5f, p0.y + Dpi(42)), Dpi(22),
                            ColAlpha(kColBorder, 0.55f));
        PushHeader();
        std::string plus = "+";
        float pw = ImGui::CalcTextSize(plus.c_str()).x;
        dl->AddText(ImVec2(p0.x + w * 0.5f - pw * 0.5f, p0.y + Dpi(27)),
                    Col(kColCyan), plus.c_str());
        PopHeader();

        char slotName[64];
        sprintf_s(slotName, sizeof(slotName), "Slot %c", letter);
        PushBold();
        float sw = ImGui::CalcTextSize(slotName).x;
        dl->AddText(ImVec2(p0.x + (w - sw) * 0.5f, p0.y + Dpi(76)), Col(kColText), slotName);
        PopBold();
        const char* hint = "Choose an application or pick a window.";
        float hw = ImGui::CalcTextSize(hint).x;
        dl->AddText(ImVec2(p0.x + (w - hw) * 0.5f, p0.y + Dpi(100)), Col(kColDim), hint);

        float bw = w - Dpi(32);
        ImGui::SetCursorPos(ImVec2(Dpi(16), h - Dpi(72)));
        if (UiButton("Choose Application", ImVec2(bw, Dpi(32)), BTN_PRIMARY, true,
                     "Open the Process & Window Explorer for this slot")) {
            g_pickSlot = (int)i;
            g_page = PAGE_EXPLORER;
            g_appFilter[0] = 0;
            g_dirty = true;
        }
        ImGui::SetCursorPos(ImVec2(Dpi(16), h - Dpi(36)));
        if (UiButton("Pick Window with Crosshair", ImVec2(bw, Dpi(28)), BTN_GHOST, true,
                     "Click a window on the desktop to bind its application root")) {
            EnterPickMode((int)i);
        }
        EndCard();
    } else {
        float pad = Dpi(16);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float iconSize = Dpi(38);
        ImVec2 iconPos = ImVec2(p0.x + pad, p0.y + Dpi(10));
        ID3D11ShaderResourceView* srv = GetOrLoadAppIconSrv(s.image.empty() ? s.exe : s.image);
        if (srv) {
            ImGui::SetCursorScreenPos(iconPos);
            ImGui::Image(ImTextureRef((ImTextureID)(UINT_PTR)srv), ImVec2(iconSize, iconSize));
        } else {
            dl->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                              ColAlpha(kColBorder, 0.9f), Dpi(8));
            std::wstring display = SlotDisplayName(s);
            char initial[2] = { (char)toupper((int)(display.empty() ? L'?' : display[0])), 0 };
            dl->AddText(ImVec2(iconPos.x + Dpi(12), iconPos.y + Dpi(9)), Col(kColDim), initial);
        }

        float textX = Dpi(66);
        float rightPad = Dpi(96);
        std::string name = ToUtf8(SlotDisplayName(s));
        PushBold();
        DrawEllipsisText(dl, ImVec2(p0.x + textX, p0.y + Dpi(5)),
                         Col(kColText), name, w - textX - rightPad);
        PopBold();
        std::string exe = ToUtf8(s.exe);
        if (exe.empty()) exe = "No application assigned";
        PushSmall();
        DrawEllipsisText(dl, ImVec2(p0.x + textX, p0.y + Dpi(28)),
                         Col(kColFaint), exe, w - textX - pad);
        PopSmall();

        std::string title = ToUtf8(s.windowTitle);
        if (title.empty()) title = name;
        DrawEllipsisText(dl, ImVec2(p0.x + pad, p0.y + Dpi(53)),
                         Col(kColDim), title, w - pad * 2.0f - Dpi(4));

        dl->AddLine(ImVec2(p0.x + pad, p0.y + Dpi(74)),
                    ImVec2(p0.x + w - pad, p0.y + Dpi(74)),
                    ColAlpha(kColBorder, 0.55f), Dpi(1));

        std::string status = SlotStatusText(s);
        float pillW = ImGui::CalcTextSize(status.c_str()).x + Dpi(18);
        DrawPill(dl, ImVec2(p0.x + w - pad - pillW, p0.y + Dpi(11)),
                 status.c_str(), SlotStatusColor(s), Dpi(22), true);

        char ws[64], cm[64], proc[48], wins[48];
        FormatMb(s.wsNow, ws, sizeof(ws));
        FormatMb(s.commitNow, cm, sizeof(cm));
        sprintf_s(proc, sizeof(proc), "%d", s.procCount);
        sprintf_s(wins, sizeof(wins), "%d", s.windowCount);
        float colW = (w - pad * 2.0f) / 4.0f;
        const char* labels[4] = { "PRIVATE RAM", "COMMIT", "PROCESSES", "WINDOWS" };
        const char* values[4] = { ws, cm, proc, wins };
        PushSmall();
        for (int k = 0; k < 4; ++k) {
            float cx = p0.x + pad + colW * k;
            dl->AddText(ImVec2(cx, p0.y + Dpi(82)), Col(kColFaint), labels[k]);
        }
        PopSmall();
        PushBold();
        for (int k = 0; k < 4; ++k) {
            float cx = p0.x + pad + colW * k;
            dl->AddText(ImVec2(cx, p0.y + Dpi(98)), Col(kColText), values[k]);
        }
        PopBold();

        if (s.state == SLOT_FROZEN) {
            unsigned long long saved = s.baselineWs > s.wsNow ? s.baselineWs - s.wsNow : 0;
            double pct = s.baselineWs ? (double)saved * 100.0 / (double)s.baselineWs : 0.0;
            char sb[64];
            FormatMb(saved, sb, sizeof(sb));
            char line[160];
            sprintf_s(line, sizeof(line), "RECLAIMED  %s  \u00B7  %.0f%% OF LOCKED BASELINE", sb, pct);
            PushSmall();
            dl->AddText(ImVec2(p0.x + pad, p0.y + Dpi(122)), Col(kColCyan), line);
            char base[128];
            char b1[64], b2[64];
            FormatMb(s.baselineWs, b1, sizeof(b1));
            FormatMb(s.wsNow, b2, sizeof(b2));
            sprintf_s(base, sizeof(base), "%s \u2192 %s", b1, b2);
            float bw = ImGui::CalcTextSize(base).x;
            dl->AddText(ImVec2(p0.x + w - pad - bw, p0.y + Dpi(122)), Col(kColFaint), base);
            PopSmall();
            ImVec2 ba = ImVec2(p0.x + pad, p0.y + Dpi(142));
            ImVec2 bb = ImVec2(p0.x + w - pad, p0.y + Dpi(147));
            dl->AddRectFilled(ba, bb, ColAlpha(kColBorder, 0.55f), Dpi(2));
            float fill = (float)(pct / 100.0);
            if (fill > 1.0f) fill = 1.0f;
            if (fill > 0.0f) {
                dl->AddRectFilled(ba, ImVec2(ba.x + (bb.x - ba.x) * fill, bb.y),
                                  Col(kColEmerald), Dpi(2));
            }
        } else if (s.state == SLOT_BUSY) {
            dl->AddText(ImVec2(p0.x + pad, p0.y + Dpi(126)), Col(kColAmber),
                        "Engine operation in progress...");
        } else {
            PushSmall();
            dl->AddText(ImVec2(p0.x + pad, p0.y + Dpi(124)), Col(kColDim),
                        "LIVE PRIVATE SET  \u00B7  BASELINE LOCKS WHEN FROZEN");
            PopSmall();
        }

        if (!s.lastError.empty()) {
            PushSmall();
            std::string err = Ellipsize(s.lastError, w - pad * 2.0f);
            dl->AddText(ImVec2(p0.x + pad, p0.y + Dpi(156)), Col(kColRed), err.c_str());
            PopSmall();
            if (ImGui::IsMouseHoveringRect(ImVec2(p0.x + pad, p0.y + Dpi(152)),
                                           ImVec2(p0.x + w - pad, p0.y + Dpi(172)))) {
                ImGui::SetTooltip("%s", s.lastError.c_str());
            }
        } else {
            PushSmall();
            char meta[160];
            sprintf_s(meta, sizeof(meta), "ROOT PID %llu  \u00B7  %s MODE",
                      s.pid, SlotTree(s) ? "TREE" : "PROCESS");
            if (s.protectedHit && g_cfg.guardAntiCheat)
                strcat_s(meta, "  \u00B7  GUARDED");
            dl->AddText(ImVec2(p0.x + pad, p0.y + Dpi(156)), Col(kColFaint), meta);
            PopSmall();
        }

        bool guarded = s.protectedHit && g_cfg.guardAntiCheat;
        const char* disabledTip = nullptr;
        if (guarded) disabledTip = "Guarded: anti-cheat protection detected in this application tree.";
        if (s.pid == 0) disabledTip = "No application is assigned to this slot.";

        bool canAct = s.state != SLOT_BUSY && s.pid != 0 && !guarded;
        const char* actionLabel = "Freeze";
        BtnKind actionKind = BTN_PRIMARY;
        if (s.state == SLOT_FROZEN) {
            actionLabel = "Thaw & Restore";
            actionKind = BTN_SUCCESS;
        } else if (s.state == SLOT_BUSY) {
            actionLabel = "Working...";
        } else if (SlotTree(s)) {
            actionLabel = "Freeze Process Tree";
        } else {
            actionLabel = "Freeze Process Only";
        }

        float actionW = w - pad * 2.0f - Dpi(44);
        ImGui::SetCursorPos(ImVec2(pad, h - Dpi(46)));
        const char* busy = s.state == SLOT_BUSY ? s.busyAction.c_str() : nullptr;
        if (UiButton(actionLabel, ImVec2(actionW, Dpi(34)), actionKind, canAct,
                     guarded ? disabledTip : (s.pid == 0 ? disabledTip :
                     (s.state == SLOT_FROZEN ? "Resume every suspended process in this tree" :
                      "Suspend this application and reclaim its private working set")),
                     busy)) {
            if (s.state == SLOT_FROZEN) QueueThaw((int)i, false);
            else QueueFreeze((int)i);
        }

        ImGui::SetCursorPos(ImVec2(w - pad - Dpi(34), h - Dpi(46)));
        if (GearButton("slotgear", ImVec2(Dpi(34), Dpi(34)),
                       "Per-slot settings: tree, auto-thaw, change or remove")) {
            g_slotSettingsId = (int)i;
            g_slotSettingsToOpen = (int)i;
        }

        if (s.state == SLOT_BUSY) {
            DrawBusyStripe(ImVec2(p0.x + pad, p0.y + h - Dpi(3)),
                           ImVec2(p0.x + w - pad, p0.y + h - Dpi(1)),
                           (float)ImGui::GetTime());
        }
        EndCard();
    }

    if (s.state == SLOT_EMPTY) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DrawDashedRect(dl, p0, ImVec2(p0.x + w, p0.y + h), ColAlpha(kColBorder, 0.85f),
                       Dpi(6), Dpi(4), Dpi(1));
    }
    ImGui::PopID();
}

void DrawAddCard(float w)
{
    ImGui::PushID("addcard");
    float h = Dpi(56);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);
    ImGui::Dummy(ImVec2(w, h));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ColAlpha(kColCard, 0.55f), Dpi(8));
    DrawDashedRect(dl, p0, p1, ColAlpha(kColBorder, 0.85f), Dpi(6), Dpi(4), Dpi(1));

    ImVec2 c = ImVec2(p0.x + Dpi(30), p0.y + h * 0.5f);
    dl->AddCircleFilled(c, Dpi(14), ColAlpha(kColCyan, 0.16f));
    PushHeader();
    float pw = ImGui::CalcTextSize("+").x;
    dl->AddText(ImVec2(c.x - pw * 0.5f, c.y - Dpi(11)), Col(kColCyan), "+");
    PopHeader();

    PushBold();
    dl->AddText(ImVec2(p0.x + Dpi(54), p0.y + Dpi(8)), Col(kColText),
                "Add Workspace Slot");
    PopBold();
    dl->AddText(ImVec2(p0.x + Dpi(54), p0.y + Dpi(30)), Col(kColDim),
                "Park one more application in parallel.");

    float primaryW = Dpi(150);
    float browseW = Dpi(160);
    float bx = p0.x + w - Dpi(16) - primaryW;
    ImGui::SetCursorScreenPos(ImVec2(bx, p0.y + Dpi(13)));
    if (UiButton("Add Empty Slot", ImVec2(primaryW, Dpi(30)), BTN_PRIMARY, true,
                 "Add another workspace slot card")) {
        AddSlot();
        g_statusLine = "Added a workspace slot";
        g_dirty = true;
    }
    ImGui::SetCursorScreenPos(ImVec2(bx - Dpi(8) - browseW, p0.y + Dpi(13)));
    if (UiButton("Browse Apps", ImVec2(browseW, Dpi(30)), BTN_GHOST, true,
                 "Open the Process & Window Explorer")) {
        g_pickSlot = -1;
        g_page = PAGE_EXPLORER;
        g_appFilter[0] = 0;
        g_dirty = true;
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
    ImGui::Dummy(ImVec2(w, Dpi(2)));
    ImGui::PopID();
}

void DrawWorkspacePage(float w, float h)
{
    while (g_slots.size() < 2) AddSlot();
    if (g_qaPopup) {
        g_slotSettingsToOpen = 0;
        g_qaPopup = false;
    }

    ImGui::BeginChild("workspace_scroll", ImVec2(w, h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_None);
    w = ImGui::GetContentRegionAvail().x;

    ImGui::SetCursorPos(ImVec2(0, 0));
    PushHeader();
    ImGui::TextColored(kColText, "Stasis Workspace");
    PopHeader();
    ImGui::SetCursorPos(ImVec2(0, Dpi(30)));
    ImGui::TextColored(kColDim, "Park applications to release physical RAM, then restore them instantly.");

    int frozen = 0, active = 0;
    unsigned long long reclaimed = 0;
    SlotTotals(frozen, active, reclaimed);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float pillY = Dpi(50);
    char one[64], two[64], three[64];
    sprintf_s(one, sizeof(one), "%d IN STASIS", frozen);
    char rm[64];
    FormatMb(reclaimed, rm, sizeof(rm));
    sprintf_s(two, sizeof(two), "%s FREED", rm);
    sprintf_s(three, sizeof(three), "%d ACTIVE", active);
    float x = 0;
    x += DrawPill(dl, ImVec2(wp.x + x, wp.y + pillY), one, frozen ? kColCyan : kColFaint) + Dpi(8);
    x += DrawPill(dl, ImVec2(wp.x + x, wp.y + pillY), two, reclaimed ? kColEmerald : kColFaint) + Dpi(8);
    x += DrawPill(dl, ImVec2(wp.x + x, wp.y + pillY), three, active ? kColBlue : kColFaint) + Dpi(8);
    if (g_telValid) {
        char freeMb[64];
        FormatMb(g_tel.freeCommit, freeMb, sizeof(freeMb));
        char four[96];
        sprintf_s(four, sizeof(four), "COMMIT FREE %s", freeMb);
        DrawPill(dl, ImVec2(wp.x + x, wp.y + pillY), four,
                 g_tel.commitFrac >= 0.90 ? kColRed : (g_tel.commitFrac >= 0.75 ? kColAmber : kColEmerald));
    }

    float gap = Dpi(12);
    float cardH = Dpi(224);
    int cols = (w >= Dpi(600)) ? 2 : 1;
    float cardW = (w - gap * (cols - 1)) / (float)cols;
    float gridY = Dpi(82);
    size_t slots = g_slots.size();
    for (size_t i = 0; i < slots; ++i) {
        int col = (int)(i % (size_t)cols);
        int row = (int)(i / (size_t)cols);
        ImGui::SetCursorPos(ImVec2((float)col * (cardW + gap),
                                   gridY + (float)row * (cardH + gap)));
        DrawSlotCard(i, cardW, cardH);
    }
    int rows = slots ? (int)((slots + (size_t)cols - 1) / (size_t)cols) : 0;
    float addY = gridY + (float)rows * cardH + (float)(rows > 0 ? rows : 0) * gap;
    ImGui::SetCursorPos(ImVec2(0, addY));
    DrawAddCard(w);

    ImGui::SetCursorPos(ImVec2(0, addY + Dpi(56)));
    ImGui::Dummy(ImVec2(w, Dpi(2)));

    DrawSlotSettingsPopup();
    ImGui::EndChild();
}

void DrawSlotSettingsPopup()
{
    bool openNow = false;
    if (g_slotSettingsToOpen >= 0) {
        g_slotSettingsId = g_slotSettingsToOpen;
        g_slotSettingsToOpen = -1;
        openNow = true;
    }
    if (g_slotSettingsId < 0 || (size_t)g_slotSettingsId >= g_slots.size()) {
        g_slotSettingsId = -1;
        return;
    }

    ImGui::PushID(g_slotSettingsId);
    if (openNow) ImGui::OpenPopup("##slotcfg");
    if (openNow && g_shotMode) {
        ImVec2 wp = ImGui::GetWindowPos();
        ImGui::SetNextWindowPos(ImVec2(wp.x + Dpi(96), wp.y + Dpi(64)), ImGuiCond_Always);
    }
    ImGui::SetNextWindowSize(ImVec2(Dpi(370), 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##slotcfg")) {
        Slot& s = g_slots[(size_t)g_slotSettingsId];
        char header[96];
        sprintf_s(header, sizeof(header), "Slot %c Settings", 'A' + (char)(g_slotSettingsId % 26));
        PushBold();
        ImGui::TextColored(kColText, "%s", header);
        PopBold();
        ImGui::TextColored(kColDim, "%s", ToUtf8(SlotDisplayName(s)).c_str());
        ImGui::Separator();
        ImGui::Spacing();

        bool tree = SlotTree(s);
        if (UiToggle("tree", "Freeze process tree",
                     s.treeOverride ? "Per-slot override" : "Inherited from global settings",
                     &tree, true, nullptr)) {
            s.treeOverride = true;
            s.tree = tree;
            g_dirty = true;
        }
        bool at = SlotAutoThaw(s);
        if (UiToggle("autothaw", "Auto-thaw on focus",
                     s.autoThawOverride ? "Per-slot override" : "Inherited from global settings",
                     &at, true, nullptr)) {
            s.autoThawOverride = true;
            s.autoThaw = at;
            g_dirty = true;
        }

        bool hasOverride = s.treeOverride || s.autoThawOverride;
        if (UiButton("Use Global Defaults", ImVec2(-1, Dpi(30)), BTN_SUBTLE, hasOverride,
                     hasOverride ? "Clear both per-slot overrides"
                                 : "This slot already inherits every global default")) {
            s.treeOverride = false;
            s.autoThawOverride = false;
            s.tree = g_cfg.defaultFreezeTree;
            s.autoThaw = g_cfg.defaultAutoThawOnFocus;
            g_dirty = true;
        }
        char inherited[192];
        sprintf_s(inherited, sizeof(inherited), "Global: tree %s, auto-thaw %s",
                  g_cfg.defaultFreezeTree ? "on" : "off",
                  g_cfg.defaultAutoThawOnFocus ? "on" : "off");
        ImGui::TextColored(kColFaint, "%s", inherited);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (UiButton("Change Application", ImVec2(-1, Dpi(32)), BTN_PRIMARY, s.state != SLOT_BUSY,
                     s.state == SLOT_BUSY ? "Wait for the current operation first"
                                          : "Choose a different application for this slot")) {
            g_pickSlot = g_slotSettingsId;
            g_page = PAGE_EXPLORER;
            g_appFilter[0] = 0;
            ImGui::CloseCurrentPopup();
            g_dirty = true;
        }
        if (UiButton("Remove Card", ImVec2(-1, Dpi(32)), BTN_DANGER, s.state != SLOT_BUSY,
                     s.state == SLOT_BUSY ? "Wait for the current operation first"
                                          : "Frozen cards are thawed before removal")) {
            int idx = g_slotSettingsId;
            ImGui::CloseCurrentPopup();
            g_slotSettingsId = -1;
            RemoveSlot((size_t)idx);
        }
        ImGui::Spacing();
        if (UiButton("Close", ImVec2(-1, Dpi(28)), BTN_GHOST, true, nullptr)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void DrawExitModal()
{
    static bool shown = false;
    if (!g_exitModal) {
        if (shown) {
            if (ImGui::IsPopupOpen("Exit while frozen")) ImGui::CloseCurrentPopup();
            shown = false;
        }
        return;
    }
    if (!shown) {
        ImGui::OpenPopup("Exit while frozen");
        shown = true;
    }
    if (ImGui::BeginPopupModal("Exit while frozen", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        char buf[160];
        sprintf_s(buf, sizeof(buf), "%d application(s) are still frozen.", g_frozenCountModal);
        PushBold();
        ImGui::TextColored(kColText, "%s", buf);
        PopBold();
        ImGui::Spacing();
        ImGui::TextColored(kColDim, "Thawing restores them before RamStasis exits. If the");
        ImGui::TextColored(kColDim, "engine cannot start during shutdown, the UI resumes");
        ImGui::TextColored(kColDim, "every recorded pid directly via NtResumeProcess.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (UiButton("Thaw All & Exit", ImVec2(Dpi(160), Dpi(34)), BTN_SUCCESS, true,
                     "Restore every frozen tree, then close RamStasis")) {
            g_exitModal = false;
            g_statusLine = "Thawing all before exit...";
            ThawEverythingForExit();
            g_quit = true;
            PostQuitMessage(0);
        }
        ImGui::SameLine();
        if (UiButton("Keep Frozen & Exit", ImVec2(Dpi(160), Dpi(34)), BTN_SUBTLE, true,
                     "Leave the trees frozen; they are recovered on next launch")) {
            g_exitModal = false;
            g_quit = true;
            PostQuitMessage(0);
        }
        ImGui::SameLine();
        if (UiButton("Cancel", ImVec2(Dpi(90), Dpi(34)), BTN_GHOST, true, "Stay open")) {
            g_exitModal = false;
            ImGui::CloseCurrentPopup();
            g_dirty = true;
        }
        ImGui::EndPopup();
    } else {
        g_exitModal = false;
        shown = false;
        g_dirty = true;
    }
}

}

namespace {

const char* kExplorerTabs[4] = { "All Apps", "Windows", "With Children", "Protected" };

bool RowMatchesTab(const PickRow& r, int tab)
{
    switch (tab) {
    case 1: return r.hasVisibleWindow;
    case 2: return !r.childProcs.empty();
    case 3: return r.protectedHit;
    default: return true;
    }
}

bool RowMatchesSearch(const PickRow& r, const std::string& filterLower)
{
    if (filterLower.empty()) return true;
    char pid[32];
    sprintf_s(pid, sizeof(pid), "%llu", r.rootPid);
    if (ContainsNoCase(ToUtf8(r.primaryWindowTitle), filterLower)) return true;
    if (ContainsNoCase(ToUtf8(r.friendlyName), filterLower)) return true;
    if (ContainsNoCase(ToUtf8(r.exe), filterLower)) return true;
    if (ContainsNoCase(ToUtf8(r.image), filterLower)) return true;
    return std::string(pid).find(filterLower) != std::string::npos;
}

void FlashOwnedWindow(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    FLASHWINFO fi;
    ZeroMemory(&fi, sizeof(fi));
    fi.cbSize = sizeof(fi);
    fi.hwnd = hwnd;
    fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
    fi.uCount = 4;
    fi.dwTimeout = 0;
    FlashWindowEx(&fi);
}

void DrawOwnedWindows(const PickRow& row, float width)
{
    ImGui::TextColored(kColFaint, "WINDOWS  (%u)", (unsigned)row.windows.size());
    ImGui::Spacing();
    if (row.windows.empty()) {
        ImGui::TextColored(kColFaint, "No visible titled windows.");
        return;
    }
    for (size_t i = 0; i < row.windows.size(); ++i) {
        const OwnedWindowEntry& win = row.windows[i];
        ImGui::PushID((int)i);
        std::string title = ToUtf8(win.title);
        if (title.empty()) title = "(untitled)";
        float flashW = Dpi(58);
        float maxW = width - flashW - Dpi(14);
        if (maxW < Dpi(80)) maxW = Dpi(80);
        std::string shown = Ellipsize(title, maxW);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX());
        ImGui::TextColored(win.isIconic ? kColDim : kColText, "%s", shown.c_str());
        if (ImGui::IsItemHovered() && shown != title) ImGui::SetTooltip("%s", title.c_str());
        ImGui::SameLine();
        float x = ImGui::GetWindowContentRegionMax().x - flashW;
        if (x < ImGui::GetCursorPosX()) x = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(x);
        if (UiButton("Flash", ImVec2(flashW, Dpi(24)), BTN_GHOST, true,
                     "Flash this window in the taskbar")) {
            FlashOwnedWindow(win.hwnd);
        }
        ImGui::PopID();
    }
}

void DrawChildProcs(const PickRow& row, float width)
{
    ImGui::TextColored(kColFaint, "PROCESSES  (%u)", (unsigned)(row.childProcs.size() + 1));
    ImGui::Spacing();
    ImGui::TextColored(kColFaint, "%s  (root)", ToUtf8(row.exe).c_str());
    for (size_t i = 0; i < row.childProcs.size(); ++i) {
        const ChildProcEntry& child = row.childProcs[i];
        char ram[64];
        FormatMb(child.ws, ram, sizeof(ram));
        char line[320];
        sprintf_s(line, sizeof(line), "%s  •  PID %llu  •  %s",
                  ToUtf8(child.exe).c_str(), child.pid, ram);
        std::string shown = Ellipsize(line, width);
        ImGui::TextColored(kColDim, "%s", shown.c_str());
        if (ImGui::IsItemHovered() && shown != line) ImGui::SetTooltip("%s", line);
    }
}

void DrawAppRow(const PickRow& row, size_t index, float w)
{
    bool expanded = g_expandedRows.find(row.rootPid) != g_expandedRows.end();
    float detailsH = Dpi(180);
    float rowH = expanded ? (Dpi(84) + detailsH + Dpi(12)) : Dpi(84);

    ImGui::PushID((int)index);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Dpi(8));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kColCard);
    ImGui::PushStyleColor(ImGuiCol_Border, kColBorder);
    ImGui::BeginChild("approw", ImVec2(0, rowH), ImGuiChildFlags_Borders);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float iconX = Dpi(14);
    float textX = Dpi(58);
    float selectW = Dpi(84);
    float detailsW = Dpi(80);
    float detailsX = w - Dpi(16) - detailsW;
    float selectX = detailsX - selectW - Dpi(8);
    float textRight = selectX - Dpi(12);
    float titleMaxW = textRight - p0.x - textX;
    if (titleMaxW < Dpi(120)) titleMaxW = Dpi(120);

    ImVec2 iconPos = ImVec2(p0.x + iconX, p0.y + Dpi(12));
    if (row.iconSrv) {
        ImGui::SetCursorScreenPos(iconPos);
        ImGui::Image(ImTextureRef((ImTextureID)(UINT_PTR)row.iconSrv), ImVec2(Dpi(32), Dpi(32)));
    } else {
        dl->AddRectFilled(iconPos, ImVec2(iconPos.x + Dpi(32), iconPos.y + Dpi(32)),
                          ColAlpha(kColBorder, 0.9f), Dpi(7));
        std::string initial = ToUtf8(row.exe);
        if (!initial.empty()) initial = initial.substr(0, 1);
        dl->AddText(ImVec2(iconPos.x + Dpi(11), iconPos.y + Dpi(6)), Col(kColDim), initial.c_str());
    }

    std::string title = ToUtf8(row.primaryWindowTitle);
    if (title.empty()) title = ToUtf8(row.friendlyName);
    if (title.empty()) title = ToUtf8(row.exe);
    PushBold();
    DrawEllipsisText(dl, ImVec2(p0.x + textX, p0.y + Dpi(7)), Col(kColText), title, titleMaxW);
    PopBold();

    std::string sub = ToUtf8(row.friendlyName);
    if (sub.empty()) sub = ToUtf8(row.exe);
    if (_wcsicmp(row.friendlyName.c_str(), row.exe.c_str()) != 0 && !row.exe.empty())
        sub += "  •  " + ToUtf8(row.exe);
    char pidbuf[48];
    sprintf_s(pidbuf, sizeof(pidbuf), "  •  PID %llu", row.rootPid);
    sub += pidbuf;
    DrawEllipsisText(dl, ImVec2(p0.x + textX, p0.y + Dpi(29)), Col(kColFaint), sub, titleMaxW);

    char ws[64], cm[64];
    FormatMb(row.treeWs, ws, sizeof(ws));
    FormatMb(row.treeCommit, cm, sizeof(cm));
    char stats[256];
    sprintf_s(stats, sizeof(stats), "RAM %s   •   Commit %s   •   %u proc   •   %u window",
              ws, cm, (unsigned)(row.childProcs.size() + 1), (unsigned)row.windows.size());
    DrawEllipsisText(dl, ImVec2(p0.x + textX, p0.y + Dpi(52)),
                     Col(row.hasVisibleWindow ? kColDim : kColFaint), stats, titleMaxW);

    if (row.protectedHit) {
        float used = ImGui::CalcTextSize(title.c_str()).x;
        float pillX = p0.x + textX + (used < titleMaxW - Dpi(120) ? used + Dpi(12) : titleMaxW - Dpi(96));
        if (pillX < p0.x + textX) pillX = p0.x + textX;
        DrawPill(dl, ImVec2(pillX, p0.y + Dpi(7)), "GUARDED", kColAmber, Dpi(20), true);
    }

    bool canSelect = !(row.protectedHit && g_cfg.guardAntiCheat);
    const char* selectTip = canSelect ? "Assign this application root to a workspace slot"
                                      : "Protected by the anti-cheat guard; selection is refused.";
    ImGui::SetCursorPos(ImVec2(selectX, Dpi(12)));
    if (UiButton("Select", ImVec2(selectW, Dpi(30)), BTN_PRIMARY, canSelect, selectTip)) {
        AssignRowToSlot(g_pickSlot, row);
        g_pickSlot = -1;
        g_dirty = true;
    }

    ImGui::SetCursorPos(ImVec2(detailsX, Dpi(12)));
    if (UiButton(expanded ? "Hide" : "Details", ImVec2(detailsW, Dpi(30)), BTN_GHOST, true,
                 expanded ? "Collapse window and process lists"
                          : "Show owned windows and child processes")) {
        if (expanded) g_expandedRows.erase(row.rootPid);
        else g_expandedRows.insert(row.rootPid);
        g_dirty = true;
    }

    if (row.protectedBy.size() && row.protectedHit) {
        float pw = ImGui::CalcTextSize(ToUtf8(row.protectedBy).c_str()).x;
        if (pw < w - Dpi(80)) {
            dl->AddText(ImVec2(p0.x + Dpi(58), p0.y + Dpi(67)),
                        ColAlpha(kColRed, 0.75f),
                        ("Guard: " + ToUtf8(row.protectedBy)).c_str());
        }
    }

    if (expanded) {
        ImGui::SetCursorPos(ImVec2(Dpi(12), Dpi(84)));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Dpi(6));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Dpi(12), Dpi(8)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kColBg);
        ImGui::PushStyleColor(ImGuiCol_Border, kColBorder);
        ImGui::BeginChild("details", ImVec2(w - Dpi(24), detailsH), ImGuiChildFlags_Borders);

        float avail = ImGui::GetContentRegionAvail().x;
        float leftW = (avail - Dpi(16)) * 0.58f;
        float rightW = avail - leftW - Dpi(16);
        ImGui::BeginChild("wins", ImVec2(leftW, 0), ImGuiChildFlags_None);
        DrawOwnedWindows(row, leftW);
        ImGui::EndChild();
        ImGui::SameLine(0, Dpi(16));
        ImGui::BeginChild("procs", ImVec2(rightW, 0), ImGuiChildFlags_None);
        DrawChildProcs(row, rightW);
        ImGui::EndChild();

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void DrawExplorerPage(float w, float h)
{
    EnsureAppRows(g_pickMode);
    if (g_qaExpandRow >= 0) {
        if ((size_t)g_qaExpandRow < g_appRows.size()) {
            g_expandedRows.insert(g_appRows[(size_t)g_qaExpandRow].rootPid);
        }
        g_qaExpandRow = -1;
    }

    ImGui::SetCursorPos(ImVec2(0, 0));
    PushHeader();
    ImGui::TextColored(kColText, "Process & Window Explorer");
    PopHeader();
    ImGui::SetCursorPos(ImVec2(0, Dpi(28)));
    ImGui::TextColored(kColDim, "Applications grouped by their visible desktop windows and process trees.");

    float refreshW = Dpi(104);
    float pickW = Dpi(120);
    ImGui::SetCursorPos(ImVec2(w - refreshW - pickW - Dpi(8), 0));
    if (UiButton("Refresh", ImVec2(refreshW, Dpi(30)), BTN_SUBTLE, true,
                 "Rescan processes and visible windows now")) {
        g_appRowsMs = 0;
        EnsureAppRows(true);
        g_statusLine = "Application snapshot refreshed";
        g_dirty = true;
    }
    ImGui::SetCursorPos(ImVec2(w - pickW, 0));
    if (UiButton("Pick Window", ImVec2(pickW, Dpi(30)), BTN_PRIMARY, true,
                 "Crosshair-pick a window on the desktop")) {
        EnterPickMode(-1);
    }

    float y = Dpi(58);
    if (g_pickSlot >= 0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 a = ImVec2(wp.x, wp.y + y);
        ImVec2 b = ImVec2(wp.x + w, wp.y + y + Dpi(40));
        dl->AddRectFilled(a, b, ColAlpha(kColBlue, 0.12f), Dpi(8));
        dl->AddRect(a, b, ColAlpha(kColBlue, 0.45f), Dpi(8), 0, Dpi(1));
        char buf[128];
        sprintf_s(buf, sizeof(buf), "Choosing an application for Slot %c",
                  'A' + (char)(g_pickSlot % 26));
        PushBold();
        dl->AddText(ImVec2(a.x + Dpi(14), a.y + Dpi(10)), Col(kColText), buf);
        PopBold();
        float cancelW = Dpi(84);
        ImGui::SetCursorPos(ImVec2(w - cancelW - Dpi(8), y + Dpi(6)));
        if (UiButton("Cancel", ImVec2(cancelW, Dpi(28)), BTN_GHOST, true,
                     "Stop choosing an application")) {
            g_pickSlot = -1;
            g_dirty = true;
        }
        y += Dpi(50);
    }

    float refreshBtnW = Dpi(98);
    float clearW = Dpi(64);
    ImGui::SetCursorPos(ImVec2(0, y));
    ImGui::SetNextItemWidth(w - refreshBtnW - Dpi(8) - (g_appFilter[0] ? clearW + Dpi(8) : 0.0f));
    if (ImGui::InputTextWithHint("##appsearch", "Search by window title, app, executable or PID...",
                                 g_appFilter, sizeof(g_appFilter))) {
        g_dirty = true;
    }
    if (g_appFilter[0]) {
        ImGui::SameLine(0, Dpi(8));
        if (UiButton("Clear", ImVec2(clearW, Dpi(34)), BTN_GHOST, true, "Clear the search filter")) {
            g_appFilter[0] = 0;
            g_dirty = true;
        }
    }
    ImGui::SetCursorPos(ImVec2(w - refreshBtnW, y));
    if (UiButton("Rescan", ImVec2(refreshBtnW, Dpi(34)), BTN_SUBTLE, true,
                 "Force an immediate process and window scan")) {
        g_appRowsMs = 0;
        EnsureAppRows(true);
        g_dirty = true;
    }

    y += Dpi(44);
    float tx = 0;
    for (int i = 0; i < 4; ++i) {
        float tw = ImGui::CalcTextSize(kExplorerTabs[i]).x + Dpi(24);
        ImGui::SetCursorPos(ImVec2(tx, y));
        BtnKind kind = (g_appFilterTab == i) ? BTN_PRIMARY : BTN_GHOST;
        if (UiButton(kExplorerTabs[i], ImVec2(tw, Dpi(28)), kind, true,
                     i == 0 ? "Show every detected application" :
                     i == 1 ? "Only applications with visible windows" :
                     i == 2 ? "Only applications with child processes" :
                              "Only applications flagged by the anti-cheat guard")) {
            g_appFilterTab = i;
            g_dirty = true;
        }
        tx += tw + Dpi(6);
    }

    std::string filterLower = LowerAscii(std::string(g_appFilter));
    int matchCount = 0;
    int windowCount = 0;
    for (size_t i = 0; i < g_appRows.size(); ++i) {
        if (!RowMatchesTab(g_appRows[i], g_appFilterTab)) continue;
        if (!RowMatchesSearch(g_appRows[i], filterLower)) continue;
        ++matchCount;
        windowCount += (int)g_appRows[i].windows.size();
    }
    char count[128];
    sprintf_s(count, sizeof(count), "%d app(s)  •  %d window(s)", matchCount, windowCount);
    float countW = ImGui::CalcTextSize(count).x;
    if (countW + Dpi(180) < w) {
        ImVec2 wp = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(wp.x + w - countW, wp.y + y + Dpi(7)),
            Col(kColFaint), count);
    }

    y += Dpi(38);
    float listH = h - y;
    if (listH < Dpi(100)) listH = Dpi(100);
    ImGui::SetCursorPos(ImVec2(0, y));
    ImGui::BeginChild("applist", ImVec2(w, listH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    int drawn = 0;
    for (size_t i = 0; i < g_appRows.size(); ++i) {
        if (!RowMatchesTab(g_appRows[i], g_appFilterTab)) continue;
        if (!RowMatchesSearch(g_appRows[i], filterLower)) continue;
        DrawAppRow(g_appRows[i], i, w - Dpi(12));
        ImGui::Dummy(ImVec2(0, Dpi(2)));
        ++drawn;
    }

    if (drawn == 0) {
        float emptyH = Dpi(160);
        ImGui::Dummy(ImVec2(w, Dpi(22)));
        ImGui::BeginChild("emptyapps", ImVec2(w, emptyH), ImGuiChildFlags_Borders);
        ImVec2 ep = ImGui::GetWindowPos();
        float ew = ImGui::GetWindowWidth();
        ImGui::SetCursorPos(ImVec2(0, Dpi(24)));
        PushHeader();
        float tw = ImGui::CalcTextSize("No matching applications").x;
        ImGui::SetCursorPosX((ew - tw) * 0.5f);
        ImGui::TextColored(kColText, "No matching applications");
        PopHeader();
        ImGui::SetCursorPos(ImVec2(0, Dpi(58)));
        const char* hint = g_appRows.empty()
            ? "The snapshot is empty. Use Rescan to scan processes and windows."
            : "Adjust the search text or filter tab.";
        float hw = ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX((ew - hw) * 0.5f);
        ImGui::TextColored(kColDim, "%s", hint);
        ImGui::SetCursorPos(ImVec2((ew - Dpi(150)) * 0.5f, Dpi(96)));
        if (UiButton("Clear Filters", ImVec2(Dpi(150), Dpi(32)), BTN_SUBTLE, true,
                     "Reset search text and filter tab")) {
            g_appFilter[0] = 0;
            g_appFilterTab = 0;
            g_dirty = true;
        }
        ImGui::EndChild();
        (void)ep;
    }
    ImGui::EndChild();
}

}

namespace {

void MarkConfigDirty()
{
    g_settingsDirty = true;
    g_settingsDirtyMs = GetTickCount64();
}

void TickConfigSave()
{
    if (!g_settingsDirty) return;
    if (GetTickCount64() - g_settingsDirtyMs < 450) return;
    SaveAppConfig();
    g_settingsDirty = false;
    g_statusLine = "Settings saved to state\\ramstasis-settings.json";
    g_dirty = true;
}

ULONGLONG g_hkCaptureSince = 0;
bool g_hkPrev[256] = {};

bool IsModifierVk(UINT vk)
{
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
        return true;
    default:
        return false;
    }
}

bool IsUsableHotkeyVk(UINT vk)
{
    if (vk == 0 || IsModifierVk(vk)) return false;
    if (vk >= VK_LBUTTON && vk <= VK_XBUTTON2) return false;
    if (vk == VK_CANCEL || vk == VK_CLEAR || vk == VK_PACKET) return false;
    return true;
}

void BeginHotkeyCapture()
{
    g_captureHotkey = true;
    g_hkCaptureSince = GetTickCount64();
    for (int vk = 0; vk < 256; ++vk) {
        g_hkPrev[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    g_statusLine = "Listening for a new swap hotkey...";
    g_dirty = true;
}

void CancelHotkeyCapture()
{
    if (!g_captureHotkey) return;
    g_captureHotkey = false;
    g_statusLine = "Hotkey capture cancelled";
    g_dirty = true;
}

void PollHotkeyCapture()
{
    if (!g_captureHotkey) return;
    if (g_shotMode) return;
    if (g_hkCaptureSince != 0 && GetTickCount64() - g_hkCaptureSince < 120) return;

    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool win = ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
    UINT mods = 0;
    if (ctrl) mods |= MOD_CONTROL;
    if (alt) mods |= MOD_ALT;
    if (shift) mods |= MOD_SHIFT;
    if (win) mods |= MOD_WIN;

    for (int v = 1; v < 256; ++v) {
        bool down = (GetAsyncKeyState(v) & 0x8000) != 0;
        bool edge = down && !g_hkPrev[v];
        g_hkPrev[v] = down;
        if (!edge) continue;
        UINT vk = (UINT)v;
        if (vk == VK_ESCAPE && mods == 0) {
            CancelHotkeyCapture();
            return;
        }
        if (IsModifierVk(vk)) continue;
        if (!IsUsableHotkeyVk(vk)) continue;
        if (mods == 0) {
            g_statusLine = "Add Ctrl, Alt, Shift or Win to the shortcut";
            g_dirty = true;
            continue;
        }

        g_cfg.swapHotkeyModifiers = mods;
        g_cfg.swapHotkeyVk = vk;
        g_captureHotkey = false;
        bool registered = RegisterConfiguredHotkey();
        MarkConfigDirty();

        char buf[192];
        sprintf_s(buf, sizeof(buf), "Swap hotkey set to %s%s",
                  ToUtf8(HotkeyLabel()).c_str(),
                  registered ? " (registered)" : " (registration failed)");
        g_statusLine = buf;
        LogUi("hotkey capture: mods=%u vk=%u registered=%d", mods, vk, (int)registered);
        g_dirty = true;
        return;
    }
}

bool BeginPanel(const char* id, float width, float height, bool dim)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Dpi(16), Dpi(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Dpi(8));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          dim ? ImVec4(0.0706f, 0.0863f, 0.1098f, 0.55f) : kColCard);
    ImGui::PushStyleColor(ImGuiCol_Border, kColBorder);
    ImGuiChildFlags flags = ImGuiChildFlags_Borders;
    if (height <= 0.0f) {

        height = 0.0f;
        flags |= ImGuiChildFlags_AutoResizeY;
    }
    return ImGui::BeginChild(id, ImVec2(width, height), flags);
}

void EndPanel()
{
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void PanelTitle(const char* title, const char* subtitle)
{
    PushBold();
    ImGui::TextColored(kColText, "%s", title);
    PopBold();
    if (subtitle && subtitle[0]) ImGui::TextColored(kColFaint, "%s", subtitle);
    ImGui::Spacing();
}

void DrawSectionHeader(const char* title, const char* desc)
{
    PushHeader();
    ImGui::TextColored(kColText, "%s", title);
    PopHeader();
    if (desc && desc[0]) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kColDim, "%s", desc);
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
}

void ClampPagefileEditors()
{
    if (g_pfDrive < 0 || (size_t)g_pfDrive >= g_pf.drives.size()) return;
    unsigned long long allowed = PagefileMaxAllowedMb(g_pf, g_pfDrive);
    if (g_pfSystemManaged) return;

    if (g_pfInitialMb < 1024ULL) g_pfInitialMb = 1024ULL;
    if (allowed >= 1024ULL && g_pfInitialMb > allowed) g_pfInitialMb = allowed;
    if (g_pfMaxMb < g_pfInitialMb) g_pfMaxMb = g_pfInitialMb;
    if (allowed >= 1024ULL && g_pfMaxMb > allowed) g_pfMaxMb = allowed;
    if (g_pfMaxMb < g_pfInitialMb) g_pfMaxMb = g_pfInitialMb;
}

void SelectPagefileDrive(int idx)
{
    if (idx < 0 || (size_t)idx >= g_pf.drives.size()) return;
    g_pfDrive = idx;
    const PagefileDriveInfo& d = g_pf.drives[(size_t)idx];
    g_pfSystemManaged = d.systemManaged;
    if (d.hasPagefile && !d.systemManaged && d.initialMb > 0) {
        g_pfInitialMb = d.initialMb;
        g_pfMaxMb = d.maxMb > 0 ? d.maxMb : d.initialMb;
    } else if (d.hasPagefile && d.systemManaged) {
        g_pfInitialMb = g_pf.recInitialMb;
        g_pfMaxMb = g_pf.recMaxMb;
    } else {
        g_pfInitialMb = g_pf.recInitialMb;
        g_pfMaxMb = g_pf.recMaxMb;
    }
    ClampPagefileEditors();
    g_dirty = true;
}

void UseRecommendedPagefileValues()
{
    if (g_pf.recDrive < 0 || (size_t)g_pf.recDrive >= g_pf.drives.size()) return;
    g_pfDrive = g_pf.recDrive;
    g_pfSystemManaged = false;
    g_pfInitialMb = g_pf.recInitialMb;
    g_pfMaxMb = g_pf.recMaxMb;
    ClampPagefileEditors();
    g_statusLine = "Loaded the recommended pagefile configuration";
    g_dirty = true;
}

void SetPagefilePreset(unsigned long long initialMb);

void EnsurePagefileScanned()
{
    if (g_pfScanned) return;
    ULONGLONG now = GetTickCount64();
    if (g_lastPfScanMs != 0 && now - g_lastPfScanMs < 5000) return;
    g_lastPfScanMs = now;

    PagefileStatus st;
    if (ScanPagefileDrives(st)) {
        g_pf = st;
        g_pfScanned = true;
        g_pfApplying = false;
        g_pfResultValid = false;
        SelectPagefileDrive(g_pf.recDrive);
        if (g_qaPfClamp) {
            g_pfInitialMb = 512ULL;
            g_pfMaxMb = 999999999ULL;
            ClampPagefileEditors();
            g_qaPfClamp = false;
        } else if (g_qaPfPreset == 0) {
            g_pfSystemManaged = true;
            g_qaPfPreset = -1;
        } else if (g_qaPfPreset > 0) {
            SetPagefilePreset((unsigned long long)g_qaPfPreset * 1024ULL);
            g_qaPfPreset = -1;
        }
        LogUi("pagefile scan: %u fixed drive(s), recommended %c: initial=%llu max=%llu",
              (unsigned)g_pf.drives.size(),
              g_pf.recDrive >= 0 ? g_pf.drives[(size_t)g_pf.recDrive].letter : L'?',
              g_pf.recInitialMb, g_pf.recMaxMb);
    } else {
        g_pf = st;
        g_pfScanned = false;
        g_pfDrive = -1;
        LogUi("pagefile scan failed: %ls", g_pf.error.c_str());
    }
}

void SetPagefilePreset(unsigned long long initialMb)
{
    if (g_pfDrive < 0) return;
    unsigned long long allowed = PagefileMaxAllowedMb(g_pf, g_pfDrive);
    if (allowed < initialMb) return;
    g_pfSystemManaged = false;
    g_pfInitialMb = initialMb;
    g_pfMaxMb = initialMb + 8192ULL;
    if (g_pfMaxMb > allowed) g_pfMaxMb = allowed;
    if (g_pfMaxMb < g_pfInitialMb) g_pfMaxMb = g_pfInitialMb;
    g_dirty = true;
}

void DrawPagefileStatusCard(float w)
{
    float h = Dpi(104);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = p;
    ImVec2 b = ImVec2(p.x + w, p.y + h);
    dl->AddRectFilled(a, b, Col(kColCard), Dpi(8));
    dl->AddRect(a, b, Col(kColBorder), Dpi(8), 0, Dpi(1));
    dl->AddRectFilled(ImVec2(a.x, a.y + Dpi(10)), ImVec2(a.x + Dpi(3), b.y - Dpi(10)),
                      ColAlpha(kColCyan, 0.9f), Dpi(2));

    double usedGb = (double)g_pf.commitUsed / 1073741824.0;
    double limitGb = (double)g_pf.commitLimit / 1073741824.0;
    double frac = limitGb > 0.0 ? usedGb / limitGb : 0.0;

    char used[96];
    sprintf_s(used, sizeof(used), "%.1f GB", usedGb);
    char limit[96];
    sprintf_s(limit, sizeof(limit), "%.1f GB", limitGb);
    char pct[32];
    sprintf_s(pct, sizeof(pct), "%.0f%%", frac * 100.0);
    char pf[96];
    FormatMb(g_pf.currentPagefileBytes, pf, sizeof(pf));
    char ram[64];
    FormatMb(g_pf.ramTotal, ram, sizeof(ram));

    dl->AddText(ImVec2(a.x + Dpi(16), a.y + Dpi(12)), Col(kColFaint), "COMMIT CHARGE");
    PushBold();
    dl->AddText(ImVec2(a.x + Dpi(16), a.y + Dpi(28)), Col(kColText), used);
    PopBold();
    float usedW = ImGui::CalcTextSize(used).x;
    dl->AddText(ImVec2(a.x + Dpi(16) + usedW + Dpi(6), a.y + Dpi(34)), Col(kColDim), "/");
    dl->AddText(ImVec2(a.x + Dpi(16) + usedW + Dpi(18), a.y + Dpi(34)), Col(kColDim), limit);
    dl->AddText(ImVec2(a.x + Dpi(16), a.y + Dpi(58)), Col(kColFaint), "COMMIT LIMIT");
    dl->AddText(ImVec2(a.x + Dpi(16), a.y + Dpi(73)), Col(kColText), limit);
    dl->AddText(ImVec2(a.x + Dpi(112), a.y + Dpi(73)), Col(kColFaint), "RAM");
    dl->AddText(ImVec2(a.x + Dpi(146), a.y + Dpi(73)), Col(kColText), ram);

    float barX = a.x + Dpi(16);
    float barW = w - Dpi(240);
    if (barW < Dpi(160)) barW = Dpi(160);
    float barY = a.y + Dpi(92);
    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + Dpi(5)),
                      ColAlpha(kColBorder, 0.8f), Dpi(2));
    ImVec4 barCol = kColEmerald;
    if (frac >= 0.90) barCol = kColRed;
    else if (frac >= 0.75) barCol = kColAmber;
    float fillW = barW * (float)(frac > 1.0 ? 1.0 : frac);
    if (fillW > 0.0f)
        dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillW, barY + Dpi(5)),
                          Col(barCol), Dpi(2));

    float rightX = a.x + w - Dpi(208);
    dl->AddText(ImVec2(rightX, a.y + Dpi(40)), Col(kColFaint), "ACTIVE PAGEFILE");
    PushBold();
    dl->AddText(ImVec2(rightX, a.y + Dpi(56)), Col(kColText), pf);
    PopBold();
    dl->AddText(ImVec2(rightX, a.y + Dpi(80)), Col(kColFaint), "COMMIT USED");
    float usedLabelW = ImGui::CalcTextSize("COMMIT USED").x;
    dl->AddText(ImVec2(rightX + usedLabelW + Dpi(10), a.y + Dpi(80)), Col(barCol), pct);

    const char* mode = g_pf.elevated ? "ADMINISTRATOR" : "STANDARD USER";
    ImVec4 modeCol = g_pf.elevated ? kColEmerald : kColAmber;
    float pw = ImGui::CalcTextSize(mode).x + Dpi(18);
    DrawPill(dl, ImVec2(a.x + w - Dpi(16) - pw, a.y + Dpi(10)), mode, modeCol, Dpi(22), true);

    ImGui::Dummy(ImVec2(w, h));
}

void DrawDriveRow(int idx, float w)
{
    const PagefileDriveInfo& d = g_pf.drives[(size_t)idx];
    bool selected = (g_pfDrive == idx);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float rowH = Dpi(46);
    ImGui::PushID(idx);
    ImGui::InvisibleButton("##driverow", ImVec2(w, rowH));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = p;
    ImVec2 b = ImVec2(p.x + w, p.y + rowH);
    if (selected) dl->AddRectFilled(a, b, ColAlpha(kColBlue, 0.13f), Dpi(6));
    else if (hovered) dl->AddRectFilled(a, b, ColAlpha(kColBorder, 0.35f), Dpi(6));
    if (selected)
        dl->AddRectFilled(ImVec2(a.x, a.y + Dpi(8)), ImVec2(a.x + Dpi(3), b.y - Dpi(8)),
                          Col(kColBlue), Dpi(2));
    dl->AddLine(ImVec2(a.x, b.y - Dpi(1)), ImVec2(b.x, b.y - Dpi(1)),
                ColAlpha(kColBorder, 0.45f), Dpi(1));

    float cy = p.y + rowH * 0.5f;
    ImVec2 dot = ImVec2(p.x + Dpi(18), cy);
    dl->AddCircle(dot, Dpi(7), selected ? Col(kColBlue) : Col(kColFaint), 16, Dpi(1.4f));
    if (selected) dl->AddCircleFilled(dot, Dpi(3.5f), Col(kColBlue), 12);

    char letter[16];
    sprintf_s(letter, sizeof(letter), "%c:", d.letter);
    PushBold();
    dl->AddText(ImVec2(p.x + Dpi(36), p.y + Dpi(12)), Col(kColText), letter);
    PopBold();

    const char* kind = d.driveTypeKnown ? (d.isSsd ? "SSD" : "HDD") : (d.isSsd ? "SSD?" : "HDD?");
    DrawPill(dl, ImVec2(p.x + Dpi(72), p.y + Dpi(13)), kind,
             d.isSsd ? kColCyan : kColAmber, Dpi(20), false);

    char freeText[96];
    FormatMb(d.freeBytes, freeText, sizeof(freeText));
    char totalText[96];
    FormatMb(d.totalBytes, totalText, sizeof(totalText));
    char space[192];
    sprintf_s(space, sizeof(space), "%s free of %s", freeText, totalText);
    dl->AddText(ImVec2(p.x + Dpi(136), p.y + Dpi(15)), Col(kColDim), space);

    float pfX = p.x + w * 0.56f;
    if (pfX < p.x + Dpi(330)) pfX = p.x + Dpi(330);
    char pfText[224];
    if (d.hasPagefile) {
        if (d.systemManaged) {
            sprintf_s(pfText, sizeof(pfText), "System managed by Windows");
        } else {
            char init[48];
            char maxs[48];
            sprintf_s(init, sizeof(init), "%llu MB", d.initialMb);
            sprintf_s(maxs, sizeof(maxs), "%llu MB", d.maxMb);
            sprintf_s(pfText, sizeof(pfText), "%s – %s configured", init, maxs);
        }
    } else {
        sprintf_s(pfText, sizeof(pfText), "No pagefile on this drive");
    }
    dl->AddText(ImVec2(pfX, p.y + Dpi(9)), d.hasPagefile ? Col(kColText) : Col(kColFaint), pfText);
    if (d.hasPagefile) {
        char active[96];
        FormatMb(d.activeBytes, active, sizeof(active));
        char sub[128];
        sprintf_s(sub, sizeof(sub), "Active allocation: %s", active);
        dl->AddText(ImVec2(pfX, p.y + Dpi(25)), Col(kColFaint), sub);
    }

    if (hovered) ImGui::SetTooltip("Select %c:\\ for pagefile configuration", d.letter);
    if (clicked) SelectPagefileDrive(idx);
}

void DrawDriveTable(float w)
{
    if (!g_pfScanned) {
        float h = Dpi(94);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(kColCard), Dpi(8));
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), ColAlpha(kColAmber, 0.3f), Dpi(8), 0, Dpi(1));
        const char* msg = g_pf.error.empty() ? "Scanning fixed drives..." : ToUtf8(g_pf.error).c_str();
        dl->AddText(ImVec2(p.x + Dpi(16), p.y + Dpi(16)), Col(kColAmber), msg);
        dl->AddText(ImVec2(p.x + Dpi(16), p.y + Dpi(42)), Col(kColDim),
                    "Pagefile changes require a fixed drive and administrator access.");
        ImGui::Dummy(ImVec2(w, h));
        return;
    }

    float headerH = Dpi(30);
    float rowsH = (float)g_pf.drives.size() * Dpi(46);
    float cardH = headerH + rowsH + Dpi(16);
    BeginPanel("drivetable", w, cardH, false);
    float avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 hp = ImGui::GetCursorScreenPos();
    dl->AddText(hp, Col(kColFaint), "DRIVE");
    dl->AddText(ImVec2(hp.x + Dpi(136), hp.y), Col(kColFaint), "FREE SPACE");
    float pfX = hp.x + w * 0.56f;
    if (pfX < hp.x + Dpi(330)) pfX = hp.x + Dpi(330);
    dl->AddText(ImVec2(pfX, hp.y), Col(kColFaint), "PAGEFILE");
    ImGui::Dummy(ImVec2(avail, headerH - Dpi(4)));
    for (size_t i = 0; i < g_pf.drives.size(); ++i) {
        DrawDriveRow((int)i, avail);
    }
    ImGui::Dummy(ImVec2(avail, Dpi(2)));
    EndPanel();
}

void DrawPagefileConfigCard(float w)
{
    float allowedMb = g_pfDrive >= 0 ? (float)PagefileMaxAllowedMb(g_pf, g_pfDrive) : 0.0f;
    bool haveDrive = g_pfDrive >= 0 && (size_t)g_pfDrive < g_pf.drives.size();
    bool spaceOk = haveDrive && allowedMb >= 1024.0f;
    bool changed = false;

    BeginPanel("pfconfig", w, 0.0f, false);
    float avail = ImGui::GetContentRegionAvail().x;
    PanelTitle("Configuration", nullptr);
    ImGui::TextColored(kColDim,
                       "Choose a drive and size. The registry is updated immediately; expanding a "
                       "pagefile also raises the live commit limit.");
    ImGui::Spacing();

    if (g_pf.recDrive >= 0 && (size_t)g_pf.recDrive < g_pf.drives.size()) {
        const PagefileDriveInfo& rec = g_pf.drives[(size_t)g_pf.recDrive];
        char recTxt[256];
        sprintf_s(recTxt, sizeof(recTxt),
                  "Recommended for this PC: %c:\\ (fastest drive with most free space), "
                  "%.1f GB initial / %.1f GB maximum.",
                  rec.letter, (double)g_pf.recInitialMb / 1024.0,
                  (double)g_pf.recMaxMb / 1024.0);
        ImGui::TextColored(kColCyan, "%s", recTxt);
    }
    ImGui::Spacing();

    float presetW = Dpi(92);
    float gap = Dpi(8);
    ImGui::TextColored(kColFaint, "QUICK PRESETS");
    ImGui::SameLine(0, Dpi(12));
    bool canPreset = haveDrive && !g_pfApplying;
    if (UiButton("16 GB", ImVec2(presetW, Dpi(28)), BTN_SUBTLE, canPreset && allowedMb >= 16384.0f,
                 "Set a 16 GB initial pagefile with an 8 GB growth ceiling",
                 g_pfApplying ? "Applying..." : nullptr)) {
        SetPagefilePreset(16384ULL);
        changed = true;
    }
    ImGui::SameLine(0, gap);
    if (UiButton("24 GB", ImVec2(presetW, Dpi(28)), BTN_SUBTLE, canPreset && allowedMb >= 24576.0f,
                 "Set a 24 GB initial pagefile with an 8 GB growth ceiling",
                 g_pfApplying ? "Applying..." : nullptr)) {
        SetPagefilePreset(24576ULL);
        changed = true;
    }
    ImGui::SameLine(0, gap);
    if (UiButton("32 GB", ImVec2(presetW, Dpi(28)), BTN_SUBTLE, canPreset && allowedMb >= 32768.0f,
                 "Set a 32 GB initial pagefile with an 8 GB growth ceiling",
                 g_pfApplying ? "Applying..." : nullptr)) {
        SetPagefilePreset(32768ULL);
        changed = true;
    }
    ImGui::SameLine(0, gap);
    if (UiButton("System Managed", ImVec2(Dpi(138), Dpi(28)), BTN_SUBTLE, canPreset,
                 "Let Windows size the pagefile automatically (restart required)",
                 g_pfApplying ? "Applying..." : nullptr)) {
        g_pfSystemManaged = true;
        changed = true;
    }

    ImGui::Spacing();
    float fieldW = Dpi(160);
    ImGui::BeginDisabled(g_pfSystemManaged || !haveDrive || g_pfApplying);
    ImGui::TextColored(kColFaint, "INITIAL (MB)");
    ImGui::SameLine(0, Dpi(12));
    ImGui::SetNextItemWidth(fieldW);
    unsigned long long step = 1024;
    unsigned long long stepFast = 8192;
    ImGui::InputScalar("##initialmb", ImGuiDataType_U64, &g_pfInitialMb, &step, &stepFast,
                       "%llu", ImGuiInputTextFlags_CharsDecimal);
    if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    ImGui::SameLine(0, Dpi(20));
    ImGui::TextColored(kColFaint, "MAXIMUM (MB)");
    ImGui::SameLine(0, Dpi(12));
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputScalar("##maxmb", ImGuiDataType_U64, &g_pfMaxMb, &step, &stepFast,
                       "%llu", ImGuiInputTextFlags_CharsDecimal);
    if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    ImGui::EndDisabled();

    if (changed) {
        ClampPagefileEditors();
        g_dirty = true;
    }

    ImGui::TextColored(kColFaint,
                       "Minimum 1,024 MB. Maximum may not exceed 70%% of free space. "
                       "Values are entered in whole megabytes.");
    if (haveDrive) {
        char allowed[96];
        sprintf_s(allowed, sizeof(allowed), "Allowed maximum on this drive: %.1f GB",
                  (double)allowedMb / 1024.0);
        ImGui::TextColored(spaceOk ? kColDim : kColRed, "%s", allowed);
    }

    ImGui::Spacing();
    bool canApply = haveDrive && !g_pfApplying && spaceOk && g_pf.elevated;
    const char* applyTip = nullptr;
    if (!g_pf.elevated)
        applyTip = "Administrator access is required to change the Windows pagefile.";
    else if (!spaceOk)
        applyTip = "This drive does not have enough free space for a pagefile.";
    else if (g_pfApplying)
        applyTip = "Applying pagefile configuration...";
    else
        applyTip = "Write PagingFiles and expand the live commit limit";

    if (UiButton("Apply Pagefile Configuration", ImVec2(Dpi(232), Dpi(34)), BTN_PRIMARY,
                 canApply, applyTip, g_pfApplying ? "Applying..." : nullptr)) {
        ClampPagefileEditors();
        QueuePagefileApply();
    }
    ImGui::SameLine(0, Dpi(8));
    if (UiButton("Use Recommendation", ImVec2(Dpi(170), Dpi(34)), BTN_SUBTLE,
                 g_pf.recDrive >= 0 && !g_pfApplying,
                 "Load the recommended drive and size for this machine",
                 g_pfApplying ? "Applying..." : nullptr)) {
        UseRecommendedPagefileValues();
    }
    ImGui::SameLine(0, Dpi(8));
    if (UiButton("Rescan", ImVec2(Dpi(86), Dpi(34)), BTN_GHOST, !g_pfApplying,
                 "Rescan drives, free space, SSD type and pagefiles",
                 g_pfApplying ? "Applying..." : nullptr)) {
        g_lastPfScanMs = 0;
        g_pfScanned = false;
        g_pfResultValid = false;
        EnsurePagefileScanned();
    }

    if (!g_pf.elevated && g_pfScanned) {
        ImGui::Spacing();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float bh = Dpi(52);
        ImVec2 a = p;
        ImVec2 b = ImVec2(p.x + avail, p.y + bh);
        dl->AddRectFilled(a, b, ColAlpha(kColAmber, 0.08f), Dpi(8));
        dl->AddRect(a, b, ColAlpha(kColAmber, 0.35f), Dpi(8), 0, Dpi(1));
        float textW = avail - Dpi(220);
        if (textW < Dpi(120)) textW = Dpi(120);
        dl->PushClipRect(ImVec2(a.x + Dpi(12), a.y + Dpi(4)),
                         ImVec2(a.x + Dpi(12) + textW, b.y - Dpi(2)), true);
        dl->AddText(ImVec2(a.x + Dpi(12), a.y + Dpi(8)), Col(kColAmber),
                    Ellipsize("Administrator access is required to write the pagefile configuration.",
                              textW).c_str());
        dl->AddText(ImVec2(a.x + Dpi(12), a.y + Dpi(28)), Col(kColDim),
                    Ellipsize("Restart RamStasis elevated. Frozen applications stay parked and are recovered.",
                              textW).c_str());
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(a.x + avail - Dpi(196), a.y + Dpi(11)));
        if (UiButton("Restart as Administrator", ImVec2(Dpi(184), Dpi(30)), BTN_SUBTLE, !g_pfApplying,
                     "Relaunch RamStasis with the runas verb and UAC elevation",
                     g_pfApplying ? "Applying..." : nullptr)) {
            SaveAppConfig();
            std::wstring err;
            if (RelaunchSelfElevated(err)) {
                LogUi("relaunching elevated for pagefile configuration");
                g_statusLine = "Restarting RamStasis as Administrator...";
                g_forceQuit = true;
                g_quit = true;
                PostQuitMessage(0);
            } else {
                g_statusLine = "Elevation failed: " + ToUtf8(err);
                g_dirty = true;
            }
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(avail, bh));
    }

    if (g_pfResultValid) {
        ImGui::Spacing();
        ImVec4 col = g_pfResultOk ? kColEmerald : kColRed;
        if (g_pfResultOk && g_pfResultReboot) col = kColAmber;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float bh = Dpi(58);
        ImVec2 a = p;
        ImVec2 b = ImVec2(p.x + avail, p.y + bh);
        dl->AddRectFilled(a, b, ColAlpha(col, 0.08f), Dpi(8));
        dl->AddRect(a, b, ColAlpha(col, 0.38f), Dpi(8), 0, Dpi(1));
        dl->AddText(ImVec2(a.x + Dpi(12), a.y + Dpi(8)), Col(col),
                    g_pfResultOk ? "Pagefile configuration saved" : "Pagefile update failed");
        const char* rb = "RESTART REQUIRED";
        float pw = ImGui::CalcTextSize(rb).x + Dpi(18);
        float reserve = g_pfResultReboot ? pw + Dpi(12) : 0.0f;
        std::string msg = Ellipsize(g_pfResultMessage, avail - Dpi(24) - reserve);
        dl->AddText(ImVec2(a.x + Dpi(12), a.y + Dpi(28)), Col(kColDim), msg.c_str());
        if (g_pfResultReboot) {
            DrawPill(dl, ImVec2(a.x + avail - Dpi(12) - pw, a.y + Dpi(9)), rb, kColAmber,
                     Dpi(22), true);
        }
        ImGui::Dummy(ImVec2(avail, bh));
    }

    EndPanel();
}

void DrawSettingsPage(float w, float h)
{
    EnsurePagefileScanned();
    if (g_qaPfResult) {
        g_pfResultValid = true;
        g_pfResultOk = g_qaPfResult != 3;
        g_pfResultReboot = g_qaPfResult == 2;
        g_pfResultMessage = g_qaPfResult == 3
            ? "Access is denied. Restart RamStasis as Administrator and try again."
            : "PagingFiles was updated. The live commit limit was expanded.";
        g_qaPfResult = 0;
    }

    ImGui::BeginChild("settings_scroll", ImVec2(w, h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    float cw = ImGui::GetContentRegionAvail().x;
    if (cw < Dpi(320)) cw = Dpi(320);

    DrawSectionHeader("Settings",
                      "Global defaults apply to every slot that does not carry its own override.");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kColFaint,
                       "Settings are stored in state\\ramstasis-settings.json and take effect immediately.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Spacing();

    float gap = Dpi(12);
    float colW = (cw - gap) * 0.5f;
    float cardH = Dpi(318);

    ImGui::BeginGroup();
    BeginPanel("behavior_left", colW, cardH, false);
    PanelTitle("Freeze Defaults", "Applied to new and inherited slots.");
    bool tree = g_cfg.defaultFreezeTree;
    if (UiToggle("default_tree", "Freeze process tree", "Include child processes.",
                 &tree, true, nullptr)) {
        g_cfg.defaultFreezeTree = tree;
        MarkConfigDirty();
    }
    bool autoThaw = g_cfg.defaultAutoThawOnFocus;
    if (UiToggle("default_autothaw", "Auto-thaw on focus", "Restore parked apps on activation.",
                 &autoThaw, true, nullptr)) {
        g_cfg.defaultAutoThawOnFocus = autoThaw;
        MarkConfigDirty();
    }
    bool watch = g_cfg.watchForegroundWake;
    if (UiToggle("watch_fg", "Watch foreground", "Detect when a parked app is activated.",
                 &watch, true, nullptr)) {
        g_cfg.watchForegroundWake = watch;
        MarkConfigDirty();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kColFaint,
                       "Per-slot overrides live behind the gear button on each workspace card.");
    ImGui::PopTextWrapPos();
    EndPanel();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    BeginPanel("behavior_right", colW, cardH, false);
    PanelTitle("Safety & Close", "Commit warnings and guarded applications.");
    bool warn = g_cfg.warnOnLowCommit;
    if (UiToggle("warn_commit", "Warn on low commit", "Show the low-memory banner.",
                 &warn, true, nullptr)) {
        g_cfg.warnOnLowCommit = warn;
        MarkConfigDirty();
    }

    ImGui::BeginDisabled(!g_cfg.warnOnLowCommit);
    ImGui::TextColored(g_cfg.warnOnLowCommit ? kColDim : kColFaint,
                       "Low commit warning threshold");
    ImGui::SameLine();
    char pctLabel[32];
    sprintf_s(pctLabel, sizeof(pctLabel), "%d%% free", g_cfg.lowCommitWarnPct);
    ImGui::TextColored(kColAmber, "%s", pctLabel);
    int warnPct = g_cfg.lowCommitWarnPct;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0706f, 0.0863f, 0.1098f, 1.0f));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::SliderInt("##lowcommitpct", &warnPct, 5, 30, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
        g_cfg.lowCommitWarnPct = warnPct;
        MarkConfigDirty();
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Banner appears when free commit falls below this percentage.");
    }
    ImGui::EndDisabled();

    bool guard = g_cfg.guardAntiCheat;
    if (UiToggle("guard_ac", "Refuse guarded apps", "Honor anti-cheat protection.",
                 &guard, true, nullptr)) {
        g_cfg.guardAntiCheat = guard;
        MarkConfigDirty();
    }
    bool trayClose = g_cfg.minimizeToTrayOnClose;
    if (UiToggle("close_tray", "Close to tray", "Keep the hotkey active.",
                 &trayClose, true, nullptr)) {
        g_cfg.minimizeToTrayOnClose = trayClose;
        MarkConfigDirty();
    }
    EndPanel();
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Spacing();

    float hkH = Dpi(104);
    BeginPanel("hotkeycard", cw, hkH, false);
    float hkAvail = ImGui::GetContentRegionAvail().x;
    ImVec2 hp = ImGui::GetCursorScreenPos();
    ImDrawList* hdl = ImGui::GetWindowDrawList();
    PushBold();
    hdl->AddText(hp, Col(kColText), "Global swap hotkey");
    PopBold();
    hdl->AddText(ImVec2(hp.x, hp.y + Dpi(22)), Col(kColDim),
                 "Freeze or restore the active app from anywhere.");
    if (g_captureHotkey) {
        hdl->AddText(ImVec2(hp.x, hp.y + Dpi(48)), Col(kColAmber),
                     "Listening... press Ctrl, Alt, Shift or Win plus a key. Esc cancels.");
    } else if (g_hotkeyRegistered) {
        hdl->AddText(ImVec2(hp.x, hp.y + Dpi(48)), Col(kColFaint),
                     "Registered with Windows and active while RamStasis runs.");
    } else {
        hdl->AddText(ImVec2(hp.x, hp.y + Dpi(48)), Col(kColRed),
                     "Registration failed. Choose a different combination.");
    }

    std::string hkLabel = ToUtf8(HotkeyLabel());
    float btnW = Dpi(206);
    ImGui::SetCursorScreenPos(ImVec2(hp.x + hkAvail - btnW, hp.y + Dpi(4)));
    if (UiButton(hkLabel.c_str(), ImVec2(btnW, Dpi(36)),
                 g_captureHotkey ? BTN_SUCCESS : BTN_PRIMARY, !g_captureHotkey,
                 g_captureHotkey ? "Listening for the new shortcut..."
                                 : "Click, then press a new key combination",
                 g_captureHotkey ? "Press keys..." : nullptr)) {
        BeginHotkeyCapture();
    }
    if (g_captureHotkey) {
        ImGui::SetCursorScreenPos(ImVec2(hp.x + hkAvail - Dpi(88), hp.y + Dpi(48)));
        if (UiButton("Cancel", ImVec2(Dpi(88), Dpi(26)), BTN_GHOST, true,
                     "Stop listening and keep the current hotkey")) {
            CancelHotkeyCapture();
        }
    } else {
        ImGui::SetCursorScreenPos(ImVec2(hp.x + hkAvail - Dpi(132), hp.y + Dpi(46)));
        if (UiButton("Reset to default", ImVec2(Dpi(132), Dpi(26)), BTN_GHOST, true,
                     "Restore Ctrl + Alt + S")) {
            g_cfg.swapHotkeyModifiers = MOD_CONTROL | MOD_ALT;
            g_cfg.swapHotkeyVk = 'S';
            bool ok = RegisterConfiguredHotkey();
            MarkConfigDirty();
            g_statusLine = ok ? "Swap hotkey reset to Ctrl + Alt + S"
                              : "Ctrl + Alt + S could not be registered.";
            g_dirty = true;
        }
    }
    ImGui::Dummy(ImVec2(hkAvail, Dpi(2)));
    EndPanel();

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    DrawSectionHeader("Pagefile & Virtual Memory",
                      "Low commit headroom is the usual reason a freeze or allocation fails. Expanding "
                      "the pagefile keeps frozen applications parked safely.");

    if (g_pfScanned) {
        DrawPagefileStatusCard(cw);
    } else {
        float ph = Dpi(78);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ph), Col(kColCard), Dpi(8));
        dl->AddRect(p, ImVec2(p.x + cw, p.y + ph), ColAlpha(kColAmber, 0.32f),
                    Dpi(8), 0, Dpi(1));
        dl->AddText(ImVec2(p.x + Dpi(16), p.y + Dpi(14)), Col(kColAmber),
                    g_pf.error.empty() ? "Scanning fixed drives..." : ToUtf8(g_pf.error).c_str());
        dl->AddText(ImVec2(p.x + Dpi(16), p.y + Dpi(40)), Col(kColDim),
                    "The pagefile manager needs at least one fixed drive with free space.");
        ImGui::SetCursorScreenPos(ImVec2(p.x + cw - Dpi(96), p.y + Dpi(24)));
        if (UiButton("Retry", ImVec2(Dpi(84), Dpi(30)), BTN_SUBTLE, true,
                     "Scan fixed drives again")) {
            g_lastPfScanMs = 0;
            EnsurePagefileScanned();
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(cw, ph));
    }

    ImGui::Spacing();
    DrawDriveTable(cw);
    ImGui::Spacing();
    DrawPagefileConfigCard(cw);

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kColFaint,
                       "Windows may keep the previous pagefile until the next restart when shrinking "
                       "or removing an existing allocation.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(cw, Dpi(12)));

    ImGui::EndChild();
}

}

namespace {

const wchar_t* kWndClass = L"RamStasisWindowClass";
const float kWinW = 780.0f;
const float kWinH = 520.0f;

bool g_sessionEnding = false;

void CenterOnWorkArea(int& x, int& y, int w, int h)
{
    RECT work;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = 0;
        work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    if (w > work.right - work.left) w = work.right - work.left;
    if (h > work.bottom - work.top) h = work.bottom - work.top;
    x = work.left + (work.right - work.left - w) / 2;
    y = work.top + (work.bottom - work.top - h) / 2;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
}

void ClampToMonitorWorkArea(int& x, int& y, int& w, int& h)
{
    RECT target;
    target.left = x;
    target.top = y;
    target.right = x + w;
    target.bottom = y + h;
    HMONITOR mon = MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return;
    const RECT& work = mi.rcWork;
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;
    if (w > workW) w = workW;
    if (h > workH) h = workH;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    if (x + w > work.right) x = work.right - w;
    if (y + h > work.bottom) y = work.bottom - h;
}

void ToggleWindowMaximize(HWND hwnd)
{
    if (!hwnd) return;
    ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    g_dirty = true;
}

int ResizeHitTest(HWND hwnd, LPARAM lParam)
{
    if (IsZoomed(hwnd)) return HTCLIENT;
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    RECT wr;
    GetWindowRect(hwnd, &wr);
    int border = (int)Dpi(6);
    bool left = pt.x >= wr.left && pt.x < wr.left + border;
    bool right = pt.x <= wr.right - 1 && pt.x >= wr.right - border;
    bool top = pt.y >= wr.top && pt.y < wr.top + border;
    bool bottom = pt.y <= wr.bottom - 1 && pt.y >= wr.bottom - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTCLIENT;
}

}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

namespace {

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_wakeMsg != 0 && msg == g_wakeMsg) {
        ShowWindowFromTray();
        return 0;
    }
    if (g_captureHotkey && msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        CancelHotkeyCapture();
        return 0;
    }
    if (g_pickMode && msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        g_statusLine = "Pick cancelled";
        ExitPickMode(true);
        return 0;
    }
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return 1;

    if (g_trayCbMsg != 0 && msg == g_trayCbMsg) {
        UINT ev = LOWORD(lParam);
        if (ev == WM_LBUTTONUP || ev == NIN_SELECT || ev == NIN_KEYSELECT) {
            if (g_hidden || !IsWindowVisible(hWnd)) ShowWindowFromTray();
            else HideToTray();
        } else if (ev == WM_LBUTTONDBLCLK) {
            ShowWindowFromTray();
        } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            if (ev == WM_CONTEXTMENU) {
                int x = GET_X_LPARAM(wParam);
                int y = GET_Y_LPARAM(wParam);
                if (x != 0 || y != 0) {
                    pt.x = x;
                    pt.y = y;
                }
            }
            TrayMenu(&pt);
        }
        return 0;
    }

    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCHITTEST:
        return ResizeHitTest(hWnd, lParam);
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        if (!mmi) break;
        mmi->ptMinTrackSize.x = (LONG)Dpi(720);
        mmi->ptMinTrackSize.y = (LONG)Dpi(480);
        HMONITOR mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        }
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION) {
            ToggleWindowMaximize(hWnd);
            return 0;
        }
        break;
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; }
            if (g_swapChain)
                g_swapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                                           DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        g_dirty = true;
        return 0;
    case WM_HOTKEY:
        if (wParam == (WPARAM)kHotkeyId && !g_captureHotkey) RequestSwapFromHotkey();
        return 0;
    case WM_TIMER:
        if (wParam == (WPARAM)kTimerId) {
            if (!g_hidden) g_dirty = true;
            return 0;
        }
        return 0;
    case WM_DPICHANGED: {
        RECT* prc = (RECT*)lParam;
        g_dpiScale = (float)LOWORD(wParam) / 96.0f;
        if (g_dpiScale < 1.0f) g_dpiScale = 1.0f;
        ImGui::GetStyle().FontScaleDpi = g_dpiScale;
        int x = prc->left;
        int y = prc->top;
        int w = prc->right - prc->left;
        int h = prc->bottom - prc->top;
        ClampToMonitorWorkArea(x, y, w, h);
        SetWindowPos(hWnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        HudCreateFonts();
        g_dirty = true;
        return 0;
    }
    case WM_QUERYENDSESSION:
        if (!g_sessionEnding) {
            g_sessionEnding = true;
            LogUi("WM_QUERYENDSESSION: thawing every frozen tree before shutdown");
            if (g_settingsDirty) SaveAppConfig();
            ThawEverythingForExit();
        }
        return TRUE;
    case WM_ENDSESSION:
        if (wParam) {
            g_sessionEnding = true;
            g_quit = true;
        } else {
            g_sessionEnding = false;
        }
        return 0;
    case WM_ACTIVATE:
        g_dirty = true;
        break;
    case WM_CLOSE:
        RequestClose();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool HasCommandLineArg(const wchar_t* arg)
{
    std::wstring cmd = GetCommandLineW();
    return cmd.find(arg) != std::wstring::npos;
}

int CommandLinePage()
{
    std::wstring cmd = GetCommandLineW();
    size_t pos = cmd.find(L"--page");
    if (pos == std::wstring::npos) return PAGE_WORKSPACE;
    const wchar_t* p = cmd.c_str() + pos + 6;
    while (*p == L' ' || *p == L'=') ++p;
    int page = _wtoi(p);
    if (page < 0 || page >= PAGE_COUNT) return PAGE_WORKSPACE;
    return page;
}

int CommandLineIntArg(const wchar_t* name, int fallback)
{
    std::wstring cmd = GetCommandLineW();
    size_t pos = cmd.find(name);
    if (pos == std::wstring::npos) return fallback;
    const wchar_t* p = cmd.c_str() + pos + wcslen(name);
    while (*p == L' ' || *p == L'=') ++p;
    int v = _wtoi(p);
    if (v < 320 || v > 6000) return fallback;
    return v;
}

int CommandLineIndexArg(const wchar_t* name, int fallback)
{
    std::wstring cmd = GetCommandLineW();
    size_t pos = cmd.find(name);
    if (pos == std::wstring::npos) return fallback;
    const wchar_t* p = cmd.c_str() + pos + wcslen(name);
    while (*p == L' ' || *p == L'=') ++p;
    int v = _wtoi(p);
    if (v < 0 || v > 200) return fallback;
    return v;
}

bool ActivateExistingInstance()
{
    HWND existing = nullptr;
    for (int attempt = 0; attempt < 40; ++attempt) {
        existing = FindWindowW(kWndClass, nullptr);
        if (existing) break;
        Sleep(50);
    }
    if (!existing) return false;
    if (g_wakeMsg == 0) g_wakeMsg = RegisterWindowMessageW(kRamStasisWakeMessage);
    AllowSetForegroundWindow(ASFW_ANY);
    if (g_wakeMsg != 0) PostMessageW(existing, g_wakeMsg, 0, 0);
    ShowWindowAsync(existing, SW_SHOW);
    return true;
}

}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    // Engine CLI first: `RamStasis.exe freeze|thaw|list|guard|... <target>` runs
    // the suspend/trim engine in this same exe and returns before any GUI
    // (window classes, single-instance mutex) is initialized.
    if (FrzEngineCliRequest()) {
        return FrzEngineMain();
    }

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
        SetProcessDpiAwarenessContextProc setCtx =
            (SetProcessDpiAwarenessContextProc)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setCtx) {
            setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            typedef BOOL (WINAPI* SetProcessDPIAwareProc)();
            SetProcessDPIAwareProc setDpi =
                (SetProcessDPIAwareProc)GetProcAddress(user32, "SetProcessDPIAware");
            if (setDpi) setDpi();
        }
    }

    UINT dpi = 96;
    if (user32) {
        typedef UINT (WINAPI* GetDpiForSystemProc)();
        GetDpiForSystemProc getSysDpi =
            (GetDpiForSystemProc)GetProcAddress(user32, "GetDpiForSystem");
        if (getSysDpi) dpi = getSysDpi();
    }
    g_dpiScale = (float)dpi / 96.0f;
    if (g_dpiScale < 1.0f) g_dpiScale = 1.0f;
    LogUi("RamStasis ui starting (dpi=%u scale=%.2f)", dpi, g_dpiScale);

    g_qaMode = HasCommandLineArg(L"--qa");
    g_shotMode = HasCommandLineArg(L"--screenshot");
    g_qaHud = HasCommandLineArg(L"--hud");
    if (g_shotMode || g_qaHud) g_qaMode = true;
    if (g_qaMode) g_page = CommandLinePage();
    g_qaPopup = HasCommandLineArg(L"--popup");
    g_qaHotkey = HasCommandLineArg(L"--hotkey");
    g_qaExpandRow = CommandLineIndexArg(L"--expand", -1);
    g_qaPfPreset = CommandLineIndexArg(L"--pfpreset", -1);
    g_qaPfResult = CommandLineIndexArg(L"--pfresult", 0);
    g_qaPfClamp = HasCommandLineArg(L"--pfclamp");
    if (g_qaHotkey) {
        g_captureHotkey = true;
        g_hkCaptureSince = GetTickCount64();
    }

    g_wakeMsg = RegisterWindowMessageW(kRamStasisWakeMessage);
    if (!g_qaMode) {
        g_instanceMutex = CreateMutexW(nullptr, FALSE, kRamStasisMutexName);
        if (g_instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            bool activated = ActivateExistingInstance();
            CloseHandle(g_instanceMutex);
            g_instanceMutex = nullptr;
            if (!activated) {
                MessageBoxW(nullptr, L"RamStasis is already running but its window could not be activated.",
                            L"RamStasis", MB_OK | MB_ICONINFORMATION);
            }
            if (SUCCEEDED(comHr)) CoUninitialize();
            return 0;
        }
    }

    g_hInst = hInst;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    static const ImWchar kUiGlyphRanges[] = {
        0x0020, 0x00FF,
        0x2000, 0x206F,
        0x2190, 0x21FF,
        0x2500, 0x25FF,
        0
    };

    g_isAdmin = IsElevatedUi();
    g_appIconLarge = LoadRamStasisAppIcon(hInst, 32);
    g_appIconSmall = LoadRamStasisAppIcon(hInst, 16);
    g_trayIcon = LoadRamStasisAppIcon(hInst, 32);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = g_appIconLarge ? g_appIconLarge : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIconSm = g_appIconSmall ? g_appIconSmall : wc.hIcon;
    wc.lpszClassName = kWndClass;
    if (!RegisterClassExW(&wc)) {
        LogUi("RegisterClassExW failed: %lu", (unsigned long)GetLastError());
        ImGui::DestroyContext();
        if (g_appIconLarge) DestroyIcon(g_appIconLarge);
        if (g_appIconSmall) DestroyIcon(g_appIconSmall);
        if (g_trayIcon) DestroyIcon(g_trayIcon);
        if (g_instanceMutex) CloseHandle(g_instanceMutex);
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }

    int logicalW = CommandLineIntArg(L"--width", (int)kWinW);
    int logicalH = CommandLineIntArg(L"--height", (int)kWinH);
    int winW = (int)Dpi((float)logicalW);
    int winH = (int)Dpi((float)logicalH);
    int winX = 0;
    int winY = 0;
    CenterOnWorkArea(winX, winY, winW, winH);

    if (g_shotMode) {
        std::wstring buildDir = UiRootDir() + L"\\build";
        CreateDirectoryW(buildDir.c_str(), nullptr);
        wchar_t name[96];
        swprintf_s(name, L"\\qa_p%d_%dx%d.bmp", g_page, logicalW, logicalH);
        g_shotPath = buildDir + name;
        LogUi("screenshot mode: page=%d size=%dx%d path=%ls", g_page, logicalW, logicalH,
              g_shotPath.c_str());
    }

    DWORD winStyle = WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
    if (!g_shotMode) winStyle |= WS_THICKFRAME;
    g_hwnd = CreateWindowExW(g_qaMode ? WS_EX_TOOLWINDOW : 0, kWndClass,
                             g_qaMode ? L"RamStasis (QA)" : L"RamStasis",
                             winStyle,
                             winX, winY, winW, winH, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) {
        LogUi("CreateWindowExW failed: %lu", (unsigned long)GetLastError());
        UnregisterClassW(kWndClass, hInst);
        ImGui::DestroyContext();
        if (g_appIconLarge) DestroyIcon(g_appIconLarge);
        if (g_appIconSmall) DestroyIcon(g_appIconSmall);
        if (g_trayIcon) DestroyIcon(g_trayIcon);
        if (g_instanceMutex) CloseHandle(g_instanceMutex);
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }

    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIconLarge);
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIconSmall);
    if (g_wakeMsg != 0 && user32) {
        typedef BOOL (WINAPI* ChangeWindowMessageFilterExProc)(HWND, UINT, DWORD, void*);
        ChangeWindowMessageFilterExProc changeFilter =
            (ChangeWindowMessageFilterExProc)GetProcAddress(user32, "ChangeWindowMessageFilterEx");
        if (changeFilter) changeFilter(g_hwnd, g_wakeMsg, MSGFLT_ALLOW, nullptr);
    }

    if (user32) {
        typedef UINT (WINAPI* GetDpiForWindowProc)(HWND);
        GetDpiForWindowProc getWinDpi =
            (GetDpiForWindowProc)GetProcAddress(user32, "GetDpiForWindow");
        if (getWinDpi) {
            UINT winDpi = getWinDpi(g_hwnd);
            if (winDpi > 0 && winDpi != dpi) {
                g_dpiScale = (float)winDpi / 96.0f;
                if (g_dpiScale < 1.0f) g_dpiScale = 1.0f;
                winW = (int)Dpi((float)logicalW);
                winH = (int)Dpi((float)logicalH);
                CenterOnWorkArea(winX, winY, winW, winH);
                SetWindowPos(g_hwnd, nullptr, winX, winY, winW, winH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }

    if (g_qaMode && !g_shotMode) {
        int virtRight = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int virtTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
        winX = virtRight - 24;
        winY = virtTop + 48;
        SetWindowPos(g_hwnd, HWND_BOTTOM, winX, winY, winW, winH, SWP_NOACTIVATE);
    }

    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    ImFontConfig fontCfg;
    fontCfg.OversampleH = 3;
    fontCfg.OversampleV = 1;
    fontCfg.PixelSnapH = true;
    g_fontBody = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f,
                                              &fontCfg, kUiGlyphRanges);
    g_fontBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 16.0f,
                                              &fontCfg, kUiGlyphRanges);
    g_fontHeader = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 20.0f,
                                                &fontCfg, kUiGlyphRanges);
    g_fontSmall = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 11.0f,
                                               &fontCfg, kUiGlyphRanges);
    if (!g_fontBody) g_fontBody = io.Fonts->AddFontDefault(&fontCfg);
    if (!g_fontBold) g_fontBold = g_fontBody;
    if (!g_fontHeader) g_fontHeader = g_fontBold;
    if (!g_fontSmall) g_fontSmall = g_fontBody;

    ApplyUiStyle();
    ImGui::GetStyle().FontScaleDpi = g_dpiScale;

    if (!CreateDeviceD3D(g_hwnd, g_warpDevice)) {
        LogUi("D3D11 device creation failed");
        CleanupDeviceD3D();
        DestroyWindow(g_hwnd);
        UnregisterClassW(kWndClass, hInst);
        ImGui::DestroyContext();
        if (g_appIconLarge) DestroyIcon(g_appIconLarge);
        if (g_appIconSmall) DestroyIcon(g_appIconSmall);
        if (g_trayIcon) DestroyIcon(g_trayIcon);
        if (g_instanceMutex) CloseHandle(g_instanceMutex);
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }
    LogUi("dx11 device created (warp=%d)", (int)g_warpDevice);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

    g_logoTexture = LoadPngResourceTexture(hInst, IDR_RAMSTASIS_LOGO, 128);
    if (!g_logoTexture) LogUi("logo resource unavailable; using vector fallback");

    LoadAppConfig();

    InitializeCriticalSection(&g_cmdLock);
    InitializeCriticalSection(&g_resLock);
    g_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_cmdEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_workerStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    g_trayCbMsg = RegisterWindowMessageW(L"RamStasisTrayCallback");
    if (!g_qaMode) TrayAdd();

    g_hotkeyRegistered = false;
    if (!g_qaMode && !RegisterConfiguredHotkey()) {
        g_hotkeyRetriesLeft = 6;
        g_hotkeyRetryAt = GetTickCount64() + 1200;
    }

    HudCreate(hInst);
    if (g_qaHud && g_hudWnd) {
        HudSetText(L"Visual Studio Code - frzui_imgui.cpp",
                   L"Code.exe  \u00B7  PID 4242",
                   L"root 4242  \u00B7  3 windows  \u00B7  41 processes  \u00B7  tree 382 MB",
                   L"Click to target this application  \u00B7  Esc to cancel", false);
        int hudW = (int)Dpi(430);
        int hudH = (int)Dpi(116);
        SetWindowPos(g_hudWnd, HWND_TOPMOST, 80, 80, hudW, hudH, SWP_NOACTIVATE);
        LogUi("hud QA: hidden sample %dx%d", hudW, hudH);
        std::wstring buildDir = UiRootDir() + L"\\build";
        CreateDirectoryW(buildDir.c_str(), nullptr);
        std::wstring hudPath = buildDir + L"\\qa_hud.bmp";
        std::string hudErr;
        if (SaveHudBmp(hudPath, hudErr)) LogUi("hud QA saved: %ls", hudPath.c_str());
        else LogUi("hud QA failed (%s): %ls", hudErr.c_str(), hudPath.c_str());
        g_quit = true;
    }

    if (g_shotMode || g_qaHud) {

    } else if (g_qaMode) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hwnd, HWND_TOP, winX, winY, winW, winH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(g_hwnd);
    } else {
        ShowWindow(g_hwnd, SW_SHOW);
        SetWindowPos(g_hwnd, HWND_TOP, winX, winY, winW, winH, SWP_SHOWWINDOW);
        UpdateWindow(g_hwnd);
        SetForegroundWindow(g_hwnd);
    }

    StartupRecovery();
    g_telValid = PollTelemetry(g_tel);
    g_lastTelMs = GetTickCount64();
    g_lastFgMs = g_lastTelMs;

    std::thread worker(WorkerMain);
    SetTimer(g_hwnd, kTimerId, 1000, nullptr);

    RECT clientRect;
    GetClientRect(g_hwnd, &clientRect);
    float cw = (float)(clientRect.right - clientRect.left);
    float ch = (float)(clientRect.bottom - clientRect.top);
    int shotFrame = 0;

    while (!g_quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_quit = true;
            g_dirty = true;
        }
        if (g_quit) break;

        DrainResults();
        TickConfigSave();
        PollHotkeyCapture();

        if (g_hidden) {
            if (g_pickMode) ExitPickMode(true);
        } else {
            UpdatePickMode();
        }

        ULONGLONG now = GetTickCount64();
        if (g_hotkeyRetriesLeft > 0 && now >= g_hotkeyRetryAt) {
            --g_hotkeyRetriesLeft;
            g_hotkeyRetryAt = now + 1500;
            if (RegisterConfiguredHotkey()) {
                g_hotkeyRetriesLeft = 0;
                g_statusLine = "Swap hotkey registered after restart";
                g_dirty = true;
            }
        }
        if (now - g_lastTelMs >= 1000) {
            g_lastTelMs = now;
            TickSlotsAndTelemetry();
            g_dirty = true;
        }
        if (now - g_lastFgMs >= 150) {
            g_lastFgMs = now;
            CheckAutoThaw();
        }

        if (!g_dirty) {
            if (g_shotMode) {
                g_dirty = true;
            } else {
                if (!g_pickMode) {
                    ++g_idleTicks;
                    if (g_hidden || g_idleTicks >= 30) {
                        if (g_idleTicks >= 30) g_idleTicks = 0;
                        PROCESS_MEMORY_COUNTERS_EX pmc;
                        ZeroMemory(&pmc, sizeof(pmc));
                        pmc.cb = sizeof(pmc);
                        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                                 (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)) &&
                            pmc.WorkingSetSize > (SIZE_T)(12ull * 1024 * 1024)) {
                            SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
                        }
                    }
                } else {
                    g_idleTicks = 0;
                }

                DWORD waitMs = g_pickMode ? 30 : 1000;
                MsgWaitForMultipleObjects(1, &g_wakeEvent, FALSE, waitMs, QS_ALLINPUT);
                continue;
            }
        }
        g_dirty = false;

        if (g_hidden || IsIconic(g_hwnd)) continue;

        GetClientRect(g_hwnd, &clientRect);
        cw = (float)(clientRect.right - clientRect.left);
        ch = (float)(clientRect.bottom - clientRect.top);
        if (cw < 1.0f || ch < 1.0f) continue;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(cw, ch));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kColBg);
        ImGui::Begin("RamStasis", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse);

        DrawHeader(cw);
        ImGui::SetCursorPos(ImVec2(0, Dpi(kHeaderH)));
        DrawNav(cw);

        float contentX = Dpi(16);
        float contentY = Dpi(kHeaderH + kNavH + 14);
        float bannerH = DrawCommitBanner(cw, contentY);
        if (bannerH > 0.0f) contentY += bannerH + Dpi(2);

        float contentH = ch - Dpi(kFooterH) - contentY - Dpi(10);
        if (contentH < Dpi(140)) contentH = Dpi(140);
        float contentW = cw - contentX * 2.0f;
        if (contentW < Dpi(200)) contentW = Dpi(200);

        ImGui::SetCursorPos(ImVec2(contentX, contentY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("pagecontent", ImVec2(contentW, contentH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_None);
        switch (g_page) {
        case PAGE_WORKSPACE: DrawWorkspacePage(contentW, contentH); break;
        case PAGE_EXPLORER:  DrawExplorerPage(contentW, contentH); break;
        case PAGE_SETTINGS:  DrawSettingsPage(contentW, contentH); break;
        default: break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        DrawFooter(cw, ch);
        DrawExitModal();

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::Render();
        g_pd3dContext->OMSetRenderTargets(1, &g_mainRTV, nullptr);
        const float clear[4] = { kColBg.x, kColBg.y, kColBg.z, 1.0f };
        g_pd3dContext->ClearRenderTargetView(g_mainRTV, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (g_shotMode) {
            if (++shotFrame >= 3) {
                std::string shotErr;
                if (SaveBackbufferBmp(g_shotPath, shotErr)) {
                    LogUi("screenshot saved: %ls", g_shotPath.c_str());
                } else {
                    LogUi("screenshot failed (%s): %ls", shotErr.c_str(), g_shotPath.c_str());
                }
                g_quit = true;
            } else {
                g_dirty = true;
            }
        } else {
            g_swapChain->Present(1, 0);
        }
    }

    KillTimer(g_hwnd, kTimerId);
    SetEvent(g_workerStop);
    if (worker.joinable()) worker.join();
    if (g_settingsDirty) SaveAppConfig();

    TrayRemove();
    if (g_trayIcon) {
        DestroyIcon(g_trayIcon);
        g_trayIcon = nullptr;
    }
    UnregisterHotKey(g_hwnd, kHotkeyId);
    HudDestroy(hInst);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_logoTexture) {
        g_logoTexture->Release();
        g_logoTexture = nullptr;
    }
    ReleaseIconCache();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(kWndClass, hInst);
    if (g_appIconLarge) {
        DestroyIcon(g_appIconLarge);
        g_appIconLarge = nullptr;
    }
    if (g_appIconSmall) {
        DestroyIcon(g_appIconSmall);
        g_appIconSmall = nullptr;
    }

    if (g_wakeEvent) CloseHandle(g_wakeEvent);
    if (g_cmdEvent) CloseHandle(g_cmdEvent);
    if (g_workerStop) CloseHandle(g_workerStop);
    if (g_instanceMutex) CloseHandle(g_instanceMutex);
    g_instanceMutex = nullptr;
    DeleteCriticalSection(&g_cmdLock);
    DeleteCriticalSection(&g_resLock);

    LogUi("exit");
    if (SUCCEEDED(comHr)) CoUninitialize();
    return 0;
}
