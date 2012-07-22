// Part of SimCoupe - A SAM Coupe emulator
//
// IO.cpp: SAM I/O port handling
//
//  Copyright (c) 1999-2012 Simon Owen
//  Copyright (c) 1996-2001 Allan Skillman
//  Copyright (c) 2000-2001 Dave Laundon
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
#include "IO.h"

#include "Atom.h"
#include "AtomLite.h"
#include "BlueAlpha.h"
#include "Clock.h"
#include "CPU.h"
#include "Drive.h"
#include "Floppy.h"
#include "Frame.h"
#include "GUI.h"
#include "HardDisk.h"
#include "Input.h"
#include "Keyboard.h"
#include "Keyin.h"
#include "Memory.h"
#include "MIDI.h"
#include "Mouse.h"
#include "Options.h"
#include "OSD.h"
#include "Parallel.h"
#include "Paula.h"
#include "SAMDOS.h"
#include "SAMVox.h"
#include "SDIDE.h"
#include "SID.h"
#include "Sound.h"
#include "Util.h"
#include "Video.h"
#include "YATBus.h"

CDiskDevice *pFloppy1, *pFloppy2, *pBootDrive;
CAtaAdapter *pAtom, *pSDIDE, *pYATBus;

CPrintBuffer *pPrinterFile;
CMonoDACDevice *pMonoDac;
CStereoDACDevice *pStereoDac;

CClockDevice *pSambus, *pDallas;
CMouseDevice *pMouse;

CMidiDevice *pMidi;
CBeeperDevice *pBeeper;
CBlueAlphaDevice *pBlueAlpha;
CSAMVoxDevice *pSAMVox;
CPaulaDevice *pPaula;
CDAC *pDAC;
CSAA *pSAA;
CSID *pSID;


// Port read/write addresses for I/O breakpoints
WORD wPortRead, wPortWrite;

// Paging ports for internal and external memory
BYTE vmpr, hmpr, lmpr, lepr, hepr;
BYTE vmpr_mode, vmpr_page1, vmpr_page2;

BYTE border, border_col;

BYTE keyboard;
BYTE status_reg;
BYTE line_int;
BYTE lpen;
BYTE attr;

UINT clut[N_CLUT_REGS], mode3clut[4];

BYTE keyports[9];       // 8 rows of keys (+ 1 row for unscanned keys)
BYTE keybuffer[9];      // working buffer for key changed, activated mid-frame

bool fASICStartup;      // If set, the ASIC will be unresponsive shortly after first power-on

#ifdef _DEBUG
static BYTE abUnhandled[32];    // track unhandled port access in debug mode
#endif

//////////////////////////////////////////////////////////////////////////////

bool IO::Init (bool fFirstInit_/*=false*/)
{
    bool fRet = true;
    Exit(true);

    lepr = hepr = lpen = border = 0;

    OutLmpr(0);     // Page 0 in section C, page 1 in section D
    OutHmpr(0);     // Page 0 in section A, page 1 in section B, ROM0 on, ROM off
    OutVmpr(0);     // Video in page 0, screen mode 1

    // No extended keys pressed, no active interrupts
    status_reg = STATUS_INT_NONE;


    // Also, if this is a power-on initialisation, set up the clut, etc.
    if (fFirstInit_)
    {
        // Line interrupts aren't cleared by a reset
        line_int = 0xff;

        // Release all keys
        memset(keyports, 0xff, sizeof(keyports));

        pDAC = new CDAC;
        pSAA = new CSAA;
        pSID = new CSID;
        pBeeper = new CBeeperDevice;
        pBlueAlpha = new CBlueAlphaDevice;
        pSAMVox = new CSAMVoxDevice;
        pPaula = new CPaulaDevice;
        pMidi = new CMidiDevice;

        pSambus = new CSambusClock;
        pDallas = new CDallasClock;
        pMouse = new CMouseDevice;

        pPrinterFile = new CPrinterFile;
        pMonoDac = new CMonoDACDevice;
        pStereoDac = new CStereoDACDevice;

        pFloppy1 = new CDrive(1);
        pFloppy2 = new CDrive(2);
        pAtom = new CAtomLiteDevice;

        pSDIDE = new CSDIDEDevice;
        pYATBus = new CYATBusDevice;

        pFloppy1->LoadState(OSD::MakeFilePath(MFP_SETTINGS, "drive1"));
        pFloppy2->LoadState(OSD::MakeFilePath(MFP_SETTINGS, "drive2"));
        pDallas->LoadState(OSD::MakeFilePath(MFP_SETTINGS, "dallas"));

        pFloppy1->Insert(GetOption(disk1));
        pFloppy2->Insert(GetOption(disk2));

        pAtom->Insert(GetOption(atomdisk0), 0);
        pAtom->Insert(GetOption(atomdisk1), 1);
        pSDIDE->Insert(GetOption(sdidedisk), 0);
        pYATBus->Insert(GetOption(yatbusdisk), 0);
    }

    // The ASIC is unresponsive during the first ~49ms on production SAM units
    if (GetOption(asicdelay))
    {
        fASICStartup = true;
        AddCpuEvent(evtAsicStartup, g_dwCycleCounter + ASIC_STARTUP_DELAY);
    }

    pDAC->Reset();
    pSID->Reset();
    pBlueAlpha->Reset();

    // Initialise the drives back to a consistent state
    pFloppy1->Reset();
    pFloppy2->Reset();
    pAtom->Reset();
    pSDIDE->Reset();

    Keyin::Stop();

    // Return true only if everything
    return fRet;
}

