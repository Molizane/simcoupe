// Part of SimCoupe - A SAM Coupe emulator
//
// PNG.h: Screenshot saving in PNG format
//
//  Copyright (c) 1999-2012 Simon Owen
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#pragma once

#include "Screen.h"

namespace PNG
{
bool Save(CScreen* pScreen_);
}

#ifdef HAVE_LIBZ

// Define just the stuff we need - taken from libPNG's png.h

typedef unsigned short      UINT16;
typedef unsigned char       UINT8;

#define PNG_SIGNATURE       "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"

#define PNG_CN_IHDR         0x49484452L
#define PNG_CN_IDAT         0x49444154L
#define PNG_CN_IEND         0x49454E44L
#define PNG_CN_tEXt         0x74455874L

#define PNG_COLOR_MASK_COLOR        2   // RGB
#define PNG_COMPRESSION_TYPE_BASE   0   // Deflate method 8, 32K window
#define PNG_FILTER_TYPE_DEFAULT     0   // Single row per-byte filtering
#define PNG_INTERLACE_NONE          0   // Non-interlaced image


// PNG header
typedef struct
{
    BYTE abWidth[4];
    BYTE abHeight[4];
    BYTE bBitDepth;
    BYTE bColourType;
    BYTE bCompressionType;
    BYTE bFilterType;
    BYTE bInterlaceType;
} PNG_IHDR;

// PNG support
typedef struct
{
    DWORD dwWidth, dwHeight;
    BYTE* pbImage;
    ULONG uSize, uCompressedSize;
} PNG_INFO;


#endif  // HAVE_LIBZ
