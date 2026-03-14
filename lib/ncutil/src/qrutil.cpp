// qrutil.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "qrutil.h"

#include <sstream>
#include <vector>

#include "fileutil.h"
#include "log.h"
#include "qrcodegen.hpp"
#include "stb_image_write.h"

std::string QrUtil::ToTerminalString(const std::string& p_Text)
{
  using namespace qrcodegen;
  QrCode qr = QrCode::encodeText(p_Text.c_str(), QrCode::Ecc::MEDIUM);
  int border = 4;
  int size = qr.getSize();
  std::ostringstream oss;
  for (int y = -border; y < size + border; y += 2)
  {
    for (int x = -border; x < size + border; x++)
    {
      bool top = (y >= 0 && y < size) ? qr.getModule(x, y) : false;
      bool bot = (y + 1 >= 0 && y + 1 < size) ? qr.getModule(x, y + 1) : false;
      if (top && bot)        oss << " ";      // both dark
      else if (top && !bot)  oss << "\u2584"; // top dark, bottom light
      else if (!top && bot)  oss << "\u2580"; // top light, bottom dark
      else                   oss << "\u2588"; // both light (full block)
    }
    oss << "\n";
  }
  return oss.str();
}

bool QrUtil::WritePngFile(const std::string& p_Text, const std::string& p_Path)
{
  using namespace qrcodegen;
  QrCode qr = QrCode::encodeText(p_Text.c_str(), QrCode::Ecc::MEDIUM);
  int size = qr.getSize();
  int scale = 8;
  int border = 2;
  int imgSize = (size + 2 * border) * scale;

  FileUtil::MkDir(FileUtil::DirName(p_Path));

  std::vector<unsigned char> pixels(imgSize * imgSize * 3, 255); // white background
  for (int y = 0; y < size; y++)
  {
    for (int x = 0; x < size; x++)
    {
      if (qr.getModule(x, y))
      {
        for (int dy = 0; dy < scale; dy++)
        {
          for (int dx = 0; dx < scale; dx++)
          {
            int px = (x + border) * scale + dx;
            int py = (y + border) * scale + dy;
            int idx = (py * imgSize + px) * 3;
            pixels[idx] = 0;
            pixels[idx + 1] = 0;
            pixels[idx + 2] = 0;
          }
        }
      }
    }
  }

  bool rv = stbi_write_png(p_Path.c_str(), imgSize, imgSize, 3, pixels.data(), imgSize * 3) != 0;
  LOG_DEBUG("write qr png %s rv %d", p_Path.c_str(), rv);
  return rv;
}