void IO::Exit (bool fReInit_/*=false*/)
{
    if (!fReInit_)
    {
        SetOption(disk1, pFloppy1->DiskPath());
        SetOption(disk2, pFloppy2->DiskPath());

        SetOption(atomdisk0, pAtom->DiskPath(0));
        SetOption(atomdisk1, pAtom->DiskPath(1));
        SetOption(sdidedisk, pSDIDE->DiskPath(0));
        SetOption(yatbusdisk, pYATBus->DiskPath(0));

        pFloppy1->SaveState(OSD::MakeFilePath(MFP_SETTINGS, "drive1"));
        pFloppy2->SaveState(OSD::MakeFilePath(MFP_SETTINGS, "drive2"));
        pDallas->SaveState(OSD::MakeFilePath(MFP_SETTINGS, "dallas"));

        delete pMidi, pMidi = NULL;
        delete pPaula, pPaula = NULL;
        delete pSAMVox, pSAMVox = NULL;
        delete pBlueAlpha, pBlueAlpha = NULL;
        delete pBeeper, pBeeper = NULL;
        delete pSID, pSID = NULL;
        delete pSAA, pSAA = NULL;
        delete pDAC, pDAC = NULL;

        delete pSambus, pSambus = NULL;
        delete pDallas, pDallas = NULL;
        delete pMouse, pMouse = NULL;

        delete pPrinterFile, pPrinterFile = NULL;
        delete pMonoDac, pMonoDac = NULL;
        delete pStereoDac, pStereoDac = NULL;

        delete pFloppy1, pFloppy1 = NULL;
        delete pFloppy2, pFloppy2 = NULL;
        delete pBootDrive, pBootDrive = NULL;

        delete pAtom, pAtom = NULL;
        delete pSDIDE, pSDIDE = NULL;
        delete pYATBus, pYATBus = NULL;
    }
}

////////////////////////////////////////////////////////////////////////////////

static inline void PaletteChange (BYTE bHMPR_)
{
    // Update the 4 colours available to mode 3 (note: the middle colours are switched)
    BYTE mode3_bcd48 = (bHMPR_ & HMPR_MD3COL_MASK) >> 3;
    mode3clut[0] = clut[mode3_bcd48 | 0];
    mode3clut[1] = clut[mode3_bcd48 | 2];
    mode3clut[2] = clut[mode3_bcd48 | 1];
    mode3clut[3] = clut[mode3_bcd48 | 3];
}


void IO::OutLmpr(BYTE val)
{
    lmpr = val;

    // Put either RAM or ROM 0 into section A
    if (lmpr & LMPR_ROM0_OFF)
        PageIn(SECTION_A, LMPR_PAGE);
    else
        PageIn(SECTION_A, ROM0);

    // Put the relevant bank into section B
    PageIn(SECTION_B, (LMPR_PAGE + 1) & LMPR_PAGE_MASK);

    // Put either RAM or ROM 1 into section D
    if (lmpr & LMPR_ROM1)
        PageIn(SECTION_D, ROM1);
    else
        PageIn(SECTION_D, (HMPR_PAGE + 1) & HMPR_PAGE_MASK);
}

