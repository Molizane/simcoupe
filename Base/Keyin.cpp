// Part of SimCoupe - A SAM Coupe emulator
//
// Keyin.cpp: Automatic keyboard input
//
//  Copyright (c) 2012 Simon Owen
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

#include "SimCoupe.h"
#include "Keyin.h"
#include "Memory.h"


static BYTE *pbInput;
static int nPos = -1;
static FILE *f;
static bool fMapChars = true;


void Keyin::String (const char *pcsz_, bool fMapChars_)
{
    // Clean up any existing input
    Stop();

    // Determine the source length and allocate a buffer for it
    int nLen = strlen(pcsz_);
    pbInput = new BYTE[nLen+1];

    // Copy the string, appending a null terminator just in case
    memcpy(pbInput, pcsz_, nLen);
    pbInput[nLen] = '\0';

    // Copy character mapping flag and set starting offset
    fMapChars = fMapChars_;
    nPos = 0;
}

void Keyin::File (const char *pcszFile_)
{
    Stop();
    f = fopen(pcszFile_, "rb");
    fMapChars = true;
}

void Keyin::Stop ()
{
    // Clean up file and string
    if (f) fclose(f), f = NULL;
    delete[] pbInput, pbInput = NULL;

    // Normal speed
    g_nTurbo &= ~TURBO_KEYIN;
}

bool Keyin::CanType ()
{
    // For safety, ensure ROM0 and the system variable page are present
    return GetSectionPage(SECTION_A) == ROM0 && GetSectionPage(SECTION_B) == 0;
}

bool Keyin::IsTyping ()
{
    // Do we have a string or open file?
    return pbInput || f;
}

bool Keyin::Next ()
{
    char bKey = 0;

    // Return if we're not typing or the previous key hasn't been consumed
    if (!IsTyping() || (apbPageWritePtrs[0][0x5c3b-0x4000] & 0x20))
        return false;

    // Read the next key from file or string
    if (!f)
        bKey = pbInput[nPos++];
    else if (!fread(&bKey, 1, 1, f))
        bKey = 0;

    // Stop at the first null character
    if (!bKey)
    {
        Stop();
        return false;
    }

    // Are we to perform character mapping? (disable to allow keyword codes)
    if (fMapChars)
    {
        // Map the character to a SAM key code, if required
        bKey = MapChar(bKey);

        // Ignore characters without a mapping
        if (!bKey)
            return false;
    }

    // Simulate the key press
    apbPageWritePtrs[0][0x5c08-0x4000] = bKey;  // set key in LASTK
    apbPageWritePtrs[0][0x5c3b-0x4000] = 0x20;	// signal key available in FLAGS

    // Run at turbo speed during input
    g_nTurbo |= TURBO_KEYIN;

    // Key simulated
    return true;
}


// Map special case input characters to the SAM key code equivalent
BYTE Keyin::MapChar (BYTE b_)
{
    switch (b_)
    {
        case '\t':	return b_;		// horizontal tab
        case '\r':	return 0;		// ignore CR
        case '\n':	return '\r';	// convert LF to CR
        case '`':	return 0x7f;	// (c)
        case 156:	return 0x60;	// GBP [DOS / CP 437]
        case 163:	return 0x60;	// GBP [ANSI]
    }

    // Ignore other extended characters
    if (b_ < 0x20 || b_ >= 0x80)
        return 0;

    // Return the original character
    return b_;
}
