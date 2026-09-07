#include "grf.h"
#include "compression.h"
#include "texture.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

// ----- GRF output code

static int GrfWrite(FILE *fp, const void *p, unsigned int size) {
	return fwrite(p, 1, size, fp) == size;
}

static int GrfEmitBlockHeader(FILE *fp, uint32_t signature, uint32_t size) {
	//round size up to a multiple of 4 in the header
	size = (size + 3) & ~3;
	
	GrfBinaryBlockHeader header;
	header.signature = signature;
	header.size = size;
	return GrfWrite(fp, &header, sizeof(header));
}

static int GrfAlignBlock(FILE *fp, unsigned int dataSize) {
	unsigned int alignment = (4 - (dataSize & 3)) & 3;
	unsigned char pad[3] = { 0, 0, 0 };
	return GrfWrite(fp, pad, alignment);
}

static int GrfWriteBlock(
	FILE        *fp,
	uint32_t     tag,
	const void  *data,
	unsigned int size
) {
	//emit the block header, data, and alignment padding.
	if (!GrfEmitBlockHeader(fp, tag, size)) return 0;
	if (!GrfWrite(fp, data, size))          return 0;
	if (!GrfAlignBlock(fp, size))           return 0;
	
	return 1;
}

static int GrfWriteBlockComp(
	FILE               *fp,
	uint32_t            tag,
	const void         *data,
	unsigned int        size,
	CxCompressionPolicy compress
) {
	unsigned int compSize;
	void *compData = CxCompress(data, size, &compSize, compress);
	if (compData == NULL) return 0;
	
	//write data block block
	int status = GrfWriteBlock(fp, tag, compData, compSize);
	free(compData);
	
	return status;
}


// ----- internal API

#if (GRF_VERSION < 2)

static int GrfBgScreenTypeToBitsPerUnit(GrfBgScreenType type) {
	switch (type) {
		case GRF_SCREEN_TYPE_NONE:
			return 0;
		case GRF_SCREEN_TYPE_TEXT_16x16:
		case GRF_SCREEN_TYPE_TEXT_256x1:
		case GRF_SCREEN_TYPE_AFFINE_EXT:
			return 16;
		case GRF_SCREEN_TYPE_AFFINE:
			return 8;
		default:
			return 0;
	}
}

#endif


// ----- public API

int GrfWriteHeader(FILE *fp) {
	GrfFileHeader header;
	header.signature = GRF_MKTAG('R', 'I', 'F', 'F');
	header.fileIdentifier = GRF_MKTAG('G', 'R', 'F', ' ');
	header.fileSize = sizeof(header);
	return GrfWrite(fp, &header, sizeof(header));
}

int GrfWriteHdr(
	FILE           *fp,
	GrfGfxAttr      gfxAttr,
	GrfBgScreenType scrType,
	int             metaUnit,
	int             nPlttColors,
	int             chrWidth,
	int             chrHeight, 
	int             metaWidth,
	int             metaHeight,
	GrfGfxFlags     flags,
	int             gfxWidth,
	int             gfxHeight
) {

	GrfHeader fileHeader = { 0 };
	fileHeader.version = GRF_VERSION;
	fileHeader.gfxAttr = gfxAttr;
#if (GRF_VERSION >= 2)
	fileHeader.bgScreenType = scrType;
#else
	fileHeader.scrUnit = GrfBgScreenTypeToBitsPerUnit(scrType);
#endif
	fileHeader.metaUnit = metaUnit;
	fileHeader.nPlttColors = nPlttColors;
	fileHeader.chrWidth = chrWidth;
	fileHeader.chrHeight = chrHeight;
	fileHeader.metaWidth = metaWidth;
	fileHeader.metaHeight = metaHeight;
#if (GRF_VERSION >= 2)
	fileHeader.flags = (uint16_t) flags;
#endif
	fileHeader.gfxWidth = gfxWidth;
	fileHeader.gfxHeight = gfxHeight;
	
	return GrfWriteBlock(fp, GRF_TAG_HDRX, &fileHeader, sizeof(fileHeader));
}