void IO::OutHmpr (BYTE bVal_)
{
    // Have the mode3 BCD4/8 bits changed?
    if ((hmpr ^ bVal_) & HMPR_MD3COL_MASK)
    {
        // The changes are effective immediately in mode 3
        if (vmpr_mode == MODE_3)
            Frame::Update();

        // Update the mode 3 colours
        PaletteChange(bVal_);
    }

    // Update the HMPR
    hmpr = bVal_;

    // Put the relevant RAM bank into section C
    PageIn(SECTION_C, HMPR_PAGE);

    // Put either RAM or ROM1 into section D
    if (lmpr & LMPR_ROM1)
        PageIn(SECTION_D, ROM1);
    else
        PageIn(SECTION_D,(HMPR_PAGE + 1) & HMPR_PAGE_MASK);

    // External RAM has priority, even over ROM1
    if (HMPR_MCNTRL)
    {
        OutLepr(lepr);
        OutHepr(hepr);
    }
}

void IO::OutVmpr (BYTE bVal_)
{
    // The ASIC changes mode before page, so consider an on-screen artifact from the mode change
    Frame::ChangeMode(bVal_);

    vmpr = bVal_ & (VMPR_MODE_MASK|VMPR_PAGE_MASK);
    vmpr_mode = VMPR_MODE;

    // Extract the page number for faster access by the memory writing functions
    vmpr_page1 = VMPR_PAGE;

    // The second page is only used by modes 3+4
    vmpr_page2 = VMPR_MODE_3_OR_4 ? ((vmpr_page1+1) & VMPR_PAGE_MASK) : 0xff;
}

void IO::OutLepr (BYTE bVal_)
{
    lepr = bVal_;

    if (HMPR_MCNTRL)
        PageIn(SECTION_C, EXTMEM+bVal_);
}

void IO::OutHepr (BYTE bVal_)
{
    hepr = bVal_;

    if (HMPR_MCNTRL)
        PageIn(SECTION_D, EXTMEM+bVal_);
}


void IO::OutClut (WORD wPort_, BYTE bVal_)
{
    wPort_ &= (N_CLUT_REGS-1);          // 16 clut registers, so only the bottom 4 bits are significant
    bVal_ &= (N_PALETTE_COLOURS-1);     // 128 colours, so only the bottom 7 bits are significant

    // Has the clut value actually changed?
    if (clut[wPort_] != bVal_)
    {
        // Draw up to the current point with the previous settings
        Frame::Update();

        // Update the clut entry and the mode 3 palette
        clut[wPort_] = bVal_;
        PaletteChange(hmpr);
    }
}


