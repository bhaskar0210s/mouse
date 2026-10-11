// Renders Mouse's app icon into an .iconset directory (see `make icon`): a light, ergonomic
// mouse seen from above on a graphite tile, with a blue scroll wheel. Drawn in code, no assets.
//
//   icon <dir.iconset>          all icon sizes (the app)
//   icon --small <dir.iconset>  up to 128 px (the agent, only shown in permission lists and prompts)
//   icon --preview <file.png>   one 1024 px image
//   icon --pack <dir.iconset> <file.icns>
//                               put the iconset's PNGs back into an .icns made by iconutil, which
//                               re-encodes them about twice as large

#include <CoreGraphics/CoreGraphics.h>
#include <zlib.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

CGColorRef color(int rgb, double alpha = 1) {
    return CGColorCreateSRGB(((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0, alpha);
}

void fill(CGContextRef g, CGPathRef path, int rgb, double alpha = 1) {
    CGColorRef c = color(rgb, alpha);
    CGContextAddPath(g, path);
    CGContextSetFillColorWithColor(g, c);
    CGContextFillPath(g);
    CGColorRelease(c);
}

void gradient(CGContextRef g, CGPathRef clip, int top, int bottom, double y0, double y1) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGColorRef c0 = color(top), c1 = color(bottom);
    const void* colors[] = {c0, c1};
    CFArrayRef arr = CFArrayCreate(nullptr, colors, 2, &kCFTypeArrayCallBacks);
    CGGradientRef grad = CGGradientCreateWithColors(space, arr, nullptr);
    CGContextSaveGState(g);
    CGContextAddPath(g, clip);
    CGContextClip(g);
    CGContextDrawLinearGradient(g, grad, CGPointMake(512, y0), CGPointMake(512, y1), 0);
    CGContextRestoreGState(g);
    CGGradientRelease(grad);
    CFRelease(arr);
    CGColorRelease(c0);
    CGColorRelease(c1);
    CGColorSpaceRelease(space);
}

void put32(std::string& s, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) s += static_cast<char>((v >> shift) & 255);
}

void chunk(std::string& out, const char* type, const std::string& data) {
    put32(out, static_cast<uint32_t>(data.size()));
    std::string body = std::string(type, 4) + data;
    out += body;
    put32(out, static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()))));
}

// PNG of straight (not premultiplied) RGBA rows. Each row uses the filter with the smallest sum of
// residuals and everything is deflated at the highest level: about half the size ImageIO writes.
std::string encodePng(const std::vector<uint8_t>& rgba, int w, int h) {
    const size_t stride = static_cast<size_t>(w) * 4;
    std::string raw;
    std::vector<uint8_t> prev(stride, 0), cand(stride), best(stride);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = rgba.data() + y * stride;
        long bestSum = LONG_MAX;
        int bestType = 0;
        for (int type = 0; type < 5; type++) {
            long sum = 0;
            for (size_t i = 0; i < stride; i++) {
                int a = i >= 4 ? row[i - 4] : 0, b = prev[i], c = i >= 4 ? prev[i - 4] : 0, pred = 0;
                if (type == 1) pred = a;
                else if (type == 2) pred = b;
                else if (type == 3) pred = (a + b) / 2;
                else if (type == 4) {
                    int pa = std::abs(b - c), pb = std::abs(a - c), pc = std::abs(a + b - 2 * c);
                    pred = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                }
                cand[i] = static_cast<uint8_t>(row[i] - pred);
                sum += cand[i] < 128 ? cand[i] : 256 - cand[i];
            }
            if (sum < bestSum) {
                bestSum = sum;
                bestType = type;
                best = cand;
            }
        }
        raw += static_cast<char>(bestType);
        raw.append(reinterpret_cast<const char*>(best.data()), stride);
        std::memcpy(prev.data(), row, stride);
    }
    uLongf size = compressBound(static_cast<uLong>(raw.size()));
    std::string deflated(size, '\0');
    compress2(reinterpret_cast<Bytef*>(deflated.data()), &size, reinterpret_cast<const Bytef*>(raw.data()),
              static_cast<uLong>(raw.size()), 9);
    deflated.resize(size);
    std::string ihdr;
    put32(ihdr, static_cast<uint32_t>(w));
    put32(ihdr, static_cast<uint32_t>(h));
    ihdr += std::string("\x08\x06\x00\x00\x00", 5);  // 8 bits per channel, RGBA, no interlace
    std::string out("\x89PNG\r\n\x1a\n", 8);
    chunk(out, "IHDR", ihdr);
    chunk(out, "sRGB", std::string(1, '\0'));
    chunk(out, "IDAT", deflated);
    chunk(out, "IEND", "");
    return out;
}

