#include <stdio.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include "compression.h"
#include "texture.h"
#include "texconv.h"
#include "palette.h"
#include "bggen.h"
#include "grf.h"
#include "gdip.h"

//ensure TCHAR and related macros are defined
#ifdef _WIN32
#   include <tchar.h>
#else
#   define TCHAR char
#   define _T(x) x
#   define _tmain main
#   define _tfopen fopen
#   define _tcscmp strcmp
#   define _tprintf printf
#   define _ftprintf fprintf
#   define _tcslen strlen
#   define _tcsdup strdup
#   define _tcsrchr strrchr
#   define _ttoi atoi
#endif

//MinGW's wprintf is defective. Account for this here.
#ifdef _MSC_VER
#   define TC_STR _T("%s")
#else //_MSC_VER
#ifdef _UNICODE
#   define TC_STR _T("%ls")
#else //_UNICODE
#   define TC_STR _T("%s")
#endif
#endif //_MSC_VER

//make sure we have an image I/O provider
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define PTC_INFILE_MAX RX_PALETTE_MAX_COUNT

typedef struct PtcImage_ {
	COLOR32 *px;
	int width;
	int height;
} PtcImage;

typedef enum PtcDataType_ {
	PTC_GMODE_BG,         // Data output is a background graphic
	PTC_GMODE_TEXTURE     // Data output is a texture
} PtcDataType;

typedef enum PtcOutputMode_ {
	PTC_OUT_MODE_BINARY,  // Data output is raw binary
	PTC_OUT_MODE_C,       // Data output is a C source and header file pair
	PTC_OUT_MODE_DIB,     // Data output is a DIB file
	PTC_OUT_MODE_NNSTGA,  // Data output is an NNS TGA file
	PTC_OUT_MODE_GRF      // Data output is a GRF file
} PtcOutputMode;

typedef struct PtcOptions_ {
	//global options
	PtcDataType genMode;
	PtcOutputMode outMode;
	CxCompressionPolicy compressionPolicy;
	RxBalanceSetting balance;
	int outFixedPalette;
	
	//command line control
	int silent;
	
	int useAlphaKey;
	COLOR32 alphaKey;
	
	//file names for conversion
	const TCHAR *(srcFiles[PTC_INFILE_MAX]);  // path of input image
	const TCHAR *outBase;                     // base file output name
	const TCHAR *fixedPalette;                // file name of fixed palette
	int nSrcFile;                             // number of input files
	
	//dithering options
	int diffuse;      // the diffusion amount, in percent
	int ditherAlpha;  // when dithering is enabled, controls dithering in the alpha channel
	
	//options for BG
	int bgType;
	int nMaxColors;
	int paletteBase;
	int nPalettes;
	int paletteOffset;
	int compressPalette;
	int charBase;
	int explicitCharBase;
	int nMaxChars;
	int screenExclusive;
	int outputScreen;
	int bgColor0Use;
	const TCHAR *srcPalFile;
	const TCHAR *srcChrFile;
	
	//options for texture
	int texFmt;              // texture format
	int c0xp;                // Color-0 transparency mode
	int trimT;               // Trim texture data on T axis
	int noLimitPaletteSize;  // Limit palette size for tex4x4 conversion
	int tex4x4Threshold;     // Palette merge threshold for tex4x4 conversion
} PtcOptions;


#ifdef _MSC_VER
int _fltused;

extern long _ftol(double d);

long _ftol2_sse(float f) { //ugly hack
	return _ftol(f);
}
#endif // _MSC_VER

//BG file suffixes
#define NBFX_EXTLEN    8 /* _xxx.bin */
#define NBFP_EXTENSION _T("_pal.bin")
#define NBFC_EXTENSION _T("_chr.bin")
#define NBFS_EXTENSION _T("_scr.bin")
#define NBFB_EXTENSION _T("_bmp.bin")

//Texture file suffixes
#define NTFX_EXTLEN    8 /* _xxx.bin */
#define NTFP_EXTENSION _T("_pal.bin")
#define NTFT_EXTENSION _T("_tex.bin")
#define NTFI_EXTENSION _T("_idx.bin")

#define VERSION "1.6.0.2"

static const char *g_helpString = ""
	"DS Texture Converter command line utility version " VERSION "\n"
	"\n"
	"Usage: ptexconv <option...> image [option...]\n"
	"\n"
	"Global options:\n"
	"   -gb     Generate BG (default)\n"
	"   -gt     Generate texture\n"
	"   -o      Specify output base name\n"
	"   -ob     Output binary (default)\n"
	"   -oc     Output as C header file\n"
	"   -og     Output as GRIT GRF file\n"
	"   -k  <c> Specify alpha key as 24-bit RRGGBB hex color\n"
	"   -d  <n> Use dithering of n% (default 0%)\n"
	"   -cm <n> Limit palette colors to n, regardless of bit depth\n"
	"   -bb <n> Lightness-Color balance [1, 39] (default 20)\n"
	"   -bc <n> Red-Green color balance [1, 39] (default 20)\n"
	"   -be     Enhance colors in gradients (off by default)\n"
	"   -v      Verbose\n"
	"   -h      Display help text\n"
	"\n"
	"BG Options:\n"
	"   -bt4    Output BG data as text            (4bpp)\n"
	"   -bt8    Output BG data as text            (8bpp)\n"
	"   -ba     Output BG data as affine          (8bpp)\n"
	"   -bA     Output BG data as affine extended (8bpp, default)\n"
	"   -bB     Output BG data as bitmap (no BG screen output)\n"
	"   -p  <n> Use n palettes in output\n"
	"   -po <n> Use per palette offset n\n"
	"   -pc     Use compressed palette\n"
	"   -pb <n> Use palette base index n\n"
	"   -p0o    Use color 0 as an opaque color slot\n"
	"   -cb <n> Use character base index n\n"
	"   -cc <n> Compress characters to a maximum of n (default is 1024)\n"
	"   -cn     No character compression\n"
	"   -wp <f> Use or overwrite an existing palette file (binary only)\n"
	"   -wc <f> Use or append to an existing character file (binary only)\n"
	"   -ns     Do not output screen data\n"
	"   -se     Output screen only. Requires -wp and -wc (will not modify).\n"
	"   -od     Output as DIB (disables character compression)\n"
	"\n"
	"Texture Options:\n"
	"   -f  <f> Specify format {palette4, palette16, palette256, a3i5, a5i3, tex4x4, direct}\n"
	"   -cn     Do not limit output palette size for tex4x4 conversion\n"
	"   -ct <n> Set tex4x4 palette compresion strength [0, 100] (default 0).\n"
	"   -ot     Output as NNS TGA\n"
	"   -tt     Trim the texture in the T axis if its height is not a power of 2\n"
	"   -t0x    Color 0 is transparent     (default: inferred)\n"
	"   -t0o    Color 0 is not transparent (default: inferred)\n"
	"   -da     Apply dithering in the alpha  channel (a3i5, a5i3)\n"
	"   -fp <f> Specify fixed palette file\n"
	"   -fpo    Outputs the fixed palette among other output files when used\n"
	"\n"
	"Compression Options:\n"
	"   -cbios  Enable use of all BIOS compression types (valid for binary, C, GRF)\n"
	"   -cno    Enable use of no/dummy compression       (valid for binary, C, GRF)\n"
	"   -clz    Enable use of LZ compression             (valid for binary, C, GRF)\n"
	"   -ch     Enable use of any Huffman compression    (valid for binary, C, GRF)\n"
	"   -ch4    Enable use of 4-bit Huffman compression  (valid for binary, C, GRF)\n"
	"   -ch8    Enable use of 4-bit Huffman compression  (valid for binary, C, GRF)\n"
	"   -crl    Enable use of RLE compression            (valid for binary, C, GRF)\n"
	"   -clzx   Enable use of LZ extended compression    (valid for binary, C, GRF)\n"
	"   -c8     Allow VRAM-unsafe compression            (valid for binary, C, GRF)\n"
	"\n"
"";

static const char *texHeader = ""
	"///////////////////////////////////////\n"
	"// \n"
	"// %s\n"
	"// Generated %d/%d/%d %d:%02d %cM\n"
	"// Format: %s\n"
	"// Colors: %d\n"
	"// Size: %dx%d\n"
	"// \n"
	"///////////////////////////////////////\n\n"
"";

static const char *bgHeader = ""
	"///////////////////////////////////////\n"
	"// \n"
	"// %s\n"
	"// Generated %d/%d/%d %d:%02d %cM\n"
	"// Format: %s (%dbpp)\n"
	"// Palettes: %d\n"
	"// Palette base: %d\n"
	"// Size: %dx%d\n"
	"// \n"
	"///////////////////////////////////////\n\n"
"";

static void PtcGetDateTime(int *month, int *day, int *year, int *hour, int *minute, int *am) {

#ifndef _MSC_VER
	time_t tm = time(NULL);
	struct tm *local = localtime(&tm);

	*month = local->tm_mon + 1, *day = local->tm_mday, *year = local->tm_year + 1900;
	*hour = local->tm_hour, *minute = local->tm_min;
#else
	SYSTEMTIME time;
	GetSystemTime(&time);

	*month = time.wMonth, *day = time.wDay, *year = time.wYear;
	*hour = time.wHour, *minute = time.wMinute;
#endif

	*am = *hour < 12;
	*hour %= 12;
	if (*hour == 0) *hour = 12;
}

static void PtcPrintHelpMessage() {
	puts(g_helpString);
}

static COLOR32 *tgdipReadImage(const TCHAR *lpszFileName, int *pWidth, int *pHeight) {

#ifdef _MSC_VER

#ifdef _UNICODE
	return gdipReadImage(lpszFileName, pWidth, pHeight);
#else //_UNICODE
	wchar_t buffer[260];
	for (int i = 0; i < _tcslen(lpszFileName); i++) {
		buffer[i] = (wchar_t) lpszFileName[i];
	}
	return gdipReadImage(buffer, pWidth, pHeight);
#endif

#else //_MSC_VER
	int channels;
	FILE *fp = _tfopen(lpszFileName, _T("rb"));
	if (fp != NULL) {
		//open succeed
		COLOR32 *px = (COLOR32 *) stbi_load_from_file(fp, pWidth, pHeight, &channels, 4);
		fclose(fp);
	
		return px;
	} else {
		//file access error
		*pWidth = 0;
		*pHeight = 0;
		return NULL;
	}
#endif

}

