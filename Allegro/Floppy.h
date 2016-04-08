// Part of SimCoupe - A SAM Coupe emulator
//
// Floppy.h: Allegro direct floppy access
//
//  Copyright (c) 1999-2006  Simon Owen
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

#ifndef FLOPPY_H
#define FLOPPY_H

#include "CStream.h"
#include "VL1772.h"

typedef struct
{
    int sectors;
    BYTE cyl, head;     // physical track location
}
TRACK, *PTRACK;

typedef struct
{
    BYTE cyl, head, sector, size;
    BYTE status;
    BYTE *pbData;
}
SECTOR, *PSECTOR;


class CFloppyStream : public CStream
{
    public:
        CFloppyStream (const char* pcszStream_, bool fReadOnly_) : CStream(pcszStream_, fReadOnly_) { }
        virtual ~CFloppyStream () { Close(); }

    public:
        static bool IsRecognised (const char* pcszStream_);

    public:
        void Close ();

    public:
        bool IsOpen () const;
        bool IsBusy (BYTE* pbStatus_, bool fWait_);

        // The normal stream functions are not used
        bool Rewind () { return false; }
        size_t Read (void*, size_t) { return 0; }
        size_t Write (void*, size_t) { return 0; }

        BYTE StartCommand (BYTE bCommand_, PTRACK pTrack_=NULL, UINT uSector_=0, BYTE *pbData_=NULL);
};

#endif  // FLOPPY_H
