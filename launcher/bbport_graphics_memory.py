# SPDX-License-Identifier: GPL-2.0-or-later
"""Retail 1.09 graphics heap layout, shared by patch generation and preflight."""
import struct
GRAPHICS_HEAPS = ((0x04b36dd8, 0x8ef00000, 0xf7000000),
                  (0x04b36de0, 0x37600000, 0xa0000000))
GRAPHICS_PHYSICAL_BASE = 0x42c00000
GRAPHICS_VRAM_MODES = ('graphics', 'graphics-private')


def graphics_heap_sizes(size, full=False):
    pixels = size[0] * size[1]
    scale = 1.0 if full else min(1.0, max(0.0, (pixels / (1920 * 1080) - 1) / 3))
    granule = 16 << 20
    return tuple(min(original + -(-int((maximum - original) * scale) // granule) * granule,
                     maximum) for _, original, maximum in GRAPHICS_HEAPS)


def direct_pool_mb(settings):
    """Preserve retail's free space, adding only bytes actually added to its two heaps."""
    size = tuple(map(int, settings['render_res'].split('x')))
    growth = sum(graphics_heap_sizes(size, settings.get('large_heaps', False))) - sum(
        original for _, original, _ in GRAPHICS_HEAPS)
    return 5056 + (growth + (1 << 20)-1) // (1 << 20)


def graphics_vram_layout(settings, mode='graphics'):
    size = tuple(map(int, settings['render_res'].split('x')))
    sizes = graphics_heap_sizes(size, settings.get('large_heaps', False))
    if mode not in GRAPHICS_VRAM_MODES:
        raise ValueError('Unknown graphics VRAM mode')
    count = sizes[0] if mode == 'graphics-private' else sum(sizes)
    end = GRAPHICS_PHYSICAL_BASE + count
    return f'{GRAPHICS_PHYSICAL_BASE:x}-{end:x}', count


def validate_heap_patches(blob, settings):
    """Include overlapping/custom writes when verifying the final patched heap table."""
    if len(blob) < 24:
        raise ValueError('Truncated native memory patches')
    magic, base, count = struct.unpack_from('<8sQQ', blob)
    if magic != b'BBPATCH2' or base != 0x400000 or count > (len(blob)-24)//16:
        raise ValueError('Invalid native memory patches')
    tables = [bytearray(struct.pack('<Q', original)) for _, original, _ in GRAPHICS_HEAPS]
    at = 24
    for _ in range(count):
        if at + 16 > len(blob):
            raise ValueError('Truncated native memory patch header')
        offset, size = struct.unpack_from('<QQ', blob, at)
        at += 16
        if size > len(blob)-at:
            raise ValueError('Truncated native memory patch data')
        for (address, _, _), table in zip(GRAPHICS_HEAPS, tables):
            target = address - base
            low, high = max(target, offset), min(target+8, offset+size)
            if low < high:
                table[low-target:high-target] = blob[at+low-offset:at+high-offset]
        at += size
    expected = graphics_heap_sizes(tuple(map(int, settings['render_res'].split('x'))),
                                   settings.get('large_heaps', False))
    if at != len(blob) or tuple(struct.unpack('<Q', t)[0] for t in tables) != expected:
        raise ValueError('Prepared graphics heaps differ from the validated VRAM layout; run setup.')