BYTE IO::In (WORD wPort_)
{
    BYTE bPortLow = (wPortRead = wPort_) & 0xff, bPortHigh = (wPort_ >> 8);

    // The ASIC doesn't respond to I/O immediately after power-on
    if (bPortLow >= BASE_ASIC_PORT && fASICStartup)
        return 0x00;

    // Ensure state is up-to-date
    CheckCpuEvents();

    switch (bPortLow)
    {
        // keyboard 1 / mouse
        case KEYBOARD_PORT:
        {
            BYTE res = 0x1f;

            if (bPortHigh == 0xff)
            {
                res = keyports[8] & 0x1f;

                if (GetOption(mouse) && res == 0x1f)
                    res = pMouse->In(wPort_) & 0x1f;
            }
            else
            {
                if (!(bPortHigh & 0x80)) res &= keyports[7] & 0x1f;
                if (!(bPortHigh & 0x40)) res &= keyports[6] & 0x1f;
                if (!(bPortHigh & 0x20)) res &= keyports[5] & 0x1f;
                if (!(bPortHigh & 0x10)) res &= keyports[4] & 0x1f;
                if (!(bPortHigh & 0x08)) res &= keyports[3] & 0x1f;
                if (!(bPortHigh & 0x04)) res &= keyports[2] & 0x1f;
                if (!(bPortHigh & 0x02)) res &= keyports[1] & 0x1f;
                if (!(bPortHigh & 0x01)) res &= keyports[0] & 0x1f;
            }

            return (keyboard & 0xe0) | res;
        }

        // keyboard 2
        case STATUS_PORT:
        {
            BYTE res = 0xe0;

            if (!(bPortHigh & 0x80)) res &= keyports[7] & 0xe0;
            if (!(bPortHigh & 0x40)) res &= keyports[6] & 0xe0;
            if (!(bPortHigh & 0x20)) res &= keyports[5] & 0xe0;
            if (!(bPortHigh & 0x10)) res &= keyports[4] & 0xe0;
            if (!(bPortHigh & 0x08)) res &= keyports[3] & 0xe0;
            if (!(bPortHigh & 0x04)) res &= keyports[2] & 0xe0;
            if (!(bPortHigh & 0x02)) res &= keyports[1] & 0xe0;
            if (!(bPortHigh & 0x01)) res &= keyports[0] & 0xe0;

            return (res & 0xe0) | (status_reg & 0x1f);
        }


        // Banked memory management
        case VMPR_PORT:     return vmpr | 0x80;     // RXMIDI bit always one for now
        case HMPR_PORT:     return hmpr;
        case LMPR_PORT:     return lmpr;

        // SAMBUS and DALLAS clock ports
        case CLOCK_PORT:
            if (wPort_ < 0xfe00 && GetOption(sambusclock))
                return pSambus->In(wPort_);
            else if (wPort_ >= 0xfe00 && GetOption(dallasclock))
                return pDallas->In(wPort_);
            break;

        // HPEN and LPEN ports
        case LPEN_PORT:
        {
            int nLine = g_dwCycleCounter / TSTATES_PER_LINE, nLineCycle = g_dwCycleCounter % TSTATES_PER_LINE;

            if ((wPort_ & PEN_MASK) == LPEN_PORT)
            {
                // LPEN reflects the horizontal scan position in the main screen area only
                BYTE bX = (nLine < TOP_BORDER_LINES || nLine >= (TOP_BORDER_LINES+SCREEN_LINES) ||
                           nLineCycle < (BORDER_PIXELS+BORDER_PIXELS)) ? 0 :
                            static_cast<BYTE>(nLineCycle - (BORDER_PIXELS+BORDER_PIXELS)) / 1;  // tstate->pixel division here?

                // Take the top 6 bits from the position, and the rest from the existing value
                return (bX & 0xfc) | (lpen & 0x03);
            }

            // HPEN reflects the vertical scan position in the main screen area only
            // Return 192 for the top/bottom border areas, or the main screen line number
            return (nLine < TOP_BORDER_LINES || nLine >= (TOP_BORDER_LINES+SCREEN_LINES)) ?
                static_cast<BYTE>(SCREEN_LINES) : (nLine - TOP_BORDER_LINES);
        }

        // Spectrum ATTR port
        case ATTR_PORT:
        {
            // If the display is off, return the cached attr value
            if (VMPR_MODE_3_OR_4 && BORD_SOFF)
                return attr;

            // Determine the 4 ASIC display bytes and return the 3rd, as documented
            BYTE b1, b2, b3, b4;
            Frame::GetAsicData(&b1, &b2, &b3, &b4);
            return b3;
        }

        // Parallel ports
        case PRINTL1_STAT:
        case PRINTL1_DATA:
            switch (GetOption(parallel1))
            {
                case 1: return pPrinterFile->In(wPort_);
                case 2: return pMonoDac->In(wPort_);
                case 3: return pStereoDac->In(wPort_);
            }
            break;

        case PRINTL2_STAT:
        case PRINTL2_DATA:
            switch (GetOption(parallel2))
            {
                case 1: return pPrinterFile->In(wPort_);
                case 2: return pMonoDac->In(wPort_);
                case 3: return pStereoDac->In(wPort_);
            }
            break;

        // Serial ports (currently unsupported)
        case SERIAL1:
        case SERIAL2:
            break;

        // MIDI IN/Network
        case MIDI_PORT:
            if (GetOption(midi) == 1) // MIDI device
                return pMidi->In(wPort_);
            break;

        // S D Software IDE interface
        case SDIDE_REG:
        case SDIDE_DATA:
            return pSDIDE->In(wPort_);

        // SID interface (reads not implemented by hardware)
        case SID_PORT:
            break;

        // Quazar Surround (unsupported)
        case QUAZAR_PORT:
            break;

        default:
        {
            // Floppy drive 1
            if ((wPort_ & FLOPPY_MASK) == FLOPPY1_BASE)
            {
                switch (GetOption(drive1))
                {
                    case drvFloppy: return (pBootDrive ? pBootDrive : pFloppy1)->In(wPort_);
                    default: break;
                }
            }

            // Floppy drive 2 *OR* the ATOM hard disk
            else if ((wPort_ & FLOPPY_MASK) == FLOPPY2_BASE)
            {
                switch (GetOption(drive2))
                {
                    case drvFloppy: return pFloppy2->In(wPort_);
                    case drvAtom:   return pAtom->In(wPort_);
                }
            }

            // YAMOD.ATBUS hard disk interface
            else if ((bPortLow & YATBUS_MASK) == YATBUS_BASE)
                return pYATBus->In(wPort_);

            // Blue Alpha and SAMVox ports overlap!
            else if ((bPortLow & 0xfc) == 0x7c)
            {
                // Blue Alpha Sampler?
                if (GetOption(bluealpha) && bPortLow == BLUE_ALPHA_PORT)
                {
                    if ((bPortHigh & 0xfc) == 0x7c)
                        return pBlueAlpha->In(bPortHigh & 0x03);
                  /*else if (highbyte == 0xff)
                        return BlueAlphaVoiceBox::In(0);*/
                }
            }
#ifdef _DEBUG
            // Only unsupported hardware should reach here
            else
            {
                int nEntry = bPortLow >> 3, nBit = 1 << (bPortLow & 7);

                if (!(abUnhandled[nEntry] & nBit))
                {
                    Message(msgWarning, "Unhandled read from port %#04x\n", wPort_);
                    abUnhandled[nEntry] |= nBit;
                    g_fDebug = true;
                }
            }
#endif
        }
    }

    // Default to returning 0xff
    return 0xff;
}


