// Part of SimCoupe - A SAM Coupe emulator
//
// HardDisk.h: Hard disk abstraction layer
//
//  Copyright (c) 2004-2014 Simon Owen
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

#ifndef HARDDISK_H
#define HARDDISK_H

#include "IO.h"
#include "ATA.h"

const unsigned int HDD_ACTIVE_FRAMES = 2;    // Frames the HDD is considered active after a command


class CHardDisk : public CATADevice
{
    public:
        CHardDisk (const char* pcszDisk_);

    public:
        static CHardDisk* OpenObject (const char* pcszDisk_, bool fReadOnly_=false);
        virtual bool Open (bool fReadOnly_=false) = 0;

    public:
        bool IsSDIDEDisk ();
        bool IsBDOSDisk (bool *pfByteSwapped=nullptr);

    protected:
        std::string m_strPath;
};


class CHDFHardDisk final : public CHardDisk
{
    public:
        CHDFHardDisk (const char* pcszDisk_);
        CHDFHardDisk (const CHDFHardDisk &) = delete;
        void operator= (const CHDFHardDisk &) = delete;
        ~CHDFHardDisk () { Close(); }

    public:
        static bool Create (const char* pcszDisk_, UINT uTotalSectors_);

    public:
        bool IsOpen () const { return m_hfDisk != nullptr; }
        bool Open (bool fReadOnly_=false) override;
        bool Create (UINT uTotalSectors_);
        void Close ();

        bool ReadSector (UINT uSector_, BYTE* pb_) override;
        bool WriteSector (UINT uSector_, BYTE* pb_) override;

    protected:
        FILE *m_hfDisk = nullptr;
        UINT m_uDataOffset = 0;
        UINT m_uSectorSize = 0;
};

#endif // HARDDISK_H
