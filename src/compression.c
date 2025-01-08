#include "compression.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

#define min(a,b)      ((a)<(b)?(a):(b))
#define max(a,b)      ((a)>(b)?(a):(b))

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
	uint32_t distance : 15;    // distance of node if reference
	uint32_t length   : 17;    // length of node
	uint32_t weight;           // weight of node
} CxiLzNode;


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


// ----- LZX Compression Routines

#define LZX_MIN_DISTANCE        0x01   // minimum distance per LZX encoding
#define LZX_MIN_SAFE_DISTANCE   0x02   // minimum safe distance per SDK LZX bug
#define LZX_MAX_DISTANCE      0x1000   // maximum distance per LZX encoding
#define LZX_MIN_LENGTH          0x03   // minimum length per LZX encoding
#define LZX_MAX_LENGTH       0x10110   // maximum length per LZX encoding
#define LZX_MIN_LENGTH_1        0x03   // size bracket 1: min length
#define LZX_MAX_LENGTH_1        0x10   // size bracket 1: max length
#define LZX_MIN_LENGTH_2        0x11   // size bracket 2: min length
#define LZX_MAX_LENGTH_2       0x110   // size bracket 2: max length
#define LZX_MIN_LENGTH_3       0x111   // size bracket 3: min length
#define LZX_MAX_LENGTH_3     0x10110   // size bracket 3: max length

static inline int CxiLzxNodeIsReference(const CxiLzNode *node) {
	return node->length >= LZX_MIN_LENGTH;
}

//length of compressed data output by LZ token
static inline unsigned int CxiLzxTokenCost(unsigned int length) {
	unsigned int nBytesToken;
	if (length >= LZX_MIN_LENGTH_3) {
		nBytesToken = 4;
	} else if (length >= LZX_MIN_LENGTH_2) {
		nBytesToken = 3;
	} else if (length >= LZX_MIN_LENGTH_1) {
		nBytesToken = 2;
	} else {
		nBytesToken = 1;
	}
	return 1 + nBytesToken * 8;
}


