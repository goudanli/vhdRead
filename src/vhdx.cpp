// vhdxTest.cpp 
#include <abprec.h>
#include <stdint.h>
#include "queue.h"
#include <iostream>
#include <fstream>
#include <string.h>
#include "vhdx.h"
#include "vd.h"
using namespace std;

/*********************************************************************************************************************************
*   On disk data structures                                                                                                      *
*********************************************************************************************************************************/
/**
* VHDX file type identifier.
*
*/

#define KiB              (1 * 1024)
#define MiB            (KiB * 1024)
#define GiB            (MiB * 1024)
#define TiB ((uint64_t) GiB * 1024)

#define DEFAULT_LOG_SIZE  1048576 /* 1MiB */
/* Structures and fields present in the VHDX file */

/* The header section has the following blocks,
* each block is 64KB:
*
* _____________________________________________________________________________________________
* | File Id. |   Header 1    | Header 2   | Region Table1 | Region Table2 | Reserved (768KB)  |
* |----------|---------------|------------|---------------|---------------|-------------------|
* |          |               |            |               |               |                   |
* 0.........64KB...........128KB........192KB...........256KB...........320KB................1MB
*/

#define VHDX_HEADER_BLOCK_SIZE      (64 * 1024)

#define VHDX_FILE_ID_OFFSET         0
/** Start offset of the first VHDX header. */
#define VHDX_HEADER1_OFFSET         (VHDX_HEADER_BLOCK_SIZE * 1)
/** Start offset of the second VHDX header. */
#define VHDX_HEADER2_OFFSET         (VHDX_HEADER_BLOCK_SIZE * 2)
#define VHDX_REGION_TABLE_OFFSET    (VHDX_HEADER_BLOCK_SIZE * 3)
#define VHDX_REGION_TABLE2_OFFSET   (VHDX_HEADER_BLOCK_SIZE * 4)

#define VHDX_HEADER_SECTION_END     (1 * MiB)

/*
* A note on the use of MS-GUID fields.  For more details on the GUID,
* please see: https://en.wikipedia.org/wiki/Globally_unique_identifier.
*
* The VHDX specification only states that these are MS GUIDs, and which
* bytes are data1-data4. It makes no mention of what algorithm should be used
* to generate the GUID, nor what standard.  However, looking at the specified
* known GUID fields, it appears the GUIDs are:
*  Standard/DCE GUID type  (noted by 10b in the MSB of byte 0 of .data4)
*  Random algorithm        (noted by 0x4XXX for .data3)
*/

/* ---- HEADER SECTION STRUCTURES ---- */

/* These structures are ones that are defined in the VHDX specification
* document */
/** VHDX file type identifier signature ("vhdxfile"). */
#define VHDX_FILE_SIGNATURE 0x656C696678646876ULL
/** Start offset of the VHDX file type identifier. */
#define VHDX_FILE_IDENTIFIER_OFFSET    0ULL

typedef struct VHDXFileIdentifier {
    /** u64Signature "vhdxfile" in ASCII */
    uint64_t    u64Signature;
    /** optional; utf-16 string to identify the vhdx file Creator.  Diagnostic only */
    uint16_t    Creator[256];                                                
} VHDXFileIdentifier;


/* the guid is a 16 byte unique ID - the definition for this used by
* Microsoft is not just 16 bytes though - it is a structure that is defined,
* so we need to follow it here so that endianness does not trip us up */

typedef struct MSGUID {
    uint32_t  data1;
    uint16_t  data2;
    uint16_t  data3;
    uint8_t   data4[8];
} MSGUID;


#define guid_eq(a, b) (memcmp(&(a), &(b), sizeof(MSGUID)) == 0)

/* although the vhdx_header struct in disk is only 582 bytes, 
for purposes of crc the header is the first 4KB of the 64KBb block */
#define VHDX_HEADER_SIZE (4 * 1024)   


/* The full header is 4KB, although the actual header data is much smaller.
* But for the checksum calculation, it is over the entire 4KB structure,
* not just the defined portion of it */

typedef struct VHDXHeader 
{
    /* "head" in ASCII */
    uint32_t    signature;
    /* CRC-32C hash of the whole header */
    uint32_t    checksum;
    /* Seq number of this header.  Each VHDX file has 2 of these headers,
    and only the header with the highest
    sequence number is valid */
    uint64_t    sequence_number;
    /* 128 bit unique identifier. Must be
    updated to new, unique value before
    the first modification is made to
    file */
    MSGUID      file_write_guid;
    /* 128 bit unique identifier. Must be
                                    updated to new, unique value before
                                    the first modification is made to
                                    visible data.   Visbile data is
                                    defined as:
                                    - system & user metadata
                                    - raw block data
                                    - disk size
                                    - any change that will
                                    cause the virtual disk
                                    sector read to differ
                                    This does not need to change if
                                    blocks are re-arranged */
    MSGUID      data_write_guid;
    /* 128 bit unique identifier. If zero,
                                        there is no valid log. If non-zero,
                                        log entries with this guid are
                                        valid. */
    MSGUID      log_guid;
    /* version of the log format. Must be set to zero */
    uint16_t    log_version;
    /* version of the vhdx file. Currently,only supported version is "1" */
    uint16_t    version;
    /* length of the log.  Must be multiple of 1MB */
    uint32_t    log_length;
    /* byte offset in the file of the log. Must also be a multiple of 1MB */
    uint64_t    log_offset;
    /* Reserved bytes. */
    uint8_t     u8Reserved[4016];
} VHDXHeader;

