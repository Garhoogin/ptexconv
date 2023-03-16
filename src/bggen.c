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

float tileDifferenceFlip(REDUCTION *reduction, BGTILE *t1, BGTILE *t2, unsigned char mode) {
	double err = 0.0;
	COLOR32 *px1 = t1->px;
	double yw2 = reduction->yWeight * reduction->yWeight;
	double iw2 = reduction->iWeight * reduction->iWeight;
	double qw2 = reduction->qWeight * reduction->qWeight;

	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {

			int x2 = (mode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (mode & TILE_FLIPY) ? (7 - y) : y;

			int *yiq1 = &t1->pxYiq[x + y * 8][0];
			int *yiq2 = &t2->pxYiq[x2 + y2 * 8][0];
			double dy = reduction->lumaTable[yiq1[0]] - reduction->lumaTable[yiq2[0]];
			double di = yiq1[1] - yiq2[1];
			double dq = yiq1[2] - yiq2[2];
			double da = yiq1[3] - yiq2[3];
			err += yw2 * dy * dy + iw2 * di * di + qw2 * dq * dq + 1600 * da * da;
		}
	}

	return (float) err;
}

float tileDifference(REDUCTION *reduction, BGTILE *t1, BGTILE *t2, unsigned char *flipMode) {
	float err = tileDifferenceFlip(reduction, t1, t2, 0);
	if (err == 0) {
		*flipMode = 0;
		return err;
	}
	float err2 = tileDifferenceFlip(reduction, t1, t2, TILE_FLIPX);
	if (err2 == 0) {
		*flipMode = TILE_FLIPX;
		return err2;
	}
	float err3 = tileDifferenceFlip(reduction, t1, t2, TILE_FLIPY);
	if (err3 == 0) {
		*flipMode = TILE_FLIPY;
		return err3;
	}
	float err4 = tileDifferenceFlip(reduction, t1, t2, TILE_FLIPXY);
	if (err4 == 0) {
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

void bgAddTileToTotal(REDUCTION *reduction, int *pxBlock, BGTILE *tile) {
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			COLOR32 col = tile->px[x + y * 8];

			int x2 = (tile->flipMode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (tile->flipMode & TILE_FLIPY) ? (7 - y) : y;
			int *dest = pxBlock + 4 * (x2 + y2 * 8);

			int yiq[4];
			rgbToYiq(col, yiq);
			dest[0] += (int) (16.0 * reduction->lumaTable[yiq[0]] + 0.5f);
			dest[1] += yiq[1];
			dest[2] += yiq[2];
			dest[3] += yiq[3];
		}
	}
}

typedef struct TILE_DIFF_ {
	int tile1;
	int tile2;
	double diff;		//post-biased
} TILE_DIFF;

typedef struct TILE_DIFF_LIST_ {
	TILE_DIFF *diffBuff;
	int diffBuffSize;
	int diffBuffLength;
	double minDiff;
	double maxDiff;
} TILE_DIFF_LIST;

void tdlInit(TILE_DIFF_LIST *list, int nEntries) {
	list->diffBuffSize = nEntries;
	list->diffBuffLength = 0;
	list->minDiff = 1e32;
	list->maxDiff = 0;
	list->diffBuff = (TILE_DIFF *) calloc(list->diffBuffSize, sizeof(TILE_DIFF));
}

void tdlFree(TILE_DIFF_LIST *list) {
	free(list->diffBuff);
	list->diffBuff = NULL;
	list->diffBuffLength = 0;
	list->diffBuffSize = 0;
}

void tdlAdd(TILE_DIFF_LIST *list, int tile1, int tile2, double diff) {
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
	memmove(list->diffBuff + destIndex + 1, list->diffBuff + destIndex, nEntriesToMove * sizeof(TILE_DIFF));
	list->diffBuff[destIndex].tile1 = tile1;
	list->diffBuff[destIndex].tile2 = tile2;
	list->diffBuff[destIndex].diff = diff;
	if (added) {
		list->diffBuffLength++;
	}
	list->minDiff = list->diffBuff[0].diff;
	list->maxDiff = list->diffBuff[list->diffBuffLength - 1].diff;
}

void tdlRemoveAll(TILE_DIFF_LIST *list, int tile1, int tile2) {
	//remove all diffs involving tile1 and tile2
	for (int i = 0; i < list->diffBuffLength; i++) {
		TILE_DIFF *td = list->diffBuff + i;
		if (td->tile1 == tile1 || td->tile2 == tile1 || td->tile1 == tile2 || td->tile2 == tile2) {
			memmove(td, td + 1, (list->diffBuffLength - i - 1) * sizeof(TILE_DIFF));
			list->diffBuffLength--;
			i--;
		}
	}
	if (list->diffBuffLength > 0) {
		list->minDiff = list->diffBuff[0].diff;
		list->maxDiff = list->diffBuff[list->diffBuffLength - 1].diff;
	}
}

void tdlPop(TILE_DIFF_LIST *list, TILE_DIFF *out) {
	if (list->diffBuffLength > 0) {
		memcpy(out, list->diffBuff, sizeof(TILE_DIFF));
		memmove(list->diffBuff, list->diffBuff + 1, (list->diffBuffLength - 1) * sizeof(TILE_DIFF));
		list->diffBuffLength--;
		if (list->diffBuffLength > 0) {
			list->minDiff = list->diffBuff[0].diff;
		}
	}
}

void tdlReset(TILE_DIFF_LIST *list) {
	list->diffBuffLength = 0;
	list->maxDiff = 0;
	list->minDiff = 1e32;
}

int performCharacterCompression(BGTILE *tiles, int nTiles, int nBits, int nMaxChars, COLOR32 *palette, int paletteSize, int nPalettes,
	int paletteBase, int paletteOffset, int balance, int colorBalance, int *progress) {
	int nChars = nTiles;
	float *diffBuff = (float *) calloc(nTiles * nTiles, sizeof(float));
	unsigned char *flips = (unsigned char *) calloc(nTiles * nTiles, 1); //how must each tile be manipulated to best match its partner

	REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
	initReduction(reduction, balance, colorBalance, 15, 0, 255);
	for (int i = 0; i < nTiles; i++) {
		BGTILE *t1 = tiles + i;
		for (int j = 0; j < i; j++) {
			BGTILE *t2 = tiles + j;

			diffBuff[i + j * nTiles] = tileDifference(reduction, t1, t2, &flips[i + j * nTiles]);
			diffBuff[j + i * nTiles] = diffBuff[i + j * nTiles];
			flips[j + i * nTiles] = flips[i + j * nTiles];
		}
		*progress = (i * i) / nTiles * 500 / nTiles;
	}

	//first, combine tiles with a difference of 0.

	for (int i = 0; i < nTiles; i++) {
		BGTILE *t1 = tiles + i;
		if (t1->masterTile != i) continue;

		for (int j = 0; j < i; j++) {
			BGTILE *t2 = tiles + j;
			if (t2->masterTile != j) continue;

			if (diffBuff[i + j * nTiles] == 0) {
				//merge all tiles with master index i to j
				for (int k = 0; k < nTiles; k++) {
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
		TILE_DIFF_LIST tdl;
		tdlInit(&tdl, 64);

		//keep finding the most similar tile until we get character count down
		int direction = 0;
		while (nChars > nMaxChars) {
			for (int iOuter = 0; iOuter < nTiles; iOuter++) {
				int i = direction ? (nTiles - 1 - iOuter) : iOuter; //criss cross the direction
				BGTILE *t1 = tiles + i;
				if (t1->masterTile != i) continue;

				for (int j = 0; j < i; j++) {
					BGTILE *t2 = tiles + j;
					if (t2->masterTile != j) continue;

					double thisErrorEntry = diffBuff[i + j * nTiles];
					double thisError = thisErrorEntry;
					double bias = t1->nRepresents + t2->nRepresents;
					bias *= bias;

					thisError = thisErrorEntry * bias;
					tdlAdd(&tdl, j, i, thisError);
				}
			}

			//now merge tiles while we can
			int tile1, tile2;
			while (tdl.diffBuffLength > 0 && nChars > nMaxChars) {
				TILE_DIFF td;
				tdlPop(&tdl, &td);

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
				for (int i = 0; i < nTiles; i++) {
					if (tiles[i].masterTile == tile2) {
						tiles[i].masterTile = tile1;
						tiles[i].flipMode ^= flipDiff;
						tiles[i].nRepresents = 0;
						tiles[tile1].nRepresents++;
					}
				}

				nChars--;
				*progress = 500 + (int) (500 * sqrt((float) (nTiles - nChars) / (nTiles - nMaxChars)));

				tdlRemoveAll(&tdl, td.tile1, td.tile2);
			}
			direction = !direction;
			tdlReset(&tdl);
		}
		tdlFree(&tdl);
	}

	free(flips);
	free(diffBuff);

	//try to make the compressed result look less bad
	for (int i = 0; i < nTiles; i++) {
		if (tiles[i].masterTile != i) continue;
		if (tiles[i].nRepresents <= 1) continue; //no averaging required for just one tile
		BGTILE *tile = tiles + i;

		//average all tiles that use this master tile.
		int pxBlock[64 * 4] = { 0 };
		int nRep = tile->nRepresents;
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile != i) continue;
			BGTILE *tile2 = tiles + j;
			bgAddTileToTotal(reduction, pxBlock, tile2);
		}

		//divide by count, convert to 32-bit RGB
		for (int j = 0; j < 64 * 4; j++) {
			int ch = pxBlock[j];

			//proper round to nearest
			if (ch >= 0) {
				ch = (ch * 2 + nRep) / (nRep * 2);
			} else {
				ch = (ch * 2 - nRep) / (nRep * 2);
			}
			pxBlock[j] = ch;
		}
		for (int j = 0; j < 64; j++) {
			int cy = pxBlock[j * 4 + 0]; //times 16
			int ci = pxBlock[j * 4 + 1];
			int cq = pxBlock[j * 4 + 2];
			int ca = pxBlock[j * 4 + 3];

			double dcy = ((double) cy) / 16.0;
			cy = (int) (pow(dcy * 0.00195695, 1.0 / reduction->gamma) * 511.0);
			int yiq[] = { cy, ci, cq, ca };
			int rgb[4];
			yiqToRgb(rgb, yiq);

			tile->px[j] = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16) | (ca << 24);
		}

		//try to determine the most optimal palette. Child tiles can be different palettes.
		int bestPalette = paletteBase;
		double bestError = 1e32;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			COLOR32 *pal = palette + (j << nBits) + paletteOffset + !paletteOffset;
			double err = computePaletteErrorYiq(reduction, tile->px, 64, pal, paletteSize - !paletteOffset, 128, bestError);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//now, match colors to indices.
		COLOR32 *pal = palette + (bestPalette << nBits);
		ditherImagePaletteEx(tile->px, NULL, 8, 8, pal + paletteOffset + !paletteOffset,
			paletteSize - !paletteOffset, 0, 1, 0, 0.0f, balance, colorBalance, 0);
		for (int j = 0; j < 64; j++) {
			COLOR32 col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = closestPalette(col, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset)
					+ !paletteOffset + paletteOffset;
			}

			tile->indices[j] = index;
			tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;
		}
		tile->palette = bestPalette;

		//lastly, copy tile->indices to all child tile->indices, just to make sure palette and character are in synch.
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile != i) continue;
			if (j == i) continue;
			BGTILE *tile2 = tiles + j;

			memcpy(tile2->indices, tile->indices, 64);
			tile2->palette = tile->palette;
		}
	}

	destroyReduction(reduction);
	free(reduction);
	return nChars;
}