extern int ilog2(int x);

static int PtcImageHasTranslucent(const COLOR32 *px, int nWidth, int nHeight) {
	for (int i = 0; i < nWidth * nHeight; i++) {
		//alpha values between 5 and 250 do not map to binary alpha values (under a5i3)
		unsigned int a = px[i] >> 24;
		if (a >= 5 && a <= 250) return 1;
	}
	return 0;
}

static int PtcAutoSelectTextureFormat(const COLOR32 *px, int nWidth, int nHeight) {
	//Guess a good format for the data. Default to 4x4.
	int fmt = CT_4x4;

	//if the texture is 1024x1024, do not choose 4x4.
	if (nWidth * nHeight == 1024 * 1024) fmt = CT_256COLOR;

	//is there translucency?
	if (PtcImageHasTranslucent(px, nWidth, nHeight)) {
		//then choose a3i5 or a5i3. Do this by using color count.
		int colorCount = ImgCountColorsEx(px, nWidth, nHeight, IMG_CCM_IGNORE_ALPHA | IMG_CCM_NO_COUNT_TRANSPARENT);
		if (colorCount < 16) {
			//colors < 16, choose a5i3.
			fmt = CT_A5I3;
		} else {
			//otherwise, choose a3i5.
			fmt = CT_A3I5;
		}
	} else {
		//weigh the other format options for optimal size.
		int nColors = ImgCountColors(px, nWidth * nHeight);

		//if <= 4 colors, choose 4-color.
		if (nColors <= 4) {
			fmt = CT_4COLOR;
		} else {
			//weigh 16-color, 256-color, and 4x4. 
			if ((nWidth * nHeight) <= 1024 * 512) {
				//under 1024x512/512x1024: use 4x4
				fmt = CT_4x4;
			} else if (nColors <= 32) {
				//not more than 32 colors: use palette16
				fmt = CT_16COLOR;
			} else {
				//otherwise, use palette256
				fmt = CT_256COLOR;
			}
		}
	}

	return fmt;
}

const char *PtcGetVersionString(void) {
	return VERSION;
}

static void PtcWriteNnsTgaSection(FILE *fp, const char *section, const void *data, unsigned int length) {
	if (length == (unsigned int) -1) length = strlen(data);
	
	//section header
	unsigned char header[0xC];
	memcpy(header, section, 8);
	*(uint32_t *) (header + 0x8) = length + 0xC;
	
	fwrite(header, sizeof(header), 1, fp);
	if (length > 0) fwrite(data, length, 1, fp);
}

void PtcWriteNnsTga(TCHAR *name, TEXELS *texels, PALETTE *palette) {
	FILE *fp = _tfopen(name, _T("wb"));

	int width = TEXW(texels->texImageParam);
	int height = TEXH(texels->texImageParam);
	COLOR32 *pixels = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
	TxRender(pixels, texels, palette);
	ImgFlip(pixels, width, height, 0, 1);
	ImgSwapRedBlue(pixels, width, height);

	unsigned char header[] = { 0x14, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 8,
		'N', 'N', 'S', '_', 'T', 'g', 'a', ' ', 'V', 'e', 'r', ' ', '1', '.', '0', 0, 0, 0, 0, 0 };
	*(uint16_t *) (header + 0xC) = width;
	*(uint16_t *) (header + 0xE) = texels->height;
	*(uint32_t *) (header + 0x22) = sizeof(header) + width * texels->height * 4;
	fwrite(header, sizeof(header), 1, fp);
	fwrite(pixels, width * texels->height * 4, 1, fp);

	const char *fstr = TxNameFromTexFormat(FORMAT(texels->texImageParam));
	PtcWriteNnsTgaSection(fp, "nns_frmt", fstr, strlen(fstr));

	//texels
	uint32_t txelLength = TxGetTexelSize(width, height, texels->texImageParam);
	PtcWriteNnsTgaSection(fp, "nns_txel", texels->texel, txelLength);

	//write 4x4 if applicable
	if (FORMAT(texels->texImageParam) == CT_4x4) {
		PtcWriteNnsTgaSection(fp, "nns_pidx", texels->cmp, txelLength / 2);
	}

	//palette (if applicable)
	if (FORMAT(texels->texImageParam) != CT_DIRECT) {
		PtcWriteNnsTgaSection(fp, "nns_pnam", palette->name, (unsigned int) -1);
		
		int nColors = palette->nColors;
		if (FORMAT(texels->texImageParam) == CT_4COLOR && nColors > 4) nColors = 4;
		PtcWriteNnsTgaSection(fp, "nns_pcol", palette->pal, nColors * sizeof(COLOR));
	}
	
	PtcWriteNnsTgaSection(fp, "nns_gnam", "ptexconv", (unsigned int) -1);
	PtcWriteNnsTgaSection(fp, "nns_gver", PtcGetVersionString(), (unsigned int) -1);
	PtcWriteNnsTgaSection(fp, "nns_imst", NULL, 0);

	//if c0xp
	if (COL0TRANS(texels->texImageParam)) {
		PtcWriteNnsTgaSection(fp, "nns_c0xp", NULL, 0);
	}

	//write end
	PtcWriteNnsTgaSection(fp, "nns_endb", NULL, 0);

	fclose(fp);
	free(pixels);
}

void PtcWriteBitmap(COLOR32 *palette, unsigned int paletteSize, int *indices, int width, int height, const TCHAR *path) {
	FILE *fp = _tfopen(path, _T("wb"));

	unsigned char header[] = { 'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char infoHeader[] = {
		0x28, 0, 0, 0,    // header size
		0, 0, 0, 0,       // width
		0, 0, 0, 0,       // height
		1, 0,             // planes
		0, 0,             // depth
		0, 0, 0, 0,       // compression
		0, 0, 0, 0,       // size
		0x68, 0x10, 0, 0, // ppm X
		0x68, 0x10, 0, 0, // ppm Y
		0, 0, 0, 0,       // palette size
		0, 0, 0, 0        // important colors
	};

	int depth = (paletteSize <= 16) ? 4 : 8;
	unsigned int paletteDataSize = paletteSize * 4;
	unsigned char *paletteData = (unsigned char *) calloc(paletteDataSize, 1);
	for (unsigned int i = 0; i < paletteSize; i++) {
		COLOR32 c = palette[i];
		paletteData[i * 4 + 0] = (c >> 16) & 0xFF;
		paletteData[i * 4 + 1] = (c >>  8) & 0xFF;
		paletteData[i * 4 + 2] = (c >>  0) & 0xFF;
	}

	//create bitmap data, don't bother with RLE because it's shit lol
	int strideLength = (width * depth + 7) / 8;
	if (strideLength & 3) strideLength = (strideLength + 3) & ~3;
	int bmpDataSize = strideLength * height;
	int posShift = (depth == 4) ? 1 : 0;

	unsigned char *bmpData = (unsigned char *) calloc(bmpDataSize, 1);
	for (int y = 0; y < height; y++) {
		unsigned char *row = bmpData + y * strideLength;

		for (int x = 0; x < width; x++) {
			unsigned char old = row[x >> posShift];
			int index = x + (height - 1 - y) * width;
			if (depth == 8) {
				row[x] = indices[index];
			} else {
				int idx = indices[index];
				if (x & 1) {
					row[x >> 1] = old | idx;
				} else {
					row[x >> 1] = idx << 4;
				}
			}
		}
	}

	*(uint32_t *) (header + 0x02) = sizeof(header) + sizeof(infoHeader) + bmpDataSize + paletteDataSize;
	*(uint32_t *) (header + 0x0A) = sizeof(header) + sizeof(infoHeader) + paletteDataSize;
	*(uint32_t *) (infoHeader + 0x04) = width;
	*(uint32_t *) (infoHeader + 0x08) = height;
	*(uint16_t *) (infoHeader + 0x0E) = depth;
	*(uint32_t *) (infoHeader + 0x14) = bmpDataSize;
	*(uint32_t *) (infoHeader + 0x20) = paletteSize;
	*(uint32_t *) (infoHeader + 0x24) = paletteSize;

	fwrite(header, sizeof(header), 1, fp);
	fwrite(infoHeader, sizeof(infoHeader), 1, fp);
	fwrite(paletteData, paletteDataSize, 1, fp);
	fwrite(bmpData, bmpDataSize, 1, fp);

	fclose(fp);
	free(bmpData);
	free(paletteData);
}


#ifdef _MSC_VER

float mylog2(float d) { //UGLY!
	float ans;
	_asm {
		fld1
		fld dword ptr[d]
		fyl2x
		fstp dword ptr[ans]
	}
	return ans;
}
#define log2f mylog2

#endif

//based on suggestions for color counts by SGC, interpolated with a log function
static int PtcAutoSelectTex4x4ColorCount(int bWidth, int bHeight) {
	int area = bWidth * bHeight;
	int nColors;

	if (area <= 128 * 128) {
		//for textures smaller than 256x256, use 8*sqrt(area)
		nColors = (int) (8 * sqrt((float) area));
	} else {
		//larger sizes, increase by 256 every width/height increment
		nColors = (int) (256 * (log2f((float) area) - 10));
	}
	nColors = (nColors + 15) & ~15;
	return nColors;
}

static COLOR32 PtcParseHexColor24(const TCHAR *str) {
	COLOR32 c = 0;
	if (*str == _T('#')) str++;
	
	while (*str) {
		char ch = *str;
		c <<= 4;
		
		if      (ch >= _T('0') && ch <= _T('9')) c |= (ch - _T('0')) + 0x0;
		else if (ch >= _T('A') && ch <= _T('F')) c |= (ch - _T('A')) + 0xA;
		else if (ch >= _T('a') && ch <= _T('f')) c |= (ch - _T('a')) + 0xA;
		else c >>= 4;
		str++;
	}
	return (REVERSE(c)) & 0xFFFFFF;
}

static TCHAR *PtcSuffixFileName(const TCHAR *base, const TCHAR *suffix) {
	int baselen = _tcslen(base), suffixlen = _tcslen(suffix);
	TCHAR *newbuf = (TCHAR *) calloc(baselen + suffixlen + 1, sizeof(TCHAR));
	
	memcpy(newbuf, base, baselen * sizeof(TCHAR));
	memcpy(newbuf + baselen, suffix, (suffixlen + 1) * sizeof(TCHAR));
	return newbuf;
}

