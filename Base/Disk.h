// Part of SimCoupe - A SAM Coupe emulator
//
// Disk.h: C++ classes used for accessing all SAM disk image types
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

#ifndef DISK_H
#define DISK_H

#include "Floppy.h"     // native floppy support
#include "Stream.h"     // for the data stream abstraction
#include "VL1772.h"     // for the VL-1772 controller definitions

////////////////////////////////////////////////////////////////////////////////

const UINT NORMAL_DISK_SIDES     = 2;    // Normally 2 sides per disk
const UINT NORMAL_DISK_TRACKS    = 80;   // Normally 80 tracks per side
const UINT NORMAL_DISK_SECTORS   = 10;   // Normally 10 sectors per track
const UINT NORMAL_SECTOR_SIZE    = 512;  // Normally 512 bytes per sector

const UINT NORMAL_DIRECTORY_TRACKS = 4;  // Normally 4 tracks in a SAMDOS directory

const UINT DOS_DISK_SECTORS = 9;         // Double-density MS-DOS disks are 9 sectors per track


// The various disk format image sizes
#define MGT_IMAGE_SIZE  (NORMAL_DISK_SIDES * NORMAL_DISK_TRACKS * NORMAL_DISK_SECTORS * NORMAL_SECTOR_SIZE)
#define DOS_IMAGE_SIZE  (NORMAL_DISK_SIDES * NORMAL_DISK_TRACKS * DOS_DISK_SECTORS * NORMAL_SECTOR_SIZE)

const UINT DISK_FILE_HEADER_SIZE = 9;    // From SAM Technical Manual  (bType, wSize, wOffset, wUnused, bPages, bStartPage)

// Maximum size of a file that will fit on a SAM disk
const UINT MAX_SAM_FILE_SIZE = ((NORMAL_DISK_SIDES * NORMAL_DISK_TRACKS) - NORMAL_DIRECTORY_TRACKS) *
                                NORMAL_DISK_SECTORS * (NORMAL_SECTOR_SIZE-2) - DISK_FILE_HEADER_SIZE;

////////////////////////////////////////////////////////////////////////////////

// The ID string for Aley Keprt's SAD disk image format (heh!)
#define SAD_SIGNATURE           "Aley's disk backup"

// Format of a SAD image header
typedef struct
{
    BYTE    abSignature[sizeof(SAD_SIGNATURE) - 1];

    BYTE    bSides;             // Number of sides on the disk
    BYTE    bTracks;            // Number of tracks per side
    BYTE    bSectors;           // Number of sectors per track
    BYTE    bSectorSizeDiv64;   // Sector size divided by 64
}
SAD_HEADER;

////////////////////////////////////////////////////////////////////////////////

#define DSK_SIGNATURE           "MV - CPC"
#define EDSK_SIGNATURE          "EXTENDED CPC DSK File\r\nDisk-Info\r\n"
#define EDSK_TRACK_SIGNATURE    "Track-Info\r\n"
#define ESDK_MAX_TRACK_SIZE     0xff00
#define EDSK_MAX_SECTORS        ((256 - sizeof(EDSK_TRACK)) / sizeof(EDSK_SECTOR))  // = 29

#define ST1_765_CRC_ERROR       0x20
#define ST2_765_DATA_NOT_FOUND  0x01
#define ST2_765_CRC_ERROR       0x20
#define ST2_765_CONTROL_MARK    0x40

typedef struct
{
    char szSignature[34];       // one of the signatures above, depending on DSK/EDSK
    char szCreator[14];         // name of creator (utility/emulator)
    BYTE bTracks;
    BYTE bSides;
    BYTE abTrackSize[2];        // fixed track size (DSK only)
}
EDSK_HEADER;

typedef struct
{
    char szSignature[13];       // Track-Info\r\n
    BYTE bRate;                 // 0=unknown (default=250K), 1=250K/300K, 2=500K, 3=1M
    BYTE bEncoding;             // 0=unknown (default=MFM), 1=FM, 2=MFM
    BYTE bUnused;
    BYTE bTrack;
    BYTE bSide;
    BYTE abUnused[2];
    BYTE bSize;
    BYTE bSectors;
    BYTE bGap3;
    BYTE bFill;
}
EDSK_TRACK;

