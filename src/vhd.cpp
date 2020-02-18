#include <abprec.h>
#include <stdint.h>
#include <iostream>
#include <fstream>
#include <string.h>
#include "vhd.h"
#include "vd.h"

using namespace std;

#define HEADER_SIZE 512

enum vhd_type {
    VHD_FIXED = 2,
    VHD_DYNAMIC = 3,
    VHD_DIFFERENCING = 4,
};

#define KiB              (1 * 1024)
#define MiB            (KiB * 1024)
#define GiB            (MiB * 1024)
#define TiB ((uint64_t) GiB * 1024)

/* Seconds since Jan 1, 2000 0:00:00 (UTC) */
#define VHD_TIMESTAMP_BASE 946684800

#define VHD_CHS_MAX_C   65535LL
#define VHD_CHS_MAX_H   16
#define VHD_CHS_MAX_S   255

#define VHD_MAX_GEOMETRY      (VHD_CHS_MAX_C * VHD_CHS_MAX_H * VHD_CHS_MAX_S)

#define VPC_OPT_FORCE_SIZE "force_size"

#define VHD_SECTOR_SIZE 512
#define VHD_BLOCK_SIZE  (2 * MiB)
#define VHD_MAX_SIZE    (2 * TiB)
/** Maximum number of 512 byte sectors for a VHD image. */
#define VHD_MAX_SECTORS (VHD_MAX_SIZE / VHD_SECTOR_SIZE)

typedef struct VHDFooter
{
    char     Cookie[8];
    uint32_t Features;
    uint32_t Version;
    uint64_t DataOffset;
    uint32_t Timestamp;
    uint8_t  CreatorApp[4];
    uint32_t CreatorVer;
    uint32_t CreatorOS;
    uint64_t OrigSize;
    uint64_t CurSize;
    uint16_t DiskGeometryCylinder;
    uint8_t  DiskGeometryHeads;
    uint8_t  DiskGeometrySectors;
    uint32_t DiskType;
    uint32_t Checksum;
    char     UniqueID[16];
    uint8_t  SavedState;
    uint8_t  Reserved[427];
} VHDFooter;


/* this really is spelled with only one n */
#define VHD_FOOTER_COOKIE "conectix"
#define VHD_FOOTER_COOKIE_SIZE 8

#define VHD_FOOTER_FEATURES_NOT_ENABLED   0
#define VHD_FOOTER_FEATURES_TEMPORARY     1
#define VHD_FOOTER_FEATURES_RESERVED      2

#define VHD_FOOTER_FILE_FORMAT_VERSION    0x00010000
#define VHD_FOOTER_DATA_OFFSET_FIXED      UINT64_C(0xffffffffffffffff)
#define VHD_FOOTER_DISK_TYPE_FIXED        2
#define VHD_FOOTER_DISK_TYPE_DYNAMIC      3
#define VHD_FOOTER_DISK_TYPE_DIFFERENCING 4

#define VHD_MAX_LOCATOR_ENTRIES           8
#define VHD_PLATFORM_CODE_NONE            0
#define VHD_PLATFORM_CODE_WI2R            0x57693272
#define VHD_PLATFORM_CODE_WI2K            0x5769326B
#define VHD_PLATFORM_CODE_W2RU            0x57327275
#define VHD_PLATFORM_CODE_W2KU            0x57326B75
#define VHD_PLATFORM_CODE_MAC             0x4D163220
#define VHD_PLATFORM_CODE_MACX            0x4D163258

typedef struct VHDParentLocatorEntry
{
    uint32_t u32Code;
    uint32_t u32DataSpace;
    uint32_t u32DataLength;
    uint32_t u32Reserved;
    uint64_t u64DataOffset;
} VHDPLE, *PVHDPLE;

typedef struct VHDDynamicDiskHeader
{
    char     Cookie[8];
    uint64_t DataOffset;
    uint64_t TableOffset;
    uint32_t HeaderVersion;
    uint32_t MaxTableEntries;
    uint32_t BlockSize;
    uint32_t Checksum;
    uint8_t  ParentUuid[16];
    uint32_t ParentTimestamp;
    uint32_t Reserved0;
    uint16_t ParentUnicodeName[256];
    VHDPLE   ParentLocatorEntry[VHD_MAX_LOCATOR_ENTRIES];
    uint8_t  Reserved1[256];
} VHDDynamicDiskHeader;


#define VHD_DYNAMIC_DISK_HEADER_COOKIE "cxsparse"
#define VHD_DYNAMIC_DISK_HEADER_COOKIE_SIZE 8
#define VHD_DYNAMIC_DISK_HEADER_VERSION 0x00010000


typedef struct VDVHDState
{
    uint32_t diskType;
    uint32_t blockSize;
    uint32_t cSectorsPerDataBlock;
    uint32_t cbDataBlockBitmap;
    uint32_t cDataBlockBitmapSectors;
    uint32_t cBlockAllocationTableEntries;
    uint32_t *pBlockAllocationTable;
    uint64_t uBlockAllocationTableOffset;
    uint64_t curSize;
}VDVHDState;