static TCHAR *PtcGetFileName(const TCHAR *path) {
	const TCHAR *start = path;
	TCHAR tc;
	while ((tc = *(path++)) != _T('\0')) {
#ifdef _WIN32
		if (tc == _T('/') || tc == _T('\\')) start = path;
#else
		if (tc == _T('/')) start = path;
#endif
	}
	
	//cast away const
	return (TCHAR *) start;
}


// ----- file output routines

static void *PtcCompressByPolicy(const void *ptr, unsigned int size, unsigned int *compressedSize, CxCompressionPolicy policy) {
	//if policy has no compression bits set, return a copy of the buffer.
	if ((policy & CX_COMPRESSION_TYPES_MASK) == 0) {
		void *copy = malloc(size);
		if (copy == NULL) return NULL;
		
		memcpy(copy, ptr, size);
		*compressedSize = size;
		return copy;
	}
	
	//else, pass to compressor
	return CxCompress(ptr, size, compressedSize, policy);
}

static void PtcTrimTextureData(TEXELS *texels) {
	//if height equals TEXIMAGE_PARAM height, do nothing
	if (texels->height == TEXH(texels->texImageParam)) return;
	
	//for all but 4x4, we may trim the texture data by cutting rows off.
	int format = FORMAT(texels->texImageParam);
	if (format != CT_4x4) {
		//trim by rows
		unsigned int bpps[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
		unsigned int stride = TEXW(texels->texImageParam) * bpps[format] / 8;
		
		texels->texel = realloc(texels->texel, stride * texels->height);
	} else {
		//trim by block of 4
		unsigned int trimHeight = (texels->height + 3) & ~3;
		unsigned int strideTxel = TEXW(texels->texImageParam);   // stride of 4 rows of pixels
		unsigned int stridePidx = strideTxel / 2;
		
		texels->texel = realloc(texels->texel, strideTxel * trimHeight);
		texels->cmp = realloc(texels->cmp, stridePidx * trimHeight);
		texels->height = trimHeight;
	}
}

static int PtcEmitBinaryData(FILE *fp, const void *buf, size_t len, CxCompressionPolicy compression) {
	unsigned int complen;
	unsigned char *comp = PtcCompressByPolicy(buf, len, &complen, compression);
	if (comp == NULL) return 0;
	
	//write binary file data
	int status = fwrite(comp, 1, complen, fp) == complen;
	free(comp);
	return status;
}

static int PtcEmitTextData(FILE *fp, FILE *fpHeader, const char *prefix, const char *name, const char *suffix, const void *buf, size_t len, unsigned int unit, CxCompressionPolicy compression) {
	unsigned int complen;
	unsigned char *comp = PtcCompressByPolicy(buf, len, &complen, compression);
	if (comp == NULL) return 0;
	
	if (compression & CX_COMPRESSION_TYPES_MASK) {
		//force unit=4 for alignment of compression header
		unit = 4;
	}
	
	//data formatting parameters
	const char *format = "0x%02X,%c";
	const char *type = "uint8_t";
	switch (unit) {
		default:
		case 1: format = "0x%02X,%c"; type = "uint8_t" ; unit = 1; break;
		case 2: format = "0x%04X,%c"; type = "uint16_t"; unit = 2; break;
		case 4: format = "0x%08X,%c"; type = "uint32_t"; unit = 4; break;
	}
	
	size_t nUnit = (complen + unit - 1) / unit;
	unsigned int nCols = 32 / unit;
	
	
	fprintf(fp, "const %s %s%s%s[] = ", type, prefix, name, suffix);
	fprintf(fp, "{\n    ");
	fprintf(fpHeader, "extern const %s %s%s%s[%d];\n", type, prefix, name, suffix, (int) nUnit);
	
	size_t i = 0, j = 0;
	const unsigned char *ptr = comp;
	while (complen) {
		unsigned int thisUnit = unit;
		if (thisUnit > complen) thisUnit = complen;
		
		uint32_t dataUnit = 0;
		for (j = 0; j < thisUnit; j++) {
			dataUnit |= ptr[j] << (j * 8);
		}
		
		fprintf(fp, format, dataUnit, (i + 1) % nCols == 0 ? '\n' : ' ');
		if (((i + 1) % nCols) == 0 && (i < nUnit - 1)) fprintf(fp, "    ");
		
		complen -= thisUnit;
		ptr += thisUnit;
		i++;
	}
	
	if (nUnit % nCols) fprintf(fp, "\n");
	fprintf(fp, "};\n");
	
	free(comp);
	return 1;
}

static void *PtcConvertBgScreenData(const uint16_t *src, unsigned int tilesX, unsigned int tilesY, int bgType, unsigned int *pOutSize) {
	
	switch (bgType) {
		case BGGEN_BGTYPE_TEXT_16x16:
		case BGGEN_BGTYPE_TEXT_256x1:
		{
			//Text BG: swizzle the data into 256x256 pixel panels.
			uint16_t *cpy = malloc(tilesX * tilesY * sizeof(uint16_t));

			//split data into panels.
			unsigned int nPnlX = (tilesX + 31) / 32;
			unsigned int nPnlY = (tilesY + 31) / 32;

			unsigned int outpos = 0;
			for (unsigned int pnlY = 0; pnlY < nPnlY; pnlY++) {
				for (unsigned int pnlX = 0; pnlX < nPnlX; pnlX++) {
					for (unsigned int y = 0; y < 32; y++) {
						for (unsigned int x = 0; x < 32; x++) {

							if ((pnlY * 32 + y) < tilesY && (pnlX * 32 + x) < tilesX) {
								cpy[outpos++] = src[(pnlX * 32 + x) + (pnlY * 32 + y) * tilesX];
							}

						}
					}
				}
			}
			
			*pOutSize = tilesX * tilesY * sizeof(uint16_t);
			return cpy;
		}
		case BGGEN_BGTYPE_AFFINE_256x1:
		{
			//Affine: trim upper 8 bits of each halfword
			uint8_t *cpy = (void *) malloc(tilesX * tilesY);
			for (unsigned int i = 0; i < tilesX * tilesY; i++) {
				cpy[i] = src[i] & 0xFF;
			}
			*pOutSize = tilesX * tilesY;
			return cpy;
		}
		case BGGEN_BGTYPE_AFFINEEXT_256x16:
		{
			//Affine EXT: return copy of data
			void *cpy = (void *) malloc(tilesX * tilesY * sizeof(uint16_t));
			memcpy(cpy, src, tilesX * tilesY * sizeof(uint16_t));
			*pOutSize = tilesX * tilesY * sizeof(uint16_t);
			return cpy;
		}
		default:
		case BGGEN_BGTYPE_BITMAP:
		{
			//Do nothing here
			*pOutSize = 0;
			return NULL;
		}
	}
}

static void *PtcReadFile(const TCHAR *path, int *pSize) {
	FILE *fp = _tfopen(path, _T("rb"));
	if (fp == NULL) {
		//put file access error
		_ftprintf(stderr, _T("Could not open '") TC_STR _T("' for read access.\n"), path);
		exit(1);
	}
	
	unsigned int size;
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	unsigned char *buf = malloc(size + 1);
	if (buf == NULL) {
		*pSize = 0;
	} else {
		*pSize = (int) size;
		
		unsigned int pos = 0;
		while (pos < size) {
			unsigned int nRead = fread(buf + pos, 1, size - pos, fp);
			if (nRead == 0) {
				//put file access error
				_ftprintf(stderr, _T("File read error on '") TC_STR _T("'.\n"), path);
				exit(1);
			}
			
			pos += nRead;
		}
	}
	
	fclose(fp);
	return buf;
}

static void *PtcPadBuffer(void *buf, unsigned int size, unsigned int newSize) {
	if (newSize < size) newSize = size;
	
	void *newbuf = realloc(buf, newSize);
	if (newbuf != NULL) {
		memset(((unsigned char *) newbuf) + size, 0, newSize - size);
	} else {
		free(buf);
	}
	
	return newbuf;
}



// ----- main command line routine

#define PTC_FAIL_IF(cond,...)  if (cond) { \
	fprintf(stderr, __VA_ARGS__);          \
	exit(1);                               \
}

typedef void (*PtcSwitchProc) (PtcOptions *options, TCHAR **argv);

typedef struct PtcSwitch_ {
	const TCHAR *switchName;  // name of switch on the command line
	int nArguments;           // number of additional arguments expected for the switch
	PtcSwitchProc proc;       // switch handler
} PtcSwitch;

static void PtcSwitch_h(PtcOptions *options, TCHAR **argv) {
	(void) options;
	(void) argv;
	
	//print help message and exit (stop further command processing)
	PtcPrintHelpMessage();
	exit(0);
}

static void PtcSwitch_s(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set silent mode on
	options->silent = 1;
}

static void PtcSwitch_v(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set silent mode off
	options->silent = 0;
}

static void PtcSwitch_o(PtcOptions *options, TCHAR **argv) {
	//set output base file name
	options->outBase = argv[0];
}

static void PtcSwitch_k(PtcOptions *options, TCHAR **argv) {
	//set the alpha key
	options->useAlphaKey = 1;
	options->alphaKey = PtcParseHexColor24(argv[0]);
}

static void PtcSwitch_d(PtcOptions *options, TCHAR **argv) {
	//set the diffusion amount
	options->diffuse = _ttoi(argv[0]);
}

static void PtcSwitch_bb(PtcOptions *options, TCHAR **argv) {
	//set lightness-color balance
	options->balance.balance = _ttoi(argv[0]);
}

static void PtcSwitch_bc(PtcOptions *options, TCHAR **argv) {
	//set color balance
	options->balance.colorBalance = _ttoi(argv[0]);
}

static void PtcSwitch_be(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set enhance colors enabled
	options->balance.enhanceColors = 1;
}

static void PtcSwitch_cm(PtcOptions *options, TCHAR **argv) {
	//set max colors
	options->nMaxColors = _ttoi(argv[0]);
}

static void PtcSwitch_gb(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set generator mode to BG mode
	options->genMode = PTC_GMODE_BG;
}

