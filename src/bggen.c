// -----------------------------------------------------------------------------------------------
// Copyright (c) 2020, Garhoogin
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials provided
//    with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// -----------------------------------------------------------------------------------------------
#include "bggen.h"
#include "palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NSCR_FLIPNONE 0
#define NSCR_FLIPX 1
#define NSCR_FLIPY 2
#define NSCR_FLIPXY (NSCR_FLIPX|NSCR_FLIPY)

#define FALSE 0
#define TRUE  1

#ifdef _MSC_VER
#define inline __inline
#endif

#ifdef BGGEN_USE_DCT

//cosine table: [frequency][t]
static const float sCosTable[8][8] = {
	{  1.000000f,  1.000000f,  1.000000f,  1.000000f,  1.000000f,  1.000000f,  1.000000f,  1.000000f },
	{  0.980785f,  0.831470f,  0.555570f,  0.195090f, -0.195090f, -0.555570f, -0.831470f, -0.980785f },
	{  0.923880f,  0.382683f, -0.382683f, -0.923880f, -0.923880f, -0.382683f,  0.382683f,  0.923880f },
	{  0.831470f, -0.195090f, -0.980785f, -0.555570f,  0.555570f,  0.980785f,  0.195090f, -0.831470f },
	{  0.707107f, -0.707107f, -0.707107f,  0.707107f,  0.707107f, -0.707107f, -0.707107f,  0.707107f },
	{  0.555570f, -0.980785f,  0.195090f,  0.831470f, -0.831470f, -0.195090f,  0.980785f, -0.555570f },
	{  0.382683f, -0.923880f,  0.923880f, -0.382683f, -0.382683f,  0.923880f, -0.923880f,  0.382683f },
	{  0.195090f, -0.555570f,  0.831470f, -0.980785f,  0.980785f, -0.831470f,  0.555570f, -0.195090f }
};

//coefficient weightings
static const float sWeightLuma[64] = {
	0.7500f, 1.0000f, 0.8571f, 0.8571f, 0.6667f, 0.5000f, 0.2449f, 0.1667f,
	1.0000f, 1.0000f, 0.9231f, 0.7059f, 0.5455f, 0.3429f, 0.1875f, 0.1304f,
	0.8571f, 0.9231f, 0.7500f, 0.5455f, 0.3243f, 0.2182f, 0.1538f, 0.1263f,
	0.8571f, 0.7059f, 0.5455f, 0.4138f, 0.2143f, 0.1875f, 0.1379f, 0.1224f,
	0.6667f, 0.5455f, 0.3243f, 0.2143f, 0.1765f, 0.1481f, 0.1165f, 0.1071f,
	0.5000f, 0.3429f, 0.2182f, 0.1875f, 0.1481f, 0.1154f, 0.0992f, 0.1200f,
	0.2449f, 0.1875f, 0.1538f, 0.1379f, 0.1165f, 0.0992f, 0.1000f, 0.1165f,
	0.1667f, 0.1304f, 0.1263f, 0.1224f, 0.1071f, 0.1200f, 0.1165f, 0.1212f
};

static const float sWeightChroma[64] = {
	0.7059f, 0.6667f, 0.5000f, 0.2553f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.6667f, 0.5714f, 0.4615f, 0.1818f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.5000f, 0.4615f, 0.2143f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.2553f, 0.1818f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f,
	0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f, 0.1212f
};

static void BgiComputeDctBlock(float *in, float *out) {
	for (int ky = 0; ky < 8; ky++) {
		for (int kx = 0; kx < 8; kx++) {

			double sum = 0.0;
			for (int y = 0; y < 8; y++) {
				double cosY = sCosTable[ky][y];
				for (int x = 0; x < 8; x++) {
					double cosX = sCosTable[kx][x];

					sum += in[y * 8 + x] * cosX * cosY;
				}
			}

			//if first row/column, halve.
			if (kx == 0) sum *= 0.5;
			if (ky == 0) sum *= 0.5;
			out[ky * 8 + kx] = (float) (sum * 0.0625);
		}
	}
}

static void BgiComputeDct(RxReduction *reduction, BgTile *tile) {
	//compute DCT block for Y, I, Q
	float blockY[64], blockI[64], blockQ[64], blockA[64];

	for (int i = 0; i < 64; i++) {
		float y = tile->pxYiq[i].y;
		if (tile->pxYiq[i].a < 0.5f) {
			//A < 128: turn black transparent
			y = 0.0;
			blockA[i] = 0;
		} else {
			//else turn full opaque
			blockA[i] = 1.0f;
		}

		if (y > 0.0) {
			blockY[i] = y;
			blockI[i] = tile->pxYiq[i].i;
			blockQ[i] = tile->pxYiq[i].q;
		} else {
			blockY[i] = 0.0f;
			blockI[i] = 0.0f;
			blockQ[i] = 0.0f;
		}
	}

	//compute output blocks
	BgiComputeDctBlock(blockY, tile->dct.blockY);
	BgiComputeDctBlock(blockI, tile->dct.blockI);
	BgiComputeDctBlock(blockQ, tile->dct.blockQ);
	BgiComputeDctBlock(blockA, tile->dct.blockA);
}

