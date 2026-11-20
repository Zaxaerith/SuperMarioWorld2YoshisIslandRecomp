#pragma once

#include "ppu.h"

/*
 * Compatibility scanline renderer.
 *
 * Selection belongs to Ppu::renderFlags; game hosts should never provide a
 * parallel renderer-selection global. This declaration stays private to the
 * PPU implementation even though the legacy renderer has its own translation
 * unit.
 */
void ppu_draw_whole_line_legacy(Ppu *ppu, int line);
void ppu_draw_side_margins_legacy(Ppu *ppu, int line);

void ppu_draw_side_margins_legacy_with_coordinates(Ppu *ppu, int line,
    PpuBgCoordinateOverride coordinates, const void *context);