/** VHDX header signature ("head"). */
#define VHDX_HEADER_SIGNATURE 0x64616568

/* Header for the region table block */
#define VHDX_REGION_SIGNATURE  0x69676572  /* "regi" in ASCII */

typedef struct VHDXRegionTableHeader 
{
    /* "regi" in ASCII */
    uint32_t    signature;
    /* CRC-32C hash of the 64KB table */
    uint32_t    checksum;
    /* number of valid entries */
    uint32_t    entry_count;
    /*reserved*/
    uint32_t    reserved;
} VHDXRegionTableHeader;



/* Individual region table entry.  There may be a maximum of 2047 of these
*
*  There are two known region table properties.  Both are required.
*  BAT (block allocation table):  2DC27766F62342009D64115E9BFD4A08
*  Metadata:                      8B7CA20647904B9AB8FE575F050F886E
*/
#define VHDX_REGION_ENTRY_REQUIRED  0x01    /* if set, parser must understand this entry in order to open file */
/** UUID for the BAT region. */
#define VHDX_REGION_TBL_ENTRY_UUID_BAT          "2dc27766-f623-4200-9d64-115e9bfd4a08"
/** UUID for the metadata region. */
#define VHDX_REGION_TBL_ENTRY_UUID_METADATA     "8b7ca206-4790-4b9a-b8fe-575f050f886e"

typedef struct VHDXRegionTableEntry {
    /* 128-bit unique identifier */
    MSGUID      guid;
    /* offset of the object in the file.Must be multiple of 1MB */
    uint64_t    file_offset;
    /* length, in bytes, of the object */
    uint32_t    length;
    uint32_t    data_bits;
} VHDXRegionTableEntry;



/* ---- LOG ENTRY STRUCTURES ---- */
#define VHDX_LOG_MIN_SIZE (1024 * 1024)
#define VHDX_LOG_SECTOR_SIZE 4096
#define VHDX_LOG_HDR_SIZE 64
/** VHDX log entry signature ("loge"). */
#define VHDX_LOG_SIGNATURE 0x65676f6c

typedef struct VHDXLogEntryHeader {
    uint32_t    signature;              /* "loge" in ASCII */
    uint32_t    checksum;               /* CRC-32C hash of the 64KB table */
    uint32_t    entry_length;           /* length in bytes, multiple of 1MB */
    uint32_t    tail;                   /* byte offset of first log entry of a
                                        seq, where this entry is the last
                                        entry */
    uint64_t    sequence_number;        /* incremented with each log entry.
                                        May not be zero. */
    uint32_t    descriptor_count;       /* number of descriptors in this log
                                        entry, must be >= 0 */
    uint32_t    reserved;
    MSGUID      log_guid;               /* value of the log_guid from
                                        vhdx_header.  If not found in
                                        vhdx_header, it is invalid */
    uint64_t    flushed_file_offset;    /* see spec for full details - this
                                        should be vhdx file size in bytes */
    uint64_t    last_file_offset;       /* size in bytes that all allocated
                                        file structures fit into */
} VHDXLogEntryHeader;


#define VHDX_LOG_DESC_SIZE 32
/** Signature of a VHDX log data descriptor ("desc"). */
#define VHDX_LOG_DESC_SIGNATURE 0x63736564
/** Signature of a VHDX log zero descriptor ("zero"). */
#define VHDX_LOG_ZERO_SIGNATURE 0x6f72657a
typedef struct VHDXLogDescriptor {
    uint32_t    signature;              /* "zero" or "desc" in ASCII */
    union {
        uint32_t    reserved;           /* zero desc */
        uint32_t    trailing_bytes;     /* data desc: bytes 4092-4096 of the
                                        data sector */
    };
    union {
        uint64_t    zero_length;        /* zero desc: length of the section to
                                        zero */
        uint64_t    leading_bytes;      /* data desc: bytes 0-7 of the data
                                        sector */
    };
    uint64_t    file_offset;            /* file offset to write zeros - multiple
                                        of 4kB */
    uint64_t    sequence_number;        /* must match same field in
                                        vhdx_log_entry_header */
} VHDXLogDescriptor;