// The actual port input and output routines
void IO::Out (WORD wPort_, BYTE bVal_)
{
    BYTE bPortLow = (wPortWrite = wPort_) & 0xff, bPortHigh = (wPort_ >> 8);

    // The ASIC doesn't respond to I/O immediately after power-on
    if (bPortLow >= BASE_ASIC_PORT && fASICStartup)
        return;

    // Ensure state is up-to-date
    CheckCpuEvents();

    switch (bPortLow)
    {
        case BORDER_PORT:
        {
            bool fScreenOffChange = ((border ^ bVal_) & BORD_SOFF_MASK) && VMPR_MODE_3_OR_4;

            // Has the border changed colour or the screen been enabled/disabled?
            if (fScreenOffChange || ((border ^ bVal_) & BORD_COLOUR_MASK))
                Frame::Update();

            // Change of screen enable state?
            if (fScreenOffChange)
            {
                // If the display is now enabled, consider a border change artefact
                if (BORD_SOFF)
                    Frame::ChangeScreen(bVal_);

                // Otherwise determine the current ATTR value to return whilst disabled
                else
                {
                    BYTE b1, b2, b3, b4;
                    Frame::GetAsicData(&b1, &b2, &b3, &b4);
                    attr = b3;
                }
            }

            // If the speaker bit has been toggled, generate a click
            if ((border ^ bVal_) & BORD_BEEP_MASK)
                pBeeper->Out(wPort_, bVal_);

            // Store the new border value and extract the border colour for faster access by the video routines
            border = bVal_;
            border_col = BORD_VAL(bVal_);

            // Update the port read value, including the screen off status and reflecting MIC back to EAR
            keyboard = (keyboard & BORD_KEY_MASK) | (border & BORD_SOFF) | ((border & BORD_MIC_MASK) << 3);

            // If the screen state has changed, we need to reconsider memory contention changes
            if (fScreenOffChange)
                CPU::UpdateContention();
        }
        break;

        // Banked memory management
        case VMPR_PORT:
        {
            // Changes to screen mode and screen page are visible at different times

            // Has the screen mode changed?
            if (vmpr_mode != (bVal_ & VMPR_MODE_MASK))
            {
                // Are either the current mode or the new mode 3 or 4?  i.e. bit MDE1 is set
                if ((bVal_ | vmpr) & VMPR_MDE1_MASK)
                {
                    // Changes to the screen MODE are visible straight away
                    Frame::Update();

                    // Change only the screen MODE for the transition block
                    OutVmpr((bVal_ & VMPR_MODE_MASK) | (vmpr & ~VMPR_MODE_MASK));
                }
                // Otherwise both modes are 1 or 2
                else
                {
                    // There are no visible changes in the transition block
                    g_dwCycleCounter += VIDEO_DELAY;
                    Frame::Update();
                    g_dwCycleCounter -= VIDEO_DELAY;

                    // Do the whole change here - the check below will not be triggered
                    OutVmpr(bVal_);
                }

                // Video mode change may have affected memory contention
                CPU::UpdateContention();
            }

            // Has the screen page changed?
            if (vmpr_page1 != (bVal_ & VMPR_PAGE_MASK))
            {
                // Changes to screen PAGE aren't visible until 8 tstates later
                // as the memory has been read by the ASIC already
                g_dwCycleCounter += VIDEO_DELAY;
                Frame::Update();
                g_dwCycleCounter -= VIDEO_DELAY;

                OutVmpr(bVal_);
            }
        }
        break;

        case HMPR_PORT:
            if (hmpr != bVal_)
                OutHmpr(bVal_);
            break;

        case LMPR_PORT:
            if (lmpr != bVal_)
                OutLmpr(bVal_);
            break;

        // SAMBUS and DALLAS clock ports
        case CLOCK_PORT:
            if (wPort_ < 0xfe00 && GetOption(sambusclock))
                pSambus->Out(wPort_, bVal_);
            else if (wPort_ >= 0xfe00 && GetOption(dallasclock))
                pDallas->Out(wPort_, bVal_);
            break;

        case CLUT_BASE_PORT:
            OutClut(bPortHigh, bVal_);
            break;

        // External memory
        case HEPR_PORT:
            OutHepr(bVal_);
            break;

        case LEPR_PORT:
            OutLepr(bVal_);
            break;

        case LINE_PORT:
            // Line changed?
            if (line_int != bVal_)
            {
                // Cancel any existing line interrupt
                if (line_int < SCREEN_LINES)
                {
                    CancelCpuEvent(evtLineIntStart);
                    status_reg |= STATUS_INT_LINE;
                }

                // Set the new value
                line_int = bVal_;

                // Valid line interrupt set?
                if (line_int < SCREEN_LINES)
                {
                    DWORD dwLineTime = (line_int+TOP_BORDER_LINES) * TSTATES_PER_LINE;

                    // Schedule the line interrupt (could be active now, or already passed this frame)
                    AddCpuEvent(evtLineIntStart, dwLineTime);
                }
            }
            break;

        case SOUND_DATA:
            pSAA->Out(wPort_, bVal_);
            break;

        // Parallel ports 1 and 2
        case PRINTL1_STAT:
        case PRINTL1_DATA:
            switch (GetOption(parallel1))
            {
                case 1: return pPrinterFile->Out(wPort_, bVal_);
                case 2: return pMonoDac->Out(wPort_, bVal_);
                case 3: return pStereoDac->Out(wPort_, bVal_);
            }
            break;

        case PRINTL2_STAT:
        case PRINTL2_DATA:
            switch (GetOption(parallel2))
            {
                case 1: return pPrinterFile->Out(wPort_, bVal_);
                case 2: return pMonoDac->Out(wPort_, bVal_);
                case 3: return pStereoDac->Out(wPort_, bVal_);
            }
            break;

        // Serial ports 1 and 2 (currently unsupported)
        case SERIAL1:
        case SERIAL2:
            break;


        // MIDI OUT/Network
        case MIDI_PORT:
        {
            // Only transmit a new byte if one isn't already being sent
            if (!(lpen & LPEN_TXFMST))
            {
                // Set the TXFMST bit in LPEN to show that we're transmitting something
                lpen |= LPEN_TXFMST;

                // Create an event to begin an interrupt at the required time
                AddCpuEvent(evtMidiOutIntStart, g_dwCycleCounter +
                            A_ROUND(MIDI_TRANSMIT_TIME + 16, 32) - 16 - 32 - MIDI_INT_ACTIVE_TIME + 1);

                // Output the byte to the appropriate device
                if (GetOption(midi) == 1)
                    pMidi->Out(wPort_, bVal_);
            }
            break;
        }

        // S D Software IDE interface
        case SDIDE_REG:
        case SDIDE_DATA:
            pSDIDE->Out(wPort_, bVal_);
            break;

        // SID interface
        case SID_PORT:
            pSID->Out(wPort_, bVal_);
            break;

        // Quazar Surround (unsupported)
        case QUAZAR_PORT:
            break;

        default:
        {
            // Floppy drive 1
            if ((wPort_ & FLOPPY_MASK) == FLOPPY1_BASE)
            {
                switch (GetOption(drive1))
                {
                    case drvFloppy: (pBootDrive ? pBootDrive : pFloppy1)->Out(wPort_, bVal_); break;
                    default: break;
                }
            }

            // Floppy drive 2 *OR* the ATOM hard disk
            else if ((wPort_ & FLOPPY_MASK) == FLOPPY2_BASE)
            {
                switch (GetOption(drive2))
                {
                    case drvFloppy: pFloppy2->Out(wPort_, bVal_); break;
                    case drvAtom:   pAtom->Out(wPort_, bVal_); break;
                    default: break;
                }
            }

            // YAMOD.ATBUS hard disk interface
            else if ((bPortLow & YATBUS_MASK) == YATBUS_BASE)
                pYATBus->Out(wPort_, bVal_);

            // Blue Alpha, SAMVox and Paula ports overlap!
            else if ((bPortLow & 0xfc) == 0x7c)
            {
                // SAMVox
                if (GetOption(samvox))
                    pSAMVox->Out(bPortLow & 0x03, bVal_);

                // Paula DAC
                if (GetOption(paula))
                    pPaula->Out(bPortLow & 0x01, bVal_);

                // Blue Alpha only uses a single port
                if (GetOption(bluealpha) && bPortLow == BLUE_ALPHA_PORT)
                {
                    if ((bPortHigh & 0xfc) == 0x7c)
                        pBlueAlpha->Out(bPortHigh & 0x03, bVal_);
/*
                    else if (bPortHigh == 0xff)
                        BlueAlphaVoiceBox::Out(0, bVal_);
*/
                }
            }

#ifdef _DEBUG
            // Only unsupported hardware should reach here
            else
            {
                int nEntry = bPortLow >> 3, nBit = 1 << (bPortLow & 7);

                if (!(abUnhandled[nEntry] & nBit))
                {
                    Message(msgWarning, "Unhandled write to port %#04x, value = %02x\n", wPort_, bVal_);
                    abUnhandled[nEntry] |= nBit;
                    g_fDebug = true;
                }
            }
#endif
        }
    }
}

