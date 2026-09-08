/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <AppKit/AppKit.h>

typedef struct {
    NSInteger pixels;
    char type[4];
} IconVariant;

static const IconVariant variants[] = {
    {16, {'i', 'c', 'p', '4'}},
    {32, {'i', 'c', 'p', '5'}},
    {64, {'i', 'c', 'p', '6'}},
    {128, {'i', 'c', '0', '7'}},
    {256, {'i', 'c', '0', '8'}},
    {512, {'i', 'c', '0', '9'}},
    {1024, {'i', 'c', '1', '0'}},
};

static void append_big_endian_u32(NSMutableData *data, uint32_t value)
{
    uint32_t big_endian = CFSwapInt32HostToBig(value);
    [data appendBytes:&big_endian length:sizeof(big_endian)];
}

static NSData *render_png(NSImage *source, NSInteger pixels)
{
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL pixelsWide:pixels pixelsHigh:pixels
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    if (bitmap == nil)
        return nil;

    bitmap.size = NSMakeSize(pixels, pixels);
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext *context =
        [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    [NSGraphicsContext setCurrentContext:context];
    context.imageInterpolation = NSImageInterpolationHigh;
    [source drawInRect:NSMakeRect(0, 0, pixels, pixels)
              fromRect:NSMakeRect(0, 0, source.size.width, source.size.height)
             operation:NSCompositingOperationCopy fraction:1.0];
    [context flushGraphics];
    [NSGraphicsContext restoreGraphicsState];

    return [bitmap representationUsingType:NSBitmapImageFileTypePNG
                                properties:@{}];
}

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        if (argc != 3) {
            fprintf(stderr, "Usage: build_macos_icns SOURCE_PNG OUTPUT_ICNS\n");
            return 2;
        }

        NSString *source_path = [NSString stringWithUTF8String:argv[1]];
        NSString *output_path = [NSString stringWithUTF8String:argv[2]];
        NSImage *source = [[NSImage alloc] initWithContentsOfFile:source_path];
        if (source == nil) {
            fprintf(stderr, "Could not load icon source: %s\n", argv[1]);
            return 1;
        }

        NSMutableArray<NSData *> *images = [NSMutableArray array];
        uint64_t total_length = 8;
        size_t variant_count = sizeof(variants) / sizeof(variants[0]);
        for (size_t index = 0; index < variant_count; ++index) {
            NSData *image = render_png(source, variants[index].pixels);
            if (image == nil) {
                fprintf(stderr, "Could not render %ldpx icon\n",
                        (long)variants[index].pixels);
                return 1;
            }
            [images addObject:image];
            total_length += 8 + image.length;
        }
        if (total_length > UINT32_MAX) {
            fprintf(stderr, "Generated icon is too large\n");
            return 1;
        }

        NSMutableData *icns = [NSMutableData dataWithCapacity:total_length];
        [icns appendBytes:"icns" length:4];
        append_big_endian_u32(icns, (uint32_t)total_length);
        for (size_t index = 0; index < variant_count; ++index) {
            NSData *image = images[index];
            [icns appendBytes:variants[index].type length:4];
            append_big_endian_u32(icns, (uint32_t)(8 + image.length));
            [icns appendData:image];
        }

        NSError *error = nil;
        if (![icns writeToFile:output_path
                       options:NSDataWritingAtomic error:&error]) {
            fprintf(stderr, "Could not write icon: %s\n",
                    error.localizedDescription.UTF8String);
            return 1;
        }
    }
    return 0;
}
