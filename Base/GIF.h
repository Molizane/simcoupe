// Part of SimCoupe - A SAM Coupe emulator
//
// GIF.h: GIF animation recording
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
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef GIF_H
#define GIF_H

#include "CScreen.h"


class GIF
{
    public:
        static bool Start (bool fAnimLoop_=false);
        static void Stop ();
        static void Toggle (bool fAnimLoop_=false);
        static bool IsRecording ();

        static void AddFrame (CScreen *pScreen_);
};


/*
  Packs a sequence of variable length codes into a buffer. Every time
  255 bytes have been completed, they are written to a binary file as a 
  data block of 256 bytes (where the first byte is the 'bytecount' of the
  rest and therefore equals 255). Any remaining bits are moved to the 
  buffer start to become part of the following block. After submitting 
  the last code via submit(), the user must call writeflush() to write 
  a terminal, possibly shorter, data block. 
*/
class BitPacker
{
private:
    FILE *binfile;
    BYTE buffer[260];	// holds the total buffer
    BYTE *pos;			// points into buffer
    WORD need;			// used by addcodetobuffer(), see there

    BYTE *AddCodeToBuffer (DWORD code, short n);

public:
    BitPacker (FILE *bf);

public:
    DWORD byteswritten;	// number of bytes written during the object's lifetime 
    BYTE *Submit (DWORD code, WORD n);
    void WriteFlush ();
};


class GifCompressor
/*
  Contains the stringtable, generates compression codes and writes them to a 
  binary file, formatted in data blocks of maximum length 255 with 
  additional bytecount header. 
  Users will open the binary file, write the first part themselves, 
  create a GifCompressor, call its method writedatablocks(), delete it and 
  write the last byte 0x3b themselves before closing the file.
*/
{
private:
    BitPacker *bp;		// object that does the packing and writing of the compression codes

    DWORD nofdata;		// number of pixels in the data stream
    DWORD width;		// width of bitmap in pixels
    DWORD height;		// height of bitmap in pixels

    DWORD curordinal;	// ordinal number of next pixel to be encoded
    BYTE pixel;			// next pixel to be encoded

    WORD nbits;			// current length of compression codes in bits (changes during encoding process)
    WORD *axon, *next;	// arrays making up the stringtable
    BYTE *pix;			// ditto
    DWORD cc;			// "clear code" which signals the clearing of the string table
    DWORD eoi;			// "end-of-information code" which must be the last item of the code stream
    WORD freecode;		// next code to be added to the string table

    void FlushStringTable ();
    void InitRoots ();
    DWORD DoNext ();
    BYTE Map (DWORD);
    WORD FindPixelOutlet(WORD headnode,BYTE pixel);

public:
    GifCompressor () { }
    DWORD WriteDataBlocks (FILE *bf,DWORD nof,WORD ds);
};

#endif
