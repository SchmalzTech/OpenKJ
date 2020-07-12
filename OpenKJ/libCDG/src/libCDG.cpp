/*
 * Copyright (c) 2013-2020 Thomas Isaac Lightburn
 *
 *
 * This file is part of OpenKJ.
 *
 * OpenKJ is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../include/libCDG.h"
#include <QFile>
#include <QDebug>
#include <QBuffer>
#include <QCryptographicHash>
#include <chrono>


CdgParser::CdgParser()
{
    reset();
}

bool CdgParser::open(const QByteArray &byteArray, const bool &bypassReset)
{
    qInfo() << "libCDG - Opening byte array for processing";
    if (!bypassReset)
        reset();
    m_cdgData = byteArray;
    if (byteArray.size() == 0)
    {
        qWarning() << "libCDG - Received zero bytes of CDG data";
        return false;
    }
    qInfo() << "libCDG - Byte array opened successfully";
    // reserve enough room in our frames vector to fit the number
    // of frames we should be producing
    m_frames.reserve(byteArray.size() / 24);
    m_skip.reserve(byteArray.size() / 24);
    return true;
}

bool CdgParser::open(const QString &filename)
{
    qInfo() << "libCDG - Opening file: " << filename;
    reset();
    QFile file(filename);
    file.open(QFile::ReadOnly);
    m_cdgData = file.readAll();
    file.close();
    return open(m_cdgData, true);
}

unsigned int CdgParser::position()
{
    float fpos = (m_position / 300.0) * 1000;
    return (int) fpos;
}

void CdgParser::reset()
{
    qDebug() << "libCDG - CDG::reset() called, freeing memory and setting isOpen to false";
    m_isOpen = false;
    m_needupdate = true;
    m_lastCmdWasMempreset = false;
    m_lastCDGCommandMS = 0;
    m_position = 0;
    m_curHOffset = 0;
    m_curVOffset = 0;
    m_cdgData = QByteArray();
    QVector<QRgb> palette;
    for (int i=0; i < 16; i++)
        palette.append(QColor(0,0,0).rgb());
    m_image = QImage(QSize(300,216),QImage::Format_Indexed8);
    m_bytesPerPixel = m_image.pixelFormat().bitsPerPixel() / 8;
    m_borderLRBytes = m_bytesPerPixel * 6;
    m_borderRBytesOffset = 294 * m_bytesPerPixel;
    m_image.setColorTable(palette);
    m_image.fill(0);
    m_frames.clear();
    m_skip.clear();
    m_tempo = 100;

    // Uncomment the following to help test for memory leaks,
    //m_frames.shrink_to_fit();
    //m_skip.shrink_to_fit();
}

bool CdgParser::canSkipFrameByTime(const unsigned int &ms)
{
    int scaledMs = ms * ((float)m_tempo / 100.0);
    size_t frameno = scaledMs / 40;
    if (ms % 40 > 0) frameno++;
    if (frameno > m_frames.size())
        return false;
    bool skip = true;
    if (!m_skip.at(frameno - 1))
        skip = false;
    if (!m_skip.at(frameno))
        skip = false;
    if (!m_skip.at(frameno + 1))
        skip = false;
    return skip;
}

bool CdgParser::process()
{
    qInfo() << "libCDG - Beginning processing of CDG data";
    auto t1 = std::chrono::high_resolution_clock::now();
    m_needupdate = false;
    cdg::CDG_SubCode subCode;
    int frameno = 0;
    QBuffer ioDevice(&m_cdgData);
    if (!ioDevice.open(QIODevice::ReadOnly))
        return false;
    while (ioDevice.read((char *)&subCode, sizeof(subCode)) > 0)
    {
        m_needupdate = false;
        readCdgSubcodePacket(subCode);
        if (m_needupdate)
            m_lastCDGCommandMS = frameno * 40;
        m_position++;
        if (((position() % 40) == 0) && position() >= 40)
        {

            m_skip.emplace_back(!m_needupdate);
            auto frame = m_frames.emplace_back(getSafeArea().convertToFormat(QImage::Format_RGB32));
            frame.setStartTime(position());
            frameno++;
        }
    }

    ioDevice.close();
    m_cdgData.clear();
    m_isOpen = true;
    auto t2 = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( t2 - t1 ).count();
    qInfo() << "libCDG - Processed CDG file in " << duration << "ms";
    return true;
}


void CdgParser::readCdgSubcodePacket(const cdg::CDG_SubCode &subCode)
{
    if ((subCode.command & m_subcodeMask) != m_subcodeCommand)
        return;
    switch (subCode.instruction & m_subcodeMask)
    {
    case cdg::CmdMemoryPreset:
        cmdMemoryPreset(cdg::CdgMemoryPresetData(subCode.data));
        m_lastCmdWasMempreset = true;
        break;
    case cdg::CmdBorderPreset:
        cmdBorderPreset(cdg::CdgBorderPresetData(subCode.data));
        break;
    case cdg::CmdTileBlock:
        cmdTileBlock(cdg::CdgTileBlockData(subCode.data), cdg::TileBlockNormal);
        break;
    case cdg::CmdScrollPreset:
        cmdScroll(subCode.data, cdg::ScrollPreset);
        break;
    case cdg::CmdScrollCopy:
        cmdScroll(subCode.data, cdg::ScrollCopy);
        break;
    case cdg::CmdDefineTrans:
        cmdDefineTransparent(subCode.data);
        break;
    case cdg::CmdColorsLow:
        cmdColors(cdg::CdgColorsData(subCode.data), cdg::LowColors);
        break;
    case cdg::CmdColorsHigh:
        cmdColors(cdg::CdgColorsData(subCode.data), cdg::HighColors);
        break;
    case cdg::CmdTileBlockXOR:
        cmdTileBlock(cdg::CdgTileBlockData(subCode.data), cdg::TileBlockXOR);
        break;
    }
    m_lastCmdWasMempreset = (subCode.instruction == cdg::CmdMemoryPreset);
}

void CdgParser::cmdBorderPreset(const cdg::CdgBorderPresetData &borderPreset)
{
    // Is there a safer C++ way to do these memory copies?

    for (auto line=0; line < 216; line++)
    {
        if (line < 12 || line > 202)
            memset(m_image.scanLine(line), borderPreset.color, m_image.bytesPerLine());
        else
        {
            memset(m_image.scanLine(line), borderPreset.color, m_borderLRBytes);
            memset(m_image.scanLine(line) + m_borderRBytesOffset, borderPreset.color, m_borderLRBytes);
        }
    }
    m_needupdate = true;
}

void CdgParser::cmdColors(const cdg::CdgColorsData &data, const cdg::CdgColorTables &table)
{
    int curColor = (table == cdg::HighColors) ? 8 : 0;
    std::for_each(data.colors.begin(), data.colors.end(), [&] (auto color) {
        if (m_image.colorTable().at(curColor) != color.rgb())
        {
            m_image.setColor(curColor, color.rgb());
            m_needupdate = true;
        }
        curColor++;
    });
}

QImage CdgParser::getSafeArea()
{

    QImage image(QSize(288,192),QImage::Format_Indexed8);
    image.setColorTable(m_image.colorTable());
    for (auto i=0; i < 192; i++)
    {
        auto curSrcLine = i + m_curVOffset;
        auto srcLineOffset = m_image.bytesPerLine() * (12 + curSrcLine);
        auto dstLineOffset = image.bytesPerLine() * i;
        auto copiedLineSize = 288 * m_bytesPerPixel;
        auto srcBits = m_image.bits();
        auto dstBits = image.bits();
        memcpy(dstBits + dstLineOffset, srcBits + srcLineOffset + m_borderLRBytes + (m_curHOffset * m_bytesPerPixel), copiedLineSize);
    }
    return image;
}



void CdgParser::cmdMemoryPreset(const cdg::CdgMemoryPresetData &memoryPreset)
{
    if (m_lastCmdWasMempreset && memoryPreset.repeat)
    {
        return;
    }
    m_image.fill(memoryPreset.color);
    m_needupdate = true;
}



void CdgParser::cmdTileBlock(const cdg::CdgTileBlockData &tileBlockPacket, const cdg::TileBlockType &type)
{
    // There's probably a better way to do this, needs research
    for (auto y = 0; y < 12; y++)
    {
        auto ptr = m_image.scanLine(y + tileBlockPacket.top);
        auto rowData = tileBlockPacket.tilePixels[y];
        switch (type) {
        case cdg::TileBlockXOR:
            *(ptr + (tileBlockPacket.left * m_bytesPerPixel))       ^= (rowData & m_masks[0]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 1) * m_bytesPerPixel)) ^= (rowData & m_masks[1]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 2) * m_bytesPerPixel)) ^= (rowData & m_masks[2]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 3) * m_bytesPerPixel)) ^= (rowData & m_masks[3]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 4) * m_bytesPerPixel)) ^= (rowData & m_masks[4]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 5) * m_bytesPerPixel)) ^= (rowData & m_masks[5]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            break;
        case cdg::TileBlockNormal:
            *(ptr + (tileBlockPacket.left * m_bytesPerPixel))       = (rowData & m_masks[0]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 1) * m_bytesPerPixel)) = (rowData & m_masks[1]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 2) * m_bytesPerPixel)) = (rowData & m_masks[2]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 3) * m_bytesPerPixel)) = (rowData & m_masks[3]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 4) * m_bytesPerPixel)) = (rowData & m_masks[4]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            *(ptr + ((tileBlockPacket.left + 5) * m_bytesPerPixel)) = (rowData & m_masks[5]) ? tileBlockPacket.color1 : tileBlockPacket.color0;
            break;
        }
    }
    m_needupdate = true;
}

const QVideoFrame& CdgParser::videoFrameByTime(const unsigned int &ms)
{
    size_t frameno = (ms * ((float)m_tempo / 100.0)) / 40;
    if (ms % 40 > 0) frameno++;
    if (frameno >= m_frames.size())
    {
        qInfo() << "Frame past end of CDG requested, returning last frame";
        return m_frames.at(m_frames.size() - 1);
    }
    return m_frames.at(frameno);
}

QString CdgParser::md5HashByTime(const unsigned int &ms)
{
    // This is for future use in a CDG fingerprinting system planned
    // for auto-naming files based on the fingerprint
    size_t frameno = ms / 40;
    if (ms % 40 > 0) frameno++;
    if (frameno > m_frames.size())
        frameno = m_frames.size() - 1;
    m_frames.at(frameno).map(QAbstractVideoBuffer::ReadOnly);
    QByteArray arr = QByteArray::fromRawData((const char*)m_frames.at(frameno).bits(), m_frames.at(frameno).mappedBytes());
    m_frames.at(frameno).unmap();
    return QString(QCryptographicHash::hash(arr, QCryptographicHash::Md5).toHex());

    /*
     * To be used after successful file processing later for CDG fingerprinting for auto-renaming
    qInfo() << "CDG Hash at 10s:  " << md5HashByTime(10000);
    qInfo() << "CDG Hash at 30s:  " << md5HashByTime(30000);
    qInfo() << "CDG Hash at 60s:  " << md5HashByTime(60000);
    qInfo() << "CDG Hash at 90s:  " << md5HashByTime(90000);
    qInfo() << "CDG Hash at 120s: " << md5HashByTime(120000);
    */

}

