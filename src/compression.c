#include "compression.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

#ifdef _MSC_VER
#define inline __inline
#endif


// ----- LZ77 encoding constants

#define LZ_MIN_DISTANCE        0x01   // minimum distance per LZ encoding
#define LZ_MIN_SAFE_DISTANCE   0x02   // minimum safe distance per BIOS LZ bug
#define LZ_MAX_DISTANCE      0x1000   // maximum distance per LZ encoding
#define LZ_MIN_LENGTH          0x03   // minimum length per LZ encoding
#define LZ_MAX_LENGTH          0x12   // maximum length per LZ encoding


// ---- LZ Common Routines

//struct for mapping an LZ graph
typedef struct CxiLzNode_ {
	uint16_t distance;         // distance of node if reference
	uint16_t length;           // length of node
	uint32_t weight;           // weight of node
} CxiLzNode;

//struct for representing tokenized LZ data
typedef struct CxiLzToken_ {
	uint8_t isReference;
	union {
		uint8_t symbol;
		struct {
			int16_t length;
			int16_t distance;
		};
	};
} CxiLzToken;


static unsigned int CxiCompareMemory(const unsigned char *b1, const unsigned char *b2, unsigned int nMax, unsigned int nAbsoluteMax) {
	if (nMax > nAbsoluteMax) nMax = nAbsoluteMax;

	if (nAbsoluteMax >= nMax) {
		//compare nAbsoluteMax bytes, do not perform any looping.
		unsigned int nSame = 0;
		while (nAbsoluteMax > 0) {
			if (*(b1++) != *(b2++)) break;
			nAbsoluteMax--;
			nSame++;
		}
		return nSame;
	} else {
		//compare nMax bytes, then repeat the comparison until nAbsoluteMax is 0.
		unsigned int nSame = 0;
		while (nAbsoluteMax > 0) {

			//compare strings once, incrementing b2 (but keeping b1 fixed since it's repeating)
			unsigned int nSameThis = 0;
			for (unsigned int i = 0; i < nMax; i++) {
				if (b1[i] == *(b2++)) {
					nSameThis++;
				} else {
					break;
				}
			}

			nAbsoluteMax -= nSameThis;
			nSame += nSameThis;
			if (nSameThis < nMax) break; //failed comparison
		}
		return nSame;
	}

}

static unsigned int CxiSearchLZ(const unsigned char *buffer, unsigned int size, unsigned int curpos, unsigned int minDistance, unsigned int maxDistance, unsigned int maxLength, unsigned int *pDistance) {
	//nProcessedBytes = curpos
	unsigned int nBytesLeft = size - curpos;

	//the maximum distance we can search backwards is limited by how far into the buffer we are. It won't
	//make sense to a decoder to copy bytes from before we've started.
	if (maxDistance > curpos) maxDistance = curpos;

	//keep track of the biggest match and where it was
	unsigned int biggestRun = 0, biggestRunIndex = 0;

	//the longest string we can match, including repetition by overwriting the source.
	unsigned int nMaxCompare = maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//begin searching backwards.
	for (unsigned int j = minDistance; j <= maxDistance; j++) {
		//compare up to 0xF bytes, at most j bytes.
		unsigned int nCompare = maxLength;
		if (nCompare > j) nCompare = j;
		if (nCompare > nMaxCompare) nCompare = nMaxCompare;

		unsigned int nMatched = CxiCompareMemory(buffer - j, buffer, nCompare, nMaxCompare);
		if (nMatched > biggestRun) {
			biggestRun = nMatched;
			biggestRunIndex = j;
			if (biggestRun == nMaxCompare) break;
		}
	}

	*pDistance = biggestRunIndex;
	return biggestRun;
}



// ----- LZ77 Compression Routines

static inline int CxiLzNodeIsReference(const CxiLzNode *node) {
	return node->length >= LZ_MIN_LENGTH;
}

//length of compressed data output by LZ token
static inline unsigned int CxiLzTokenCost(unsigned int length) {
	unsigned int nBytesToken;
	if (length >= LZ_MIN_LENGTH) {
		nBytesToken = 2;
	} else {
		nBytesToken = 1;
	}
	return 1 + nBytesToken * 8;
}


