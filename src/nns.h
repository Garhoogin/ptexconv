#pragma once
#include <stdint.h>

#include "bstream.h"

// ----- NNS Stream functions

#define NNS_SIG_LE 0
#define NNS_SIG_BE 1

#define NNS_TYPE_G2D 0
#define NNS_TYPE_G3D 1

typedef struct NnsStream_ {
	unsigned char header[16];
	int g3d;
	int old;
	int sigByteorder;
	uint16_t nBlocks;
	BSTREAM headerStream;
	BSTREAM blockStream;
	BSTREAM currentStream;
} NnsStream;

void NnsStreamCreate(NnsStream *stream, const char *identifier, int versionHigh, int versionLow, int type, int sigByteOrder);

void NnsStreamStartBlock(NnsStream *stream, const char *identifier);

void NnsStreamEndBlock(NnsStream *stream);

void NnsStreamWrite(NnsStream *stream, const void *bytes, unsigned int size);

void NnsStreamAlign(NnsStream *stream, int to);

BSTREAM *NnsStreamGetBlockStream(NnsStream *stream);

void NnsStreamFinalize(NnsStream *stream);

void NnsStreamFlushOut(NnsStream *stream, BSTREAM *out);

void NnsStreamFree(NnsStream *stream);