/** Signature of a VHDX log data sector ("data"). */
#define VHDX_LOG_DATA_SIGNATURE 0x61746164
typedef struct VHDXLogDataSector {
    uint32_t    data_signature;         /* "data" in ASCII */
    uint32_t    sequence_high;          /* 4 MSB of 8 byte sequence_number */
    uint8_t     data[4084];             /* raw data, bytes 8-4091 (inclusive).
                                        see the data descriptor field for the
                                        other mising bytes */
    uint32_t    sequence_low;           /* 4 LSB of 8 byte sequence_number */
} VHDXLogDataSector;




/* block states - different state values depending on whether it is a
* payload block, or a sector block. */

#define PAYLOAD_BLOCK_NOT_PRESENT       0
#define PAYLOAD_BLOCK_UNDEFINED         1
#define PAYLOAD_BLOCK_ZERO              2
#define PAYLOAD_BLOCK_UNMAPPED          3
#define PAYLOAD_BLOCK_UNMAPPED_v095     5
#define PAYLOAD_BLOCK_FULLY_PRESENT     6
#define PAYLOAD_BLOCK_PARTIALLY_PRESENT 7

#define SB_BLOCK_NOT_PRESENT    0
#define SB_BLOCK_PRESENT        6

/* per the spec */
#define VHDX_MAX_SECTORS_PER_BLOCK  (1 << 23)

/* upper 44 bits are the file offset in 1MB units lower 3 bits are the state
other bits are reserved */
#define VHDX_BAT_STATE_BIT_MASK 0x07
#define VHDX_BAT_FILE_OFF_MASK  0xFFFFFFFFFFF00000ULL /* upper 44 bits */
typedef uint64_t VHDXBatEntry;
typedef VHDXBatEntry *PVhdxBatEntry;


/* ---- METADATA REGION STRUCTURES ---- */

#define VHDX_METADATA_ENTRY_SIZE 32
#define VHDX_METADATA_MAX_ENTRIES 2047  /* not including the header */
#define VHDX_METADATA_TABLE_MAX_SIZE (VHDX_METADATA_ENTRY_SIZE * (VHDX_METADATA_MAX_ENTRIES+1))

/** Signature of a VHDX metadata table header ("metadata"). */
#define VHDX_METADATA_SIGNATURE 0x617461646174656DULL  /* "metadata" in ASCII */
typedef struct VHDXMetadataTableHeader {
    uint64_t    signature;              /* "metadata" in ASCII */
    uint16_t    reserved;
    uint16_t    entry_count;            /* number table entries. <= 2047 */
    uint32_t    reserved2[5];
} VHDXMetadataTableHeader;


#define VHDX_META_FLAGS_IS_USER         0x01    /* max 1024 entries */
#define VHDX_META_FLAGS_IS_VIRTUAL_DISK 0x02    /* virtual disk metadata if set,
otherwise file metdata */
#define VHDX_META_FLAGS_IS_REQUIRED     0x04    /* parse must understand this entry to open the file */

typedef struct VHDXMetadataTableEntry {
    MSGUID      item_id;                /* 128-bit identifier for metadata */
    uint32_t    offset;                 /* byte offset of the metadata.  At
                                        least 64kB.  Relative to start of
                                        metadata region */
                                        /* note: if length = 0, so is offset */
    uint32_t    length;                 /* length of metadata. <= 1MB. */
    uint32_t    data_bits;              /* least-significant 3 bits are flags,
                                        the rest are reserved (see above) */
    uint32_t    reserved2;
} VHDXMetadataTableEntry;




#define VHDX_PARAMS_LEAVE_BLOCKS_ALLOCED 0x01   /* Do not change any blocks to
be BLOCK_NOT_PRESENT.
If set indicates a fixed
size VHDX file */
#define VHDX_PARAMS_HAS_PARENT           0x02    /* has parent / backing file */

#define VHDX_BLOCK_SIZE_MIN             (1   * MiB)
#define VHDX_BLOCK_SIZE_MAX             (256 * MiB)

typedef struct VHDXFileParameters {
    uint32_t    block_size;             /* size of each payload block, always
                                        power of 2, <= 256MB and >= 1MB. */
    uint32_t data_bits;                 /* least-significant 2 bits are flags,
                                        the rest are reserved (see above) */
} VHDXFileParameters;

#define VHDX_MAX_IMAGE_SIZE  ((uint64_t) 64 * TiB)
typedef struct VHDXVirtualDiskSize {
    /** Size of the virtual disk, in bytes. Must be multiple of the sector size,max of 64TB */
    uint64_t    virtual_disk_size;
} VHDXVirtualDiskSize;


typedef struct VHDXPage83Data {
    MSGUID      page_83_data;           /* unique id for scsi devices that
                                        support page 0x83 */
} VHDXPage83Data;


typedef struct VHDXVirtualDiskLogicalSectorSize {
    uint32_t    logical_sector_size;    /* virtual disk sector size (in bytes).
                                        Can only be 512 or 4096 bytes */
} VHDXVirtualDiskLogicalSectorSize;


typedef struct VHDXVirtualDiskPhysicalSectorSize {
    uint32_t    physical_sector_size;   /* physical sector size (in bytes).
                                        Can only be 512 or 4096 bytes */
} VHDXVirtualDiskPhysicalSectorSize;


