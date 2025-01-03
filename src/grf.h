#pragma once

#include <stdio.h>
#include <stdint.h>

#include "compression.h"

#define GRF_MKTAG(c1,c2,c3,c4)   (((c1)<<0)|((c2)<<8)|((c3)<<16)|((c4)<<24))
#define GRF_VERSION              2

typedef enum GrfGfxAttr_ {
	GRF_GFX_ATTR_2BIT      = 0x02,        // palette4
	GRF_GFX_ATTR_4BIT      = 0x04,        // palette16,  char 4-bit
	GRF_GFX_ATTR_8BIT      = 0x08,        // palette256, char 8-bit, bitmap 8-bit
	GRF_GFX_ATTR_16BIT     = 0x10,        // direct,                 bitmap 16-bit
	GRF_GFX_ATTR_A5I3      = 0x80,        // a5i3
	GRF_GFX_ATTR_A3I5      = 0x81,        // a3i5
	GRF_GFX_ATTR_TEX4x4    = 0x82         // tex4x4
} GrfGfxAttr;


typedef struct GrfFileHeader_ {
	uint32_t signature;                   // RIFF file signature
	uint32_t fileSize;                    // RIFF file size
	uint32_t fileIdentifier;              // GRF file identifier
} GrfFileHeader;

typedef struct GrfBinaryBlockHeader_ {
	uint32_t signature;                   // GRF binary block signature
	uint32_t size;                        // GRF binary block size
} GrfBinaryBlockHeader;

typedef struct GrfHeader_ {
	uint16_t version;                     // GRF format version
	uint16_t gfxAttr;                     // Graphics attributes
	uint16_t scrUnit;                     // bits per unit of BG screen data
	uint16_t metaUnit;                    // bits per unit of meta data
	uint16_t nPlttColors;                 // number of palette colors
	uint8_t chrWidth;                     // width of character unit
	uint8_t chrHeight;                    // height of character unit
	uint8_t metaWidth;                    // width of meta unit
	uint8_t metaHeight;                   // height of meta unit
	uint32_t gfxWidth;                    // width of graphics
	uint32_t gfxHeight;                   // height of graphics
} GrfHeader;



int GrfWriteHeader(
	FILE                 *fp          // file handle
);

int GrfWriteHdr(
	FILE                 *fp,          // file handle
	GrfGfxAttr            gfxAttr,
	int                   scrUnit,
	int                   metaUnit,
	int                   nPlttColors,
	int                   chrWidth,
	int                   chrHeight, 
	int                   metaWidth,
	int                   metaHeight,
	int                   gfxWidth,
	int                   gfxHeight
);

int GrfBgWriteHdr(
	FILE                 *fp,          // file handle
	int                   depth,       // BG graphics bit depth
	int                   scrUnit,     // BG screen data unit size in bits
	int                   width,       // BG screen width in pixels
	int                   height,      // BG screen height in pixels
	int                   paletteSize  // BG palette size in colors
);

int GrfTexWriteHdr(
	FILE                 *fp,          // file handle
	int                   fmt,         // texture format
	int                   width,       // texture width in pixels
	int                   height,      // texture height in pixels
	int                   paletteSize  // size of texture palette in colors
);

int GrfWritePltt(
	FILE                 *fp,          // file handle
	const void           *data,        // color palette data
	unsigned int          nColors,     // size of color palette in colors
	CxCompressionPolicy   compress     // data compression policy
);

int GrfWriteGfx(
	FILE                 *fp,          // file handle
	const void           *data,        // graphics data
	unsigned int          size,        // size of graphics data in bytes
	CxCompressionPolicy   compress     // data compression policy
);

int GrfWriteScr(
	FILE                 *fp,          // file handle
	const void           *data,        // BG screen data
	unsigned int          size,        // size of BG screen data
	CxCompressionPolicy   compress     // data compression policy
);

int GrfWriteTexImage(
	FILE                 *fp,          // file handle
	const void           *txel,        // texel data
	unsigned int          txelSize,    // texel data size
	const void           *pidx,        // palette index data
	unsigned int          pidxSize,    // size of palette index data
	CxCompressionPolicy   compress     // data compression policy
);

int GrfFinalize(
	FILE                 *fp           // file handle
);

