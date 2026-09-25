#ifndef SNESRECOMP_MODE7_HD_H
#define SNESRECOMP_MODE7_HD_H

#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

/* Presentation-only affine sampling. This uses the project's Mode 7 register
 * interpretation with fractional screen coordinates and without the hardware's
 * intermediate six-bit truncation. No bsnes implementation is incorporated.
 * Coordinates are in texels; integer screen coordinates retain the SNES grid.
 * A host renderer may also interpolate these transforms within a known camera
 * region. Do not interpolate across an HDMA split or a change of overflow mode. */
typedef struct SnesMode7HdTransform {
  double origin_x, origin_y;
  double step_x, step_y;
  double row_x, row_y;
  uint8_t control;
} SnesMode7HdTransform;

static inline int SnesMode7HdSign13(int value) {
  value &= 0x1fff;
  return value & 0x1000 ? value - 0x2000 : value;
}

static inline SnesMode7HdTransform SnesMode7HdMakeTransform(
    const int16_t matrix[8], uint8_t control, unsigned line) {
  int cx = SnesMode7HdSign13(matrix[4]);
  int cy = SnesMode7HdSign13(matrix[5]);
  int h = SnesMode7HdSign13(matrix[6]) - cx;
  int v = SnesMode7HdSign13(matrix[7]) - cy;
  h = h & 0x2000 ? (h | ~1023) : (h & 1023);
  v = v & 0x2000 ? (v | ~1023) : (v & 1023);
  double y = control & 2 ? 255.0 - line : line;
  SnesMode7HdTransform result = {
    cx + (matrix[0] * (double)h + matrix[1] * (y + v)) / 256.0,
    cy + (matrix[2] * (double)h + matrix[3] * (y + v)) / 256.0,
    matrix[0] / 256.0, matrix[2] / 256.0,
    matrix[1] / 256.0, matrix[3] / 256.0, control
  };
  if (control & 1) {
    result.origin_x += 255 * result.step_x;
    result.origin_y += 255 * result.step_y;
    result.step_x = -result.step_x;
    result.step_y = -result.step_y;
  }
  if (control & 2) {
    result.row_x = -result.row_x;
    result.row_y = -result.row_y;
  }
  return result;
}

static inline uint8_t SnesMode7HdSample(const SnesMode7HdTransform *transform,
                                       const uint16_t vram[0x8000],
                                       double x, double subline) {
  double u = floor(transform->origin_x + x * transform->step_x +
                   subline * transform->row_x);
  double v = floor(transform->origin_y + x * transform->step_y +
                   subline * transform->row_y);
  /* Also makes the reusable helper safe for host-generated transforms. */
  if (!isfinite(u) || !isfinite(v)) return 0;
  bool outside = u < 0 || u >= 1024 || v < 0 || v >= 1024;
  if (outside && (transform->control & 0x80) &&
      !(transform->control & 0x40)) return 0;
  /* Register-derived transforms fit in int. Reserve the slower reduction
   * for transforms supplied by an external host. */
  if (u < INT_MIN || u > INT_MAX) u = fmod(u, 1024.0);
  if (v < INT_MIN || v > INT_MAX) v = fmod(v, 1024.0);
  unsigned tx = (unsigned)(int)u & 1023;
  unsigned ty = (unsigned)(int)v & 1023;
  unsigned tile = outside && (transform->control & 0x80) ? 0 :
      vram[(ty / 8) * 128 + tx / 8] & 255;
  return (uint8_t)(vram[tile * 64 + (ty & 7) * 8 + (tx & 7)] >> 8);
}

#endif
