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

typedef struct BGTILE_ {
	unsigned char indices[64];
	COLOR32 px[64]; //redundant, speed
	int masterTile;
	int nRepresents;
	int flipMode;
} BGTILE;

int tileDifferenceFlip(BGTILE *t1, BGTILE *t2, unsigned char mode) {
	int err = 0;
	COLOR32 *px1 = t1->px;
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {

			int x2 = (mode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (mode & TILE_FLIPY) ? (7 - y) : y;
			COLOR32 c1 = *(px1++);
			COLOR32 c2 = t2->px[x2 + y2 * 8];

			int dr = (c1 & 0xFF) - (c2 & 0xFF);
			int dg = ((c1 >> 8) & 0xFF) - ((c2 >> 8) & 0xFF);
			int db = ((c1 >> 16) & 0xFF) - ((c2 >> 16) & 0xFF);
			int da = ((c1 >> 24) & 0xFF) - ((c2 >> 24) & 0xFF);
			int dy, du, dv;
			convertRGBToYUV(dr, dg, db, &dy, &du, &dv);

			err += 4 * dy * dy + du * du + dv * dv + 16 * da * da;

		}
	}

	return err;
}

int tileDifference(BGTILE *t1, BGTILE *t2, unsigned char *flipMode) {
	int err = tileDifferenceFlip(t1, t2, 0);
	if (err == 0) {
		*flipMode = 0;
		return err;
	}
	int err2 = tileDifferenceFlip(t1, t2, TILE_FLIPX);
	if (err2 == 0) {
		*flipMode = TILE_FLIPX;
		return err2;
	}
	int err3 = tileDifferenceFlip(t1, t2, TILE_FLIPY);
	if (err3 == 0) {
		*flipMode = TILE_FLIPY;
		return err3;
	}
	int err4 = tileDifferenceFlip(t1, t2, TILE_FLIPXY);
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

void bgAddTileToTotal(COLOR32 *pxBlock, BGTILE *tile) {
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			COLOR32 col = tile->px[x + y * 8];

			int x2 = (tile->flipMode & TILE_FLIPX) ? (7 - x) : x;
			int y2 = (tile->flipMode & TILE_FLIPY) ? (7 - y) : y;
			COLOR32 *dest = pxBlock + 4 * (x2 + y2 * 8);

			dest[0] += col & 0xFF;
			dest[1] += (col >> 8) & 0xFF;
			dest[2] += (col >> 16) & 0xFF;
			dest[3] += (col >> 24) & 0xFF;
		}
	}
}