typedef struct VHDXParentLocatorHeader {
    MSGUID      locator_type;           /* type of the parent virtual disk. */
    uint16_t    reserved;
    uint16_t    key_value_count;        /* number of key/value pairs for this
                                        locator */
} VHDXParentLocatorHeader;


/* key and value strings are UNICODE strings, UTF-16 LE encoding, no NULs */
typedef struct VHDXParentLocatorEntry {
    uint32_t    key_offset;             /* offset in metadata for key, > 0 */
    uint32_t    value_offset;           /* offset in metadata for value, >0 */
    uint16_t    key_length;             /* length of entry key, > 0 */
    uint16_t    value_length;           /* length of entry value, > 0 */
} VHDXParentLocatorEntry;


/* ----- END VHDX SPECIFICATION STRUCTURES ---- */


typedef struct VHDXMetadataEntries {
    VHDXMetadataTableEntry file_parameters_entry;
    VHDXMetadataTableEntry virtual_disk_size_entry;
    VHDXMetadataTableEntry page83_data_entry;
    VHDXMetadataTableEntry logical_sector_size_entry;
    VHDXMetadataTableEntry phys_sector_size_entry;
    VHDXMetadataTableEntry parent_locator_entry;
    uint16_t present;
} VHDXMetadataEntries;

typedef struct VHDXLogEntries {
    uint64_t offset;
    uint64_t length;
    uint32_t write;
    uint32_t read;
    VHDXLogEntryHeader *hdr;
    void *desc_buffer;
    uint64_t sequence;
    uint32_t tail;
} VHDXLogEntries;

typedef struct VHDXRegionEntry {
    uint64_t start;
    uint64_t end;
    QLIST_ENTRY(VHDXRegionEntry) entries;
} VHDXRegionEntry;

/* ------- Known Region Table GUIDs ---------------------- */
static const MSGUID bat_guid = { 0x2dc27766, 0xf623,0x4200, { 0x9d, 0x64, 0x11, 0x5e,0x9b, 0xfd, 0x4a, 0x08 } };

static const MSGUID metadata_guid = {0x8b7ca206,0x4790,0x4b9a,{ 0xb8, 0xfe, 0x57, 0x5f,0x05, 0x0f, 0x88, 0x6e } };

/* ------- Known Metadata Entry GUIDs ---------------------- */
static const MSGUID file_param_guid = { 0xcaa16737, 0xfa36,0x4d43,{ 0xb3, 0xb6, 0x33, 0xf0,0xaa, 0x44, 0xe7, 0x6b } };

static const MSGUID virtual_size_guid = { 0x2FA54224,0xcd1b,0x4876,{ 0xb2, 0x11, 0x5d, 0xbe,0xd8, 0x3b, 0xf4, 0xb8 } };

static const MSGUID page83_guid = { 0xbeca12ab, 0xb2e6,0x4523,{ 0x93, 0xef, 0xc3, 0x09,0xe0, 0x00, 0xc7, 0x46 } };

static const MSGUID phys_sector_guid = { 0xcda348c7,0x445d,0x4471,{ 0x9c, 0xc9, 0xe9, 0x88,0x52, 0x51, 0xc5, 0x56 } };

static const MSGUID parent_locator_guid = {0xa8d35f2d, 0xb30b, 0x454d,{ 0xab, 0xf7, 0xd3,0xd8, 0x48, 0x34,0xab, 0x0c } };

static const MSGUID logical_sector_guid = { 0x8141bf1d, 0xa96f, 0x4709,{ 0xba, 0x47, 0xf2,0x33, 0xa8, 0xfa,0xab, 0x5f } };

static const MSGUID parent_vhdx_guid = { 0xb04aefb7, 0xd19e,0x4a81,{ 0xb7, 0x89, 0x25, 0xb8,0xe9, 0x44, 0x59, 0x13 } };

#define META_FILE_PARAMETER_PRESENT      0x01
#define META_VIRTUAL_DISK_SIZE_PRESENT   0x02
#define META_PAGE_83_PRESENT             0x04
#define META_LOGICAL_SECTOR_SIZE_PRESENT 0x08
#define META_PHYS_SECTOR_SIZE_PRESENT    0x10
#define META_PARENT_LOCATOR_PRESENT      0x20

#define META_ALL_PRESENT    \
    (META_FILE_PARAMETER_PRESENT | META_VIRTUAL_DISK_SIZE_PRESENT | \
     META_PAGE_83_PRESENT | META_LOGICAL_SECTOR_SIZE_PRESENT | \
     META_PHYS_SECTOR_SIZE_PRESENT)

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