void setupBgTiles(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse) {
	setupBgTilesEx(tiles, nTiles, nBits, palette, paletteSize, nPalettes, paletteBase, paletteOffset, dither, diffuse, BALANCE_DEFAULT, BALANCE_DEFAULT, FALSE);
}

void setupBgTilesEx(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse, int balance, int colorBalance, int enhanceColors) {
	REDUCTION *reduction = (REDUCTION *) calloc(1, sizeof(REDUCTION));
	initReduction(reduction, balance, colorBalance, 15, enhanceColors, paletteSize);

	if (!dither) diffuse = 0.0f;
	for (int i = 0; i < nTiles; i++) {
		BGTILE *tile = tiles + i;

		//create histogram for tile
		resetHistogram(reduction);
		computeHistogram(reduction, tile->px, 8, 8);
		flattenHistogram(reduction);

		int bestPalette = paletteBase;
		double bestError = 1e32;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			COLOR32 *pal = palette + (j << nBits);
			double err = computeHistogramPaletteError(reduction, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, bestError);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//match colors
		COLOR32 *pal = palette + (bestPalette << nBits);

		//do optional dithering (also matches colors at the same time)
		ditherImagePaletteEx(tile->px, NULL, 8, 8, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, FALSE, TRUE, FALSE, diffuse, balance, colorBalance, enhanceColors);
		for (int j = 0; j < 64; j++) {
			COLOR32 col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = closestPalette(col, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset)
					+ !paletteOffset + paletteOffset;
			}

			tile->indices[j] = index;
			tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;

			//YIQ color
			rgbToYiq(col, &tile->pxYiq[j][0]);
		}

		tile->masterTile = i;
		tile->nRepresents = 1;
		tile->palette = bestPalette;
	}
	destroyReduction(reduction);
	free(reduction);
}