static void PtcSwitch_gt(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set generator mode to texture mode
	options->genMode = PTC_GMODE_TEXTURE;
}

static void PtcSwitch_ob(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set output type to binary
	options->outMode = PTC_OUT_MODE_BINARY;
}

static void PtcSwitch_oc(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set output type to C source+header
	options->outMode = PTC_OUT_MODE_C;
}

static void PtcSwitch_og(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set output type to GRF
	options->outMode = PTC_OUT_MODE_GRF;
}

static void PtcSwitch_od(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set output type to DIB
	options->outMode = PTC_OUT_MODE_DIB;
}

static void PtcSwitch_ot(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set output type to NNS TGA
	options->outMode = PTC_OUT_MODE_NNSTGA;
}

static void PtcSwitch_cno(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//add "no" compression to compression types
	options->compressionPolicy |= CX_COMPRESSION_NONE;
}

static void PtcSwitch_cbios(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//add all BIOS compression types
	options->compressionPolicy |= CX_COMPRESSION_LZ;
	options->compressionPolicy |= CX_COMPRESSION_HUFFMAN4;
	options->compressionPolicy |= CX_COMPRESSION_HUFFMAN8;
	options->compressionPolicy |= CX_COMPRESSION_RLE;
}

static void PtcSwitch_clz(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//add LZ compression
	options->compressionPolicy |= CX_COMPRESSION_LZ;
}

static void PtcSwitch_ch(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//add Huffman compression types
	options->compressionPolicy |= CX_COMPRESSION_HUFFMAN4;
	options->compressionPolicy |= CX_COMPRESSION_HUFFMAN8;
}

static void PtcSwitch_ch4(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//add 4-bit Huffman
	options->compressionPolicy |= CX_COMPRESSION_HUFFMAN4;
}

static void PtcSwitch_ch8(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//add 8-bit Huffman
	options->compressionPolicy |= CX_COMPRESSION_HUFFMAN8;
}

static void PtcSwitch_crl(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//add RLE compression
	options->compressionPolicy |= CX_COMPRESSION_RLE;
}

static void PtcSwitch_clzx(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//add LZX compression
	options->compressionPolicy |= CX_COMPRESSION_LZX;
}

static void PtcSwitch_c8(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//allow VRAM-unsafe compression
	options->compressionPolicy &= ~CX_COMPRESSION_VRAM_SAFE;
}

static void PtcSwitch_b(PtcOptions *options, TCHAR **argv) {
	//LEGACY: bit depth select (4bpp -> text 16x16, 8bpp -> affine ext 256x16)
	int nBit = _ttoi(argv[0]);
	if      (nBit == 4) options->bgType = BGGEN_BGTYPE_TEXT_16x16;
	else if (nBit == 8) options->bgType = BGGEN_BGTYPE_AFFINEEXT_256x16;
	else                PTC_FAIL_IF(1, "Incorrect bit depth specified.\n");
}

static void PtcSwitch_bt4(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//Set BG type to 4bpp text (16x16)
	options->bgType = BGGEN_BGTYPE_TEXT_16x16;
}

static void PtcSwitch_bt8(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//Set BG type to 8bpp text (256x1)
	options->bgType = BGGEN_BGTYPE_TEXT_256x1;  // BG type
	options->nPalettes = 1;                     // use only one palette
}

static void PtcSwitch_ba(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//Set BG type to 8bpp affine (256x1)
	options->bgType = BGGEN_BGTYPE_AFFINE_256x1;             // BG type
	options->nPalettes = 1;                                  // use only one palette
	if (options->nMaxChars > 256) options->nMaxChars = 256;  // adjust default max char count
}

static void PtcSwitch_bA(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//Set BG type to 8bpp affine extended (256x16)
	options->bgType = BGGEN_BGTYPE_AFFINEEXT_256x16;
}

static void PtcSwitch_bB(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//Set BG type to 8bpp bitmap (256x1)
	options->bgType = BGGEN_BGTYPE_BITMAP;  // BG type
	options->nPalettes = 1;                 // use only one palette
}

static void PtcSwitch_p(PtcOptions *options, TCHAR **argv) {
	//set number of palettes
	options->nPalettes = _ttoi(argv[0]);
}

static void PtcSwitch_pb(PtcOptions *options, TCHAR **argv) {
	//set the BG palette base index
	options->paletteBase = _ttoi(argv[0]);
}

static void PtcSwitch_po(PtcOptions *options, TCHAR **argv) {
	//set the BG palette offset
	options->paletteOffset = _ttoi(argv[0]);
}

static void PtcSwitch_p0o(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set the BG color-0 mode to used
	options->bgColor0Use = 1;
}

static void PtcSwitch_cc(PtcOptions *options, TCHAR **argv) {
	//set BG max character count
	options->nMaxChars = _ttoi(argv[0]);
}

static void PtcSwitch_cn(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//this was a switch that accidentally had an overlaoded meaning.
	options->nMaxChars = -1;         // BG: disable character compression
	options->noLimitPaletteSize = 1; // texture: disable 4x4 compression palette limit
}

static void PtcSwitch_ns(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//disable BG screen data output
	options->outputScreen = 0;
}

static void PtcSwitch_se(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//only write BG screen data (no palette/character data output)
	options->screenExclusive = 1;
	options->outputScreen = 1;
}

static void PtcSwitch_cb(PtcOptions *options, TCHAR **argv) {
	//set the BG character base address
	options->explicitCharBase = 1;
	options->charBase = _ttoi(argv[0]);
}

static void PtcSwitch_wp(PtcOptions *options, TCHAR **argv) {
	//set the BG palette input file
	options->srcPalFile = argv[0];
}

static void PtcSwitch_wc(PtcOptions *options, TCHAR **argv) {
	//set the bG character input file
	options->srcChrFile = argv[0];
}

static void PtcSwitch_pc(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//enable palette compression on the BG (outputs only used palette rows)
	options->compressPalette = 1;
}

static void PtcSwitch_f(PtcOptions *options, TCHAR **argv) {
	const TCHAR *fmtString = argv[0];

	//what format?
	if      (_tcscmp(fmtString, _T("a3i5"      )) == 0) options->texFmt = CT_A3I5;
	else if (_tcscmp(fmtString, _T("a5i3"      )) == 0) options->texFmt = CT_A5I3;
	else if (_tcscmp(fmtString, _T("palette4"  )) == 0) options->texFmt = CT_4COLOR;
	else if (_tcscmp(fmtString, _T("palette16" )) == 0) options->texFmt = CT_16COLOR;
	else if (_tcscmp(fmtString, _T("palette256")) == 0) options->texFmt = CT_256COLOR;
	else if (_tcscmp(fmtString, _T("tex4x4"    )) == 0) options->texFmt = CT_4x4;
	else if (_tcscmp(fmtString, _T("direct"    )) == 0) options->texFmt = CT_DIRECT;
	else {
		//maybe a format number
		int fid = _ttoi(fmtString);
		if (fid >= 1 && fid <= 7) options->texFmt = fid;
		else _tprintf(_T("Unknown texture format ") TC_STR _T(".\n"), fmtString);
	}
}

static void PtcSwitch_ct(PtcOptions *options, TCHAR **argv) {
	//specify the 4x4 palette compression threshold
	options->tex4x4Threshold = _ttoi(argv[0]);
}

static void PtcSwitch_fp(PtcOptions *options, TCHAR **argv) {
	//set the path to the fixed palette file
	options->fixedPalette = argv[0];
}

static void PtcSwitch_fpo(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//enables outputting the palette data even when fixed palette is enabled
	options->outFixedPalette = 1;
}

static void PtcSwitch_tt(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//enable T-axis trimming of texture data (when height is < power-of-2)
	options->trimT = 1;
}

static void PtcSwitch_t0o(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set texture palette color 0 to opaque
	options->c0xp = 0;
}

static void PtcSwitch_t0x(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//set texture palette color 0 to transparent
	options->c0xp = 1;
}

static void PtcSwitch_da(PtcOptions *options, TCHAR **argv) {
	(void) argv;
	
	//enable dithering of the alpha channel
	options->ditherAlpha = 1;
}


static const PtcSwitch sSwitches[] = {
	// ----- Global switches
	{ _T("h"),     0, PtcSwitch_h  },
	{ _T("s") ,    0, PtcSwitch_s  },
	{ _T("v") ,    0, PtcSwitch_v  },
	{ _T("o") ,    1, PtcSwitch_o  },
	{ _T("k") ,    1, PtcSwitch_k  },
	{ _T("d") ,    1, PtcSwitch_d  },
	{ _T("bb"),    1, PtcSwitch_bb },
	{ _T("bc"),    1, PtcSwitch_bc },
	{ _T("be"),    0, PtcSwitch_be },
	{ _T("cm"),    1, PtcSwitch_cm },
	
	// ----- Generate mode switches
	{ _T("gb"),    0, PtcSwitch_gb },
	{ _T("gt"),    0, PtcSwitch_gt },
	
	// ----- Output type switches
	{ _T("ob"),    0, PtcSwitch_ob },
	{ _T("oc"),    0, PtcSwitch_oc },
	{ _T("og"),    0, PtcSwitch_og },
	{ _T("od"),    0, PtcSwitch_od },
	{ _T("ot"),    0, PtcSwitch_ot },
	
	// ----- Compression switches
	{ _T("cbios"), 0, PtcSwitch_cbios },
	{ _T("cno"),   0, PtcSwitch_cno   },
	{ _T("clz"),   0, PtcSwitch_clz   },
	{ _T("ch"),    0, PtcSwitch_ch    },
	{ _T("ch4"),   0, PtcSwitch_ch4   },
	{ _T("ch8"),   0, PtcSwitch_ch8   },
	{ _T("crl"),   0, PtcSwitch_crl   },
	{ _T("clzx"),  0, PtcSwitch_clzx  },
	{ _T("c8"),    0, PtcSwitch_c8    },
	
	// ----- BG switches
	{ _T("b") ,    1, PtcSwitch_b   },
	{ _T("bt4"),   0, PtcSwitch_bt4 },
	{ _T("bt8"),   0, PtcSwitch_bt8 },
	{ _T("ba"),    0, PtcSwitch_ba  },
	{ _T("bA"),    0, PtcSwitch_bA  },
	{ _T("bB"),    0, PtcSwitch_bB  },
	{ _T("p"),     1, PtcSwitch_p   },
	{ _T("pb"),    1, PtcSwitch_pb  },
	{ _T("po"),    1, PtcSwitch_po  },
	{ _T("p0o"),   0, PtcSwitch_p0o },
	{ _T("cc"),    1, PtcSwitch_cc  },
	{ _T("cn"),    0, PtcSwitch_cn  },
	{ _T("ns"),    0, PtcSwitch_ns  },
	{ _T("se"),    0, PtcSwitch_se  },
	{ _T("cb"),    1, PtcSwitch_cb  },
	{ _T("wp"),    1, PtcSwitch_wp  },
	{ _T("wc"),    1, PtcSwitch_wc  },
	{ _T("pc"),    0, PtcSwitch_pc  },
	
	// ----- Texture switches
	{ _T("f"),     1, PtcSwitch_f   },
	{ _T("ct"),    1, PtcSwitch_ct  },
	{ _T("fp"),    1, PtcSwitch_fp  },
	{ _T("fpo"),   0, PtcSwitch_fpo },
	{ _T("tt"),    0, PtcSwitch_tt  },
	{ _T("t0o"),   0, PtcSwitch_t0o },
	{ _T("t0x"),   0, PtcSwitch_t0x },
	{ _T("da"),    0, PtcSwitch_da  }
};