static double BgiCompareTilesDct(RxReduction *reduction, BgTile *tile1, BgTile *tile2, unsigned char mode) {
	//sum of square
	double error = 0.0;

	for (int i = 0; i < 64; i++) {
		double block2Y = tile2->dct.blockY[i];
		double block2I = tile2->dct.blockI[i];
		double block2Q = tile2->dct.blockQ[i];
		double block2A = tile2->dct.blockA[i];

		//flip X: negate every odd column
		if ((mode & TILE_FLIPX) && (i % 2 == 1)) {
			block2Y = -block2Y, block2I = -block2I, block2Q = -block2Q, block2A = -block2A;
		}

		//flip Y: negate every odd row
		if ((mode & TILE_FLIPY) && ((i / 8) % 2 == 1)) {
			block2Y = -block2Y, block2I = -block2I, block2Q = -block2Q, block2A = -block2A;
		}

		double dy = sqrt(sWeightLuma[i])   * reduction->yWeight * (tile1->dct.blockY[i] - block2Y);
		double di = sqrt(sWeightChroma[i]) * reduction->iWeight * (tile1->dct.blockI[i] - block2I);
		double dq = sqrt(sWeightChroma[i]) * reduction->qWeight * (tile1->dct.blockQ[i] - block2Q);
		double da = 40.0                                        * (tile1->dct.blockA[i] - block2A);

		//weighted error
		error += dy * dy + di * di + dq * dq + da * da;
	}

	return error;
}

#endif // BGGEN_USE_DCT

static double BgiTileDifferenceFlip(RxReduction *reduction, BgTile *t1, BgTile *t2, unsigned char mode) {
#ifndef BGGEN_USE_DCT
	//xor mask for translating pixel addresses
	unsigned int iXor = 0;
	if (mode & TILE_FLIPX) iXor ^= 007;
	if (mode & TILE_FLIPY) iXor ^= 070;

	double err = 0.0;
	for (unsigned int i = 0; i < 64; i++) {
		const RxYiqColor *yiq1 = &t1->pxYiq[i];
		const RxYiqColor *yiq2 = &t2->pxYiq[i ^ iXor];
		err += RxComputeColorDifference(reduction, yiq1, yiq2);
	}

	return err;
#else // BGGEN_USE_DCT
	return BgiCompareTilesDct(reduction, t1, t2, mode);
#endif
}

static double BgiTileDifference(RxReduction *reduction, BgTile *t1, BgTile *t2, unsigned char *flipMode, int allowFlip) {
	double err = BgiTileDifferenceFlip(reduction, t1, t2, 0);
	if (err == 0.0 || !allowFlip) {
		*flipMode = 0;
		return err;
	}
	double err2 = BgiTileDifferenceFlip(reduction, t1, t2, TILE_FLIPX);
	if (err2 == 0.0) {
		*flipMode = TILE_FLIPX;
		return err2;
	}
	double err3 = BgiTileDifferenceFlip(reduction, t1, t2, TILE_FLIPY);
	if (err3 == 0.0) {
		*flipMode = TILE_FLIPY;
		return err3;
	}
	double err4 = BgiTileDifferenceFlip(reduction, t1, t2, TILE_FLIPXY);
	if (err4 == 0.0) {
		*flipMode = TILE_FLIPXY;
		return err4;
	}

	if (err <= err2 && err <= err3 && err <= err4) {
		*flipMode = 0;
		return err;
	}
	if (err2 <= err && err2 <= err3 && err2 <= err4) {
		*flipMode = TILE_FLIPX;
		return err2;
	}
	if (err3 <= err && err3 <= err2 && err3 <= err4) {
		*flipMode = TILE_FLIPY;
		return err3;
	}
	if (err4 <= err && err4 <= err2 && err4 <= err3) {
		*flipMode = TILE_FLIPXY;
		return err4;
	}
	*flipMode = 0;
	return err;
}

static void BgiAddTileToTotal(RxYiqColor *pxBlock, BgTile *tile) {
	unsigned int iXor = 0;
	if (tile->flipMode & TILE_FLIPX) iXor ^= 007;
	if (tile->flipMode & TILE_FLIPY) iXor ^= 070;

	for (unsigned int i = 0; i < 64; i++) {
		COLOR32 col = tile->px[i];
		RxYiqColor *dest = &pxBlock[i ^ iXor];

		RxYiqColor yiq;
		RxConvertRgbToYiq(col, &yiq);
		dest->y += yiq.y;
		dest->i += yiq.i;
		dest->q += yiq.q;
		dest->a += yiq.a;
	}
}

typedef struct BgTileDiff_ {
	int tile1;
	int tile2;
	double diff;		//post-biased
} BgTileDiff;

