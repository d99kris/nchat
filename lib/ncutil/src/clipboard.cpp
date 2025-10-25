// clipboard.cpp
//
// Copyright (c) 2022-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "clipboard.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

#ifdef HAVE_PNG_H
#include <png.h>
#endif

#include "clip.h"
#include "log.h"
#include "sysutil.h"

#ifdef HAVE_PNG_H
static bool WriteRgbaPng(const std::string& p_Path, unsigned p_W, unsigned p_H, const uint8_t* p_Rgba, size_t p_Stride)
{
  FILE* fp = fopen(p_Path.c_str(), "wb");
  if (!fp)
  {
    LOG_WARNING("failed to open %s for writing", p_Path.c_str());
    return false;
  }

  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr)
  {
    LOG_WARNING("create png write struct failed");
    fclose(fp);
    return false;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr)
  {
    LOG_WARNING("create png info struct failed");
    png_destroy_write_struct(&png_ptr, nullptr);
    fclose(fp);
    return false;
  }

  if (setjmp(png_jmpbuf(png_ptr)))
  {
    LOG_WARNING("write png error");
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return false;
  }

  png_init_io(png_ptr, fp);
  png_set_IHDR(png_ptr, info_ptr, p_W, p_H, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png_ptr, info_ptr);

  std::vector<png_const_bytep> rows(p_H);
  for (unsigned y = 0; y < p_H; ++y)
  {
    rows[y] = p_Rgba + y * p_Stride;
  }

  png_write_image(png_ptr, const_cast<png_bytep*>(rows.data()));
  png_write_end(png_ptr, nullptr);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  fclose(fp);

  return true;
}

static inline unsigned MaskBitCount(uint32_t p_Mask)
{
  unsigned n = 0;
  while (p_Mask)
  {
    n += (p_Mask & 1u);
    p_Mask >>= 1;
  }

  return n;
}

static inline uint8_t ExtractChan(uint32_t p_Px, uint32_t p_Mask, unsigned p_Shift)
{
  if (!p_Mask) return 0;

  uint32_t v = (p_Px & p_Mask) >> p_Shift;
  unsigned bits = MaskBitCount(p_Mask);
  if (bits == 8) return static_cast<uint8_t>(v);

  // scale to 8 bits (rounding)
  uint32_t maxv = (bits >= 31) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
  return static_cast<uint8_t>((v * 255u + maxv / 2u) / maxv);
}

static bool SaveImagePng(const clip::image& p_Image, const std::string& p_Path)
{
  const clip::image_spec spec = p_Image.spec();

  if (spec.bits_per_pixel != 24 && spec.bits_per_pixel != 32)
  {
    LOG_WARNING("unsupported bpp (expected 24 or 32)");
    return false;
  }

  const unsigned w = static_cast<unsigned>(spec.width);
  const unsigned h = static_cast<unsigned>(spec.height);
  const uint8_t* src = reinterpret_cast<const uint8_t*>(p_Image.data());
  const size_t stride = spec.bytes_per_row;

  // rgba8 buffer
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);

  for (unsigned y = 0; y < h; ++y)
  {
    const uint8_t* srcRow = src + y * stride;
    uint8_t* drow = rgba.data() + static_cast<size_t>(y) * w * 4;

    if (spec.bits_per_pixel == 32)
    {
      // 32 bpp path
      const uint32_t* s32 = reinterpret_cast<const uint32_t*>(srcRow);
      for (unsigned x = 0; x < w; ++x)
      {
        uint32_t px = s32[x];
        uint8_t r = ExtractChan(px, (uint32_t)spec.red_mask, (unsigned)spec.red_shift);
        uint8_t g = ExtractChan(px, (uint32_t)spec.green_mask, (unsigned)spec.green_shift);
        uint8_t b = ExtractChan(px, (uint32_t)spec.blue_mask, (unsigned)spec.blue_shift);
        uint8_t a = (spec.alpha_mask ? ExtractChan(px, (uint32_t)spec.alpha_mask, (unsigned)spec.alpha_shift) : 255);

        drow[4*x + 0] = r;
        drow[4*x + 1] = g;
        drow[4*x + 2] = b;
        drow[4*x + 3] = a; // straight alpha (per clip.h)
      }
    }
    else
    {
      // 24 bpp path (no alpha, fill 255)
      for (unsigned x = 0; x < w; ++x)
      {
        const uint8_t* p = srcRow + x * 3;
        uint32_t px = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
        uint8_t r = ExtractChan(px, (uint32_t)spec.red_mask, (unsigned)spec.red_shift);
        uint8_t g = ExtractChan(px, (uint32_t)spec.green_mask, (unsigned)spec.green_shift);
        uint8_t b = ExtractChan(px, (uint32_t)spec.blue_mask, (unsigned)spec.blue_shift);

        drow[4*x + 0] = r;
        drow[4*x + 1] = g;
        drow[4*x + 2] = b;
        drow[4*x + 3] = 255;
      }
    }
  }

  return WriteRgbaPng(p_Path, w, h, rgba.data(), static_cast<size_t>(w) * 4);
}
#endif

void Clipboard::SetText(const std::string& p_Text)
{
  clip::set_text(p_Text);
}

std::string Clipboard::GetText()
{
  std::string text;
  clip::get_text(text);
  return text;
}

bool Clipboard::HasImage()
{
#ifdef HAVE_PNG_H
  return clip::has(clip::image_format());
#else
  return false;
#endif
}

bool Clipboard::GetImage(const std::string& p_Path)
{
#ifdef HAVE_PNG_H
  if (!clip::has(clip::image_format()))
  {
    LOG_WARNING("no clipboard image");
    return false;
  }

  clip::image image;
  if (!clip::get_image(image))
  {
    LOG_WARNING("get clipboard image failed");
    return false;
  }

  bool rv = SaveImagePng(image, p_Path);
  return rv;
#else
  UNUSED(p_Path);
  return false;
#endif
}
