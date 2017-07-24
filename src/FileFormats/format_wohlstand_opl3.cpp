/*
 * OPL Bank Editor by Wohlstand, a free tool for music bank editing
 * Copyright (c) 2016-2017 Vitaly Novichkov <admin@wohlnet.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "format_wohlstand_opl3.h"
#include "../common.h"

static const char       *wopl3_magic = "WOPL3-BANK\0";
static const char       *wopli_magic = "WOPL3-INST\0";

static const uint16_t   latest_version = 1;
/*
Version history:
V. 1
    * first release
*/

bool WohlstandOPL3::detect(const QString &, char *magic)
{
    return (strncmp(magic, wopl3_magic, 11) == 0);
}

bool WohlstandOPL3::detectInst(const QString &, char *magic)
{
    return (strncmp(magic, wopli_magic, 11) == 0);
}

enum WOPL_InstrumentFlags
{
    Flags_NONE      = 0,
    Flag_Enable4OP  = 0x01,
    Flag_Pseudo4OP  = 0x02,
};

static bool readInstrument(QFile &file, FmBank::Instrument &ins)
{
    uint8_t idata[62];
    if(file.read(char_p(idata), 62) != 62)
        return false;
    strncpy(ins.name, char_p(idata), 32);
    ins.note_offset1 = toSint16BE(idata + 32);
    ins.note_offset2 = toSint16BE(idata + 34);
    ins.velocity_offset = int8_t(idata[36]);
    ins.fine_tune       = int8_t(idata[37]);
    ins.percNoteNum     = idata[38];
    uint8_t flags       = idata[39];
    ins.en_4op          = (flags & Flag_Enable4OP) != 0;
    ins.en_pseudo4op    = (flags & Flag_Pseudo4OP) != 0;
    ins.setFBConn1(idata[40]);
    ins.setFBConn2(idata[41]);
    for(int op = 0; op < 4; op++)
    {
        size_t off = 42 + size_t(op) * 5;
        ins.setAVEKM(op,    idata[off + 0]);
        ins.setKSLL(op,     idata[off + 1]);
        ins.setAtDec(op,    idata[off + 2]);
        ins.setSusRel(op,   idata[off + 3]);
        ins.setWaveForm(op, idata[off + 4]);
    }
    return true;
}

static bool writeInstrument(QFile &file, FmBank::Instrument &ins)
{
    uint8_t odata[62];
    memset(odata, 0, 62);
    strncpy(char_p(odata), ins.name, 32);       //32
    fromSint16BE(ins.note_offset1, odata + 32); //2
    fromSint16BE(ins.note_offset2, odata + 34); //2
    odata[36] = uint8_t(ins.velocity_offset); //1
    odata[37] = uint8_t(ins.fine_tune);       //1
    odata[38] = ins.percNoteNum;              //1
    odata[39] = (ins.en_4op ? Flag_Enable4OP : 0) |
                (ins.en_pseudo4op ? Flag_Pseudo4OP : 0);
    odata[40] = ins.getFBConn1();             //1
    odata[41] = ins.getFBConn2();             //1
    for(int op = 0; op < 4; op++)                  //20
    {
        size_t off = 42 + size_t(op) * 5;
        odata[off + 0] = ins.getAVEKM(op);
        odata[off + 1] = ins.getKSLL(op);
        odata[off + 2] = ins.getAtDec(op);
        odata[off + 3] = ins.getSusRel(op);
        odata[off + 4] = ins.getWaveForm(op);
    }
    return (file.write(char_p(odata), 62) == 62);
}

FfmtErrCode WohlstandOPL3::loadFile(QString filePath, FmBank &bank)
{
    uint16_t version = 0;
    uint16_t count_melodic_banks     = 1;
    uint16_t count_percusive_banks   = 1;

    char magic[32];
    memset(magic, 0, 32);

    QFile file(filePath);
    if(!file.open(QIODevice::ReadOnly))
        return FfmtErrCode::ERR_NOFILE;

    bank.reset();
    if(file.read(magic, 11) != 11)
        return FfmtErrCode::ERR_BADFORMAT;
    if(strncmp(magic, wopl3_magic, 11) != 0)
        return FfmtErrCode::ERR_BADFORMAT;

    if(readLE(file, version) != 2)
        return FfmtErrCode::ERR_BADFORMAT;
    if(version > latest_version)
        return FfmtErrCode::ERR_UNSUPPORTED_FORMAT;

    uint8_t head[6];
    memset(head, 0, 6);
    if(file.read(char_p(head), 6) != 6)
        return FfmtErrCode::ERR_BADFORMAT;
    count_melodic_banks     = toUint16BE(head);
    count_percusive_banks   = toUint16BE(head + 2);
    //5'th byte reserved for Deep-Tremolo and Deep-Vibrato flags
    //6'th byte reserved for ADLMIDI's default volume model
    if((count_melodic_banks < 1) || (count_percusive_banks < 1))
        return FfmtErrCode::ERR_BADFORMAT;

    bank.reset(count_melodic_banks, count_percusive_banks);

    uint16_t total = 128 * count_melodic_banks;
    bool readPercussion = false;

tryAgain:
    for(uint16_t i = 0; i < total; i++)
    {
        FmBank::Instrument &ins = (readPercussion) ? bank.Ins_Percussion[i] : bank.Ins_Melodic[i];
        if(!readInstrument(file, ins))
        {
            bank.reset();
            return FfmtErrCode::ERR_BADFORMAT;
        }
    }

    if(!readPercussion)
    {
        total = 128 * count_percusive_banks;
        readPercussion = true;
        goto tryAgain;
    }
    file.close();

    return FfmtErrCode::ERR_OK;
}