typedef struct BgTileDiffList_ {
	BgTileDiff *diffBuff;
	int diffBuffSize;
	int diffBuffLength;
	double minDiff;
	double maxDiff;
} BgTileDiffList;

static void BgiTdlInit(BgTileDiffList *list, int nEntries) {
	list->diffBuffSize = nEntries;
	list->diffBuffLength = 0;
	list->minDiff = 1e32;
	list->maxDiff = 0;
	list->diffBuff = (BgTileDiff *) calloc(list->diffBuffSize, sizeof(BgTileDiff));
}

static void BgiTdlFree(BgTileDiffList *list) {
	free(list->diffBuff);
	list->diffBuff = NULL;
	list->diffBuffLength = 0;
	list->diffBuffSize = 0;
}

static void BgiTdlAdd(BgTileDiffList *list, int tile1, int tile2, double diff) {
	if (list->diffBuffLength == list->diffBuffSize && diff >= list->maxDiff) return;

	//find an insertion point
	//TODO: binary search
	int destIndex = list->diffBuffLength;
	if (diff < list->minDiff) {
		destIndex = 0;
	} else {
		for (int i = 0; i < list->diffBuffLength; i++) {
			if (diff < list->diffBuff[i].diff) {
				destIndex = i;
				break;
			}
		}
	}

	//insert
	int nEntriesToMove = list->diffBuffLength - destIndex;
	int added = 1; //was a new entry created?
	if (destIndex + 1 + nEntriesToMove > list->diffBuffSize) {
		nEntriesToMove = list->diffBuffSize - destIndex - 1;
		added = 0;
	}
	memmove(list->diffBuff + destIndex + 1, list->diffBuff + destIndex, nEntriesToMove * sizeof(BgTileDiff));
	list->diffBuff[destIndex].tile1 = tile1;
	list->diffBuff[destIndex].tile2 = tile2;
	list->diffBuff[destIndex].diff = diff;
	if (added) {
		list->diffBuffLength++;
	}
	list->minDiff = list->diffBuff[0].diff;
	list->maxDiff = list->diffBuff[list->diffBuffLength - 1].diff;
}

static void BgiTdlRemoveAll(BgTileDiffList *list, int tile1, int tile2) {
	//remove all diffs involving tile1 and tile2
	for (int i = 0; i < list->diffBuffLength; i++) {
		BgTileDiff *td = list->diffBuff + i;
		if (td->tile1 == tile1 || td->tile2 == tile1 || td->tile1 == tile2 || td->tile2 == tile2) {
			memmove(td, td + 1, (list->diffBuffLength - i - 1) * sizeof(BgTileDiff));
			list->diffBuffLength--;
			i--;
		}
	}
	if (list->diffBuffLength > 0) {
		list->minDiff = list->diffBuff[0].diff;
		list->maxDiff = list->diffBuff[list->diffBuffLength - 1].diff;
	}
}

static void BgiTdlPop(BgTileDiffList *list, BgTileDiff *out) {
	if (list->diffBuffLength > 0) {
		memcpy(out, list->diffBuff, sizeof(BgTileDiff));
		memmove(list->diffBuff, list->diffBuff + 1, (list->diffBuffLength - 1) * sizeof(BgTileDiff));
		list->diffBuffLength--;
		if (list->diffBuffLength > 0) {
			list->minDiff = list->diffBuff[0].diff;
		}
	}
}

static void BgiTdlReset(BgTileDiffList *list) {
	list->diffBuffLength = 0;
	list->maxDiff = 0;
	list->minDiff = 1e32;
}

static inline int BgiGetDiffEntry(int row, int col, int dim) {
	//we simulate a symmetric matrix (with a zeroed diagonal) using half the memory of one. 
	//this creates a buffer ordered like:
	//    0  1  2  3  4
	// 0     5  6  7  8
	// 1  5     9 10 11
	// 2  6  9    12 13
	// 3  7 10 12    14
	// 4  8 11 13 14
	if (col > row) {
		return col - 1 + row * (2 * dim - row - 3) / 2;
	} else {
		return row - 1 + col * (2 * dim - col - 3) / 2;
	}
}

static inline float BgiGetDiff(const float *diffBuff, int dim, int i, int j) {
	return diffBuff[BgiGetDiffEntry(i, j, dim)];
}

static inline void BgiPutDiff(float *diffBuff, int dim, int i, int j, float val) {
	diffBuff[BgiGetDiffEntry(i, j, dim)] = val;
}