typedef struct
{
    BYTE bTrack, bSide, bSector, bSize;
    BYTE bStatus1, bStatus2;
    BYTE bDatalow, bDatahigh;
}
EDSK_SECTOR;

////////////////////////////////////////////////////////////////////////////////

enum { dtNone, dtUnknown, dtFloppy, dtFile, dtEDSK, dtSAD, dtMGT, dtSBT, dtCAPS };

#define LOAD_DELAY  3   // Number of status reads to artificially stay busy for image file track loads
                        // Pro-Dos relies on data not being available immediately a command is submitted

class CDisk
{
    friend class CDrive;

    // Constructor and virtual destructor
    public:
        CDisk (CStream* pStream_, int nType_);
        virtual ~CDisk ();

    public:
        static int GetType (CStream* pStream_);
        static CDisk* Open (const char* pcszDisk_, bool fReadOnly_=false);
        static CDisk* Open (void* pv_, size_t uSize_, const char* pcszDisk_);

        virtual void Close () { m_pStream->Close(); }
        virtual void Flush () { }
        virtual bool Save () = 0;
        virtual BYTE FormatTrack (UINT uSide_, UINT uTrack_, IDFIELD* paID_, BYTE* papbData_[], UINT uSectors_) = 0;


    // Public query functions
    public:
        const char* GetPath () { return m_pStream->GetPath(); }
        const char* GetFile () { return m_pStream->GetFile(); }
        UINT GetSpinPos (bool fAdvance_=false);
        bool IsReadOnly () const { return m_pStream->IsReadOnly(); }
        bool IsModified () const { return m_fModified; }

        void SetModified (bool fModified_=true) { m_fModified = fModified_; }

    // Protected overrides
    protected:
        virtual UINT FindInit (UINT uSide_, UINT uTrack_);
        virtual bool FindNext (IDFIELD* pIdField_=NULL, BYTE* pbStatus_=NULL);
        virtual bool FindSector (UINT uSide_, UINT uTrack_, UINT uIdTrack_, UINT uSector_, IDFIELD* pID_=NULL);

        virtual BYTE LoadTrack (UINT uSide_, UINT uTrack_) { m_nBusy = LOAD_DELAY; return 0; }
        virtual BYTE ReadData (BYTE* pbData_, UINT* puSize_) = 0;
        virtual BYTE WriteData (BYTE* pbData_, UINT* puSize_) = 0;

        virtual bool IsBusy (BYTE* pbStatus_, bool fWait_=false) { if (!m_nBusy) return false; m_nBusy--; return true; }

    protected:
        int     m_nType, m_nBusy;
        UINT    m_uSides, m_uTracks, m_uSectors, m_uSectorSize;
        UINT    m_uSide, m_uTrack, m_uSector, m_uSize;
        bool    m_fModified;

        UINT    m_uSpinPos;
        CStream*m_pStream;
        BYTE*   m_pbData;
};


class CMGTDisk : public CDisk
{
    public:
        CMGTDisk (CStream* pStream_, UINT uSectors_=NORMAL_DISK_SECTORS);

    public:
        static bool IsRecognised (CStream* pStream_);

    public:
        BYTE ReadData (BYTE* pbData_, UINT* puSize_);
        BYTE WriteData (BYTE* pbData_, UINT* puSize_);
        bool Save ();
        BYTE FormatTrack (UINT uSide_, UINT uTrack_, IDFIELD* paID_, BYTE* papbData_[], UINT uSectors_);
};


class CSADDisk : public CDisk
{
    public:
        CSADDisk (CStream* pStream_, UINT uSides_=NORMAL_DISK_SIDES, UINT uTracks_=NORMAL_DISK_TRACKS,
                    UINT uSectors_=NORMAL_DISK_SECTORS, UINT uSectorSize_=NORMAL_SECTOR_SIZE);

    public:
        static bool IsRecognised (CStream* pStream_);