static void PtcOptParse(PtcOptions *opt, int argc, TCHAR **argv) {
	memset(opt, 0, sizeof(*opt));
	
	//check argument count
	if (argc < 1) {
		//print help
		PtcPrintHelpMessage();
		exit(0);
	}
	
	//data output settings
	opt->silent = 1;                                    // default output level (silent)
	opt->genMode = PTC_GMODE_BG;                        // default output type (BG graphics)
	opt->outMode = PTC_OUT_MODE_BINARY;                 // default output format (raw binary data)
	opt->compressionPolicy = CX_COMPRESSION_VRAM_SAFE;  // default compression (none explicit, VRAM safety required)
	
	//color conversion options
	opt->balance.balance       = BALANCE_DEFAULT; // default lightness-color balance setting
	opt->balance.colorBalance  = BALANCE_DEFAULT; // default color balance setting (neutral)
	opt->balance.enhanceColors = 0;               // enhance largely used colors
	opt->diffuse = 0;                             // default error diffusion amount (0%)
	opt->ditherAlpha = 0;                         // dither the alpha channel?
	
	//begin processing arguments. First, determine global settings and conversion mode.
	opt->nMaxColors = -1;                // default. 256 for BG, automatic for texture
	opt->useAlphaKey = 0;                // use alpha key?

	//BG settings
	opt->bgType = BGGEN_BGTYPE_AFFINEEXT_256x16;
	opt->charBase = 0;                   // character base address for BG generator
	opt->nMaxChars = 1024;               // maximum character count for BG generator
	opt->nPalettes = 1;                  // number of palettes for BG generator
	opt->paletteBase = 0;                // palette base index for BG generator
	opt->compressPalette = 0;            // only output target palettes/colors?
	opt->paletteOffset = 0;              // offset from start of hw palette
	opt->outputScreen = 1;               // output BG screen data?
	opt->screenExclusive = 0;            // output only screen file?
	opt->bgColor0Use = 0;                // use color 0 in BG generator

	//Texture settings
	opt->texFmt = -1;                    // texture format (default: judge automatically)
	opt->noLimitPaletteSize = 0;         // disable 4x4 compression palette size limit (up to 32k colors)
	opt->tex4x4Threshold = 0;            // 4x4 compression threshold of mergence
	opt->trimT = 0;                      // trim texture in the T axis if not a power of 2 in height
	opt->outFixedPalette = 0;            // output fixed palette among other output files
	opt->c0xp = -1;                      // is color 0 transparent reserved?

	for (int i = 0; i < argc; i++) {
		const TCHAR *arg = argv[i];
		
		//process arguments
		if (arg[0] == _T('-')) {
			//process switch
			
			const PtcSwitch *sw = NULL;
			for (unsigned int j = 0; j < sizeof(sSwitches) / sizeof(sSwitches[0]); j++) {
				if (_tcscmp(arg + 1, sSwitches[j].switchName) == 0) {
					sw = &sSwitches[j];
					break;
				}
			}
			
			if (sw != NULL) {
				//call switch
				int nArgsRequired = sw->nArguments;
				int nArgsLeft = argc - (i + 1);
				
				PTC_FAIL_IF(nArgsLeft < nArgsRequired, "Too few arguments to switch.\n");
				sw->proc(opt, &argv[i + 1]);
				
				//increment i
				i += nArgsRequired;
			} else {
				//unknown switch
				_tprintf(_T("Ignoring unknown switch ") TC_STR _T(".\n"), arg);
			}
			
		} else {
			//add input file
			PTC_FAIL_IF(opt->nSrcFile >= PTC_INFILE_MAX, "Too many input files.\n");
			
			opt->srcFiles[opt->nSrcFile++] = arg;
		}
	}
}