typedef struct VDVHDXState {
    int curr_header;
    VHDXHeader *headers[2];

    VHDXRegionTableHeader rt;
    VHDXRegionTableEntry bat_rt;         /* region table for the BAT */
    VHDXRegionTableEntry metadata_rt;    /* region table for the metadata */

    VHDXMetadataTableHeader metadata_hdr;
    VHDXMetadataEntries metadata_entries;

    VHDXFileParameters params;
    uint32_t block_size;
    uint32_t block_size_bits;
    uint32_t sectors_per_block;
    uint32_t sectors_per_block_bits;
    
    uint64_t virtual_disk_size;
    uint32_t logical_sector_size;
    uint32_t physical_sector_size;

    uint64_t chunk_ratio;
    uint32_t chunk_ratio_bits;
    uint32_t logical_sector_size_bits;

    uint32_t bat_entries;
    VHDXBatEntry *bat;
    uint64_t bat_offset;

    VHDXParentLocatorHeader parent_header;
    VHDXParentLocatorEntry *parent_entries;

    QLIST_HEAD(, VHDXRegionEntry) regions;
} VDVHDXState;


static uint32_t __inline ctz32(uint32_t x)
{
    unsigned  long r = 0;
    _BitScanReverse(&r, x);
    return r;
}

static uint32_t __inline ctz64(uint64_t x)
{
    unsigned  long r = 0;
    _BitScanReverse64(&r, x);
    return r;
}

static uint32_t __inline clz32(uint32_t x)
{
    unsigned  long  r = 0;
    _BitScanForward(&r, x);
    return r;
}

static uint32_t __inline clz64(uint32_t x)
{
    unsigned  long  r = 0;
    _BitScanForward64(&r, x);
    return r;
}



void VHDXParser::vhdxRegionUnregisterAll(VDVHDXState *s)
{
    VHDXRegionEntry *r, *r_next;

    QLIST_FOREACH_SAFE(r, &s->regions, entries, r_next) {
        QLIST_REMOVE(r, entries);
        free(r);
    }
}

void VHDXParser::vhdxParseHeader(VDVHDXState *s)
{
    VHDXHeader *header1;
    VHDXHeader *header2;
    bool h1_valid = false;
    bool h2_valid = false;
    uint64_t h1_seq = 0;
    uint64_t h2_seq = 0;
    header1 = (VHDXHeader *)malloc(sizeof(VHDXHeader));
    header2 = (VHDXHeader *)malloc(sizeof(VHDXHeader));
    uint8_t *buffer;
    buffer = (uint8_t *)malloc(VHDX_HEADER_SIZE);
    s->headers[0] = header1;
    s->headers[1] = header2;
    Read(fileHandle, VHDX_HEADER1_OFFSET, (char *)buffer, VHDX_HEADER_SIZE);
    memcpy(header1, buffer, sizeof(VHDXHeader));
    if (header1->signature == VHDX_HEADER_SIGNATURE &&
        header1->version == 1) {
        h1_seq = header1->sequence_number;
        h1_valid = true;
    }
    Read(fileHandle, VHDX_HEADER2_OFFSET,  (char *)buffer, VHDX_HEADER_SIZE);
    memcpy(header2, buffer, sizeof(VHDXHeader));
    if (header2->signature == VHDX_HEADER_SIGNATURE &&
        header2->version == 1) {
        h2_seq = header2->sequence_number;
        h2_valid = true;
    }

    if (h1_valid && !h2_valid) {
        s->curr_header = 0;
    }
    else if (!h1_valid && h2_valid) {
        s->curr_header = 1;
    }
    else if (!h1_valid && !h2_valid) {
        goto fail;
    }
    else {
        /* If both headers are valid, then we choose the active one by the
        * highest sequence number.  If the sequence numbers are equal, that is
        * invalid */
        if (h1_seq > h2_seq) {
            s->curr_header = 0;
        }
        else if (h2_seq > h1_seq) {
            s->curr_header = 1;
        }
        else {
            /* The Microsoft Disk2VHD tool will create 2 identical
            * headers, with identical sequence numbers.  If the headers are
            * identical, don't consider the file corrupt */
            if (!memcmp(header1, header2, sizeof(VHDXHeader))) {
                s->curr_header = 0;
            }
            else {
                goto fail;
            }
        }
    }
    goto exit;

fail:
    free(header1);
    free(header2);
    s->headers[0] = NULL;
    s->headers[1] = NULL;
exit:
    free(buffer);
}

/* Register a region for future checks */
void VHDXParser::vhdxRegionRegister(VDVHDXState *s,uint64_t start, uint64_t length)
{
    VHDXRegionEntry *r;

    r = (VHDXRegionEntry *)malloc(sizeof(*r));

    r->start = start;
    r->end = start + length;

    QLIST_INSERT_HEAD(&s->regions, r, entries);
}