static unsigned char *CxCompressLZXCommon(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize, unsigned int minDistance) {
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
		unsigned int maxSearchLen = LZX_MAX_LENGTH_3;
		if (maxSearchLen > (size - pos)) maxSearchLen = size - pos;
		if (maxSearchLen < LZX_MIN_LENGTH) maxSearchLen = 1;

		//search for largest LZ string match
		unsigned int len, dist;
		if (maxSearchLen >= LZX_MIN_LENGTH) {
			len = CxiSearchLZ(buffer + pos, size, pos, minDistance, LZX_MAX_DISTANCE, maxSearchLen, &dist);
		} else {
			//dummy
			len = 1, dist = 1;
		}

		//if len < LZX_MIN_LENGTH, treat as literal byte node.
		if (len == 0 || len < LZX_MIN_LENGTH) {
			len = 1;
		}

		//if node takes us to the end of file, set weight to cost of this node.
		if ((pos + len) == size) {
			//token takes us to the end of the file, its weight equals this token cost.
			node->length = len;
			node->distance = dist;
			node->weight = CxiLzxTokenCost(len);
		} else {
			//else, search LZ matches from here down.
			unsigned int weightBest = UINT_MAX;
			unsigned int lenBest = 1;
			while (len) {
				//measure cost
				unsigned int weightNext = nodes[pos + len].weight;
				unsigned int weight = CxiLzxTokenCost(len) + weightNext;
				if (weight < weightBest) {
					lenBest = len;
					weightBest = weight;
				}

				//decrement length w.r.t. length discontinuity
				len--;
				if (len != 0 && len < LZX_MIN_LENGTH) len = 1;
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
	*(uint32_t *) (bufpos) = (size << 8) | 0x11;
	bufpos += 4;

	CxiLzNode *curnode = &nodes[0];

	unsigned int srcpos = 0;
	while (srcpos < size) {
		uint8_t head = 0;
		unsigned char *headpos = bufpos++;

		for (unsigned int i = 0; i < 8 && srcpos < size; i++) {
			unsigned int length = curnode->length;
			unsigned int distance = curnode->distance;

			if (CxiLzxNodeIsReference(curnode)) {
				//node is reference
				head |= 1 << (7 - i);
				
				uint32_t enc = (distance - LZX_MIN_DISTANCE) & 0xFFF;
				if (length >= LZX_MIN_LENGTH_3) {
					enc |= ((length - LZX_MIN_LENGTH_3) << 12) | (1 << 28);
					*(bufpos++) = (enc >> 24) & 0xFF;
					*(bufpos++) = (enc >> 16) & 0xFF;
					*(bufpos++) = (enc >>  8) & 0xFF;
					*(bufpos++) = (enc >>  0) & 0xFF;
				} else if (length >= LZX_MIN_LENGTH_2) {
					enc |= ((length - LZX_MIN_LENGTH_2) << 12) | (0 << 20);
					*(bufpos++) = (enc >> 16) & 0xFF;
					*(bufpos++) = (enc >>  8) & 0xFF;
					*(bufpos++) = (enc >>  0) & 0xFF;
				} else if (length >= LZX_MIN_LENGTH_1) {
					enc |= ((length - LZX_MIN_LENGTH_1 + 2) << 12);
					*(bufpos++) = (enc >>  8) & 0xFF;
					*(bufpos++) = (enc >>  0) & 0xFF;
				}
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


unsigned char *CxCompressLZX16(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//consider standard faulty VRAM-safe decompression routine
	return CxCompressLZXCommon(buffer, size, compressedSize, LZX_MIN_SAFE_DISTANCE);
}

unsigned char *CxCompressLZX8(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//no consideration of minimum length
	return CxCompressLZXCommon(buffer, size, compressedSize, LZX_MIN_DISTANCE);
}


// ----- Dummy Compression

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


// ----- Huffman Compression

typedef struct CxiBitStream_ {
	uint32_t *bits;
	int nWords;
	int nBitsInLastWord;
	int nWordsAlloc;
	int length;
} CxiBitStream;

static void CxiBitStreamCreate(CxiBitStream *stream) {
	stream->nWords = 0;
	stream->length = 0;
	stream->nBitsInLastWord = 32;
	stream->nWordsAlloc = 16;
	stream->bits = (uint32_t *) calloc(stream->nWordsAlloc, 4);
}

static void CxiBitStreamFree(CxiBitStream *stream) {
	free(stream->bits);
	memset(stream, 0, sizeof(CxiBitStream));
}

static void CxiBitStreamWrite(CxiBitStream *stream, int bit) {
	if (stream->nBitsInLastWord == 32) {
		stream->nBitsInLastWord = 0;
		stream->nWords++;
		if (stream->nWords > stream->nWordsAlloc) {
			int newAllocSize = (stream->nWordsAlloc + 2) * 3 / 2;
			stream->bits = realloc(stream->bits, newAllocSize * 4);
			stream->nWordsAlloc = newAllocSize;
		}
		stream->bits[stream->nWords - 1] = 0;
	}

	stream->bits[stream->nWords - 1] |= (bit << (31 - stream->nBitsInLastWord));
	stream->nBitsInLastWord++;
	stream->length++;
}

typedef struct CxiHuffNode_ {
	uint16_t sym;
	uint16_t symMin; // had space to spare, maybe make searches a little simpler
	uint16_t symMax;
	uint16_t nRepresent;
	int freq;
	struct CxiHuffNode_ *left;
	struct CxiHuffNode_ *right;
} CxiHuffNode;

typedef struct CxiHuffTreeCode_ {
	uint8_t value;
	uint8_t leaf;
	uint8_t lrbit : 7;
} CxiHuffTreeCode;

#define ISLEAF(n) ((n)->left==NULL&&(n)->right==NULL)

static int CxiHuffmanNodeComparator(const void *p1, const void *p2) {
	return ((CxiHuffNode *) p2)->freq - ((CxiHuffNode *) p1)->freq;
}

static void CxiHuffmanMakeShallowFirst(CxiHuffNode *node) {
	if (ISLEAF(node)) return;
	if (node->left->nRepresent > node->right->nRepresent) {
		CxiHuffNode *left = node->left;
		node->left = node->right;
		node->right = left;
	}
	CxiHuffmanMakeShallowFirst(node->left);
	CxiHuffmanMakeShallowFirst(node->right);
}

static int CxiHuffmanHasSymbol(CxiHuffNode *node, uint16_t sym) {
	if (ISLEAF(node)) return node->sym == sym;
	if (sym < node->symMin || sym > node->symMax) return 0;
	CxiHuffNode *left = node->left;
	CxiHuffNode *right = node->right;
	return CxiHuffmanHasSymbol(left, sym) || CxiHuffmanHasSymbol(right, sym);
}

static void CxiHuffmanConstructTree(CxiHuffNode *nodes, int nNodes) {
	//sort by frequency, then cut off the remainder (freq=0).
	qsort(nodes, nNodes, sizeof(CxiHuffNode), CxiHuffmanNodeComparator);
	for (int i = 0; i < nNodes; i++) {
		if (nodes[i].freq == 0) {
			nNodes = i;
			break;
		}
	}

	//unflatten the histogram into a huffman tree. 
	int nRoots = nNodes;
	int nTotalNodes = nNodes;
	while (nRoots > 1) {
		//copy bottom two nodes to just outside the current range
		CxiHuffNode *srcA = nodes + nRoots - 2;
		CxiHuffNode *destA = nodes + nTotalNodes;
		memcpy(destA, srcA, sizeof(CxiHuffNode));

		CxiHuffNode *left = destA;
		CxiHuffNode *right = nodes + nRoots - 1;
		CxiHuffNode *branch = srcA;

		branch->freq = left->freq + right->freq;
		branch->sym = 0;
		branch->left = left;
		branch->right = right;
		branch->symMin = min(left->symMin, right->symMin);
		branch->symMax = max(right->symMax, left->symMax);
		branch->nRepresent = left->nRepresent + right->nRepresent; //may overflow for root, but the root doesn't really matter for this

		nRoots--;
		nTotalNodes++;
		qsort(nodes, nRoots, sizeof(CxiHuffNode), CxiHuffmanNodeComparator);
	}

	//just to be sure, make sure the shallow node always comes first
	CxiHuffmanMakeShallowFirst(nodes);
}

static void CxiHuffmanWriteSymbol(CxiBitStream *bits, uint16_t sym, CxiHuffNode *tree) {
	if (ISLEAF(tree)) return;
	CxiHuffNode *left = tree->left;
	CxiHuffNode *right = tree->right;
	if (CxiHuffmanHasSymbol(left, sym)) {
		CxiBitStreamWrite(bits, 0);
		CxiHuffmanWriteSymbol(bits, sym, left);
	} else {
		CxiBitStreamWrite(bits, 1);
		CxiHuffmanWriteSymbol(bits, sym, right);
	}
}

static uint8_t CxiHuffmanGetFlagForNode(CxiHuffNode *root) {
	CxiHuffNode *left = root->left;
	CxiHuffNode *right = root->right;
	
	return (ISLEAF(left) << 1) | (ISLEAF(right) << 0);
}

static CxiHuffTreeCode *CxiHuffmanCreateTreeCode(CxiHuffNode *root, CxiHuffTreeCode *treeCode) {
	CxiHuffNode *left = root->left;
	CxiHuffNode *right = root->right;
	
	CxiHuffTreeCode *base = treeCode;
	
	//left node
	{
		if (ISLEAF(left)) {
			base[0].value = left->sym;
			base[0].leaf = 1;
		} else {
			uint8_t flag = CxiHuffmanGetFlagForNode(left);
			
			CxiHuffTreeCode *wr = treeCode + 2;
			treeCode = CxiHuffmanCreateTreeCode(left, wr);
			
			base[0].value = ((wr - base - 2) / 2);
			base[0].lrbit = flag;
			base[0].leaf = 0;
		}
	}
	
	//right node
	{
		if (ISLEAF(right)) {
			base[1].value = right->sym;
			base[1].leaf = 1;
		} else {
			uint8_t flag = CxiHuffmanGetFlagForNode(right);
			
			CxiHuffTreeCode *wr = treeCode + 2;
			treeCode = CxiHuffmanCreateTreeCode(right, wr);
			
			base[1].value = ((wr - base - 2) / 2);
			base[1].lrbit = flag;
			base[1].leaf = 0;
		}
	}
	return treeCode;
}

static void CxiHuffmanCheckTree(CxiHuffTreeCode *treeCode, int nNode) {
	for (int i = 2; i < nNode; i++) {
		if (treeCode[i].leaf) continue;
		
		//check node distance out of range
		if (treeCode[i].value <= 0x3F) continue;
		
		int slideDst = 1;
		if (treeCode[i ^ 1].value == 0x3F) {
			//other node in pair is at maximum distance
			i ^= 1;
		} else {
			//required slide distnace to bring node in range
			slideDst = treeCode[i].value - 0x3F;
		}
		
		int slideMax = (i >> 1) + treeCode[i].value + 1;
		int slideMin = slideMax - slideDst;
		
		//move node back and rotate node pair range forward one position
		CxiHuffTreeCode cpy[2];
		memcpy(cpy, &treeCode[(slideMax << 1)], sizeof(cpy));
		memmove(&treeCode[(slideMin + 1) << 1], &treeCode[(slideMin + 0) << 1], 2 * slideDst * sizeof(CxiHuffTreeCode));
		memcpy(&treeCode[(slideMin << 1)], cpy, sizeof(cpy));
		
		//update node references to rotated range
		treeCode[i].value -= slideDst;
		
		//if the moved node pair is branch nodes, adjust outgoing references
		if (!treeCode[(slideMin << 1) + 0].leaf) treeCode[(slideMin << 1) + 0].value += slideDst;
		if (!treeCode[(slideMin << 1) + 1].leaf) treeCode[(slideMin << 1) + 1].value += slideDst;
		
		for (int j = i + 1; j < (slideMin << 1); j++) {
			if (treeCode[j].leaf) continue;
			
			//increment node values referring to slid nodes
			int refb = (j >> 1) + treeCode[j].value + 1;
			if ((refb >= slideMin) && (refb < slideMax)) treeCode[j].value++;
		}
		
		for (int j = (slideMin + 1) << 1; j < ((slideMax + 1) << 1); j++) {
			if (treeCode[j].leaf) continue;
			
			//adjust outgoing references from slid nodes
			int refb = (j >> 1) + treeCode[j].value + 1;
			if (refb > slideMax) treeCode[j].value--;
		}
		
		//continue again from start of this node pair
		i &= ~1;
		i--;
	}
}

unsigned char *CxCompressHuffman(const unsigned char *buffer, unsigned int size, int nBits, unsigned int *compressedSize) {
	//create a histogram of each byte in the file.
	CxiHuffNode *nodes = (CxiHuffNode *) calloc(512, sizeof(CxiHuffNode));
	int nSym = 1 << nBits;
	for (int i = 0; i < nSym; i++) {
		nodes[i].sym = i;
		nodes[i].symMin = i;
		nodes[i].symMax = i;
		nodes[i].nRepresent = 1;
	}

	//construct histogram
	if (nBits == 8) {
		for (unsigned int i = 0; i < size; i++) {
			nodes[buffer[i]].freq++;
		}
	} else {
		for (unsigned int i = 0; i < size; i++) {
			nodes[(buffer[i] >> 0) & 0xF].freq++;
			nodes[(buffer[i] >> 4) & 0xF].freq++;
		}
	}
	
	//count nodes
	int nLeaf = 0;
	for (int i = 0; i < nSym; i++) {
		if (nodes[i].freq) nLeaf++;
	}
	if (nLeaf < 2) {
		//insert dummy nodes
		for (int i = 0; i < nSym && nLeaf < 2; i++) {
			if (nodes[i].freq == 0) nodes[i].freq = 1;
		}
	}
	
	//build Huffman tree
	CxiHuffmanConstructTree(nodes, nSym);
	
	//construct Huffman tree encoding
	CxiHuffTreeCode treeCode[512] = { 0 };
	treeCode[0].value = ((nLeaf + 1) & ~1) - 1;
	treeCode[0].lrbit = 0;
	treeCode[1].value = 0;
	treeCode[1].lrbit = CxiHuffmanGetFlagForNode(nodes);
	CxiHuffmanCreateTreeCode(nodes, treeCode + 2);
	CxiHuffmanCheckTree(treeCode, nLeaf * 2);
	
	//now write bits out.
	CxiBitStream stream;
	CxiBitStreamCreate(&stream);
	if (nBits == 8) {
		for (unsigned int i = 0; i < size; i++) {
			CxiHuffmanWriteSymbol(&stream, buffer[i], nodes);
		}
	} else {
		for (unsigned int i = 0; i < size; i++) {
			CxiHuffmanWriteSymbol(&stream, (buffer[i] >> 0) & 0xF, nodes);
			CxiHuffmanWriteSymbol(&stream, (buffer[i] >> 4) & 0xF, nodes);
		}
	}

	//create output bytes
	unsigned int treeSize = (nLeaf * 2 + 3) & ~3;
	unsigned int outSize = 4 + treeSize + stream.nWords * 4;
	unsigned char *finbuf = (unsigned char *) malloc(outSize);
	*(uint32_t *) finbuf = 0x20 | nBits | (size << 8);
	
	for (int i = 0; i < nLeaf * 2; i++) {
		finbuf[4 + i] = treeCode[i].value | (treeCode[i].leaf ? 0 : (treeCode[i].lrbit << 6));
	}
	
	memcpy(finbuf + 4 + treeSize, stream.bits, stream.nWords * 4);
	free(nodes);
	CxiBitStreamFree(&stream);
	
	*compressedSize = outSize;
	return finbuf;
}


// ----- RLE Compression


typedef struct CxiRlNode_ {
	uint32_t weight : 31; // weight of this node
	uint32_t isRun  :  1; // is node compressed run
	uint8_t length  :  8; // length of node in bytes
} CxiRlNode;

static unsigned int CxiFindRlRun(const unsigned char *buffer, unsigned int size, unsigned int maxSize) {
	if (maxSize > size) maxSize = size;
	if (maxSize == 0) return 0;

	unsigned char first = buffer[0];
	for (unsigned int i = 1; i < maxSize; i++) {
		if (buffer[i] != first) return i;
	}
	return maxSize;
}

unsigned char *CxCompressRL(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//construct a graph
	CxiRlNode *nodes = (CxiRlNode *) calloc(size, sizeof(CxiRlNode));

	unsigned int pos = size;
	while (pos--) {
		CxiRlNode *node = nodes + pos;

		//find longest run up to 130 bytes
		unsigned int runLength = CxiFindRlRun(buffer + pos, size - pos, 130);
		
		unsigned int bestLength = 1, bestCost = UINT_MAX, bestRun = 0;
		if (runLength >= 3) {
			//meets threshold, explore run lengths.
			unsigned int tmpLength = runLength;
			bestRun = 1;
			while (tmpLength >= 3) {
				unsigned int cost = 2;
				if ((pos + tmpLength) < size) cost += nodes[pos + tmpLength].weight;

				if (cost < bestCost) {
					bestCost = cost;
					bestLength = tmpLength;
				}

				tmpLength--;
			}
		}

		//explore cost of storing a byte run
		unsigned int tmpLength = 0x80;
		if ((pos + tmpLength) > size) tmpLength = size - pos;
		while (tmpLength >= 1) {
			unsigned int cost = (1 + tmpLength);
			if ((pos + tmpLength) < size) cost += nodes[pos + tmpLength].weight;

			if (cost < bestCost) {
				bestCost = cost;
				bestLength = tmpLength;
				bestRun = 0; // best is not a run
			}
			tmpLength--;
		}

		//put best
		node->weight = bestCost;
		node->length = bestLength;
		node->isRun = bestRun;
	}

	//produce RL encoding
	pos = 0;
	unsigned int outLength = 4;
	while (pos < size) {
		CxiRlNode *node = nodes + pos;

		if (node->isRun) outLength += 2;
		else             outLength += 1 + node->length;
		pos += node->length;
	}

	unsigned char *out = (unsigned char *) calloc(outLength, 1);
	*(uint32_t *) out = 0x30 | (size << 8);

	pos = 0;
	unsigned int outpos = 4;
	while (pos < size) {
		CxiRlNode *node = nodes + pos;

		if (node->isRun) {
			out[outpos++] = 0x80 | (node->length - 3);
			out[outpos++] = buffer[pos];
		} else {
			out[outpos++] = 0x00 | (node->length - 1);
			memcpy(out + outpos, buffer + pos, node->length);
			outpos += node->length;
		}
		pos += node->length;
	}

	free(nodes);

	*compressedSize = outLength;
	return out;
}



// ----- Generic compression

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
	if (compression & CX_COMPRESSION_LZX) {
		if (compression & CX_COMPRESSION_VRAM_SAFE) {
			outs[nTypes] = CxCompressLZX16(data, size, &sizes[nTypes]);
		} else {
			outs[nTypes] = CxCompressLZX8(data, size, &sizes[nTypes]);
		}
		if (outs[nTypes] == NULL) goto Error;
		nTypes++;
	}
	if (compression & CX_COMPRESSION_HUFFMAN4) {
		outs[nTypes] = CxCompressHuffman(data, size, 4, &sizes[nTypes]);
		if (outs[nTypes] == NULL) goto Error;
		nTypes++;
	}
	if (compression & CX_COMPRESSION_HUFFMAN8) {
		outs[nTypes] = CxCompressHuffman(data, size, 8, &sizes[nTypes]);
		if (outs[nTypes] == NULL) goto Error;
		nTypes++;
	}
	if (compression & CX_COMPRESSION_RLE) {
		outs[nTypes] = CxCompressRL(data, size, &sizes[nTypes]);
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


