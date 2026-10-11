// Renders Mouse's app icon into an .iconset directory (see `make icon`): a light, ergonomic
// mouse seen from above on a graphite tile, with a blue scroll wheel. Drawn in code, no assets.
//
//   icon <dir.iconset>        all icon sizes
//   icon --preview <file.png> one 1024 px image

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstdio>
#include <cstring>
#include <string>

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

    CGImageRef image = CGBitmapContextCreateImage(g);
    CFStringRef path = CFStringCreateWithCString(nullptr, file.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path, kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
    CGImageDestinationAddImage(dest, image, nullptr);
    CGImageDestinationFinalize(dest);

    CFRelease(dest);
    CFRelease(url);
    CFRelease(path);
    CGImageRelease(image);
    CGPathRelease(body);
    CGPathRelease(tile);
    CGContextRelease(g);
    CGColorSpaceRelease(space);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--preview") == 0) {
        render(1024, argv[2]);
        return 0;
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: icon <dir.iconset> | icon --preview <file.png>\n");
        return 1;
    }
    const std::string dir = argv[1];
    // Sizes above 256 pt @2x are left out on purpose: Finder scales up, and the app stays small.
    const struct {
        int points, scale;
    } sizes[] = {{16, 1}, {16, 2}, {32, 1}, {32, 2}, {128, 1}, {128, 2}, {256, 1}, {256, 2}};
    for (const auto& size : sizes) {
        char name[64];
        std::snprintf(name, sizeof(name), "/icon_%dx%d%s.png", size.points, size.points, size.scale == 2 ? "@2x" : "");
        render(size.points * size.scale, dir + name);
    }
    return 0;
}