int BgPerformCharacterCompression(
	BgTile                 *tiles,
	unsigned int            nTiles,
	unsigned int            nBits,
	unsigned int            nMaxChars,
	int                     allowFlip,
	const COLOR32          *palette,
	unsigned int            paletteSize,
	unsigned int            nPalettes,
	unsigned int            paletteBase,
	unsigned int            paletteOffset,
	const RxBalanceSetting *balance,
	volatile int           *progress
) {
	unsigned int nChars = nTiles;
	float *diffBuff = (float *) calloc(nTiles * (nTiles - 1) / 2, sizeof(float));
	unsigned char *flips = (unsigned char *) calloc(nTiles * nTiles, 1); //how must each tile be manipulated to best match its partner

	RxReduction *reduction = RxNew(balance);
	for (unsigned int i = 0; i < nTiles; i++) {
		BgTile *t1 = &tiles[i];
		for (unsigned int j = 0; j < i; j++) {
			BgTile *t2 = &tiles[j];

			float diff = (float) BgiTileDifference(reduction, t1, t2, &flips[i + j * nTiles], allowFlip);
			BgiPutDiff(diffBuff, nTiles, j, i, diff);
			flips[j + i * nTiles] = flips[i + j * nTiles];
		}
		*progress = (i * i) / nTiles * 500 / nTiles;
	}

	//first, combine tiles with a difference of 0.
	for (unsigned int i = 0; i < nTiles; i++) {
		BgTile *t1 = &tiles[i];
		if (t1->masterTile != i) continue;

		for (unsigned int j = 0; j < i; j++) {
			BgTile *t2 = &tiles[j];
			if (t2->masterTile != j) continue;

			if (BgiGetDiff(diffBuff, nTiles, j, i) == 0) {
				//merge all tiles with master index i to j
				for (unsigned int k = 0; k < nTiles; k++) {
					if (tiles[k].masterTile == i) {
						tiles[k].masterTile = j;
						tiles[k].flipMode ^= flips[i + j * nTiles];
						tiles[k].nRepresents = 0;
						tiles[j].nRepresents++;
					}
				}
				nChars--;
				if (nTiles > nMaxChars) *progress = 500 + (int) (500 * sqrt((float) (nTiles - nChars) / (nTiles - nMaxChars)));
			}
		}
	}

	//still too many? 
	if (nChars > nMaxChars) {
		//damn

		//create a rolling buffer of similar tiles. 
		//when tiles are combined, combinations that involve affected tiles in the array are removed.
		//fill it to capacity initially, then keep using it until it's empty, then fill again.
		BgTileDiffList tdl;
		BgiTdlInit(&tdl, 64);

		//keep finding the most similar tile until we get character count down
		int direction = 0;
		while (nChars > nMaxChars) {
			for (unsigned int iOuter = 0; iOuter < nTiles; iOuter++) {
				unsigned int i = direction ? (nTiles - 1 - iOuter) : iOuter; //criss cross the direction
				BgTile *t1 = &tiles[i];
				if (t1->masterTile != i) continue;

				for (unsigned int j = 0; j < i; j++) {
					BgTile *t2 = &tiles[j];
					if (t2->masterTile != j) continue;

					double thisErrorEntry = BgiGetDiff(diffBuff, nTiles, j, i);
					double thisError = thisErrorEntry;
					double bias = t1->nRepresents + t2->nRepresents;
					bias *= bias;

					thisError = thisErrorEntry * bias;
					BgiTdlAdd(&tdl, j, i, thisError);
				}
			}

			//now merge tiles while we can
			int tile1, tile2;
			while (tdl.diffBuffLength > 0 && nChars > nMaxChars) {
				BgTileDiff td;
				BgiTdlPop(&tdl, &td);

				//tile merging
				tile1 = td.tile1;
				tile2 = td.tile2;

				//should we swap tile1 and tile2? tile2 should have <= tile1's nRepresents
				if (tiles[tile2].nRepresents > tiles[tile1].nRepresents) {
					int t = tile1;
					tile1 = tile2;
					tile2 = t;
				}

				//merge tile1 and tile2. All tile2 tiles become tile1 tiles
				unsigned char flipDiff = flips[tile1 + tile2 * nTiles];
				for (unsigned int i = 0; i < nTiles; i++) {
					if (tiles[i].masterTile == (unsigned int) tile2) {
						tiles[i].masterTile = tile1;
						tiles[i].flipMode ^= flipDiff;
						tiles[i].nRepresents = 0;
						tiles[tile1].nRepresents++;
					}
				}

				nChars--;
				*progress = 500 + (int) (500 * sqrt((float) (nTiles - nChars) / (nTiles - nMaxChars)));

				BgiTdlRemoveAll(&tdl, td.tile1, td.tile2);
			}
			direction = !direction;
			BgiTdlReset(&tdl);
		}
		BgiTdlFree(&tdl);
	}

	free(diffBuff);
	free(flips);

	//process each graphical tile for output
	int charIdx = 0;
	for (unsigned int i = 0; i < nTiles; i++) {
		BgTile *tile = &tiles[i];
		if (tile->masterTile != i) continue;

		//put character index of output
		tile->charNo = charIdx++;

		//no averaging required for just one tile
		if (tile->nRepresents <= 1) continue;

		//average all tiles that use this master tile.
		RxYiqColor pxBlock[64] = { 0 };
		for (unsigned int j = 0; j < nTiles; j++) {
			BgTile *tile2 = &tiles[j];
			if (tile2->masterTile == i) BgiAddTileToTotal(pxBlock, tile2);
		}

		//divide by count, convert to 32-bit RGB
		int nRep = tile->nRepresents;
		for (unsigned int j = 0; j < 64; j++) {
			pxBlock[j].y /= nRep;
			pxBlock[j].i /= nRep;
			pxBlock[j].q /= nRep;
			pxBlock[j].a /= nRep;
		}
		for (unsigned int j = 0; j < 64; j++) {
			tile->px[j] = RxConvertYiqToRgb(&pxBlock[j]);
		}

		//try to determine the most optimal palette. Child tiles can be different palettes.
		int bestPalette = paletteBase;
		double bestError = 1e32;
		for (unsigned int j = paletteBase; j < paletteBase + nPalettes; j++) {
			const COLOR32 *pal = palette + (j << nBits) + paletteOffset + !paletteOffset;
			double err = RxComputePaletteError(reduction, tile->px, 8, 8, pal, paletteSize - !paletteOffset, bestError);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//now, match colors to indices.
		const COLOR32 *pal = palette + (bestPalette << nBits);
		int idxs[64];
		RxReduceImageWithContext(reduction, tile->px, idxs, 8, 8, pal + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset,
			RX_FLAG_ALPHA_MODE_RESERVE | RX_FLAG_PRESERVE_ALPHA | RX_FLAG_NO_ALPHA_DITHER, 0.0f);
		for (unsigned int j = 0; j < 64; j++) {
			tile->indices[j] = idxs[j] == 0 ? 0 : (idxs[j] + paletteOffset - !!paletteOffset);
			tile->px[j] = tile->indices[j] ? (pal[tile->indices[j]] | 0xFF000000) : 0;
		}
		tile->palette = bestPalette;

		//lastly, copy tile->indices to all child tile->indices, just to make sure palette and character are in synch.
		for (unsigned int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile != i) continue;
			if (j == i) continue;
			BgTile *tile2 = &tiles[j];

			memcpy(tile2->indices, tile->indices, 64);
			tile2->palette = tile->palette;
		}
	}

	//last, set the character index for the non-master tiles.
	for (unsigned int i = 0; i < nTiles; i++) {
		tiles[i].charNo = tiles[tiles[i].masterTile].charNo;
	}

	RxFree(reduction);
	return nChars;
}

