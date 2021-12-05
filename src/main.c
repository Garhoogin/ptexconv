#include <stdio.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include "texture.h"
#include "texconv.h"
#include "bggen.h"

#ifdef _WIN32
#   include <tchar.h>
#   include "gdip.h"
#else
#   define TCHAR char
#   define _T(x) x
#   define _tmain main
#   define _tfopen fopen
#   define _tcscmp strcmp
#   define _tprintf printf
#   define _tcslen strlen
#   define _ttoi atoi

#	define STB_IMAGE_IMPLEMENTATION
#	include "stb_image.h"
#endif

#define MODE_BG      0
#define MODE_TEXTURE 1

int _fltused;

const char *g_helpString = ""
	"DS Texture Converter command line utility version 1.0.0.0\n"
	"\n"
	"Usage: ptexconv <option...> image [option...]\n"
	"\n"
	"Global options:\n"
	"   -gb     Generate BG (default)\n"
	"   -gt     Generate texture\n"
	"   -o      Specify output base name\n"
	"   -ob     Output binary (default)\n"
	"   -oc     Output as C header file\n"
	"   -d  <n> Use dithering of n% (default 0%)\n"
	"   -cm <n> Limit palette colors to n, regardless of bit depth\n"
	"   -s      Silent\n"
	"   -h      Display help text\n"
	"\n"
	"BG Options:\n"
	"   -b  <n> Specify output bit depth {4, 8}\n"
	"   -p  <n> Use n palettes in output (Only valid for 4bpp)\n"
	"   -pb <n> Use palette base index n (Only valid for 4bpp)\n"
	"   -cc <n> Compress characters to a maximum of n (default is 1024)\n"
	"   -cn     No character compression\n"
	"   -ns     Do not output screen data\n"
	"   -nx     No tile flip X\n"
	"   -ny     No tile flip Y\n"
	"\n"
	"Texture Options:\n"
	"   -f     Specify format {palette4, palette16, palette256, a3i5, a5i3, tex4x4, direct}\n"
	"   -fp    Specify fixed palette file\n\n"
"";

const char *texHeader = ""
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

const char *bgHeader = ""
	"///////////////////////////////////////\n"
	"// \n"
	"// %s\n"
	"// Generated %d/%d/%d %d:%02d %cM\n"
	"// Bit depth: %d\n"
	"// Palettes: %d\n"
	"// Palette base: %d\n"
	"// Size: %dx%d\n"
	"// \n"
	"///////////////////////////////////////\n\n"
"";

void getDate(int *month, int *day, int *year, int *hour, int *minute, int *am) {

#ifndef _WIN32
	time_t tm = time(NULL);
	struct tm *local = localtime(&tm);

	*month = local->tm_mon + 1, *day = local->tm_mday, *year = local->tm_year + 1900;
	*hour = local->tm_hour, *minute = local->tm_min;
#else
	SYSTEMTIME time;
	GetSystemTime(&time);

	*month = time.wYear, *day = time.wDay, *year = time.wYear;
	*hour = time.wHour, *minute = time.wMinute;
#endif

	*am = *hour < 12;
	*hour %= 12;
	if (*hour == 0) *hour = 12;
}

void printHelp() {
	puts(g_helpString);
}

COLOR32 *tgdipReadImage(const TCHAR *lpszFileName, int *pWidth, int *pHeight) {

#ifdef _WIN32

#ifdef _UNICODE
	return gdipReadImage(lpszFileName, pWidth, pHeight);
#else //_UNICODE
	wchar_t buffer[260];
	for (int i = 0; i < _tcslen(lpszFileName); i++) {
		buffer[i] = (wchar_t) lpszFileName[i];
	}
	return gdipReadImage(buffer, pWidth, pHeight);
#endif

#else //_WIN32
	int channels;
	return (COLOR32 *) stbi_load(lpszFileName, pWidth, pHeight, &channels, 4);
#endif

}

int isTranslucent(COLOR32 *px, int nWidth, int nHeight) {
	for (int i = 0; i < nWidth * nHeight; i++) {
		int a = px[i] >> 24;
		if (a && a != 255) return 1;
	}
	return 0;
}