bool vhdBlockBitmapSectorContainsData(uint8_t *pu8Bitmap, uint32_t cBlockBitmapEntry)
{
    uint32_t iBitmap = (cBlockBitmapEntry / 8); /* Byte in the block bitmap. */

    uint8_t  iBitInByte = (8 - 1) - (cBlockBitmapEntry % 8);
    uint8_t *puBitmap =pu8Bitmap + iBitmap;

    return ((*puBitmap) & (1<<iBitInByte)) != 0;
}

void VHDParser::vhdParseHeader(VDVHDState *pImage)
{
    uint64_t fileSize;
    VHDFooter vhdFooter;
    pImage->diskType = VHD_DYNAMIC;
    fileSize = GetFileSize(fileHandle);
    Read(fileHandle, 0, (char *)&vhdFooter, sizeof(VHDFooter));
    if (memcmp(vhdFooter.Cookie, VHD_FOOTER_COOKIE, VHD_FOOTER_COOKIE_SIZE) != 0) {
        Read(fileHandle, fileSize - sizeof(VHDFooter), (char *)&vhdFooter, sizeof(VHDFooter));
        if (memcmp(vhdFooter.Cookie, VHD_FOOTER_COOKIE, VHD_FOOTER_COOKIE_SIZE) != 0) {
            throw exception("vhd format error");
        }
        pImage->diskType = VHD_FIXED;
    }

    //uint64_t total_sectors = swap64(vhdFooter.CurSize) / 512;

    pImage->curSize = swap64(vhdFooter.CurSize);
    VHDDynamicDiskHeader vhdDynamicDiskHeader;
    uint32_t *pBlockAllocationTable;
    if (pImage->diskType == VHD_DYNAMIC) {
        Read(fileHandle, swap64(vhdFooter.DataOffset), (char *)&vhdDynamicDiskHeader, sizeof(VHDDynamicDiskHeader));
        pImage->blockSize = swap32(vhdDynamicDiskHeader.BlockSize);
        pImage->cSectorsPerDataBlock = pImage->blockSize / VHD_SECTOR_SIZE;
        pImage->cbDataBlockBitmap = pImage->cSectorsPerDataBlock / 8;
        pImage->cDataBlockBitmapSectors = pImage->cbDataBlockBitmap / VHD_SECTOR_SIZE;
        pImage->cBlockAllocationTableEntries = swap32(vhdDynamicDiskHeader.MaxTableEntries);
        pBlockAllocationTable = (uint32_t *)malloc(pImage->cBlockAllocationTableEntries * 4);
        if (!pBlockAllocationTable)
            throw exception("malloc memory error");

        pImage->uBlockAllocationTableOffset = swap64(vhdDynamicDiskHeader.TableOffset);
        Read(fileHandle, pImage->uBlockAllocationTableOffset, (char *)pBlockAllocationTable, pImage->cBlockAllocationTableEntries * 4);
        pImage->pBlockAllocationTable = (uint32_t *)malloc(pImage->cBlockAllocationTableEntries * 4);
        for (size_t i = 0; i < pImage->cBlockAllocationTableEntries; i++) {
            pImage->pBlockAllocationTable[i] = swap32(pBlockAllocationTable[i]);
        }

        free(pBlockAllocationTable);
        
        //uint8_t *pu8Bitmap = (uint8_t *)malloc(pImage->cbDataBlockBitmap);
        //Read(fileHandle, pBlockAllocationTable2[i] * VHD_SECTOR_SIZE, (char *)pu8Bitmap, cbDataBlockBitmap);
        //for (int j = 0; i < cSectorsPerDataBlock; ++j) {
        //    vhdBlockBitmapSectorContainsData(pu8Bitmap, j);
        //}
    }
}

void VHDParser::vhdInit(VDVHDState *pImage)
{
    pImage->pBlockAllocationTable = NULL;
}


NS_IMPL_ISUPPORTS1(VHDParser, ncIVDParser)

VHDParser::VHDParser()
{
    pImage = 0;
}

VHDParser::~VHDParser()
{
    Close();
}



NS_IMETHODIMP_(void)
VHDParser::Open(const std::string & filePath)
{
    fileHandle.open(filePath.c_str(), ios::in | ios::binary);
    if (fileHandle.fail()) {
        throw exception("open file failed");
    }
    pImage = (VDVHDState *)malloc(sizeof(VDVHDState));
    vhdInit(pImage);
    vhdParseHeader(pImage);
}

NS_IMETHODIMP_(void)
VHDParser::Close()
{
    fileHandle.close();
    if (pImage && pImage->pBlockAllocationTable) {
        free(pImage->pBlockAllocationTable);
        pImage->pBlockAllocationTable = NULL;
    }
    if (pImage)
    {
        free(pImage);
        pImage = NULL;
    }
}


NS_IMETHODIMP_(void)
VHDParser::GetDataAreaList(std::list<DataArea> & arealist)
{
    if (pImage->diskType == VHD_DYNAMIC) {
        for (uint64_t i = 0; i < pImage->cBlockAllocationTableEntries; ++i) {
            if (pImage->pBlockAllocationTable[i] == ~0U) {
                continue;
            }
            DataArea area;
            area.offset = (uint32_t)((i*pImage->cSectorsPerDataBlock*VHD_SECTOR_SIZE) / MiB);
            area.length = pImage->blockSize / MiB;
            arealist.push_back(area);
        }
    }
    else {
        DataArea area;
        area.offset = 0;
        area.length = (uint32_t)(pImage->curSize / MiB);
        arealist.push_back(area);
    }
}
