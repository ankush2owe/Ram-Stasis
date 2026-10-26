// RamStasis — Windows Process Stasis & Pagefile Memory Manager
// SPDX-License-Identifier: MIT

#include "frzui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

struct Rgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

Rgba Color(uint32_t rgb, float alpha = 1.0f)
{
    Rgba c;
    c.r = (float)((rgb >> 16) & 0xFF) / 255.0f;
    c.g = (float)((rgb >> 8) & 0xFF) / 255.0f;
    c.b = (float)(rgb & 0xFF) / 255.0f;
    c.a = alpha;
    return c;
}

void Blend(std::vector<Rgba>& buf, int w, int h, int x, int y, const Rgba& c)
{
    if (x < 0 || y < 0 || x >= w || y >= h || c.a <= 0.0f) return;
    Rgba& d = buf[(size_t)y * (size_t)w + (size_t)x];
    float sa = c.a;
    float da = d.a;
    float oa = sa + da * (1.0f - sa);
    if (oa <= 0.0f) {
        d = Rgba();
        return;
    }
    d.r = (c.r * sa + d.r * da * (1.0f - sa)) / oa;
    d.g = (c.g * sa + d.g * da * (1.0f - sa)) / oa;
    d.b = (c.b * sa + d.b * da * (1.0f - sa)) / oa;
    d.a = oa;
}

void FillRect(std::vector<Rgba>& buf, int w, int h,
              float x0, float y0, float x1, float y1, const Rgba& c)
{
    int ax = (int)std::floor(x0);
    int ay = (int)std::floor(y0);
    int bx = (int)std::ceil(x1);
    int by = (int)std::ceil(y1);
    for (int y = ay; y < by; ++y) {
        for (int x = ax; x < bx; ++x) Blend(buf, w, h, x, y, c);
    }
}

void FillRoundRect(std::vector<Rgba>& buf, int w, int h,
                   float x0, float y0, float x1, float y1, float radius, const Rgba& c)
{
    float r = radius;
    if (r < 0.0f) r = 0.0f;
    float maxR = std::min(x1 - x0, y1 - y0) * 0.5f;
    if (r > maxR) r = maxR;
    for (int y = (int)std::floor(y0); y < (int)std::ceil(y1); ++y) {
        float py = (float)y + 0.5f;
        for (int x = (int)std::floor(x0); x < (int)std::ceil(x1); ++x) {
            float px = (float)x + 0.5f;
            float cx = px;
            if (cx < x0 + r) cx = x0 + r;
            if (cx > x1 - r) cx = x1 - r;
            float cy = py;
            if (cy < y0 + r) cy = y0 + r;
            if (cy > y1 - r) cy = y1 - r;
            float dx = px - cx;
            float dy = py - cy;
            if (dx * dx + dy * dy <= r * r) Blend(buf, w, h, x, y, c);
        }
    }
}