static unsigned char *CxCompressLZCommon(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize, unsigned int minDistance) {
	//create node list
	CxiLzNode *nodes = (CxiLzNode *) calloc(size, sizeof(CxiLzNode));
	if (nodes == NULL) return NULL;

	//work backwards from the end of file
	unsigned int pos = size;
	while (pos) {
		//decrement
		pos--;

		//get node at pos
		CxiLzNode *node = nodes + pos;

		//optimization: limit max search length towards end of file
		unsigned int maxSearchLen = LZ_MAX_LENGTH;
		if (maxSearchLen > (size - pos)) maxSearchLen = size - pos;
		if (maxSearchLen < LZ_MIN_LENGTH) maxSearchLen = 1;

		//search for largest LZ string match
		unsigned int len, dist;
		if (maxSearchLen >= LZ_MIN_LENGTH) {
			len = CxiSearchLZ(buffer + pos, size, pos, minDistance, LZ_MAX_DISTANCE, maxSearchLen, &dist);
		} else {
			//dummy
			len = 1, dist = 1;
		}

		//if len < LZ_MIN_LENGTH, treat as literal byte node.
		if (len == 0 || len < LZ_MIN_LENGTH) {
			len = 1;
		}

		//if node takes us to the end of file, set weight to cost of this node.
		if ((pos + len) == size) {
			//token takes us to the end of the file, its weight equals this token cost.
			node->length = len;
			node->distance = dist;
			node->weight = CxiLzTokenCost(len);
		} else {
			//else, search LZ matches from here down.
			unsigned int weightBest = UINT_MAX;
			unsigned int lenBest = 1;
			while (len) {
				//measure cost
				unsigned int weightNext = nodes[pos + len].weight;
				unsigned int weight = CxiLzTokenCost(len) + weightNext;
				if (weight < weightBest) {
					lenBest = len;
					weightBest = weight;
				}

				//decrement length w.r.t. length discontinuity
				len--;
				if (len != 0 && len < LZ_MIN_LENGTH) len = 1;
			}

			//put node
			node->length = lenBest;
			node->distance = dist;
			node->weight = weightBest;
		}
	}

	//from here on, we have a direct path to the end of file. All we need to do is traverse it.

	//get max compressed size
	unsigned int maxCompressed = 4 + size + (size + 7) / 8;

	//encode LZ data
	unsigned char *buf = (unsigned char *) calloc(maxCompressed, 1);
	if (buf == NULL) {
		free(nodes);
		return NULL;
	}
	
	unsigned char *bufpos = buf;
	*(uint32_t *) (bufpos) = (size << 8) | 0x10;
	bufpos += 4;

	CxiLzNode *curnode = &nodes[0];

	unsigned int srcpos = 0;
	while (srcpos < size) {
		uint8_t head = 0;
		unsigned char *headpos = bufpos++;

		for (unsigned int i = 0; i < 8 && srcpos < size; i++) {
			unsigned int length = curnode->length;
			unsigned int distance = curnode->distance;

			if (CxiLzNodeIsReference(curnode)) {
				//node is reference
				uint16_t enc = (distance - LZ_MIN_DISTANCE) | ((length - LZ_MIN_LENGTH) << 12);
				*(bufpos++) = (enc >> 8) & 0xFF;
				*(bufpos++) = (enc >> 0) & 0xFF;
				head |= 1 << (7 - i);
			} else {
				//node is literal byte
				*(bufpos++) = buffer[srcpos];
			}

			srcpos += length; //remember: nodes correspond to byte positions
			curnode += length;
		}

		//put head byte
		*headpos = head;
	}

	//nodes no longer needed
	free(nodes);

	unsigned int outSize = bufpos - buf;
	*compressedSize = outSize;
	return realloc(buf, outSize); //reduce buffer size
}


unsigned char *CxCompressLZ16(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//consider BIOS ROM's faulty VRAM-safe decompression routine
	return CxCompressLZCommon(buffer, size, compressedSize, LZ_MIN_SAFE_DISTANCE);
}

unsigned char *CxCompressLZ8(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//no consideration of minimum length
	return CxCompressLZCommon(buffer, size, compressedSize, LZ_MIN_DISTANCE);
}


unsigned char *CxCompressDummy(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	unsigned int outSize = 4 + ((size + 3) & ~3);
	unsigned char *buf = (unsigned char *) calloc(outSize, 1);
	if (buf == NULL) {
		*compressedSize = 0;
		return NULL;
	}
	
	//build output (dummy)
	*(uint32_t *) (buf) = (size << 8) | 0x00;
	memcpy(buf + 4, buffer, size);
	
	*compressedSize = outSize;
	return buf;
}


unsigned char *CxCompress(const unsigned char *data, unsigned int size, unsigned int *pOutSize, CxCompressionPolicy compression) {
	unsigned int nTypes = 0;
	unsigned int sizes[CX_NUM_COMPRESSION_TYPES] = { 0 };
	unsigned char *outs[CX_NUM_COMPRESSION_TYPES] = { 0 };
	
	//if compression policy has no compression types specified, return error
	if ((compression & (CX_COMPRESSION_TYPES_MASK)) == 0) goto Error;
	
	//try each of the available compressions
	if (compression & CX_COMPRESSION_LZ) {
		if (compression & CX_COMPRESSION_VRAM_SAFE) {
			outs[nTypes] = CxCompressLZ16(data, size, &sizes[nTypes]);
		} else {
			outs[nTypes] = CxCompressLZ8(data, size, &sizes[nTypes]);
		}
		if (outs[nTypes] == NULL) goto Error;
		nTypes++;
	}
	if (compression & CX_COMPRESSION_NONE) {
		outs[nTypes] = CxCompressDummy(data, size, &sizes[nTypes]);
		if (outs[nTypes] == NULL) goto Error;
		nTypes++;
	}
	
	//compare compression results
	unsigned char *best = outs[0];
	unsigned int bestSize = sizes[0];
	for (unsigned int i = 1; i < nTypes; i++) {
		if (sizes[i] < bestSize) {
			bestSize = sizes[i];
			best = outs[i];
		}
	}
	
	//free all but the best result
	for (unsigned int i = 0; i < nTypes; i++) {
		if (outs[i] != NULL && outs[i] != best) {
			free(outs[i]);
		}
	}
	
	*pOutSize = bestSize;
	return best;
	
Error:
	//free everything
	for (unsigned int i = 0; i < nTypes; i++) {
		if (outs[i] != NULL) free(outs[i]);
	}
	*pOutSize = 0;
	return NULL;
}


