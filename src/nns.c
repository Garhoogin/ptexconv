#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nns.h"


// ----- NNS Stream functions

void NnsStreamCreate(NnsStream *stream, const char *identifier, int versionHigh, int versionLow, int type, int sigByteOrder) {
	//initialize
	memset(stream->header, 0, sizeof(stream->header));
	stream->g3d = (type == NNS_TYPE_G3D);
	stream->sigByteorder = sigByteOrder;
	stream->nBlocks = 0;
	stream->old = 0;

	if (sigByteOrder == NNS_SIG_BE) {
		memcpy(stream->header, identifier, 4);
	} else {
		stream->header[0] = identifier[3];
		stream->header[1] = identifier[2];
		stream->header[2] = identifier[1];
		stream->header[3] = identifier[0];
	}
	stream->header[0x4] = 0xFF;
	stream->header[0x5] = 0xFE;
	stream->header[0x6] = versionLow;
	stream->header[0x7] = versionHigh;
	stream->header[0xC] = 0x10; // 16-byte header
	stream->header[0xD] = 0x00;
	
	bstreamCreate(&stream->headerStream, NULL, 0);
	bstreamCreate(&stream->blockStream, NULL, 0);
	bstreamWrite(&stream->headerStream, stream->header, sizeof(stream->header));
}

void NnsStreamStartBlock(NnsStream *stream, const char *identifier) {
	//create a new block stream
	char header[8] = { 0 };
	bstreamCreate(&stream->currentStream, NULL, 0);

	if (stream->sigByteorder == NNS_SIG_BE) {
		memcpy(header, identifier, 4);
	} else {
		header[0] = identifier[3];
		header[1] = identifier[2];
		header[2] = identifier[1];
		header[3] = identifier[0];
	}
	bstreamWrite(&stream->currentStream, header, sizeof(header));
}

void NnsStreamEndBlock(NnsStream *stream) {
	//align section, compute section size, write to block, flush block
	bstreamAlign(&stream->currentStream, 4);

	//finalize block
	uint32_t size = stream->currentStream.size;
	if (stream->old) size -= 8;
	bstreamSeek(&stream->currentStream, 4, 0);
	bstreamWrite(&stream->currentStream, &size, sizeof(size));

	//get size of block stream, append to offset table if applicable
	uint32_t blockPos = stream->blockStream.size;
	if (stream->g3d) {
		uint32_t headerSize = stream->headerStream.size;
		bstreamSeek(&stream->headerStream, headerSize, 0);
		bstreamWrite(&stream->headerStream, &blockPos, sizeof(blockPos));
	}

	//increment block count
	stream->nBlocks++;
	bstreamSeek(&stream->headerStream, 0xE, 0);
	bstreamWrite(&stream->headerStream, &stream->nBlocks, sizeof(stream->nBlocks));

	//write block to out, clean buffer
	bstreamWrite(&stream->blockStream, stream->currentStream.buffer, stream->currentStream.size);
	bstreamFree(&stream->currentStream);
}

void NnsStreamWrite(NnsStream *stream, const void *bytes, unsigned int size) {
	bstreamWrite(&stream->currentStream, (void *) bytes, size);
}

void NnsStreamAlign(NnsStream *stream, int to) {
	bstreamAlign(&stream->currentStream, to);
}

BSTREAM *NnsStreamGetBlockStream(NnsStream *stream) {
	return &stream->currentStream;
}

void NnsStreamFinalize(NnsStream *stream) {
	//for G3D: add current header size to all block offsets
	if (stream->g3d) {
		uint32_t headerSize = stream->headerStream.size;
		for (unsigned int i = 0; i < stream->nBlocks; i++) {
			*(uint32_t *) (stream->headerStream.buffer + 0x10 + i * 0x4) += headerSize;
		}
	}

	//write file size into file header
	uint32_t fileSize = stream->headerStream.size + stream->blockStream.size;
	if (stream->old) fileSize -= 8 * stream->nBlocks;
	bstreamSeek(&stream->headerStream, 8, 0);
	bstreamWrite(&stream->headerStream, &fileSize, sizeof(fileSize));
}

void NnsStreamFlushOut(NnsStream *stream, BSTREAM *out) {
	//write to stream
	bstreamWrite(out, stream->headerStream.buffer, stream->headerStream.size);
	bstreamWrite(out, stream->blockStream.buffer, stream->blockStream.size);
}

void NnsStreamFree(NnsStream *stream) {
	//free all streams (freeing twice is not forbidden)
	bstreamFree(&stream->headerStream);
	bstreamFree(&stream->blockStream);
	bstreamFree(&stream->currentStream);
}