void bgGenerate(COLOR32 *imgBits, int width, int height, int nBits, int dither, float diffuse,
				COLOR **pOutPalette, unsigned char **pOutChars, unsigned short **pOutScreen,
				int *outPalSize, int *outCharSize, int *outScreenSize,
				int paletteBase, int nPalettes, int fmt, int tileBase, int mergeTiles,
				int paletteSize, int paletteOffset, int rowLimit, int nMaxChars,
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

	COLOR32 *palette = (COLOR32 *) calloc(256, 4);
	if (nBits < 5) nBits = 4;
	else nBits = 8;
	if (nPalettes == 1) {
		if (paletteOffset) {
			createPaletteExact(imgBits, width, height, palette + (paletteBase << nBits) + paletteOffset, paletteSize);
		} else {
			createPalette_(imgBits, width, height, palette + (paletteBase << nBits), paletteSize);
		}
	} else {
		createMultiplePalettes(imgBits, tilesX, tilesY, palette, paletteBase, nPalettes, 1 << nBits, paletteSize, paletteOffset, progress1);
	}
	*progress1 = nTiles * 2; //make sure it's done

							 //match palettes to tiles
	for (int i = 0; i < nTiles; i++) {
		BGTILE *tile = tiles + i;

		int bestPalette = paletteBase;
		int bestError = 0x7FFFFFFF;
		for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
			COLOR32 *pal = palette + (j << nBits);
			int err = getPaletteError((RGB *) tile->px, 64, (RGB *) pal + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset);

			if (err < bestError) {
				bestError = err;
				bestPalette = j;
			}
		}

		//match colors
		COLOR32 *pal = palette + (bestPalette << nBits);

		//do optional dithering (also matches colors at the same time)
		if(dither) ditherImagePalette(tile->px, 8, 8, pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, 0, 1, 0, diffuse);
		for (int j = 0; j < 64; j++) {
			COLOR32 col = tile->px[j];
			int index = 0;
			if (((col >> 24) & 0xFF) > 127) {
				index = closestpalette(*(RGB *) &col, (RGB *) pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, NULL)
					+ !paletteOffset + paletteOffset;
			}
			if (nBits == 4) {
				tile->indices[j] = (bestPalette << 4) | index;
			} else {
				tile->indices[j] = index;
			}
			tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;
		}
		tile->masterTile = i;
		tile->nRepresents = 1;
	}

	//match tiles to each other
	int nChars = nTiles;
	if (mergeTiles) {
		int *diffBuff = (int *) calloc(nTiles * nTiles, sizeof(int));
		unsigned char *flips = (unsigned char *) calloc(nTiles * nTiles, 1); //how must each tile be manipulated to best match its partner

		for (int i = 0; i < nTiles; i++) {
			BGTILE *t1 = tiles + i;
			for (int j = 0; j < i; j++) {
				BGTILE *t2 = tiles + j;

				diffBuff[i + j * nTiles] = tileDifference(t1, t2, &flips[i + j * nTiles]);
				diffBuff[j + i * nTiles] = diffBuff[i + j * nTiles];
				flips[j + i * nTiles] = flips[i + j * nTiles];
			}
			*progress2 = (i * i) / nTiles * 500 / nTiles;
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
					if (nTiles > nMaxChars) *progress2 = 500 + (nTiles - nChars) * 500 / (nTiles - nMaxChars);
				}
			}
		}

		//still too many?
		if (nChars > nMaxChars) {
			//damn

			//keep finding the most similar tile until we get character count down
			while (nChars > nMaxChars) {
				unsigned long long int leastError = 0x7FFFFFFF;
				int tile1 = 0, tile2 = 1;

				for (int i = 0; i < nTiles; i++) {
					BGTILE *t1 = tiles + i;
					if (t1->masterTile != i) continue;

					for (int j = 0; j < i; j++) {
						BGTILE *t2 = tiles + j;
						if (t2->masterTile != j) continue;
						unsigned long long int thisError = diffBuff[i + j * nTiles] * t1->nRepresents * t2->nRepresents;

						if (thisError < leastError) {
							//if (nBits == 8 || ((t2->indices[0] >> 4) == (t1->indices[0] >> 4))) { //make sure they're the same palette
							tile1 = j;
							tile2 = i;
							leastError = thisError;
							//}
						}
					}
				}

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
				*progress2 = 500 + (nTiles - nChars) * 500 / (nTiles - nMaxChars);
			}
		}

		free(flips);
		free(diffBuff);

		//try to make the compressed result look less bad
		for (int i = 0; i < nTiles; i++) {
			if (tiles[i].masterTile != i) continue;
			if (tiles[i].nRepresents <= 1) continue; //no averaging required for just one tile
			BGTILE *tile = tiles + i;

			//average all tiles that use this master tile.
			COLOR32 pxBlock[64 * 4] = { 0 };
			int nRep = tile->nRepresents;
			for (int j = 0; j < nTiles; j++) {
				if (tiles[j].masterTile != i) continue;
				BGTILE *tile2 = tiles + j;
				bgAddTileToTotal(pxBlock, tile2);
			}
			for (int j = 0; j < 64 * 4; j++) {
				pxBlock[j] = (pxBlock[j] + (nRep >> 1)) / nRep;
			}
			for (int j = 0; j < 64; j++) {
				tile->px[j] = pxBlock[j * 4] | (pxBlock[j * 4 + 1] << 8) | (pxBlock[j * 4 + 2] << 16) | (pxBlock[j * 4 + 3] << 24);
			}

			//try to determine the most optimal palette. Child tiles can be different palettes.
			int bestPalette = paletteBase;
			int bestError = 0x7FFFFFFF;
			for (int j = paletteBase; j < paletteBase + nPalettes; j++) {
				COLOR32 *pal = palette + (j << nBits);
				int err = getPaletteError((RGB *) tile->px, 64, (RGB *) pal + paletteOffset - !!paletteOffset, paletteSize + !!paletteOffset);

				if (err < bestError) {
					bestError = err;
					bestPalette = j;
				}
			}

			//now, match colors to indices.
			COLOR32 *pal = palette + (bestPalette << nBits);
			for (int j = 0; j < 64; j++) {
				COLOR32 col = tile->px[j];
				int index = 0;
				if (((col >> 24) & 0xFF) > 127) {
					index = closestpalette(*(RGB *) &col, (RGB *) pal + paletteOffset + !paletteOffset, paletteSize - !paletteOffset, NULL)
						+ !paletteOffset + paletteOffset;
				}
				if (nBits == 4) {
					tile->indices[j] = (bestPalette << 4) | index;
				} else {
					tile->indices[j] = index;
				}
				tile->px[j] = index ? (pal[index] | 0xFF000000) : 0;
			}

			//lastly, copy tile->indices to all child tile->indices, just to make sure palette and character are in synch.
			for (int j = 0; j < nTiles; j++) {
				if (tiles[j].masterTile != i) continue;
				if (j == i) continue;
				BGTILE *tile2 = tiles + j;

				memcpy(tile2->indices, tile->indices, 64);
			}
		}
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