int _tmain(int argc, TCHAR **argv) {
	argc--;
	argv++;
	
	//parse command line
	PtcOptions opt;
	PtcOptParse(&opt, argc, argv);

	//check for errors
	PTC_FAIL_IF(opt.nSrcFile == 0, "No source image specified.\n");
	PTC_FAIL_IF(opt.outBase == NULL, "No output name specified.\n");
	PTC_FAIL_IF(opt.diffuse < 0 || opt.diffuse > 100, "Diffuse amount out of range (%d)\n", opt.diffuse);
	
	if (opt.genMode == PTC_GMODE_BG) {
		//BG mode paramter checks
		int maxPltt = 16, maxCharsFmt = 1024, depth = 8;
		switch (opt.bgType) {
			case BGGEN_BGTYPE_AFFINEEXT_256x16:
				depth = 8;
				maxPltt = 16;
				maxCharsFmt = 1024;
				break;
			case BGGEN_BGTYPE_TEXT_16x16:
				depth = 4;
				maxPltt = 16;
				maxCharsFmt = 1024;
				break;
			case BGGEN_BGTYPE_TEXT_256x1:
				depth = 8;
				maxPltt = 1;
				break;
			case BGGEN_BGTYPE_AFFINE_256x1:
				depth = 8;
				maxPltt = 1;
				maxCharsFmt = 256;
				break;
			case BGGEN_BGTYPE_BITMAP:
				depth = 8;
				maxPltt = 1;
				break;
		}
		
		int maxPlttAddr = opt.paletteBase + opt.nPalettes;
		int maxColAddr = opt.paletteOffset + opt.nMaxColors;
		
		PTC_FAIL_IF(opt.outMode == PTC_OUT_MODE_NNSTGA,                "NNS TGA output is not applicable for BG.\n");
		PTC_FAIL_IF(maxColAddr > (1 << depth),                         "Invalid color count per palette specified for BG of %d-bit depth (%d).\n", depth, opt.nMaxColors);
		PTC_FAIL_IF(maxPlttAddr > maxPltt,                             "Invalid palette count or base specified for BG (%d).\n", opt.nPalettes);
		PTC_FAIL_IF(opt.nMaxChars > maxCharsFmt || opt.nMaxChars < -1, "Invalid maximum character count specified for BG (%d).\n", opt.nMaxChars);
		PTC_FAIL_IF(opt.screenExclusive && (opt.srcChrFile == NULL || opt.srcPalFile == NULL), "Palette and character file required for this command.\n");
	} else if (opt.genMode == PTC_GMODE_TEXTURE) {
		//texture mode paramter checks
		PTC_FAIL_IF(opt.outMode == PTC_OUT_MODE_DIB,              "DIB output is not applicable for texture.\n");
	}

	//MBS copy of base
	int baseLength = _tcslen(opt.outBase);

	PtcImage images[PTC_INFILE_MAX];
	for (int i = 0; i < opt.nSrcFile; i++) {
		images[i].px = tgdipReadImage(opt.srcFiles[i], &images[i].width, &images[i].height);
		PTC_FAIL_IF(images[i].px == NULL, "Could not read image file.\n");
	}
	
	//apply alpha key
	if (opt.useAlphaKey) {
		for (int j = 0; j < opt.nSrcFile; j++) {
			for (int i = 0; i < images[j].width * images[j].height; i++) {
				COLOR32 c = images[j].px[i];
				if ((c & 0xFFFFFF) == (opt.alphaKey & 0xFFFFFF)) images[j].px[i] = 0;
			}
		}
	}
	
	//preprocess transparent pixels by zeroing RGB color channels of transparent pixels
	for (int j = 0; j < opt.nSrcFile; j++) {
		for (int i = 0; i < images[j].width * images[j].height; i++) {
			COLOR32 c = images[j].px[i];
			if (((c >> 24) & 0xFF) == 0) images[j].px[i] = 0;
		}
	}
	
	if (opt.genMode == PTC_GMODE_BG) {
		//Generate BG
		PTC_FAIL_IF(opt.nSrcFile > 1, "Too many input images for BG generator.\n");
		
		//fix up automatic flags
		int depth = 4;
		if (opt.bgType != BGGEN_BGTYPE_TEXT_16x16) depth = 8;

		int bitmap = 0;
		if (opt.bgType == BGGEN_BGTYPE_BITMAP) {
			bitmap = 1;
			opt.outputScreen = 0;
		}
		
		if (opt.nMaxColors == -1) opt.nMaxColors = 1 << depth;

		if (opt.outMode == PTC_OUT_MODE_DIB) {
			opt.outputScreen = 0;
			opt.nMaxChars = -1;
		}

		//determine palette size for output
		int paletteOutBase = 0, paletteOutSize = depth == 4 ? 256 : ((opt.paletteBase + opt.nPalettes) * 256);
		if (opt.compressPalette) {
			if (opt.nPalettes == 1) {
				//output only the subsection of the palette written to
				paletteOutBase = opt.paletteOffset + (opt.paletteBase << depth);
				paletteOutSize = opt.nMaxColors;
			} else {
				//include whole palettes, but only those written to
				paletteOutBase = opt.paletteBase << depth;
				paletteOutSize = opt.nPalettes << depth;
			}
		}

		//initialize palette. Read in base palette if specified.
		COLOR *pal = (COLOR *) calloc(256 * 16, sizeof(COLOR));
		if (opt.srcPalFile != NULL) {
			//read from palette file, check it exists
			//assume a palette base of 0
			int nRead;
			COLOR *pal2 = (COLOR *) PtcReadFile(opt.srcPalFile, &nRead);
			
			if (nRead > 256 * 16) nRead = 256 * 16; // truncate input
			
			if (nRead > (paletteOutBase + paletteOutSize)) {
				paletteOutSize = nRead - paletteOutBase;
			}
			
			memcpy(pal, pal2, nRead);
			free(pal2);

			//we're now responsible for the whole file; output as such
			paletteOutSize += paletteOutBase;
			paletteOutBase = 0;
		}

		//read from character input file if specified
		void *existingChars = NULL;
		int existingCharsSize = 0;
		if (opt.srcChrFile != NULL) {
			//read from file, check it exists
			existingChars = PtcReadFile(opt.srcChrFile, &existingCharsSize);
			
			//set character offset based on file size and current bit depth
			int nExistingChars = (existingCharsSize + 8 * depth - 1) / (8 * depth); // round up
			int padSize = nExistingChars * (8 * depth);
			existingChars = PtcPadBuffer(existingChars, existingCharsSize, padSize);
			
			existingCharsSize = padSize;
			if (!opt.explicitCharBase) opt.charBase = nExistingChars;
		}

		if (!opt.silent) {
			printf("Generating BG\nBits: %d\nPalettes: %d\nPalette size: %d\nMax chars: %d\nDiffuse: %d%%\nPalette base: %d\n\n",
				depth, opt.nPalettes, opt.nMaxColors, opt.nMaxChars, opt.diffuse, opt.paletteBase);
		}

		//perform appropriate generation of data.
		unsigned char *chars = NULL;
		unsigned short *screen = NULL;
		int palSize = 0, charSize = 0, screenSize = 0;
		int p1, p1max, p2, p2max;
		if (!opt.screenExclusive) {
			//from scratch
			BgGenerateParameters params = { 0 };
			memcpy(&params.balance, &opt.balance, sizeof(opt.balance));

			params.compressPalette = opt.compressPalette;
			params.paletteRegion.base = opt.paletteBase;
			params.paletteRegion.count = opt.nPalettes;
			params.paletteRegion.length = opt.nMaxColors;
			params.paletteRegion.offset = opt.paletteOffset;

			params.bgType = opt.bgType;
			params.color0Mode = (opt.bgColor0Use ? BGGEN_COLOR0_USE : BGGEN_COLOR0_FIXED);
			params.dither.dither = (opt.diffuse != 0);
			params.dither.diffuse = ((float) opt.diffuse) / 100.0f;
			params.characterSetting.base = opt.charBase;
			params.characterSetting.compress = (opt.nMaxChars != -1);
			params.characterSetting.nMax = opt.nMaxChars;
			params.characterSetting.alignment = 1;
			BgGenerate(pal, &chars, &screen, &palSize, &charSize, &screenSize, images[0].px, images[0].width, images[0].height,
				&params, &p1, &p1max, &p2, &p2max);
		} else {
			//from existing palette+char
			BgAssemble(images[0].px, images[0].width, images[0].height, depth, pal, opt.nPalettes, existingChars,
				existingCharsSize / (8 * depth), &screen, &screenSize,
				opt.balance.balance, opt.balance.colorBalance, opt.balance.enhanceColors);
		}
		
		//convert BG format
		if (opt.bgType != BGGEN_BGTYPE_BITMAP) {
			unsigned int convSize = 0;
			unsigned short *conv = PtcConvertBgScreenData(screen, images[0].width / 8, images[0].height / 8, opt.bgType, &convSize);
			
			free(screen);
			screen = conv;
			screenSize = convSize;
		}
		
		//for alpha keyed images, set color 0 to alpha key color
		if (opt.useAlphaKey && opt.paletteOffset == 0) {
			for (int i = opt.paletteBase; i < opt.paletteBase + opt.nPalettes; i++) {
				pal[i << depth] = ColorConvertToDS(opt.alphaKey);
			}
		}

		//prep data out for character
		if (!opt.screenExclusive && existingChars != NULL) {
			//consider the existing character and the generated ones. 
			int requiredCharSize = charSize + opt.charBase * (8 * depth);
			if (requiredCharSize < existingCharsSize) requiredCharSize = existingCharsSize;

			//make large allocation to cover everything
			existingChars = realloc(existingChars, requiredCharSize);
			memset(((unsigned char *) existingChars) + existingCharsSize, 0, requiredCharSize - existingCharsSize);
			memcpy(((unsigned char *) existingChars) + opt.charBase * (8 * depth), chars, charSize);
			free(chars);
			chars = (unsigned char *) existingChars; //replace with new char data
			charSize = requiredCharSize;
		}

		if (opt.outMode == PTC_OUT_MODE_GRF) {
			//output GRIT GRF file
			TCHAR *nameBuffer = PtcSuffixFileName(opt.outBase, _T(".grf"));
			
			//GRF requires one compression type specified
			if (!(opt.compressionPolicy & CX_COMPRESSION_TYPES_MASK)) opt.compressionPolicy |= CX_COMPRESSION_NONE;
			
			//get BG screen type
			GrfBgScreenType scrType = GRF_SCREEN_TYPE_NONE;
			switch (opt.bgType) {
				case BGGEN_BGTYPE_TEXT_16x16:       scrType = GRF_SCREEN_TYPE_TEXT_16x16; break;
				case BGGEN_BGTYPE_TEXT_256x1:       scrType = GRF_SCREEN_TYPE_TEXT_256x1; break;
				case BGGEN_BGTYPE_AFFINE_256x1:     scrType = GRF_SCREEN_TYPE_AFFINE;     break;
				case BGGEN_BGTYPE_AFFINEEXT_256x16: scrType = GRF_SCREEN_TYPE_AFFINE_EXT; break;
				case BGGEN_BGTYPE_BITMAP:           scrType = GRF_SCREEN_TYPE_NONE;       break;
			}
			
			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			GrfWriteHeader(fp);
			GrfBgWriteHdr(fp, depth, scrType, images[0].width, images[0].height, paletteOutSize);
			GrfWritePltt(fp, pal, paletteOutSize, opt.compressionPolicy);
			GrfWriteGfx(fp, chars, charSize, opt.compressionPolicy);
			GrfWriteScr(fp, screen, screenSize, opt.compressionPolicy);
			GrfFinalize(fp);
			fclose(fp);
		} else if (opt.outMode == PTC_OUT_MODE_BINARY) {
			//output NBFP, NBFC, NBFS.

			//suffix the filename with .nbfp, .nbfc, .nbfs. So reserve 6 characters+base length.
			FILE *fp;
			TCHAR *nameBuffer = PtcSuffixFileName(opt.outBase, NBFP_EXTENSION);

			if (!opt.screenExclusive) {
				fp = _tfopen(opt.srcPalFile == NULL ? nameBuffer : opt.srcPalFile, _T("wb"));
				PtcEmitBinaryData(fp, pal + paletteOutBase, paletteOutSize * sizeof(COLOR), opt.compressionPolicy);
				fclose(fp);
				if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), opt.srcPalFile == NULL ? nameBuffer : opt.srcPalFile);

				memcpy(nameBuffer + baseLength, bitmap ? NBFB_EXTENSION : NBFC_EXTENSION, (NBFX_EXTLEN + 1) * sizeof(TCHAR));
				fp = _tfopen(opt.srcChrFile == NULL ? nameBuffer : opt.srcChrFile, _T("wb"));
				PtcEmitBinaryData(fp, chars, charSize, opt.compressionPolicy);
				fclose(fp);
				if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), opt.srcChrFile == NULL ? nameBuffer : opt.srcChrFile);
			}

			if (opt.outputScreen) {
				memcpy(nameBuffer + baseLength, NBFS_EXTENSION, (NBFX_EXTLEN + 1) * sizeof(TCHAR));
				fp = _tfopen(nameBuffer, _T("wb"));
				PtcEmitBinaryData(fp, screen, screenSize, opt.compressionPolicy);
				fclose(fp);
				if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), nameBuffer);
			}

			free(nameBuffer);

		} else if (opt.outMode == PTC_OUT_MODE_DIB) { // output DIB file
			//we physically cannot cram this many colors into a DIB palette
			if (depth == 8 && opt.nPalettes > 1) {
				fprintf(stderr, "Cannot output DIB for EXT BG.");
				return 1;
			}

			//suffix filename with .bmp, reserve 5 characters+base length
			TCHAR *nameBuffer = PtcSuffixFileName(opt.outBase, _T(".bmp"));

			int charsX = images[0].width / 8, charsY = images[0].height / 8;
			int outWidth = charsX * 8, outHeight = charsY * 8;
			int bytesPerChar = depth == 8 ? 64 : 32;
			int *indexBuffer = (int *) calloc(outWidth * outHeight, sizeof(int));
			for (int cy = 0; cy < charsY; cy++) {
				for (int cx = 0; cx < charsX; cx++) {
					unsigned char *thisChar = chars + (cx + cy * (charsX)) * bytesPerChar;
					unsigned short thisScr = screen[cx + cy * charsX]; //this works because no char compression
					int palIndex = (thisScr >> 12) & 0xF;

					for (int y = 0; y < 8; y++) {
						for (int x = 0; x < 8; x++) {
							int indexValue;
							if (depth == 8) {
								indexValue = thisChar[x + y * 8];
							} else {
								indexValue = thisChar[(x / 2) + y * 4];
								if ((x & 1) == 0) indexValue &= 0xF;
								else indexValue >>= 4;
								indexValue |= (palIndex << 4);
							}
							indexBuffer[cx * 8 + x + (cy * 8 + y) * outWidth] = indexValue;
						}
					}
				}
			}

			COLOR32 *palette32 = (COLOR32 *) calloc(palSize / 2, sizeof(COLOR32));
			for (int i = 0; i < palSize / 2; i++) {
				palette32[i] = ColorConvertFromDS(pal[i]);
			}
			PtcWriteBitmap(palette32, palSize / 2, indexBuffer, images[0].width, images[0].height, nameBuffer);

			if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), nameBuffer);

			free(palette32);
			free(indexBuffer);
			free(nameBuffer);
		} else { //output header and source file
			//suffix the filename with .c, So reserve 3 characters+base length.
			TCHAR *nameBuffer = PtcSuffixFileName(opt.outBase, _T(".c"));

			int month, day, year, hour, minute, am;
			PtcGetDateTime(&month, &day, &year, &hour, &minute, &am);

			//find BG name
			const TCHAR *name = PtcGetFileName(opt.srcFiles[0]);

			//copy to MBS buffer, stripping extension too
			char *bgName = (char *) calloc(_tcslen(name) + 1, sizeof(char));
			for (unsigned i = 0; i < _tcslen(name); i++) {
				TCHAR tch = name[i];
				if (tch == _T('.')) {
					bgName[i] = '\0';
					break;
				}
				bgName[i] = (char) tch;
			}
			bgName[_tcslen(name)] = '\0';

			//if name doesn't start with a letter, prepend "bg_" to its name.
			char *prefix = ((bgName[0] < 'a' || bgName[0] > 'z') && (bgName[0] < 'A' || bgName[0] > 'Z')) ? "bg_" : "";

			//write
			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			nameBuffer[_tcslen(nameBuffer) - 1] = _T('h');
			FILE *fpHeader = _tfopen(nameBuffer, _T("wb"));
			
			const char *bgFormatName = "Text";
			switch (opt.bgType) {
				case BGGEN_BGTYPE_TEXT_16x16:
				case BGGEN_BGTYPE_TEXT_256x1:
					bgFormatName = "Text";
					break;
				case BGGEN_BGTYPE_AFFINE_256x1:
					bgFormatName = "Affine";
					break;
				case BGGEN_BGTYPE_AFFINEEXT_256x16:
					bgFormatName = "Affine EXT";
					break;
				case BGGEN_BGTYPE_BITMAP:
					bgFormatName = "Bitmap";
					break;
			}
			
			fprintf(fp, bgHeader, bgName, month, day, year, hour, minute, am ? 'A' : 'P',
				bgFormatName, depth, opt.nPalettes,
				opt.paletteBase, images[0].width, images[0].height);
			fprintf(fpHeader, bgHeader, bgName, month, day, year, hour, minute, am ? 'A' : 'P',
				bgFormatName, depth, opt.nPalettes,
				opt.paletteBase, images[0].width, images[0].height);
			fprintf(fp, "#include <stdint.h>\n\n");
			fprintf(fpHeader, "#pragma once\n\n#include <stdint.h>\n\n");

			if (!opt.screenExclusive) {
				//write character
				{
					fprintf(fpHeader, "//\n// Generated character data\n//\n");
					PtcEmitTextData(fp, fpHeader, prefix, bgName, "_char", chars, charSize, 2, opt.compressionPolicy);
					
					fprintf(fp, "\n");
					fprintf(fpHeader, "\n");
				}

				//write palette
				{
					fprintf(fpHeader, "//\n// Generated palette data\n//\n");
					PtcEmitTextData(fp, fpHeader, prefix, bgName, "_pal", pal, paletteOutSize * 2, 2, opt.compressionPolicy);
					
					fprintf(fp, "\n");
					fprintf(fpHeader, "\n");
				}
			}

			if (opt.outputScreen) {
				//write screen
				fprintf(fpHeader, "//\n// Generated screen data\n//\n");
				PtcEmitTextData(fp, fpHeader, prefix, bgName, "_screen", screen, screenSize, 2, opt.compressionPolicy);
				
				fprintf(fp, "\n");
				fprintf(fpHeader, "\n");
			}
			fclose(fp);
			fclose(fpHeader);

			free(nameBuffer);
			free(bgName);
		}

		if (pal != NULL) free(pal);
		if (chars != NULL) free(chars);
		if (screen != NULL) free(screen);
	} else {
		//Generate Texture
		
		//check image dimensions
		for (int i = 1; i < opt.nSrcFile; i++) {
			if (images[i].width != images[0].width || images[i].height != images[0].height) {
				PTC_FAIL_IF(1, "Input images must all have the same dimension.\n");
			}
		}
		int width = images[0].width, height = images[0].height;
		
		//fix up automatic flags
		if (opt.texFmt == -1) {
			opt.texFmt = PtcAutoSelectTextureFormat(images[0].px, width, height);
			
			if (opt.nSrcFile > 1 && !(opt.texFmt == CT_4COLOR || opt.texFmt == CT_16COLOR || opt.texFmt == CT_256COLOR)) {
				//set texture format to palette256 by default
				opt.texFmt = CT_256COLOR;
			}
		}
		
		//checks for multiple image generation mode
		if (opt.nSrcFile > 1) {
			switch (opt.texFmt) {
				case CT_4COLOR:
				case CT_16COLOR:
				case CT_256COLOR:
					//OK
					break;
				default:
					PTC_FAIL_IF(1, "The texture format is not supported for multiple palette generation.\n");
					break;
			}
		}
		
		if (opt.nMaxColors == -1) {
			switch (opt.texFmt) {
				case CT_A3I5     : opt.nMaxColors =  32; break;
				case CT_A5I3     : opt.nMaxColors =   8; break;
				case CT_4COLOR   : opt.nMaxColors =   4; break;
				case CT_16COLOR  : opt.nMaxColors =  16; break;
				case CT_256COLOR : opt.nMaxColors = 256; break;
				case CT_DIRECT   : opt.nMaxColors =   0; break;
				case CT_4x4:
					opt.nMaxColors = PtcAutoSelectTex4x4ColorCount(width, height);
					break;
			}
		}
		
		//concatenate all pixel buffers
		COLOR32 *px = (COLOR32 *) calloc(width * height * opt.nSrcFile, sizeof(COLOR32));
		for (int i = 0; i < opt.nSrcFile; i++) {
			memcpy(px + i * (width * height), images[i].px, width * height * sizeof(COLOR32));
		}

		//infer color 0 mode
		if (opt.c0xp == -1) {
			opt.c0xp = 0;
			
			if (opt.texFmt == CT_4COLOR || opt.texFmt == CT_16COLOR || opt.texFmt == CT_256COLOR) {
				for (int i = 0; i < width * height * opt.nSrcFile; i++) {
					unsigned int a = px[i] >> 24;
					if (a < 0x80) {
						opt.c0xp = 1; // transparent pixel presence
						break;
					}
				}
			}
		}
		
		if (opt.texFmt == CT_4x4 && opt.noLimitPaletteSize) {
			//set high palette size (effectively no limit)
			opt.nMaxColors = 32768;
		}

		static const int colorMaxes[] = { 0, 32, 4, 16, 256, 32768, 8,  0 };
		static const int bppArray[]   = { 0,  8, 2,  4,   8,     2, 8, 16 };
		if (opt.nMaxColors > colorMaxes[opt.texFmt]) {
			opt.nMaxColors = colorMaxes[opt.texFmt];
			if (!opt.silent) printf("Color count truncated to %d.\n", opt.nMaxColors);
		}
		if (!opt.silent) printf("Generating texture\nMax colors: %d\nFormat: %d\nDiffuse: %d%%\nSize: %dx%d\n\n",
			opt.nMaxColors, opt.texFmt, opt.diffuse, width, height);

		TEXTURE texture = { 0 };
		
		if (opt.nSrcFile == 1) {
			//generation mode for one single image: call to texture conversion routines
			TxConversionParameters params = { 0 };
			params.dest = &texture;
			params.colorEntries = opt.nMaxColors;
			params.diffuseAmount = (float) opt.diffuse / 100.0f;
			params.dither = !!opt.diffuse;
			params.ditherAlpha = params.dither && opt.ditherAlpha && (opt.texFmt == CT_A3I5 || opt.texFmt == CT_A5I3);
			params.fixedPalette = NULL;
			params.fmt = opt.texFmt;
			params.width = width;
			params.height = height;
			params.px = images[0].px;
			params.c0xp = opt.c0xp;
			params.threshold = opt.tex4x4Threshold;
			memcpy(&params.balance, &opt.balance, sizeof(opt.balance));
			params.pnam = (char *) calloc(1, 1);
			
			//read the fixed palette file, if one was specified
			if (opt.fixedPalette != NULL) {
				int size;
				params.fixedPalette = (COLOR *) PtcReadFile(opt.fixedPalette, &size);

				if (params.colorEntries > (unsigned int) (size >> 1)) {
					params.colorEntries = size >> 1;
					if (!opt.silent) printf("Color count truncated to %d.\n", params.colorEntries);
				}
			}
			
			TxConvert(&params);
			
			if (params.fixedPalette != NULL) free(params.fixedPalette);
		} else {
			//generation mode for multiple input images
			PTC_FAIL_IF(opt.fixedPalette != NULL, "Multiple image generation texture mode does not support the fixed palette.\n");
			
			RxFlag flag = RX_FLAG_NO_WRITEBACK | RX_FLAG_NO_ALPHA_DITHER;
			if (opt.c0xp) flag |= RX_FLAG_ALPHA_MODE_RESERVE;
			else          flag |= RX_FLAG_ALPHA_MODE_NONE;
			
			int padHeight = 1;
			while (padHeight < height) padHeight <<= 1;
			unsigned int nPx = width * padHeight;
			
			float diffuse = (float) opt.diffuse / 100.0f;
			
			RxReduction *reduction = RxNew(&opt.balance);
			RxSetPaletteLayers(reduction, opt.nSrcFile);
			RxApplyFlags(reduction, flag);
			
			//build histogram and create the palette
			RxHistAdd(reduction, px, width, height);
			RxHistFinalize(reduction);
			RxComputePalette(reduction, opt.nMaxColors - (opt.c0xp ? 1 : 0));
			
			//get the palette data
			COLOR32 *pltt = (COLOR32 *) calloc(opt.nSrcFile * opt.nMaxColors, sizeof(COLOR32));
			RxSortPalette(reduction, RX_FLAG_SORT_ONLY_USED | RX_FLAG_SORT_END_DIFFER);
			for (int i = 0; i < opt.nSrcFile; i++) {
				RxGetPalette(reduction, pltt + i * opt.nMaxColors, i);
			}
			
			int *indices = (int *) calloc(width * height, sizeof(int));
			RxPaletteLoad(reduction, pltt, opt.nMaxColors);
			RxReduceImage(reduction, px, indices, width, height, flag, diffuse);
			RxFree(reduction);
			
			//create the texel data
			unsigned int bpp = bppArray[opt.texFmt];
			unsigned int texelSize = (width * padHeight * bpp) / 8;
			unsigned char *texel = (unsigned char *) calloc(texelSize, 1);

			unsigned int pxPerByte = 8 / bpp;
			for (unsigned int i = 0; i < nPx; i++) {
				unsigned char icol = (unsigned char) indices[i];

				unsigned int iPx = i / pxPerByte;
				unsigned int shift = (i % pxPerByte) * bpp;
				texel[iPx] |= icol << shift;
			}
			free(indices);

			//compute the TEXIMAGE_PARAM
			uint32_t texImageParam = 0;
			if (opt.c0xp) texImageParam |= (1 << 29);
			texImageParam |= (1 << 17) | (1 << 16);
			texImageParam |= (ilog2(width >> 3) << 20) | (ilog2(padHeight >> 3) << 23);
			texImageParam |= opt.texFmt << 26;
			
			texture.texels.name = strdup("");
			texture.texels.height = height;
			texture.texels.texel = texel;
			texture.texels.cmp = NULL;
			texture.texels.texImageParam = texImageParam;
			
			texture.palette.name = strdup("");
			texture.palette.nColors = opt.nSrcFile * opt.nMaxColors;
			texture.palette.pal = (COLOR *) calloc(texture.palette.nColors, sizeof(COLOR));
			for (int i = 0; i < opt.nSrcFile * opt.nMaxColors; i++) {
				texture.palette.pal[i] = ColorConvertToDS(pltt[i]);
			}
			free(pltt);
		}
		
		//trim texture data by height
		if (opt.outMode != PTC_OUT_MODE_NNSTGA && opt.trimT) {
			//NNS TGA texture data shall not be trimmed
			texture.texels.height = height;
			PtcTrimTextureData(&texture.texels);
		} else {
			//no trim height
			texture.texels.height = TEXH(texture.texels.texImageParam);
		}
		int texelSize = TEXW(texture.texels.texImageParam) * texture.texels.height * bppArray[opt.texFmt] / 8;
		int indexSize = (opt.texFmt == CT_4x4) ? (texelSize >> 1) : 0;

		if (opt.outMode == PTC_OUT_MODE_GRF) {
			//output GRIT GRF file
			TCHAR *nameBuffer = PtcSuffixFileName(opt.outBase, _T(".grf"));
			
			//GRF requires one compression type specified
			if (!(opt.compressionPolicy & CX_COMPRESSION_TYPES_MASK)) opt.compressionPolicy |= CX_COMPRESSION_NONE;
			
			int fmt = FORMAT(texture.texels.texImageParam);
			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			GrfWriteHeader(fp);
			GrfTexWriteHdr(fp, fmt, width, texture.texels.height, texture.palette.nColors, opt.c0xp);
			GrfWritePltt(fp, texture.palette.pal, texture.palette.nColors, opt.compressionPolicy);
			GrfWriteTexImage(fp, texture.texels.texel, texelSize, texture.texels.cmp, indexSize, opt.compressionPolicy);
			GrfFinalize(fp);
			fclose(fp);
		} else if (opt.outMode == PTC_OUT_MODE_BINARY) {
			//suffix the filename with .ntft, .nfti, .nftp. So reserve 6 characters+base length.
			TCHAR *nameBuffer = PtcSuffixFileName(opt.outBase, NTFT_EXTENSION);

			//output texel always
			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			PtcEmitBinaryData(fp, texture.texels.texel, texelSize, opt.compressionPolicy);
			fclose(fp);
			if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), nameBuffer);

			//output palette if not direct
			if (opt.texFmt != CT_DIRECT && (opt.fixedPalette == NULL || opt.outFixedPalette)) {
				//depending on the number of input images, we may have to use a more specific name template.
				if (opt.nSrcFile == 1) {
					//suffix _pal.bin for single palette
					memcpy(nameBuffer + baseLength, NTFP_EXTENSION, (NTFX_EXTLEN + 1) * sizeof(TCHAR));
					fp = _tfopen(nameBuffer, _T("wb"));
					PtcEmitBinaryData(fp, texture.palette.pal, texture.palette.nColors * sizeof(COLOR), opt.compressionPolicy);
					fclose(fp);
					if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), nameBuffer);
				} else {
					//suffix _imageName_pal.bin for multiple palette
					for (int i = 0; i < opt.nSrcFile; i++) {
						const TCHAR *imageFileName = PtcGetFileName(opt.srcFiles[i]);
						
						//strip suffix
						TCHAR *imageName = _tcsdup(imageFileName);
						if (_tcsrchr(imageName, _T('.')) != NULL) {
							*_tcsrchr(imageName, _T('.')) = _T('\0');
						}
						
						//suffix file name: outBase_imageName_pal.bin
						TCHAR *pltName1 = PtcSuffixFileName(opt.outBase, _T("_"));
						TCHAR *pltName2 = PtcSuffixFileName(pltName1, imageName);
						TCHAR *pltName = PtcSuffixFileName(pltName2, NTFP_EXTENSION);
						free(pltName1);
						free(pltName2);
						
						//put data
						fp = _tfopen(pltName, _T("wb"));
						PtcEmitBinaryData(fp, texture.palette.pal + i * opt.nMaxColors, opt.nMaxColors * sizeof(COLOR), opt.compressionPolicy);
						fclose(fp);
						
						if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), pltName);
						free(pltName);
						free(imageName);
					}
				}
			}

			//output index if 4x4
			if (opt.texFmt == CT_4x4) {
				memcpy(nameBuffer + baseLength, NTFI_EXTENSION, (NTFX_EXTLEN + 1) * sizeof(TCHAR));
				fp = _tfopen(nameBuffer, _T("wb"));
				PtcEmitBinaryData(fp, texture.texels.cmp, indexSize, opt.compressionPolicy);
				fclose(fp);
				if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), nameBuffer);
			}

			free(nameBuffer);
		} else if (opt.outMode == PTC_OUT_MODE_NNSTGA) {
			//output as NNS TGA file
			TCHAR *nameBuffer = PtcSuffixFileName(opt.outBase, _T(".tga"));

			PtcWriteNnsTga(nameBuffer, &texture.texels, &texture.palette);
			if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), nameBuffer);
			free(nameBuffer);
		} else {
			//suffix the filename with .c, So reserve 3 characters+base length.
			TCHAR *nameBuffer = PtcSuffixFileName(opt.outBase, _T(".c"));

			int month, day, year, hour, minute, am;
			PtcGetDateTime(&month, &day, &year, &hour, &minute, &am);

			//find texture name
			const TCHAR *name = PtcGetFileName(opt.srcFiles[0]);

			//copy to MBS buffer, stripping extension too
			char *texName = (char *) calloc(_tcslen(name) + 1, sizeof(char));
			for (unsigned i = 0; i < _tcslen(name); i++) {
				TCHAR tch = name[i];
				if (tch == _T('.')) {
					texName[i] = '\0';
					break;
				}
				texName[i] = (char) tch;
			}
			texName[_tcslen(name)] = '\0';

			//if texture name doesn't start with a letter, prepend "tex_" to its name.
			char *prefix = ((texName[0] < 'a' || texName[0] > 'z') && (texName[0] < 'A' || texName[0] > 'Z')) ? "tex_" : "";

			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			nameBuffer[_tcslen(nameBuffer) - 1] = _T('h');
			FILE *fpHeader = _tfopen(nameBuffer, _T("wb"));
			
			fprintf(fp, texHeader, texName, month, day, year, hour, minute, am ? 'A' : 'P', TxNameFromTexFormat(opt.texFmt),
				texture.palette.nColors, TEXW(texture.texels.texImageParam), height);
			fprintf(fp, "#include <stdint.h>\n\n");
			
			fprintf(fpHeader, texHeader, texName, month, day, year, hour, minute, am ? 'A' : 'P', TxNameFromTexFormat(opt.texFmt),
				texture.palette.nColors, TEXW(texture.texels.texImageParam), height);
			fprintf(fpHeader, "#pragma once\n\n#include <stdint.h>\n\n");

			//write texel
			{
				fprintf(fpHeader, "//\n// Generated texel data\n//\n");
				PtcEmitTextData(fp, fpHeader, prefix, texName, "_texel", texture.texels.texel, texelSize, 2, opt.compressionPolicy);
				
				fprintf(fp, "\n");
				fprintf(fpHeader, "\n");
			}

			//write index
			if (opt.texFmt == CT_4x4) {
				fprintf(fpHeader, "//\n// Generated index data\n//\n");
				PtcEmitTextData(fp, fpHeader, prefix, texName, "_idx", texture.texels.cmp, indexSize, 2, opt.compressionPolicy);
				
				fprintf(fp, "\n");
				fprintf(fpHeader, "\n");
			}

			//write palette
			if (opt.texFmt != CT_DIRECT && opt.fixedPalette == NULL) {
				fprintf(fpHeader, "//\n// Generated palette data\n//\n");
				PtcEmitTextData(fp, fpHeader, prefix, texName, "_pal", texture.palette.pal, texture.palette.nColors * 2, 2, opt.compressionPolicy);
				
				fprintf(fp, "\n");
				fprintf(fpHeader, "\n");
			}
			
			fclose(fp);
			fclose(fpHeader);

			if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), nameBuffer);
			if (!opt.silent) _tprintf(_T("Wrote ") TC_STR _T("\n"), nameBuffer);
			
			free(texName);
			free(nameBuffer);
		}
		
		free(px);
	}

	return 0;
}

#ifdef _MSC_VER

extern int __getmainargs(int *_Argc, char ***_Argv, char ***_Env, int _DoWildCard, int *_StartInfo);
extern int __wgetmainargs(int *_Argc, wchar_t ***_Argv, wchar_t ***_Env, int _DoWildCard, int *_StartInfo);

int Entry(PVOID Peb) {
	int argc, startInfo;

#ifdef _UNICODE
	wchar_t **argv, **envp;
	__wgetmainargs(&argc, &argv, &envp, 0, &startInfo);
#else
	char **argv, char **envp;
	__getmainargs(&argc, &argv, &envp, 0, &startInfo);
#endif

	exit(_tmain(argc, argv));
	return 0;
}

#endif