int VHDXParser::vhdxOpenRegionTables(VDVHDXState *s)
{
    int ret = 0;
    uint8_t *buffer;
    int offset = 0;
    VHDXRegionTableEntry rt_entry;
    uint32_t i;
    bool bat_rt_found = false;
    bool metadata_rt_found = false;
    /* We have to read the whole 64KB block, because the crc32 is over the
    * whole block */
    buffer = (uint8_t *)malloc(VHDX_HEADER_BLOCK_SIZE);
    Read(fileHandle, VHDX_REGION_TABLE_OFFSET, (char *)buffer, VHDX_HEADER_BLOCK_SIZE);
    memcpy(&s->rt, buffer, sizeof(s->rt));
    offset += sizeof(s->rt);

    if (s->rt.signature != VHDX_REGION_SIGNATURE) {
        ret = -EINVAL;
        goto fail;
    }
    if (s->rt.entry_count > 2047) {
        ret = -EINVAL;
        goto fail;
    }
    for (i = 0; i < s->rt.entry_count; i++) {
        memcpy(&rt_entry, buffer + offset, sizeof(rt_entry));
        offset += sizeof(rt_entry);
        vhdxRegionRegister(s, rt_entry.file_offset, rt_entry.length);
        /* see if we recognize the entry */
        if (guid_eq(rt_entry.guid, bat_guid)) {
            /* must be unique; if we have already found it this is invalid */
            if (bat_rt_found) {
                ret = -EINVAL;
                goto fail;
            }
            bat_rt_found = true;
            s->bat_rt = rt_entry;
            continue;
        }
        if (guid_eq(rt_entry.guid, metadata_guid)) {
            /* must be unique; if we have already found it this is invalid */
            if (metadata_rt_found) {
                ret = -EINVAL;
                goto fail;
            }
            metadata_rt_found = true;
            s->metadata_rt = rt_entry;
            continue;
        }
        if (rt_entry.data_bits & VHDX_REGION_ENTRY_REQUIRED) {
            /* cannot read vhdx file - required region table entry that
            * we do not understand.  per spec, we must fail to open */
            ret = -ENOTSUP;
            goto fail;
        }
    }
    if (!bat_rt_found || !metadata_rt_found) {
        ret = -EINVAL;
        goto fail;
    }

    s->bat_offset = s->bat_rt.file_offset;

    ret = 0;
fail:
    free(buffer);
    return ret;
}