void IO::FrameUpdate ()
{
    pFloppy1->FrameEnd();
    pFloppy2->FrameEnd();
    pAtom->FrameEnd();
    pPrinterFile->FrameEnd();

    Input::Update();

    if (!g_nTurbo)
        Sound::FrameUpdate();
}

void IO::UpdateInput()
{
    // To avoid accidents, purge keyboard input during accelerated disk access
    if (GetOption(turboload) && (pFloppy1->IsActive() || pFloppy2->IsActive()))
        Input::Purge();

    // Copy the working buffer to the live port buffer
    memcpy(keyports, keybuffer, sizeof(keyports));
}

const COLOUR* IO::GetPalette ()
{
    static COLOUR asPalette[N_PALETTE_COLOURS];

    // Look-up table for an even intensity spread, used to map SAM colours to RGB
    static const BYTE abIntensities[] = { 0x00, 0x24, 0x49, 0x6d, 0x92, 0xb6, 0xdb, 0xff };

    // Build the full palette: SAM's 128 colours and the extra colours for the GUI
    for (int i = 0; i < N_PALETTE_COLOURS ; i++)
    {
        // Convert from SAM palette position to 8-bit RGB
        BYTE bRed   = abIntensities[(i&0x02)     | ((i&0x20) >> 3) | ((i&0x08) >> 3)];
        BYTE bGreen = abIntensities[(i&0x04) >> 1| ((i&0x40) >> 4) | ((i&0x08) >> 3)];
        BYTE bBlue  = abIntensities[(i&0x01) << 1| ((i&0x10) >> 2) | ((i&0x08) >> 3)];

        // If greyscale is enabled, convert the colour a suitable intensity grey
        if (GetOption(greyscale))
        {
            BYTE bGrey = static_cast<BYTE>(0.299 * bRed + 0.587 * bGreen + 0.114 * bBlue + 0.5);
            bRed = bGreen = bBlue = bGrey;
        }

        // Store the calculated values for the entry
        asPalette[i].bRed = bRed;
        asPalette[i].bGreen = bGreen;
        asPalette[i].bBlue = bBlue;
    }

    // Return the freshly prepared palette
    return asPalette;
}