int GrfBgWriteHdr(
	FILE           *fp,
	int             depth,
	GrfBgScreenType scrType,
	int             width,
	int             height,
	int             paletteSize
) {
	//write BG header for GRF
	int chrSize = (scrType == GRF_SCREEN_TYPE_NONE) ? 0 : 8;
	return GrfWriteHdr(fp, depth, scrType, 0, paletteSize, chrSize, chrSize, 0, 0, GRF_GFX_FLAG_TYPE_BG, width, height);
}

int GrfTexWriteHdr(
	FILE  *fp,
	int    fmt,
	int    width,
	int    height,
	int    paletteSize,
	int    c0xp
) {
	//convert texture format into what GRF expects
	int gfxAttr = 0;
	switch (fmt) {
		case GX_TEXFMT_PLTT4   : gfxAttr = GRF_GFX_ATTR_2BIT;   break;
		case GX_TEXFMT_PLTT16  : gfxAttr = GRF_GFX_ATTR_4BIT;   break;
		case GX_TEXFMT_PLTT256 : gfxAttr = GRF_GFX_ATTR_8BIT;   break;
		case GX_TEXFMT_A3I5    : gfxAttr = GRF_GFX_ATTR_A3I5;   c0xp = 0; break;
		case GX_TEXFMT_A5I3    : gfxAttr = GRF_GFX_ATTR_A5I3;   c0xp = 0; break;
		case GX_TEXFMT_DIRECT  : gfxAttr = GRF_GFX_ATTR_16BIT;  c0xp = 0; break;
		case GX_TEXFMT_TEX4x4  : gfxAttr = GRF_GFX_ATTR_TEX4x4; c0xp = 0; break;
	}
	
	GrfGfxFlags flags = 0;
	flags |= GRF_GFX_FLAG_TYPE_TEX;
	if (c0xp) flags |= GRF_GFX_FLAG_C0XP;
	
	//write texture header for GRF
	int tileSize = (fmt == CT_4x4) ? 4 : 1;
	return GrfWriteHdr(fp, gfxAttr, GRF_SCREEN_TYPE_NONE, 0, paletteSize, tileSize, tileSize, 0, 0, flags, width, height);
}

int GrfWritePltt(
	FILE               *fp,
	const void         *data,
	unsigned int        nColors,
	CxCompressionPolicy compress
) {
	//encapsulate the data in a compression header
	return GrfWriteBlockComp(fp, GRF_TAG_PAL, data, nColors * 2, compress);
}

int GrfWriteGfx(
	FILE               *fp,
	const void         *data,
	unsigned int        size,
	CxCompressionPolicy compress
) {
	//encapsulate the data in a compression header
	return GrfWriteBlockComp(fp, GRF_TAG_GFX, data, size, compress);
}

int GrfWriteScr(
	FILE               *fp,
	const void         *data,
	unsigned int        size,
	CxCompressionPolicy compress
) {
	//encapsulate the data in a compression header
	return GrfWriteBlockComp(fp, GRF_TAG_MAP, data, size, compress);
}

int GrfWriteTexImage(
	FILE                 *fp,
	const void           *txel,
	unsigned int          txelSize,
	const void           *pidx,
	unsigned int          pidxSize,
	CxCompressionPolicy   compress
) {
	//texture image data encapsulated in GFX block.
	int status = GrfWriteBlockComp(fp, GRF_TAG_GFX, txel, txelSize, compress);
	
	//write PIDX block
	if (pidx != NULL && status) {
		status = GrfWriteBlockComp(fp, GRF_TAG_PIDX, pidx, pidxSize, compress);
	}
	
	return status;
}

int GrfFinalize(FILE *fp) {
	//seek to the start of the file and write the correct data size
	uint32_t pos = ftell(fp) - 8;
	fseek(fp, offsetof(GrfFileHeader, fileSize), SEEK_SET);
	fwrite(&pos, 1, sizeof(pos), fp);
	
	fseek(fp, 0, SEEK_END);
	return 1;
}