int guessFormat(COLOR32 *px, int nWidth, int nHeight) {
	//Guess a good format for the data. Default to 4x4.
	int fmt = CT_4x4;

	//if the texture is 1024x1024, do not choose 4x4.
	if (nWidth * nHeight == 1024 * 1024) fmt = CT_256COLOR;

	//is there translucency?
	if (isTranslucent(px, nWidth, nHeight)) {
		//then choose a3i5 or a5i3. Do this by using color count.
		int colorCount = countColors(px, nWidth * nHeight);
		if (colorCount < 16) {
			//colors < 16, choose a5i3.
			fmt = CT_A5I3;
		} else {
			//otherwise, choose a3i5.
			fmt = CT_A3I5;
		}
	} else {
		//weigh the other format options for optimal size.
		int nColors = countColors(px, nWidth * nHeight);

		//if <= 4 colors, choose 4-color.
		if (nColors <= 4) {
			fmt = CT_4COLOR;
		} else {
			//weigh 16-color, 256-color, and 4x4.
			//take the number of pixels per color.
			int pixelsPerColor = nWidth * nHeight / nColors;
			if (pixelsPerColor >= 2 && !(nWidth * nHeight >= 1024 * 512)) {
				fmt = CT_4x4;
			} else {
				//otherwise, 4x4 probably isn't a good option.
				if (nColors < 32) {
					fmt = CT_16COLOR;
				} else {
					fmt = CT_256COLOR;
				}
			}
		}
	}

	return fmt;
}

#ifdef _WIN32

float mylog2(float d) { //UGLY!
	float ans;
	_asm {
		fld1
		fld dword ptr [d]
		fyl2x
		fstp dword ptr [ans]
	}
	return ans;
}
#define log2f mylog2

#endif

//based on suggestions for color counts by SGC, interpolated with a log function
int chooseColorCount(int bWidth, int bHeight) {
	int colors = (int) (250.0f * (0.5f * log2f((float) bWidth * bHeight) - 5.0f) + 0.5f);
	if (sqrt(bWidth * bHeight) < 83.0f) {
		colors = (int) (4.345466990625f * sqrt(bWidth * bHeight) - 16.5098578365f);
	}

	//color count must be multiple of 8! Adjust here.
	if (colors & 7) {
		colors += 8 - (colors & 7);
	}
	return colors;
}