    public:
        bool FindNext (IDFIELD* pIdField_, BYTE* pbStatus_);
        BYTE ReadData (BYTE* pbData_, UINT* puSize_);
        BYTE WriteData (BYTE* pbData_, UINT* puSize_);
        bool Save ();
        BYTE FormatTrack (UINT uSide_, UINT uTrack_, IDFIELD* paID_, BYTE* papbData_[], UINT uSectors_);
};


class CEDSKDisk : public CDisk
{
    public:
        CEDSKDisk (CStream* pStream_, UINT uSides_=NORMAL_DISK_SIDES, UINT uTracks_=NORMAL_DISK_TRACKS);
        ~CEDSKDisk ();

    public:
        static bool IsRecognised (CStream* pStream_);

    public:
        UINT FindInit (UINT uSide_, UINT uTrack_);
        bool FindNext (IDFIELD* pIdField_, BYTE* pbStatus_);
        BYTE ReadData (BYTE* pbData_, UINT* puSize_);
        BYTE WriteData (BYTE* pbData_, UINT* puSize_);
        bool Save ();
        BYTE FormatTrack (UINT uSide_, UINT uTrack_, IDFIELD* paID_, BYTE* papbData_[], UINT uSectors_);

    protected:
        EDSK_TRACK* m_apTracks[MAX_DISK_SIDES][MAX_DISK_TRACKS];
        BYTE m_abSizes[MAX_DISK_SIDES][MAX_DISK_TRACKS];

        EDSK_TRACK* m_pTrack;     // Last track
        EDSK_SECTOR* m_pFind;     // Last sector found with FindNext()
        BYTE* m_pbFind;           // Last sector data
};


class CFloppyDisk : public CDisk
{
    public:
        CFloppyDisk (CStream* pStream_);

    public:
        static bool IsRecognised (CStream* pStream_);

    public:
        UINT FindInit (UINT uSide_, UINT uTrack_);
        bool FindNext (IDFIELD* pIdField_, BYTE* pbStatus_);
        void Close () { m_pFloppy->Close(); m_uCacheTrack = 0U-1; }
        void Flush () { Close(); }

        BYTE LoadTrack (UINT uSide_, UINT uTrack_);
        BYTE ReadData (BYTE* pbData_, UINT* puSize_);
        BYTE WriteData (BYTE* pbData_, UINT* puSize_);
        bool Save ();

        BYTE FormatTrack (UINT uSide_, UINT uTrack_, IDFIELD* paID_, BYTE* papbData_[], UINT uSectors_);

        bool IsBusy (BYTE* pbStatus_, bool fWait_);

    protected:
        CFloppyStream* m_pFloppy;

        BYTE m_bCommand, m_bStatus;     // Current command and final status

        PTRACK  m_pTrack;               // Current track
        PSECTOR m_pSector;              // Pointer to first sector on track
        BYTE   *m_pbWrite;              // Data for in-progress write, to copy if successful

        UINT m_uCacheSide, m_uCacheTrack;
};


class CFileDisk : public CDisk
{
    public:
        CFileDisk (CStream* pStream_);

    public:
        static bool IsRecognised (CStream* pStream_);

    public:
        BYTE ReadData (BYTE* pbData_, UINT* puSize_);
        BYTE WriteData (BYTE* pbData_, UINT* puSize_);
        bool Save ();
        BYTE FormatTrack (UINT uSide_, UINT uTrack_, IDFIELD* paID_, BYTE* papbData_[], UINT uSectors_);

    protected:
        UINT  m_uSize;
};


// Namespace wrapper for the Huffman decompression code, to keep the global namespace clean
class LZSS
{
    public:
        static size_t Unpack (BYTE* pIn_, size_t uSize_, BYTE* pOut_);

    protected:
        static void Init ();
        static void RebuildTree ();
        static void UpdateTree (int c);

        static UINT GetChar () { return (pIn < pEnd) ? *pIn++ : 0; }
        static UINT GetBit ();
        static UINT GetByte ();
        static UINT DecodeChar ();
        static UINT DecodePosition ();

    protected:
        static BYTE ring_buff[], d_code[], d_len[];
        static WORD freq[];
        static short parent[], son[];

        static BYTE *pIn, *pEnd;
        static UINT uBits, uBitBuff, r;
};

#endif  // DISK_H