// Check if we're at the striped SAM startup screen
bool IO::IsAtStartupScreen (bool fExit_)
{

    // Search the top 10 stack entries
    for (int i = 0 ; i < 20 ; i += 2)
    {
        // Check for values: 0fa5 0f78
        if (read_word(SP+i) == 0x0fa5 && read_word(SP+i+2) == 0x0f78)
        {
            // Optionally exit WTFK loop at copyright message
            if (fExit_)
                write_byte(SP+i, 0xa7);

            return true;
        }
    }

    // Not found
    return false;
}

void IO::AutoLoad (int nType_)
{
    // Set the auto-load type if we're forced or at the startup screen
    if (GetOption(autoboot) && ((nType_ & AUTOLOAD_FORCE) || IsAtStartupScreen()))
    {
        // Keep only the type bits
        nType_ &= AUTOLOAD_MASK;

        // F9 to boot floppy 1, F7 to load tape
        const char *pcszKey = (nType_ == AUTOLOAD_DISK) ? "\xc9" : "\xc7";

        // Simulate the key press
        Keyin::String(pcszKey, false);
    }
}

void IO::WakeAsic ()
{
    // No longer in ASIC startup phase
    fASICStartup = false;
}


bool IO::EiHook ()
{
    // If we're leaving the ROM interrupt handler, inject any auto-typing input
    if (PC == 0x005a && GetSectionPage(SECTION_A) == ROM0)
        Keyin::Next();

    // Continue EI processing
    return false;
}