int _tmain(int argc, TCHAR **argv) {
	argc--;
	argv++;

	//check arguments
	if (argc < 1) {
		//print help
		printHelp();
		return 0;
	}

	//help?
	for (int i = 0; i < argc; i++) {
		if (_tcscmp(argv[i], _T("-h")) == 0) {
			printHelp();
			return 0;
		}
	}

	//begin processing arguments. First, determine global settings and conversion mode.
	const TCHAR *fixedPalette = NULL;
	const TCHAR *srcImage = NULL;
	const TCHAR *outBase = NULL;
	int nMaxColors = -1; //default. 256 for BG, automatic for texture
	int silent = 0;
	int diffuse = 0;
	int outputBinary = 1;
	int mode = MODE_BG;

	//BG settings
	int nMaxChars = 1024;
	int depth = 8;
	int nPalettes = 1;
	int paletteBase = 0;
	int flipX = 1, flipY = 1;
	int outputScreen = 1;


	//Texture settings
	int format = -1; //default, just guess

	for (int i = 0; i < argc; i++) {
		const TCHAR *arg = argv[i];

		if (_tcscmp(arg, _T("-gb")) == 0) {
			mode = MODE_BG;
		} else if (_tcscmp(arg, _T("-gt")) == 0) {
			mode = MODE_TEXTURE;
		} else if (_tcscmp(arg, _T("-o")) == 0) {
			i++;
			if (i < argc) outBase = argv[i];
		} else if (_tcscmp(arg, _T("-ob")) == 0) {
			outputBinary = 1;
		} else if (_tcscmp(arg, _T("-oc")) == 0) {
			outputBinary = 0;
		} else if (_tcscmp(arg, _T("-d")) == 0) {
			i++;
			if (i < argc) diffuse = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-cm")) == 0) {
			i++;
			if (i < argc) nMaxColors = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-s")) == 0) {
			silent = 1;
		}

		//BG option
		else if (_tcscmp(arg, _T("-b")) == 0) {
			i++;
			if (i < argc) depth = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-p")) == 0) {
			i++;
			if (i < argc) nPalettes = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-pb")) == 0) {
			i++;
			if (i < argc) paletteBase = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-cc")) == 0) {
			i++;
			if (i < argc) nMaxChars = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-cn")) == 0) {
			nMaxChars = -1;
		} else if (_tcscmp(arg, _T("-nx")) == 0) {
			flipX = 0;
		} else if(_tcscmp(arg, _T("-ny")) == 0){
			flipY = 0;
		} else if (_tcscmp(arg, _T("-ns")) == 0) {
			outputScreen = 0;
		}

		//Texture option
		else if (_tcscmp(arg, _T("-f")) == 0) {
			i++;
			if (i < argc) {
				const TCHAR *fmtString = argv[i];

				//what format?
				if (_tcscmp(fmtString, _T("a3i5")) == 0) format = CT_A3I5;
				else if (_tcscmp(fmtString, _T("a5i3")) == 0) format = CT_A5I3;
				else if (_tcscmp(fmtString, _T("palette4")) == 0) format = CT_4COLOR;
				else if (_tcscmp(fmtString, _T("palette16")) == 0) format = CT_16COLOR;
				else if (_tcscmp(fmtString, _T("palette256")) == 0) format = CT_256COLOR;
				else if (_tcscmp(fmtString, _T("tex4x4")) == 0) format = CT_4x4;
				else if (_tcscmp(fmtString, _T("direct")) == 0) format = CT_DIRECT;
				else {
					//maybe a format number
					int fid = _ttoi(fmtString);
					if (fid >= 1 && fid <= 7) format = fid;
					else _tprintf(_T("Unknown texture format %s.\n"), fmtString);
				}
			}
		} else if (arg[0] != _T('-')) { //not a switch
			srcImage = arg;
		}

		//unknown switch
		else {
			_tprintf(_T("Ignoring unknown switch %s.\n"), arg);
		}
	}

	//check for errors
	if (srcImage == NULL) {
		puts("No source image specified.");
		return 1;
	}
	if (outBase == NULL) {
		puts("No output name specified.");
		return 1;
	}
	if (depth != 4 && depth != 8) {
		printf("Invaid number of bits specified %d.\n", depth);
		return 1;
	}
	if (diffuse < 0 || diffuse > 100) {
		printf("Diffuse amount out of range: %d\n", diffuse);
		return 1;
	}
	if (mode == MODE_BG && ((depth == 4 && nMaxColors > 16) || (depth == 8 && nMaxColors > 256))) {
		printf("Too many output colors specified: %d\n", nMaxColors);
		return 1;
	}

	int width, height;
	COLOR32 *px = tgdipReadImage(srcImage, &width, &height);

	int baseLength = _tcslen(outBase);
	if (mode == MODE_BG) {
		//Generate BG
		//fix up automatic flags

		if (nMaxColors == -1) nMaxColors = (depth == 4) ? 16 : 256;
		if (depth == 4 && nMaxColors > 16) nMaxColors = 16;
		if (depth == 8 && nMaxColors > 256) nMaxColors = 256;
		if (depth == 8 && nPalettes != 1) nPalettes = 1;
		if (nPalettes > 16) nPalettes = 16;

		if(!silent) printf("Generating BG\nBits: %d\nPalettes: %d\nPalette size: %d\nMax chars: %d\nDiffuse: %d%%\nPalette base: %d\n\n",
						   depth, nPalettes, nMaxColors, nMaxChars, diffuse, paletteBase);

		COLOR *pal;
		unsigned char *chars;
		unsigned short *screen;
		int palSize, charSize, screenSize;
		int p1, p1max, p2, p2max;
		bgGenerate(px, width, height, depth, !!diffuse, diffuse / 100.0f, &pal, &chars, &screen, &palSize, &charSize, &screenSize,
				   paletteBase, nPalettes, 0, 0, nMaxChars != -1, nMaxColors, 0, 0, nMaxChars, &p1, &p1max, &p2, &p2max);

		if (outputBinary) {
			//output NBFP, NBFC, NBFS.

			//suffix the filename with .nbfp, .nbfc, .nbfs. So reserve 6 characters+base length.
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 6, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".nbfp"), 6 * sizeof(TCHAR));

			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			fwrite(pal, palSize, 1, fp);
			fclose(fp);
			if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);

			memcpy(nameBuffer + baseLength, _T(".nbfc"), 6 * sizeof(TCHAR));
			fp = _tfopen(nameBuffer, _T("wb"));
			fwrite(chars, charSize, 1, fp);
			fclose(fp);
			if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);

			if (outputScreen) {
				memcpy(nameBuffer + baseLength, _T(".nbfs"), 6 * sizeof(TCHAR));
				fp = _tfopen(nameBuffer, _T("wb"));
				fwrite(screen, screenSize, 1, fp);
				fclose(fp);
				if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);
			}

			free(nameBuffer);

		} else { //output header file

			//suffix the filename with .h, So reserve 3 characters+base length.
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 3, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".h"), 3 * sizeof(TCHAR));

			int month, day, year, hour, minute, am;
			getDate(&month, &day, &year, &hour, &minute, &am);

			//find BG name
			int lastSepIndex = -1;
			for (unsigned i = 0; i < _tcslen(srcImage); i++) {
				if (srcImage[i] == _T('/') || srcImage[i] == _T('\\')) lastSepIndex = i;
			}
			const TCHAR *name = srcImage + lastSepIndex + 1;

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
			fprintf(fp, bgHeader, bgName, month, day, year, hour, minute, am ? 'A' : 'P', depth, nPalettes,
					paletteBase, width, height);
			fprintf(fp, "#include <nds.h>\n\n");

			//write character
			{
				fprintf(fp, "static const u16 %s%s_char[] = {\n    ", prefix, bgName);
				unsigned short *schars = (unsigned short *) chars;
				int nShort = charSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", schars[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}

			//write palette
			{
				fprintf(fp, "static const u16 %s%s_pal[] = {\n    ", prefix, bgName);
				int nShort = palSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", pal[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}

			if (outputScreen) {
				//write screen
				fprintf(fp, "static const u16 %s%s_screen[] = {\n    ", prefix, bgName);
				int nShort = screenSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", screen[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}
			fclose(fp);

			free(nameBuffer);
			free(bgName);
		}

		free(pal);
		free(chars);
		free(screen);
	} else {
		//Generate Texture
		//fix up automatic flags

		if (format == -1) {
			format = guessFormat(px, width, height);
		}
		if (nMaxColors == -1) {
			switch (format) {
				case CT_A3I5:
					nMaxColors = 32;
					break;
				case CT_A5I3:
					nMaxColors = 8;
					break;
				case CT_4COLOR:
					nMaxColors = 4;
					break;
				case CT_16COLOR:
					nMaxColors = 16;
					break;
				case CT_256COLOR:
					nMaxColors = 256;
					break;
				case CT_DIRECT:
					nMaxColors = 0;
					break;
				case CT_4x4:
					nMaxColors = chooseColorCount(width, height);
					break;
			}
		}

		int colorMaxes[] = {0, 32, 4, 16, 256, 32768, 8, 0};
		int bppArray[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
		if (nMaxColors > colorMaxes[format]) {
			nMaxColors = colorMaxes[format];
			if(!silent) printf("Color count truncated to %d.\n", nMaxColors);
		}
		if(!silent) printf("Generating texture\nMax colors: %d\nFormat: %d\nDiffuse: %d%%\nSize: %dx%d\n\n",
						   nMaxColors, format, diffuse, width, height);

		TEXTURE texture = { 0 };

		CREATEPARAMS params;
		params.callback = NULL;
		params.callbackParam = NULL;
		params.colorEntries = nMaxColors;
		params.dest = &texture;
		params.diffuseAmount = (float) diffuse / 100.0f;
		params.dither = !!diffuse;
		params.ditherAlpha = (format == CT_A3I5 || format == CT_A5I3) && params.dither;
		params.useFixedPalette = fixedPalette != NULL;
		params.fixedPalette = NULL;
		params.fmt = format;
		params.width = width;
		params.height = height;
		params.px = px;
		params.threshold = 0;
		memset(params.pnam, 0, sizeof(params.pnam));

		if (fixedPalette != NULL) {
			FILE *fp = _tfopen(fixedPalette, _T("rb"));
			fseek(fp, 0, SEEK_END);
			int size = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			params.fixedPalette = (COLOR *) malloc(size);
			fread(params.fixedPalette, 2, size >> 1, fp);
			fclose(fp);

			if (params.colorEntries > (size >> 1)) {
				params.colorEntries = size >> 1;
				if(!silent) printf("Color count truncated to %d.\n", params.colorEntries);
			}
		}

		startConvert(&params);
		int texelSize = TEXW(texture.texels.texImageParam) * TEXH(texture.texels.texImageParam) * bppArray[format] / 8;
		int indexSize = (format == CT_4x4) ? (texelSize >> 1) : 0;

		//if binary, output as NTFT, NTFI, NTFP.
		if (outputBinary) {
			//suffix the filename with .ntft, .nfti, .nftp. So reserve 6 characters+base length.
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 6, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".ntft"), 6 * sizeof(TCHAR));

			//output texel always
			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			fwrite(texture.texels.texel, 1, texelSize, fp);
			fclose(fp);
			if(!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);

			//output palette if not direct
			if (format != CT_DIRECT) {
				memcpy(nameBuffer + baseLength, _T(".ntfp"), 6 * sizeof(TCHAR));
				fp = _tfopen(nameBuffer, _T("wb"));
				fwrite(texture.palette.pal, 2, texture.palette.nColors, fp);
				fclose(fp);
				if(!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);
			}

			//output index if 4x4
			if (format == CT_4x4) {
				memcpy(nameBuffer + baseLength, _T(".ntfi"), 6 * sizeof(TCHAR));
				fp = _tfopen(nameBuffer, _T("wb"));
				fwrite(texture.texels.cmp, 1, indexSize, fp);
				fclose(fp);
				if(!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);
			}

			free(nameBuffer);
		} else {

			//suffix the filename with .h, So reserve 3 characters+base length.
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 3, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".h"), 3 * sizeof(TCHAR));

			int month, day, year, hour, minute, am;
			getDate(&month, &day, &year, &hour, &minute, &am);

			//find texture name
			int lastSepIndex = -1;
			for (unsigned i = 0; i < _tcslen(srcImage); i++) {
				if (srcImage[i] == _T('/') || srcImage[i] == _T('\\')) lastSepIndex = i;
			}
			const TCHAR *name = srcImage + lastSepIndex + 1;

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
			fprintf(fp, texHeader, texName, month, day, year, hour, minute, am ? 'A' : 'P', stringFromFormat(format), texture.palette.nColors,
					TEXW(texture.texels.texImageParam), TEXH(texture.texels.texImageParam));
			fprintf(fp, "#include <nds.h>\n\n");

			//write texel
			{
				fprintf(fp, "static const u16 %s%s_texel[] = {\n    ", prefix, texName);
				unsigned short *txel = (unsigned short *) texture.texels.texel;
				int nShort = texelSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", txel[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}

			//write index
			if (format == CT_4x4) {
				fprintf(fp, "static const u16 %s%s_idx[] = {\n    ", prefix, texName);
				unsigned short *pidx = (unsigned short *) texture.texels.cmp;
				int nShort = indexSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", pidx[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}

			//write palette
			if (format != CT_DIRECT) {
				fprintf(fp, "static const u16 %s%s_pal[] = {\n    ", prefix, texName);
				unsigned short *pcol = (unsigned short *) texture.palette.pal;
				int nShort = texture.palette.nColors;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", pcol[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				if (nShort & 0xF) fprintf(fp, "\n"); //force this to be on a new line
				fprintf(fp, "};\n\n");
			}
			fclose(fp);

			if(!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);
			free(texName);
			free(nameBuffer);
			if(params.fixedPalette != NULL) free(params.fixedPalette);
		}
	}


	return 0;
}

#ifdef _WIN32

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