unsigned int CdgParser::duration()
{
    return m_cdgData.size() * 40;
}

bool CdgParser::isOpen()
{
    return m_isOpen;
}

unsigned int CdgParser::lastCDGUpdate()
{
    return m_lastCDGCommandMS;
}

int CdgParser::tempo()
{
    return m_tempo;
}

void CdgParser::setTempo(const int &percent)
{
    m_tempo = percent;
}

void CdgParser::cmdScroll(const cdg::CdgScrollCmdData &scrollCmdData, const cdg::ScrollType type)
{
    if (scrollCmdData.hSCmd == 2)
    {
        // scroll left 6px
        for (auto i=0; i < 216; i++)
        {
            auto bits = m_image.scanLine(i);
            unsigned char* tmpPixels[6];
            memcpy(tmpPixels, bits, 6);
            memcpy(bits, bits + (6 * m_bytesPerPixel), 294 * m_bytesPerPixel);
            if (type == cdg::ScrollCopy)
                memcpy(bits + m_borderRBytesOffset, tmpPixels, 6);
            else
                memset(bits + m_borderLRBytes, scrollCmdData.color, 6);
        }
    }
    if (scrollCmdData.hSCmd == 1)
    {
        // scroll right 6px
        for (auto i=0; i < 216; i++)
        {
            auto bits = m_image.scanLine(i);
            unsigned char* tmpPixels[6];
            memcpy(tmpPixels, bits + (m_bytesPerPixel * 294), 6);
            memcpy(bits + (6 * m_bytesPerPixel), bits , 294 * m_bytesPerPixel);
            if (type == cdg::ScrollCopy)
                memcpy(bits, tmpPixels, 6);
            else
                memset(bits, scrollCmdData.color, 6);
        }
    }
    if (scrollCmdData.vSCmd == 2)
    {
        // scroll up 12px
        auto bits = m_image.bits();
        unsigned char* tmpLines[3600]; // m_image.bytesPerLine() * 12
        memcpy(tmpLines, bits, m_image.bytesPerLine() * 12);
        memcpy(bits, bits + m_image.bytesPerLine() * 12, 204 * m_image.bytesPerLine());
        if (type == cdg::ScrollCopy)
            memcpy(bits + (204 * m_image.bytesPerLine()), tmpLines, m_image.bytesPerLine() * 12);
        else
            memset(bits + (204 * m_image.bytesPerLine()), scrollCmdData.color, m_image.bytesPerLine() * 12);
    }
    if (scrollCmdData.vSCmd == 1)
    {
        // scroll down 12px
        auto bits = m_image.bits();
        unsigned char* tmpLines[3600];
        memcpy(tmpLines, bits + (m_image.bytesPerLine() * 204), m_image.bytesPerLine() * 12);
        memcpy(bits + (m_image.bytesPerLine() * 12), bits, 204 * m_image.bytesPerLine());
        if (type == cdg::ScrollCopy)
            memcpy(bits, tmpLines, m_image.bytesPerLine() * 12);
        else
            memset(bits, scrollCmdData.color, m_image.bytesPerLine() * 12);
    }
    if (m_curVOffset != scrollCmdData.vSOffset)
    m_curHOffset = scrollCmdData.hSOffset;
    m_curVOffset = scrollCmdData.vSOffset;
    m_needupdate = true;

}

void CdgParser::cmdDefineTransparent([[maybe_unused]] const std::array<char,16> &data)
{
    qInfo() << "libCDG - unsupported DefineTransparent command called";
    // Unused CDG command from redbook spec
    // I've never actually seen this command used in the wild on commercial CD+Gs
    // No idea what the data structure is, it's missing from CDG Revealed
}