void BgSetupTiles(
	BgTile                 *tiles,
	unsigned int            nTiles,
	int                     nBits,
	const COLOR32          *palette,
	unsigned int            paletteSize,
	unsigned int            nPalettes,
	unsigned int            paletteBase,
	unsigned int            paletteOffset,
	int                     dither,
	float                   diffuse,
	const RxBalanceSetting *balance
) {
	RxReduction *reduction = RxNew(balance);

	if (!dither) diffuse = 0.0f;

	unsigned int effectivePaletteOffset = paletteOffset;
	unsigned int effectivePaletteSize = paletteSize;
	if (paletteOffset == 0) {
		effectivePaletteSize--;
		effectivePaletteOffset++;
	}

	for (unsigned int i = 0; i < nTiles; i++) {
		BgTile *tile = &tiles[i];

		//create histogram for tile
		RxHistClear(reduction);
		RxHistAdd(reduction, tile->px, 8, 8);
		RxHistFinalize(reduction);

		int bestPalette = paletteBase;
		double bestError = 1e32;
		for (unsigned int j = paletteBase; j < paletteBase + nPalettes; j++) {
			const COLOR32 *pal = palette + (j << nBits);
			double err = RxHistComputePaletteError(reduction, pal + effectivePaletteOffset, effectivePaletteSize, bestError);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//match colors
		const COLOR32 *pal = palette + (bestPalette << nBits);

		//reduce the tile graphics. Subtract 1 from the effective offset for the placeholder transparent entry
		//(we will always have space for this). Reduction producing a color index 0 will be taken to be
		//transparent.
		int idxs[64];
		RxReduceImageWithContext(reduction, tile->px, idxs, 8, 8, pal + effectivePaletteOffset - 1, effectivePaletteSize + 1,
			RX_FLAG_ALPHA_MODE_RESERVE | RX_FLAG_PRESERVE_ALPHA | RX_FLAG_NO_ALPHA_DITHER, diffuse);
		for (int j = 0; j < 64; j++) {
			//YIQ color
			RxConvertRgbToYiq(tile->px[j], &tile->pxYiq[j]);

			//adjust the color indices. Index 0 maps to 0, otherwise shift by the effective palette offset.
			tile->indices[j] = idxs[j] == 0 ? 0 : (idxs[j] + effectivePaletteOffset - 1);
			tile->px[j] = pal[tile->indices[j]];
		}

#ifdef BGGEN_USE_DCT
		//compute DCT
		BgiComputeDct(reduction, tile);
#endif

		tile->masterTile = i;
		tile->nRepresents = 1;
		tile->palette = bestPalette;
		tile->charNo = i;
	}
	RxFree(reduction);
}

void BgGenerate(
	COLOR                      *pOutPalette,
	unsigned char             **pOutChars,
	unsigned short            **pOutScreen, 
	int                        *outPalSize,
	int                        *outCharSize,
	int                        *outScreenSize,
	COLOR32                    *imgBits,
	unsigned int                width,
	unsigned int                height,
	const BgGenerateParameters *params,
	volatile int               *progress1,
	volatile int               *progress1Max,
	volatile int               *progress2,
	volatile int               *progress2Max
) {
	//palette setting
	unsigned int nPalettes = params->paletteRegion.count;
	unsigned int paletteBase = params->paletteRegion.base;
	unsigned int paletteOffset = params->paletteRegion.offset;
	unsigned int paletteSize = params->paletteRegion.length;

	//character setting
	int tileBase = params->characterSetting.base;
	int characterCompression = params->characterSetting.compress;
	unsigned int nMaxChars = params->characterSetting.nMax;

	//get parameters for BG type
	unsigned int nBits = 4, nMaxPltt = 16, nMaxCharLimit = 1024, nMaxColsPalette = 16, allowFlip = 1;
	switch (params->bgType) {
		case BGGEN_BGTYPE_TEXT_16x16:
			nBits = 4;
			nMaxPltt = 16;
			nMaxColsPalette = 16;
			nMaxCharLimit = 1024;
			allowFlip = 1;
			break;
		case BGGEN_BGTYPE_TEXT_256x1:
			nBits = 8;
			nMaxPltt = 1;
			nMaxColsPalette = 256;
			nMaxCharLimit = 1024;
			allowFlip = 1;
			break;
		case BGGEN_BGTYPE_AFFINE_256x1:
			nBits = 8;
			nMaxPltt = 1;
			nMaxColsPalette = 256;
			nMaxCharLimit = 256;
			allowFlip = 0;
			break;
		case BGGEN_BGTYPE_AFFINEEXT_256x16:
			nBits = 8;
			nMaxPltt = 16;
			nMaxColsPalette = 256;
			nMaxCharLimit = 1024;
			allowFlip = 1;
			break;
		case BGGEN_BGTYPE_BITMAP:
			nBits = 8;
			nMaxPltt = 1;
			nMaxColsPalette = 256;
			characterCompression = 0;
			allowFlip = 0;
			break;
	}
	
	//check parameters for BG type
	if (paletteBase >= nMaxPltt) paletteBase = nMaxPltt - 1;
	if (nPalettes < 1) nPalettes = 1;
	if ((paletteBase + nPalettes) > nMaxPltt) nPalettes = nMaxPltt - paletteBase;
	if (paletteOffset > nMaxColsPalette) paletteOffset = nMaxColsPalette - 1;
	if (paletteSize < 1) paletteSize = 1;
	if ((paletteOffset + paletteSize) > nMaxColsPalette) paletteSize = nMaxColsPalette - paletteOffset;
	if (nMaxChars < 1) nMaxChars = 1;
	if (nMaxChars > nMaxCharLimit) nMaxChars = nMaxCharLimit;

	unsigned int tilesX = width / 8;
	unsigned int tilesY = height / 8;
	unsigned int nTiles = tilesX * tilesY;
	BgTile *tiles = (BgTile *) RxMemCalloc(nTiles, sizeof(BgTile));

	//initialize progress
	*progress1Max = nTiles * 2; //2 passes
	*progress2Max = 1000;
	
	COLOR32 *palette = (COLOR32 *) calloc(256 * 16, sizeof(COLOR32));
	COLOR32 color0 = 0xFF00FF;
	
	//region of used palette area
	int color0Transparent = params->color0Mode != BGGEN_COLOR0_USE;
	unsigned int usedPaletteOffset = paletteOffset;
	unsigned int usedPaletteSize = paletteSize;
	if (paletteOffset == 0 && color0Transparent) {
		//color 0 is transparent, so we do not use it in color indexing.
		usedPaletteOffset++;
		usedPaletteSize--;
	}
	
	//create color palettes for the background.
	if (nPalettes == 1) {
		RxFlag flag = RX_FLAG_SORT_ALL | RX_FLAG_ALPHA_MODE_NONE;
		RxCreatePalette(imgBits, width, height, palette + (paletteBase << nBits) + usedPaletteOffset,
			usedPaletteSize, &params->balance, flag, NULL);
	} else {
		RxCreateMultiplePalettes(imgBits, tilesX, tilesY, palette, paletteBase, nPalettes, 1 << nBits,
			paletteSize, paletteOffset, !color0Transparent, &params->balance, progress1);
	}

	//insert the reserved transparent color, if not marked as used for color.
	if (paletteOffset == 0 && color0Transparent) {
		for (unsigned int i = paletteBase; i < paletteBase + nPalettes; i++) palette[i << nBits] = color0;
	}

	*progress1 = nTiles * 2; //make sure it's done

	//split image into 8x8 tiles.
	for (unsigned int y = 0; y < tilesY; y++) {
		for (unsigned int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			COLOR32 *block = tiles[x + y * tilesX].px;

			//copy block of pixels
			for (int i = 0; i < 8; i++) {
				memcpy(block + i * 8, imgBits + srcOffset + width * i, 8 * sizeof(COLOR32));
			}
			
			for (int i = 0; i < 8 * 8; i++) {
				int a = (block[i] >> 24) & 0xFF;
				if (a < 128) block[i] = 0; //make transparent pixels transparent black
				else block[i] |= 0xFF000000; //opaque
			}
		}
	}

	//match palettes to tiles
	BgSetupTiles(tiles, nTiles, nBits, palette, paletteSize, nPalettes, paletteBase, paletteOffset,
		params->dither.dither, params->dither.diffuse, &params->balance);

	//match tiles to each other
	unsigned int nChars = nTiles;
	if (characterCompression) {
		nChars = BgPerformCharacterCompression(tiles, nTiles, nBits, nMaxChars, allowFlip, palette, paletteSize, nPalettes, paletteBase,
			paletteOffset, &params->balance, progress2);
	}
	*progress2 = 1000;

	//create the output character data
	unsigned int nCharsFile = nChars;
	unsigned char *blocks = (unsigned char *) calloc(nCharsFile, 64 * sizeof(unsigned char));
	for (unsigned int i = 0; i < nTiles; i++) {
		BgTile *t = &tiles[i];
		if (t->masterTile != (unsigned int) i) continue;

		unsigned int chno = t->charNo;
		if (params->bgType != BGGEN_BGTYPE_BITMAP) {
			//put color index data in character order
			memcpy(&blocks[chno * 64], t->indices, sizeof(t->indices));
		} else {
			//put color index data in bitmap order
			int chX = (chno % tilesX) * 8;
			int chY = (chno / tilesX) * 8;
			for (int y = 0; y < 8; y++) memcpy(&blocks[chX + (chY + y) * tilesX * 8], t->indices + y * 8, 8);
		}
	}
	
	//prep data output
	if (params->bgType != BGGEN_BGTYPE_BITMAP) {
		//construct the BG screen data
		uint16_t *scrdat = (uint16_t *) calloc(nTiles, sizeof(uint16_t));
		for (unsigned int i = 0; i < nTiles; i++) {
			unsigned int chrno = (tiles[i].charNo + tileBase) & 0x03FF;
			unsigned int flip = tiles[i].flipMode & 0x3;
			unsigned int pltt = tiles[i].palette & 0xF;
			scrdat[i] = chrno | (flip << 10) | (pltt << 12);
		}
		*pOutScreen = scrdat;
		*outScreenSize = tilesX * tilesY * sizeof(uint16_t);
	} else {
		//bitmap BG: no screen data
		*pOutScreen = NULL;
		*outScreenSize = 0;
	}
	
	unsigned int outPaletteSize = nBits == 4 ? 256 : ((paletteBase + nPalettes) * 256);
	for (unsigned int i = paletteBase; i < paletteBase + nPalettes; i++) {
		for (unsigned int j = paletteOffset; j < paletteOffset + paletteSize; j++) {
			unsigned int index = (i << nBits) + j;
			pOutPalette[index] = ColorConvertToDS(palette[index]);
		}
	}
	*outPalSize = outPaletteSize * sizeof(COLOR);
	
	//prepare the ouput character data
	unsigned int outCharsSize = nChars * nBits * 8;
	unsigned char *outChars = (unsigned char *) calloc(outCharsSize, 1);
	if (nBits == 8) {
		memcpy(outChars, blocks, outCharsSize);
	} else {
		for (unsigned int i = 0; i < outCharsSize; i++) {
			outChars[i] = blocks[i * 2] | (blocks[i * 2 + 1] << 4);
		}
	}
	*pOutChars = outChars;
	*outCharSize = outCharsSize;
	
	free(blocks);
	free(palette);
	RxMemFree(tiles);
}

static double BgiPaletteCharError(RxReduction *reduction, COLOR32 *block, RxYiqColor *pals, unsigned char *character, int flip, double maxError) {
	unsigned int iXor = 0;
	if (flip & TILE_FLIPX) iXor ^= 007;
	if (flip & TILE_FLIPY) iXor ^= 070;

	double error = 0;
	for (unsigned int i = 0; i < 64; i++) {
		//convert source image pixel
		RxYiqColor yiq;
		COLOR32 col = block[i ^ iXor];
		RxConvertRgbToYiq(col, &yiq);

		//char pixel
		int index = character[i];
		RxYiqColor *matchedYiq = pals + index;
		int matchedA = index > 0 ? 255 : 0;
		if (matchedA == 0 && yiq.a < 128) {
			continue; //to prevent superfluous non-alpha difference
		}

		//diff
		error += RxComputeColorDifference(reduction, &yiq, matchedYiq);
		if (error >= maxError) return maxError;
	}
	return error;
}

double BgiBestPaletteCharError(RxReduction *reduction, COLOR32 *block, RxYiqColor *pals, unsigned char *character, int *flip, double maxError) {
	double e00 = BgiPaletteCharError(reduction, block, pals, character, TILE_FLIPNONE, maxError);
	if (e00 == 0) {
		*flip = TILE_FLIPNONE;
		return e00;
	}
	double e01 = BgiPaletteCharError(reduction, block, pals, character, TILE_FLIPX, maxError);
	if (e01 == 0) {
		*flip = TILE_FLIPX;
		return e01;
	}
	double e10 = BgiPaletteCharError(reduction, block, pals, character, TILE_FLIPY, maxError);
	if (e10 == 0) {
		*flip = TILE_FLIPY;
		return e10;
	}
	double e11 = BgiPaletteCharError(reduction, block, pals, character, TILE_FLIPXY, maxError);
	if (e11 == 0) {
		*flip = TILE_FLIPXY;
		return e11;
	}

	if (e00 <= e01 && e00 <= e10 && e00 <= e11) {
		*flip = TILE_FLIPNONE;
		return e00;
	}
	if (e01 <= e00 && e01 <= e10 && e01 <= e11) {
		*flip = TILE_FLIPX;
		return e01;
	}
	if (e10 <= e00 && e10 <= e01 && e10 <= e11) {
		*flip = TILE_FLIPY;
		return e10;
	}
	*flip = TILE_FLIPXY;
	return e11;
}

void BgAssemble(COLOR32 *imgBits, int width, int height, int nBits, COLOR *pals, int nPalettes,
	unsigned char *chars, int nChars, unsigned short **pOutScreen, int *outScreenSize,
	int balance, int colorBalance, int enhanceColors) {

	int tilesX = width / 8;
	int tilesY = height / 8;
	int nTiles = tilesX * tilesY;
	BgTile *tiles = (BgTile *) RxMemCalloc(nTiles, sizeof(BgTile));

	RxBalanceSetting balanceSetting;
	balanceSetting.balance = balance;
	balanceSetting.colorBalance = colorBalance;
	balanceSetting.enhanceColors = enhanceColors;

	//init params and convert palette
	RxYiqColor *paletteYiq = (RxYiqColor *) RxMemCalloc(nPalettes << nBits, sizeof(RxYiqColor));
	RxReduction *reduction = RxNew(&balanceSetting); // , (1 << nBits) - 1
	for (int i = 0; i < (nPalettes << nBits); i++) {
		RxConvertRgbToYiq(ColorConvertFromDS(pals[i]) | 0xFF000000, &paletteYiq[i]);
	}

	//split image into 8x8 tiles.
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			int srcOffset = x * 8 + y * 8 * (width);
			COLOR32 *block = tiles[x + y * tilesX].px;

			memcpy(block, imgBits + srcOffset, 32);
			memcpy(block + 8, imgBits + srcOffset + width, 32);
			memcpy(block + 16, imgBits + srcOffset + width * 2, 32);
			memcpy(block + 24, imgBits + srcOffset + width * 3, 32);
			memcpy(block + 32, imgBits + srcOffset + width * 4, 32);
			memcpy(block + 40, imgBits + srcOffset + width * 5, 32);
			memcpy(block + 48, imgBits + srcOffset + width * 6, 32);
			memcpy(block + 56, imgBits + srcOffset + width * 7, 32);
		}
	}

	//split input character to 8bpp
	unsigned char *charBuf = (unsigned char *) calloc(nChars, 64);
	if (nBits == 8) {
		memcpy(charBuf, chars, nChars * 64);
	} else {
		for (int i = 0; i < nChars * 32; i++) {
			charBuf[i * 2 + 0] = chars[i] & 0xF;
			charBuf[i * 2 + 1] = chars[i] >> 4;
		}
	}

	//construct output screen data.
	unsigned short *screen = (unsigned short *) calloc(tilesX * tilesY, 2);
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			BgTile *srcTile = tiles + (x + y * tilesX);
			COLOR32 *block = srcTile->px;

			//determine which input character (with which palette) best matches.
			double maxError = 1e32;
			int bestChar = 0, bestPalette = 0, bestFlip = 0;
			for (int i = 0; i < nChars; i++) {
				unsigned char *currentChar = charBuf + i * 64;

				for (int j = 0; j < nPalettes; j++) {
					int flipMode;
					RxYiqColor *currentPal = paletteYiq + (j << nBits);
					double err = BgiBestPaletteCharError(reduction, block, currentPal, currentChar, &flipMode, maxError);
					if (err < maxError) {
						maxError = err;
						bestChar = i;
						bestPalette = j;
						bestFlip = flipMode;
					}
				}
			}

			//exhausted all permutations, write
			screen[x + y * tilesX] = (bestChar & 0x3FF) | ((bestPalette & 0xF) << 12) | ((bestFlip & 0x3) << 10);
		}
	}
	
	RxFree(reduction);
	RxMemFree(tiles);
	RxMemFree(paletteYiq);
	free(charBuf);

	*pOutScreen = screen;
	*outScreenSize = (tilesX * tilesY) * 2;
}