int VHDXParser::vhdxParseMetadata(VDVHDXState *s)
{
    int ret = 0;
    uint8_t *buffer;
    int offset = 0;
    uint32_t i = 0;
    VHDXMetadataTableEntry md_entry;

    buffer = (uint8_t *)malloc(VHDX_METADATA_TABLE_MAX_SIZE);

    Read(fileHandle, s->metadata_rt.file_offset, (char *)buffer, VHDX_METADATA_TABLE_MAX_SIZE);
    memcpy(&s->metadata_hdr, buffer, sizeof(s->metadata_hdr));
    offset += sizeof(s->metadata_hdr);


    if (s->metadata_hdr.signature != VHDX_METADATA_SIGNATURE) {
        ret = -EINVAL;
        goto exit;
    }

    s->metadata_entries.present = 0;
    if ((s->metadata_hdr.entry_count * sizeof(md_entry)) >
        (VHDX_METADATA_TABLE_MAX_SIZE - offset)) {
        ret = -EINVAL;
        goto exit;
    }
    for (i = 0; i < s->metadata_hdr.entry_count; i++) {
        memcpy(&md_entry, buffer + offset, sizeof(md_entry));
        offset += sizeof(md_entry);
        if (guid_eq(md_entry.item_id, file_param_guid)) {
            if (s->metadata_entries.present & META_FILE_PARAMETER_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.file_parameters_entry = md_entry;
            s->metadata_entries.present |= META_FILE_PARAMETER_PRESENT;
            continue;
        }
        if (guid_eq(md_entry.item_id, virtual_size_guid)) {
            if (s->metadata_entries.present & META_VIRTUAL_DISK_SIZE_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.virtual_disk_size_entry = md_entry;
            s->metadata_entries.present |= META_VIRTUAL_DISK_SIZE_PRESENT;
            continue;
        }

        if (guid_eq(md_entry.item_id, page83_guid)) {
            if (s->metadata_entries.present & META_PAGE_83_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.page83_data_entry = md_entry;
            s->metadata_entries.present |= META_PAGE_83_PRESENT;
            continue;
        }

        if (guid_eq(md_entry.item_id, logical_sector_guid)) {
            if (s->metadata_entries.present &
                META_LOGICAL_SECTOR_SIZE_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.logical_sector_size_entry = md_entry;
            s->metadata_entries.present |= META_LOGICAL_SECTOR_SIZE_PRESENT;
            continue;
        }
        if (guid_eq(md_entry.item_id, phys_sector_guid)) {
            if (s->metadata_entries.present & META_PHYS_SECTOR_SIZE_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.phys_sector_size_entry = md_entry;
            s->metadata_entries.present |= META_PHYS_SECTOR_SIZE_PRESENT;
            continue;
        }

        if (guid_eq(md_entry.item_id, parent_locator_guid)) {
            if (s->metadata_entries.present & META_PARENT_LOCATOR_PRESENT) {
                ret = -EINVAL;
                goto exit;
            }
            s->metadata_entries.parent_locator_entry = md_entry;
            s->metadata_entries.present |= META_PARENT_LOCATOR_PRESENT;
            continue;
        }

        if (md_entry.data_bits & VHDX_META_FLAGS_IS_REQUIRED) {
            /* cannot read vhdx file - required region table entry that
            * we do not understand.  per spec, we must fail to open */
            ret = -ENOTSUP;
            goto exit;
        }
    }
    //avhdx has parent_locator
    /*if (s->metadata_entries.present != META_ALL_PRESENT) {
        ret = -ENOTSUP;
        goto exit;
    }*/

    Read(fileHandle, s->metadata_entries.file_parameters_entry.offset + s->metadata_rt.file_offset, (char *)&s->params, sizeof(s->params));

    /* We now have the file parameters, so we can tell if this is a
    * differencing file (i.e.. has_parent), is dynamic or fixed
    * sized (leave_blocks_allocated), and the block size */
    /* The parent locator required if the file parameters has_parent set */
    if (s->params.data_bits & VHDX_PARAMS_HAS_PARENT) {
        if (s->metadata_entries.present & META_PARENT_LOCATOR_PRESENT) {
           Read(fileHandle, s->metadata_entries.parent_locator_entry.offset + s->metadata_rt.file_offset, (char *)&s->parent_header, sizeof(VHDXParentLocatorHeader));
           
           if (guid_eq(s->parent_header.locator_type, parent_vhdx_guid)) {
               s->parent_entries = (VHDXParentLocatorEntry *)malloc(sizeof(VHDXParentLocatorEntry));
               //
               /*for (i = 0; i < s->parent_header.key_value_count; ++i) {
                   Read(fileHandle, s->metadata_entries.parent_locator_entry.offset + s->metadata_rt.file_offset + sizeof(VHDXParentLocatorHeader) + i * sizeof(VHDXParentLocatorEntry),
                       (char *)(&(s->parent_entries[i])), sizeof(VHDXParentLocatorEntry));
               }*/
           }
        }
        else {
            /* if has_parent is set, but there is not parent locator present,
            * then that is an invalid combination */
        }
    }
    
    /* determine virtual disk size, logical sector size,
    * and phys sector size */

    Read(fileHandle, s->metadata_entries.virtual_disk_size_entry.offset + s->metadata_rt.file_offset, (char *)(&s->virtual_disk_size), sizeof(uint64_t));
    Read(fileHandle, s->metadata_entries.logical_sector_size_entry.offset + s->metadata_rt.file_offset,(char *)(&s->logical_sector_size), sizeof(uint32_t));
    Read(fileHandle, s->metadata_entries.phys_sector_size_entry.offset + s->metadata_rt.file_offset,(char *) (&s->physical_sector_size), sizeof(uint32_t));

    if (s->params.block_size < VHDX_BLOCK_SIZE_MIN ||
        s->params.block_size > VHDX_BLOCK_SIZE_MAX) {
        ret = -EINVAL;
        goto exit;
    }
    /* only 2 supported sector sizes */
    if (s->logical_sector_size != 512 && s->logical_sector_size != 4096) {
        ret = -EINVAL;
        goto exit;
    }
    /* Both block_size and sector_size are guaranteed powers of 2, below.
    Due to range checks above, s->sectors_per_block can never be < 256 */
    s->sectors_per_block = s->params.block_size / s->logical_sector_size;
    s->chunk_ratio = (VHDX_MAX_SECTORS_PER_BLOCK) *(uint64_t)s->logical_sector_size /(uint64_t)s->params.block_size;
    
    /* These values are ones we will want to use for division / multiplication
    * later on, and they are all guaranteed (per the spec) to be powers of 2,
    * so we can take advantage of that for shift operations during
    * reads/writes */
    if (s->logical_sector_size & (s->logical_sector_size - 1)) {
        ret = -EINVAL;
        goto exit;
    }
    if (s->sectors_per_block & (s->sectors_per_block - 1)) {
        ret = -EINVAL;
        goto exit;
    }
    if (s->chunk_ratio & (s->chunk_ratio - 1)) {
        ret = -EINVAL;
        goto exit;
    }
    s->block_size = s->params.block_size;
    if (s->block_size & (s->block_size - 1)) {
        ret = -EINVAL;
        goto exit;
    }

    s->logical_sector_size_bits = ctz32(s->logical_sector_size);
    s->sectors_per_block_bits = ctz32(s->sectors_per_block);
    s->chunk_ratio_bits = ctz64(s->chunk_ratio);
    s->block_size_bits = ctz32(s->block_size);

    ret = 0;
exit:
    free(buffer);
    return ret;
}

/*
* Calculate the number of BAT entries, including sector
* bitmap entries.
*/
void VHDXParser::vhdxCalcBatEntries(VDVHDXState *s)
{
    uint32_t data_blocks_cnt, bitmap_blocks_cnt;

    data_blocks_cnt = (uint32_t)DIV_ROUND_UP(s->virtual_disk_size, s->block_size);//(n+d-1)/d
    bitmap_blocks_cnt = (uint32_t)DIV_ROUND_UP(data_blocks_cnt, s->chunk_ratio);

    if (s->parent_entries) {
        s->bat_entries = (uint32_t)(bitmap_blocks_cnt * (s->chunk_ratio + 1));
    }
    else {
        s->bat_entries = (uint32_t)(data_blocks_cnt + ((data_blocks_cnt - 1) >> s->chunk_ratio_bits));
    }
}

/* Check for region overlaps inside the VHDX image */
int VHDXParser::vhdxRegionCheck(VDVHDXState *s, uint64_t start, uint64_t length)
{
    int ret = 0;
    uint64_t end;
    VHDXRegionEntry *r;
    end = start + length;
    QLIST_FOREACH(r, &s->regions, entries) {
        if (!((start >= r->end) || (end <= r->start))) {
            ret = -EINVAL;
            goto exit;
        }
    }
exit:
    return ret;
}

bool VHDXParser::vhdxSignatureCheck(VDVHDXState *s)
{
    //check file
    uint64_t signature;
    fileHandle.read((char *)&signature, sizeof(uint64_t));
    if (memcmp(&signature, "vhdxfile", 8)) {
        return false;
    }
    return true;
}

void VHDXParser::vhdxInit(VDVHDXState *s)
{
    s->parent_entries = NULL;
    s->headers[0] = NULL;
    s->headers[1] = NULL;
    s->bat = NULL;
    QLIST_INIT(&s->regions);
}

NS_IMETHODIMP_(void) 
VHDXParser::Open(const string & filePath)
{
    s = (VDVHDXState *)malloc(sizeof(VDVHDXState));
    if ( NULL == s) {
        throw exception("malloc memory failed");
    }

    fileHandle.open(filePath.c_str(), ios::in | ios::binary);
    if (fileHandle.fail()) {
        throw exception("open file failed");
    }
    vhdxSignatureCheck(s);
    vhdxInit(s);
    vhdxParseHeader(s);
    int ret = 0;
    ret = vhdxOpenRegionTables(s);
    if (ret < 0) {
        throw exception("vhdxOpenRegionTables failed");
    }
    ret = vhdxParseMetadata(s);
    if (ret < 0) {
        throw exception("vhdxParseMetadata failed");
    }
    vhdxCalcBatEntries(s);

    if (s->bat_entries > s->bat_rt.length / sizeof(VHDXBatEntry)) {
    /* BAT allocation is not large enough for all entries */ 
        throw exception("vhdx format error");
    }
}

NS_IMETHODIMP_(void)
VHDXParser::Close()
{
    fileHandle.close();
    if (s && s->bat) {
        free(s->bat);
        s->bat = NULL;
    }
    if (s && s->parent_entries) {
        free(s->parent_entries);
        s->parent_entries = NULL;
    }
    if (s) {
        if(s->headers[0]) {
            free(s->headers[0]);
            s->headers[0] = NULL;
        }
        if (s->headers[1]) {
            free(s->headers[1]);
            s->headers[1] = NULL;
        }
    }
    if (s) {
        vhdxRegionUnregisterAll(s);
    }
    if (s) {
        free(s);
        s = NULL;
    }
}

NS_IMETHODIMP_(void)
VHDXParser::GetDataAreaList(std::list<DataArea> & arealist)
{
    int ret = 0;
    s->bat = (VHDXBatEntry*)malloc(s->bat_rt.length);
    if (s->bat == NULL) {
        throw exception("malloc  memory failed");
    }
    Read(fileHandle, s->bat_offset, (char *)s->bat, s->bat_rt.length);
    uint64_t payblocks = s->chunk_ratio;
    uint64_t pbindex = 0;
    for (uint32_t i = 0; i < s->bat_entries; ++i) {
        if (payblocks--) {
            /* payload bat entries */
            if (((s->bat[i] & VHDX_BAT_STATE_BIT_MASK) == PAYLOAD_BLOCK_FULLY_PRESENT)
                || ((s->bat[i] & VHDX_BAT_STATE_BIT_MASK) == PAYLOAD_BLOCK_PARTIALLY_PRESENT)) {
                //uint64_t payblocksOffset = s->bat[i] & VHDX_BAT_FILE_OFF_MASK;
                DataArea area;
                area.offset = (uint32_t)((pbindex*s->block_size)/MiB);
                area.length = s->block_size/MiB;
                arealist.push_back(area);
            }
            ++pbindex;
        }
        else {
            payblocks = s->chunk_ratio;
            /* Once differencing files are supported, verify sector bitmap
            * blocks here */
        }
    }
}

NS_IMPL_ISUPPORTS1(VHDXParser, ncIVDParser)

VHDXParser::VHDXParser()
    :s(NULL)
{

}

VHDXParser::~VHDXParser()
{
    Close();
}