bool IO::Rst8Hook ()
{
    // Return for normal processing if we're not executing ROM code
    if ((PC <  0x4000 && GetSectionPage(SECTION_A) != ROM0) ||
        (PC >= 0xc000 && GetSectionPage(SECTION_D) != ROM1))
      return false;

    // Read the error code after the RST
    BYTE bErrCode = read_byte(PC);

    // If a drive object exists, clean up after our boot attempt, whether or not it worked
    if (pBootDrive)
        delete pBootDrive, pBootDrive = NULL;

    // Stop auto-typing on any error
    if (bErrCode != 0)
        Keyin::Stop();

    // Are we about to trigger "NO DOS" in ROM1, and with DOS booting enabled?
    if (bErrCode == 0x35 && GetOption(dosboot))
    {
        // If there's a custom boot disk, load it read-only
        CDisk *pDisk = CDisk::Open(GetOption(dosdisk), true);

        // Fall back on the built-in SAMDOS2 image
        if (!pDisk)
            pDisk = CDisk::Open(abSAMDOS, sizeof(abSAMDOS), "mem:SAMDOS.sbt");

        if (pDisk)
        {
            pBootDrive = new CDrive(1, pDisk);

            // Jump back to BOOTEX to try again
            PC = 0xd8e5;
            return true;
        }
    }

    // Continue with RST
    return false;
}

bool IO::Rst48Hook ()
{
    // Are we at READKEY in ROM0?
    if (PC == 0x1cb2 && GetSectionPage(SECTION_A) == ROM0)
    {
        // If we have auto-type input, skip the startup screen
        if (Keyin::IsTyping())
            IsAtStartupScreen(true);
    }

    // Continue with RST
    return false;
}