// The mouse seen from above (y grows upwards): narrow at the front, wide palm, a thumb rest on
// the left.
CGPathRef bodyPath() {
    CGMutablePathRef p = CGPathCreateMutable();
    CGPathMoveToPoint(p, nullptr, 520, 812);
    CGPathAddCurveToPoint(p, nullptr, 618, 812, 676, 742, 690, 622);   // front right
    CGPathAddCurveToPoint(p, nullptr, 706, 488, 706, 360, 662, 270);   // right flank
    CGPathAddCurveToPoint(p, nullptr, 622, 190, 548, 168, 480, 176);   // palm, right to centre
    CGPathAddCurveToPoint(p, nullptr, 410, 184, 330, 214, 300, 286);   // palm, centre to thumb rest
    CGPathAddCurveToPoint(p, nullptr, 276, 344, 300, 398, 352, 430);   // thumb rest
    CGPathAddCurveToPoint(p, nullptr, 378, 446, 382, 476, 378, 520);   // thumb pocket
    CGPathAddCurveToPoint(p, nullptr, 370, 640, 410, 812, 520, 812);   // front left
    CGPathCloseSubpath(p);
    return p;
}

void render(int pixels, const std::string& file) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef g = CGBitmapContextCreate(nullptr, pixels, pixels, 8, 0, space, kCGImageAlphaPremultipliedLast);
    CGContextScaleCTM(g, pixels / 1024.0, pixels / 1024.0);

    // macOS icon grid: an 824 pt rounded square centred in a 1024 pt canvas.
    CGPathRef tile = CGPathCreateWithRoundedRect(CGRectMake(100, 100, 824, 824), 185, 185, nullptr);
    gradient(g, tile, 0x3A3A3C, 0x1C1C1E, 924, 100);

    // Soft shadow under the mouse.
    CGPathRef body = bodyPath();
    CGContextSaveGState(g);
    CGColorRef shadow = color(0x000000, 0.45);
    CGContextSetShadowWithColor(g, CGSizeMake(0, -18), 40, shadow);
    fill(g, body, 0xE5E5EA);
    CGContextRestoreGState(g);
    CGColorRelease(shadow);
    gradient(g, body, 0xFFFFFF, 0xD1D1D6, 812, 176);

    // The seam between the two main buttons.
    CGMutablePathRef seam = CGPathCreateMutable();
    CGPathMoveToPoint(seam, nullptr, 528, 812);
    CGPathAddCurveToPoint(seam, nullptr, 530, 760, 532, 690, 534, 600);
    CGPathRef seamStroke = CGPathCreateCopyByStrokingPath(seam, nullptr, 6, kCGLineCapRound, kCGLineJoinRound, 0);
    fill(g, seamStroke, 0xAEAEB2);
    CGPathRelease(seamStroke);
    CGPathRelease(seam);

    // The scroll wheel in its slot.
    CGPathRef slot = CGPathCreateWithRoundedRect(CGRectMake(498, 610, 72, 150), 36, 36, nullptr);
    fill(g, slot, 0x2C2C2E);
    CGPathRef wheel = CGPathCreateWithRoundedRect(CGRectMake(512, 626, 44, 118), 22, 22, nullptr);
    gradient(g, wheel, 0x5AC8FA, 0x0A84FF, 744, 626);
    CGPathRelease(wheel);
    CGPathRelease(slot);

    // The small button behind the wheel.
    CGPathRef top = CGPathCreateWithRoundedRect(CGRectMake(516, 552, 36, 40), 14, 14, nullptr);
    fill(g, top, 0xC7C7CC);
    CGPathRelease(top);

    // The context holds premultiplied RGBA, top row first; PNG wants straight alpha.
    const auto* data = static_cast<const uint8_t*>(CGBitmapContextGetData(g));
    size_t bytesPerRow = CGBitmapContextGetBytesPerRow(g);
    std::vector<uint8_t> rgba(static_cast<size_t>(pixels) * pixels * 4);
    for (int y = 0; y < pixels; y++) {
        for (int x = 0; x < pixels; x++) {
            const uint8_t* p = data + y * bytesPerRow + x * 4;
            uint8_t* q = rgba.data() + (static_cast<size_t>(y) * pixels + x) * 4;
            int alpha = p[3];
            for (int k = 0; k < 3; k++) q[k] = alpha ? static_cast<uint8_t>(std::min(255, (p[k] * 255 + alpha / 2) / alpha)) : 0;
            q[3] = static_cast<uint8_t>(alpha);
        }
    }
    std::string png = encodePng(rgba, pixels, pixels);
    if (FILE* f = std::fopen(file.c_str(), "wb")) {
        std::fwrite(png.data(), 1, png.size(), f);
        std::fclose(f);
    }
    CGPathRelease(body);
    CGPathRelease(tile);
    CGContextRelease(g);
    CGColorSpaceRelease(space);
}