void bgGenerate(COLOR32 *imgBits, int width, int height, int nBits, int dither, float diffuse,
	COLOR **pOutPalette, unsigned char **pOutChars, unsigned short **pOutScreen,
	int *outPalSize, int *outCharSize, int *outScreenSize,
	int paletteBase, int nPalettes, int fmt, int tileBase, int mergeTiles,
	int paletteSize, int paletteOffset, int rowLimit, int nMaxChars,
	int balance, int colorBalance, int enhanceColors,
	int *progress1, int *progress1Max, int *progress2, int *progress2Max) {

	//cursory sanity checks
	if (nBits == 4) {
		if (paletteBase >= 16) paletteBase = 15;
		else if (paletteBase < 0) paletteBase = 0;
		if (nPalettes > 16) nPalettes = 16;
		else if (nPalettes < 1) nPalettes = 1;
		if (paletteBase + nPalettes > 16) nPalettes = 16 - paletteBase;

		if (paletteOffset < 0) paletteOffset = 0;
		else if (paletteOffset >= 16) paletteOffset = 15;
		if (paletteOffset + paletteSize > 16) paletteSize = 16 - paletteOffset;
	} else {
		paletteBase = 0;
		nPalettes = 1;

		if (paletteOffset < 0) paletteOffset = 0;
		if (paletteSize < 1) paletteSize = 1;
		if (paletteOffset >= 256) paletteOffset = 255;
		if (paletteSize > 256) paletteSize = 256;
		if (paletteOffset + paletteSize > 256) paletteSize = 256 - paletteOffset;
	}
	if (paletteSize < 1) paletteSize = 1;

	int tilesX = width / 8;
	int tilesY = height / 8;
	int nTiles = tilesX * tilesY;
	BGTILE *tiles = (BGTILE *) calloc(nTiles, sizeof(BGTILE));

	//initialize progress
	*progress1Max = nTiles * 2; //2 passes
	*progress2Max = 1000;

	COLOR32 *palette = (COLOR32 *) calloc(256, 4);
	if (nBits < 5) nBits = 4;
	else nBits = 8;
	if (nPalettes == 1) {
		if (paletteOffset) {
			createPaletteSlowEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset, paletteSize, balance, colorBalance, enhanceColors, 0);
		} else {
			createPaletteSlowEx(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset + 1, paletteSize - 1, balance, colorBalance, enhanceColors, 0);
			palette[(paletteBase << nBits) + paletteOffset] = 0xFF00FF; //transparent fill color
		}
	} else {
		createMultiplePalettesEx(imgBits, tilesX, tilesY, palette, paletteBase, nPalettes, 1 << nBits, paletteSize, paletteOffset, balance, colorBalance, enhanceColors, progress1);
		if (paletteOffset == 0) {
			for (int i = paletteBase; i < paletteBase + nPalettes; i++) palette[i << nBits] = 0xFF00FF;
		}
	}
	*progress1 = nTiles * 2; //make sure it's done

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

	setupBgTilesEx(tiles, nTiles, nBits, palette, paletteSize, nPalettes, paletteBase, paletteOffset, dither, diffuse, balance, colorBalance, enhanceColors);

	//match tiles to each other
	int nChars = nTiles;
	if (mergeTiles) {
		nChars = performCharacterCompression(tiles, nTiles, nBits, nMaxChars, palette, paletteSize, nPalettes, paletteBase,
			paletteOffset, balance, colorBalance, progress2);
	}

	COLOR32 *blocks = (COLOR32 *) calloc(64 * nChars, sizeof(COLOR32));
	int writeIndex = 0;
	for (int i = 0; i < nTiles; i++) {
		if (tiles[i].masterTile != i) continue;
		BGTILE *t = tiles + i;
		COLOR32 *dest = blocks + 64 * writeIndex;

		for (int j = 0; j < 64; j++) {
			if (nBits == 4) dest[j] = t->indices[j] & 0xF;
			else dest[j] = t->indices[j];
		}

		writeIndex++;
		if (writeIndex >= nTiles) {
			break;
		}
	}
	*progress2 = 1000;

	//scrunch down masterTile indices
	int nFoundMasters = 0;
	for (int i = 0; i < nTiles; i++) {
		int master = tiles[i].masterTile;
		if (master != i) continue;

		//a master tile. Overwrite all tiles that use this master with nFoundMasters with bit 31 set (just in case)
		for (int j = 0; j < nTiles; j++) {
			if (tiles[j].masterTile == master) tiles[j].masterTile = nFoundMasters | 0x40000000;
		}
		nFoundMasters++;
	}
	for (int i = 0; i < nTiles; i++) {
		tiles[i].masterTile &= 0xFFFF;
	}

	//prep data output
	unsigned short *indices = (unsigned short *) calloc(nTiles, 2);
	for (int i = 0; i < nTiles; i++) {
		indices[i] = tiles[i].masterTile + tileBase;
	}
	unsigned char *modes = (unsigned char *) calloc(nTiles, 1);
	for (int i = 0; i < nTiles; i++) {
		modes[i] = tiles[i].flipMode;
	}
	unsigned char *paletteIndices = (unsigned char *) calloc(nTiles, 1);
	if (nBits == 4) {
		for (int i = 0; i < nTiles; i++) {
			paletteIndices[i] = tiles[i].indices[0] >> 4;
		}
	}

	*pOutPalette = (COLOR *) calloc(nPalettes << nBits, sizeof(COLOR));
	for (int i = 0; i < (nPalettes << nBits); i++) {
		(*pOutPalette)[i] = ColorConvertToDS(palette[i]);
	}
	*pOutChars = (unsigned char *) calloc(nChars, nBits * 8);
	for (int i = 0; i < nChars; i++) {
		unsigned char *dest = (*pOutChars) + (i * nBits * 8);

		if (nBits == 8) {
			for (int j = 0; j < 64; j++) dest[j] = blocks[i * 64 + j];
		} else {
			for (int j = 0; j < 32; j++) {
				dest[j] = blocks[i * 64 + j * 2] | (blocks[i * 64 + j * 2 + 1] << 4);
			}
		}
	}
	*pOutScreen = (unsigned short *) calloc(tilesX * tilesY, 2);
	for (int i = 0; i < tilesX * tilesY; i++) {
		(*pOutScreen)[i] = indices[i] | (modes[i] << 10) | (paletteIndices[i] << 12);
	}
	*outPalSize = (nPalettes << nBits) * 2;
	*outCharSize = nChars * nBits * 8;
	*outScreenSize = tilesX * tilesY * 2;

	free(modes);
	free(blocks);
	free(indices);
	free(palette);
	free(paletteIndices);
}
