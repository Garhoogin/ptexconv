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

static unsigned int GrfWrite(FILE *fp, const void *p, unsigned int size) {
	return fwrite(p, 1, size, fp);
}

static int GrfEmitBlockHeader(FILE *fp, uint32_t signature, uint32_t size) {
	//round size up to a multiple of 4 in the header
	size = (size + 3) & ~3;
	
	GrfBinaryBlockHeader header;
	header.signature = signature;
	header.size = size;
	return GrfWrite(fp, &header, sizeof(header)) == sizeof(header);
}

static int GrfAlignBlock(FILE *fp, unsigned int dataSize) {
	unsigned int alignment = (4 - (dataSize & 3)) & 3;
	unsigned char pad[3] = { 0, 0, 0 };
	return GrfWrite(fp, pad, alignment) == alignment;
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
	return GrfWrite(fp, &header, sizeof(header)) == sizeof(header);
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
	
	int status = 1;
	if (status) status = GrfEmitBlockHeader(fp, GRF_TAG_HDRX, sizeof(fileHeader));
	if (status) status = GrfWrite(fp, &fileHeader, sizeof(fileHeader));
	return status;
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
	return GrfWriteHdr(fp, depth, scrType, 0, paletteSize, 8, 8, 0, 0, GRF_GFX_FLAG_TYPE_BG, width, height);
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
		case CT_4COLOR:   gfxAttr = GRF_GFX_ATTR_2BIT;   break;
		case CT_16COLOR:  gfxAttr = GRF_GFX_ATTR_4BIT;   break;
		case CT_256COLOR: gfxAttr = GRF_GFX_ATTR_8BIT;   break;
		case CT_A3I5:     gfxAttr = GRF_GFX_ATTR_A3I5;   c0xp = 0; break;
		case CT_A5I3:     gfxAttr = GRF_GFX_ATTR_A5I3;   c0xp = 0; break;
		case CT_DIRECT:   gfxAttr = GRF_GFX_ATTR_16BIT;  c0xp = 0; break;
		case CT_4x4:      gfxAttr = GRF_GFX_ATTR_TEX4x4; c0xp = 0; break;
	}
	
	GrfGfxFlags flags = 0;
	flags |= GRF_GFX_FLAG_TYPE_TEX;
	if (c0xp) flags |= GRF_GFX_FLAG_C0XP;
	
	//write texture header for GRF
	int tileSize = (fmt == CT_4x4) ? 4 : 1;
	return GrfWriteHdr(fp, gfxAttr, GRF_SCREEN_TYPE_NONE, 0, paletteSize, tileSize, tileSize, 0, 0, flags, width, height);
}

int GrfWritePltt(FILE *fp, const void *data, unsigned int nColors, CxCompressionPolicy compress) {
	//encapsulate the data in a compression header
	unsigned int dataSize;
	void *compData = CxCompress(data, nColors * 2, &dataSize, compress);
	if (compData == NULL) return 0;
	
	int status = 1;
	if (status) status = GrfEmitBlockHeader(fp, GRF_TAG_PAL, dataSize);
	if (status) status = GrfWrite(fp, compData, dataSize);
	if (status) status = GrfAlignBlock(fp, dataSize);
	free(compData);
	
	return status;
}

int GrfWriteGfx(FILE *fp, const void *data, unsigned int size, CxCompressionPolicy compress) {
	//encapsulate the data in a compression header
	unsigned int dataSize;
	void *compData = CxCompress(data, size, &dataSize, compress);
	if (compData == NULL) return 0;
	
	int status = 1;
	if (status) status = GrfEmitBlockHeader(fp, GRF_TAG_GFX, dataSize);
	if (status) status = GrfWrite(fp, compData, dataSize);
	if (status) status = GrfAlignBlock(fp, dataSize);
	free(compData);
	
	return status;
}

int GrfWriteScr(FILE *fp, const void *data, unsigned int size, CxCompressionPolicy compress) {
	//encapsulate the data in a compression header
	unsigned int dataSize;
	void *compData = CxCompress(data, size, &dataSize, compress);
	if (compData == NULL) return 0;
	
	int status = 1;
	if (status) status = GrfEmitBlockHeader(fp, GRF_TAG_MAP, dataSize);
	if (status) status = GrfWrite(fp, compData, dataSize);
	if (status) status = GrfAlignBlock(fp, dataSize);
	free(compData);
	
	return status;
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
	unsigned int dataSize;
	void *compData = CxCompress(txel, txelSize, &dataSize, compress);
	if (compData == NULL) return 0;
	
	//write GFX block
	int status = 1;
	if (status) status = GrfEmitBlockHeader(fp, GRF_TAG_GFX, dataSize);
	if (status) status = GrfWrite(fp, compData, dataSize);
	if (status) status = GrfAlignBlock(fp, dataSize);
	free(compData);
	
	//write PIDX block
	if (pidx != NULL && status) {
		unsigned int dataSizePidx;
		void *pidxComp = CxCompress(pidx, pidxSize, &dataSizePidx, compress);
		if (pidxComp == NULL) status = 0;
		
		if (status) status = GrfEmitBlockHeader(fp, GRF_TAG_PIDX, dataSizePidx);
		if (status) status = GrfWrite(fp, pidxComp, dataSizePidx);
		if (status) status = GrfAlignBlock(fp, dataSizePidx);
		if (pidxComp != NULL) free(pidxComp);
	}
	
	return status;
}

int GrfFinalize(FILE *fp) {
	uint32_t pos = ftell(fp) - 8;
	fseek(fp, offsetof(GrfFileHeader, fileSize), SEEK_SET);
	fwrite(&pos, 1, sizeof(pos), fp);
	
	fseek(fp, 0, SEEK_END);
	return 1;
}