bool PointInPolygon(const float* pts, int count, float x, float y)
{
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        float xi = pts[i * 2];
        float yi = pts[i * 2 + 1];
        float xj = pts[j * 2];
        float yj = pts[j * 2 + 1];
        if (((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / ((yj - yi) == 0.0f ? 1.0f : (yj - yi)) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

void FillPolygon(std::vector<Rgba>& buf, int w, int h, const float* pts, int count,
                 float cx, float cy, float radius, const Rgba& c)
{
    int ax = (int)std::floor(cx - radius);
    int ay = (int)std::floor(cy - radius);
    int bx = (int)std::ceil(cx + radius);
    int by = (int)std::ceil(cy + radius);
    for (int y = ay; y < by; ++y) {
        for (int x = ax; x < bx; ++x) {
            if (PointInPolygon(pts, count, (float)x + 0.5f, (float)y + 0.5f))
                Blend(buf, w, h, x, y, c);
        }
    }
}

void HexPoints(float cx, float cy, float radius, float* pts)
{
    static const float kCos[6] = { 0.0f, 0.8660254f, 0.8660254f, 0.0f, -0.8660254f, -0.8660254f };
    static const float kSin[6] = { -1.0f, -0.5f, 0.5f, 1.0f, 0.5f, -0.5f };
    for (int i = 0; i < 6; ++i) {
        pts[i * 2] = cx + kCos[i] * radius;
        pts[i * 2 + 1] = cy + kSin[i] * radius;
    }
}

void FillCircle(std::vector<Rgba>& buf, int w, int h, float cx, float cy, float r,
                const Rgba& c)
{
    for (int y = (int)std::floor(cy - r); y < (int)std::ceil(cy + r); ++y) {
        for (int x = (int)std::floor(cx - r); x < (int)std::ceil(cx + r); ++x) {
            float dx = (float)x + 0.5f - cx;
            float dy = (float)y + 0.5f - cy;
            if (dx * dx + dy * dy <= r * r) Blend(buf, w, h, x, y, c);
        }
    }
}

Rgba Downsample(const std::vector<Rgba>& src, int sw, int sx, int sy, int factor)
{
    float sa = 0.0f, sr = 0.0f, sg = 0.0f, sb = 0.0f;
    for (int yy = 0; yy < factor; ++yy) {
        for (int xx = 0; xx < factor; ++xx) {
            int x = sx * factor + xx;
            int y = sy * factor + yy;
            const Rgba& c = src[(size_t)y * (size_t)sw + (size_t)x];
            sa += c.a;
            sr += c.r * c.a;
            sg += c.g * c.a;
            sb += c.b * c.a;
        }
    }
    int n = factor * factor;
    Rgba out;
    out.a = sa / (float)n;
    if (sa > 0.0001f) {
        out.r = sr / sa;
        out.g = sg / sa;
        out.b = sb / sa;
    }
    return out;
}

}

HICON CreateRamStasisIcon(int sizePx, bool hasFrozenApps)
{
    if (sizePx < 8) sizePx = 8;
    if (sizePx > 256) sizePx = 256;
    int factor = sizePx <= 64 ? 4 : 2;
    int sw = sizePx * factor;

    std::vector<Rgba> canvas((size_t)sw * (size_t)sw);
    float cx = (float)sw * 0.5f;
    float cy = (float)sw * 0.5f;
    float radius = (float)sw * 0.455f;
    float border = std::max(1.0f, (float)sw * (1.8f / 256.0f));

    uint32_t borderColor = 0x06B6D4;
    uint32_t chipFill = 0x0D1520;
    uint32_t chipStroke = 0x294257;
    uint32_t coreColor = hasFrozenApps ? 0x22D3EE : 0x10B981;
    uint32_t glowColor = hasFrozenApps ? 0x06B6D4 : 0x3B82F6;

    float outer[12];
    float inner[12];
    HexPoints(cx, cy, radius, outer);
    HexPoints(cx, cy, radius - border, inner);

    FillPolygon(canvas, sw, sw, outer, 6, cx, cy, radius, Color(0x07101C));
    FillPolygon(canvas, sw, sw, outer, 6, cx, cy, radius, Color(borderColor));
    FillPolygon(canvas, sw, sw, inner, 6, cx, cy, radius - border, Color(chipFill));

    float glowRadius = radius * 1.03f;
    float glow[12];
    HexPoints(cx, cy, glowRadius, glow);
    for (int i = 0; i < 6; ++i) {
        Rgba gc = Color(borderColor, 0.12f);
        Blend(canvas, sw, sw, (int)glow[i * 2], (int)glow[i * 2 + 1], gc);
    }

    Rgba pinCol = Color(0x38BDF8, 0.96f);
    float pinW = (float)sw * 0.028f;
    float chipHalf = (float)sw * 0.19f;
    float pinStart = chipHalf + (float)sw * 0.025f;
    float pinLen = radius * 0.55f - pinStart;
    if (pinLen < pinW) pinLen = pinW;
    for (int i = -1; i <= 1; ++i) {
        float y = cy + (float)i * (float)sw * 0.085f;
        FillRect(canvas, sw, sw, cx - pinStart - pinLen, y - pinW * 0.5f,
                 cx - pinStart, y + pinW * 0.5f, pinCol);
        FillRect(canvas, sw, sw, cx + pinStart, y - pinW * 0.5f,
                 cx + pinStart + pinLen, y + pinW * 0.5f, pinCol);
        FillCircle(canvas, sw, sw, cx - pinStart - pinLen, y, pinW * 0.9f, pinCol);
        FillCircle(canvas, sw, sw, cx + pinStart + pinLen, y, pinW * 0.9f, pinCol);
    }

    Rgba chipBg = Color(0x111C2B);
    Rgba chipBd = Color(chipStroke, 0.95f);
    float chipX = cx - chipHalf;
    float chipY = cy - chipHalf;
    float chipSize = chipHalf * 2.0f;
    FillRoundRect(canvas, sw, sw, chipX, chipY, chipX + chipSize, chipY + chipSize,
                  (float)sw * 0.035f, chipBd);
    float inset = border * 1.4f;
    FillRoundRect(canvas, sw, sw, chipX + inset, chipY + inset,
                  chipX + chipSize - inset, chipY + chipSize - inset,
                  (float)sw * 0.028f, chipBg);

    Rgba glowCore = Color(glowColor, 0.24f);
    FillCircle(canvas, sw, sw, cx, cy, (float)sw * 0.155f, glowCore);
    Rgba core = Color(coreColor, 0.98f);
    float barW = (float)sw * 0.055f;
    float barH = (float)sw * 0.16f;
    float gap = (float)sw * 0.045f;
    FillRoundRect(canvas, sw, sw, cx - gap * 0.5f - barW, cy - barH * 0.5f,
                  cx - gap * 0.5f, cy + barH * 0.5f, barW * 0.5f, core);
    FillRoundRect(canvas, sw, sw, cx + gap * 0.5f, cy - barH * 0.5f,
                  cx + gap * 0.5f + barW, cy + barH * 0.5f, barW * 0.5f, core);

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sizePx;
    bi.bmiHeader.biHeight = -sizePx;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !color || !bits) {
        if (color) DeleteObject(color);
        if (dc) DeleteDC(dc);
        return CreateRamStasisIcon(16, hasFrozenApps);
    }

    uint32_t* px = static_cast<uint32_t*>(bits);
    for (int y = 0; y < sizePx; ++y) {
        for (int x = 0; x < sizePx; ++x) {
            Rgba c = Downsample(canvas, sw, x, y, factor);
            uint32_t a = (uint32_t)(std::min(1.0f, std::max(0.0f, c.a)) * 255.0f + 0.5f);
            uint32_t r = (uint32_t)(std::min(1.0f, std::max(0.0f, c.r)) * 255.0f + 0.5f);
            uint32_t g = (uint32_t)(std::min(1.0f, std::max(0.0f, c.g)) * 255.0f + 0.5f);
            uint32_t b = (uint32_t)(std::min(1.0f, std::max(0.0f, c.b)) * 255.0f + 0.5f);
            px[(size_t)y * (size_t)sizePx + (size_t)x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    int maskStride = ((sizePx + 15) / 16) * 2;
    std::vector<BYTE> maskBits((size_t)maskStride * (size_t)sizePx, 0);
    HBITMAP mask = CreateBitmap(sizePx, sizePx, 1, 1, maskBits.data());

    ICONINFO ii;
    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(mask);
    DeleteObject(color);
    DeleteDC(dc);
    return icon;
}

HICON LoadRamStasisAppIcon(HINSTANCE hInst, int sizePx)
{
    HICON icon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_RAMSTASIS), IMAGE_ICON,
                                   sizePx, sizePx, LR_DEFAULTCOLOR);
    if (!icon) icon = CreateRamStasisIcon(sizePx, false);
    return icon;
}