FfmtErrCode WohlstandOPL3::saveFile(QString filePath, FmBank &bank)
{
    FmBank::Instrument null;
    memset(&null, 0, sizeof(FmBank::Instrument));

    uint16_t count_melodic_banks     = uint16_t(((bank.countMelodic() - 1)/ 128) + 1);
    uint16_t count_percusive_banks = uint16_t(((bank.countDrums() - 1)/ 128) + 1);

    QFile file(filePath);
    if(!file.open(QIODevice::WriteOnly))
        return FfmtErrCode::ERR_NOFILE;

    //Write header
    file.write(char_p(wopl3_magic), 11);
    writeLE(file, latest_version);
    uint8_t head[6];
    fromUint16BE(count_melodic_banks,   head);
    fromUint16BE(count_percusive_banks, head + 2);
    //5'th byte reserved for Deep-Tremolo and Deep-Vibrato flags
    //6'th byte reserved for ADLMIDI's default volume model
    file.write(char_p(head), 6);

    uint16_t total = 128 * count_melodic_banks;
    uint16_t total_insts = uint16_t(bank.Ins_Melodic_box.size());
    bool wrtiePercussion = false;
    FmBank::Instrument *insts = bank.Ins_Melodic;

tryAgain:
    for(uint16_t i = 0; i < total; i++)
    {
        if(i < total_insts)
        {
            FmBank::Instrument &ins = insts[i];
            if(!writeInstrument(file, ins))
                return FfmtErrCode::ERR_BADFORMAT;
        }
        else
        {
            if(!writeInstrument(file, null))
                return FfmtErrCode::ERR_BADFORMAT;
        }
    }

    if(!wrtiePercussion)
    {
        total = 128 * count_percusive_banks;
        insts = bank.Ins_Percussion;
        total_insts = uint16_t(bank.Ins_Percussion_box.size());
        wrtiePercussion = true;
        goto tryAgain;
    }

    file.close();

    return FfmtErrCode::ERR_OK;
}

int WohlstandOPL3::formatCaps()
{
    return (int)FormatCaps::FORMAT_CAPS_EVERYTHING;
}

QString WohlstandOPL3::formatName()
{
    return "Standard OPL3 bank by Wohlstand";
}

QString WohlstandOPL3::formatExtensionMask()
{
    return "*.wopl";
}

BankFormats WohlstandOPL3::formatId()
{
    return BankFormats::FORMAT_WOHLSTAND_OPL3;
}

FfmtErrCode WohlstandOPL3::loadFileInst(QString filePath, FmBank::Instrument &inst, bool *isDrum)
{
    char magic[32];
    memset(magic, 0, 32);
    uint16_t version = 0;
    uint8_t isDrumFlag = 0;
    QFile file(filePath);

    if(!file.open(QIODevice::ReadOnly))
        return FfmtErrCode::ERR_NOFILE;
    if(file.read(magic, 11) != 11)
        return FfmtErrCode::ERR_BADFORMAT;
    if(strncmp(magic, wopli_magic, 11) != 0)
        return FfmtErrCode::ERR_BADFORMAT;
    if(readLE(file, version) != 2)
        return FfmtErrCode::ERR_BADFORMAT;
    if(version > latest_version)
        return FfmtErrCode::ERR_UNSUPPORTED_FORMAT;
    if(file.read(char_p(&isDrumFlag), 1) != 1)
        return FfmtErrCode::ERR_BADFORMAT;
    if(isDrum)
        *isDrum = bool(isDrumFlag);
    if(!readInstrument(file, inst))
        return FfmtErrCode::ERR_BADFORMAT;
    file.close();

    return FfmtErrCode::ERR_OK;
}

FfmtErrCode WohlstandOPL3::saveFileInst(QString filePath, FmBank::Instrument &inst, bool isDrum)
{
    uint8_t isDrumFlag = uint8_t(isDrum);
    QFile file(filePath);

    if(!file.open(QIODevice::WriteOnly))
        return FfmtErrCode::ERR_NOFILE;
    if(file.write(char_p(wopli_magic), 11) != 11)
        return FfmtErrCode::ERR_BADFORMAT;
    if(writeLE(file, latest_version) != 2)
        return FfmtErrCode::ERR_BADFORMAT;
    if(file.write(char_p(&isDrumFlag), 1) != 1)
        return FfmtErrCode::ERR_BADFORMAT;
    if(!writeInstrument(file, inst))
        return FfmtErrCode::ERR_BADFORMAT;
    file.close();
    return FfmtErrCode::ERR_OK;
}

int WohlstandOPL3::formatInstCaps()
{
    return (int)FormatCaps::FORMAT_CAPS_EVERYTHING;
}

QString WohlstandOPL3::formatInstName()
{
    return "Standard OPL3 instrument by Wohlstand";
}

QString WohlstandOPL3::formatInstExtensionMask()
{
    return "*.opli";
}

InstFormats WohlstandOPL3::formatInstId()
{
    return InstFormats::FORMAT_INST_WOPL3;
}