bool readAll(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

int pack(const std::string& dir, const std::string& icns) {
    static const struct {
        const char* type;
        const char* file;
    } kPng[] = {{"ic11", "icon_16x16@2x"}, {"ic12", "icon_32x32@2x"},   {"ic07", "icon_128x128"},
                {"ic13", "icon_128x128@2x"}, {"ic08", "icon_256x256"}, {"ic14", "icon_256x256@2x"},
                {"ic09", "icon_512x512"}, {"ic10", "icon_512x512@2x"}};
    std::string in;
    if (!readAll(icns, in) || in.size() < 8 || in.compare(0, 4, "icns") != 0) return 1;
    auto be32 = [&in](size_t at) {
        return static_cast<uint32_t>(static_cast<uint8_t>(in[at])) << 24 | static_cast<uint32_t>(static_cast<uint8_t>(in[at + 1])) << 16 |
               static_cast<uint32_t>(static_cast<uint8_t>(in[at + 2])) << 8 | static_cast<uint8_t>(in[at + 3]);
    };
    std::string entries;
    for (size_t at = 8; at + 8 <= in.size();) {
        std::string type = in.substr(at, 4);
        uint32_t size = be32(at + 4);
        if (size < 8 || at + size > in.size()) return 1;
        std::string payload = in.substr(at + 8, size - 8);
        at += size;
        if (type == "TOC ") continue;  // optional, and it would list the old sizes
        for (const auto& k : kPng) {
            std::string png;
            if (type == k.type && readAll(dir + "/" + k.file + ".png", png) && png.compare(0, 4, "\x89PNG") == 0) payload = png;
        }
        entries += type;
        put32(entries, static_cast<uint32_t>(payload.size() + 8));
        entries += payload;
    }
    std::string out = "icns";
    put32(out, static_cast<uint32_t>(entries.size() + 8));
    out += entries;
    FILE* f = std::fopen(icns.c_str(), "wb");
    if (!f) return 1;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    return std::fclose(f) == 0 && ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--pack") == 0) return pack(argv[2], argv[3]);
    if (argc == 3 && std::strcmp(argv[1], "--preview") == 0) {
        render(1024, argv[2]);
        return 0;
    }
    bool small = argc == 3 && std::strcmp(argv[1], "--small") == 0;
    if (argc < 2 || (argc == 3 && !small)) {
        std::fprintf(stderr, "usage: icon [--small] <dir.iconset> | icon --preview <file.png>\n");
        return 1;
    }
    const std::string dir = argv[argc - 1];
    // Sizes above 256 pt @2x are left out on purpose: Finder scales up, and the app stays small.
    const struct {
        int points, scale;
    } sizes[] = {{16, 1}, {16, 2}, {32, 1}, {32, 2}, {128, 1}, {128, 2}, {256, 1}, {256, 2}};
    for (const auto& size : sizes) {
        if (small && size.points * size.scale > 128) continue;
        char name[64];
        std::snprintf(name, sizeof(name), "/icon_%dx%d%s.png", size.points, size.points, size.scale == 2 ? "@2x" : "");
        render(size.points * size.scale, dir + name);
    }
    return 0;
}
