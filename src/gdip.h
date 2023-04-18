#pragma once

#ifdef _MSC_VER
#include <Windows.h>

#include "color.h"

extern INT __stdcall GdiplusStartup(void *n, void *n2, int n3);
extern INT __stdcall GdipCreateBitmapFromFile(LPWSTR str, void *bmp);
extern INT __stdcall GdipGetImageWidth(void *img, INT *width);
extern INT __stdcall GdipGetImageHeight(void *img, INT *width);
extern INT __stdcall GdipBitmapLockBits(void *img, RECT *rc, int n1, int n2, void *n3);
extern INT __stdcall GdipDisposeImage(void *img);
extern INT __stdcall GdipCreateBitmapFromScan0(INT n1, INT n2, INT n3, INT n4, void *n5, void *n6);
extern INT __stdcall GdipSaveImageToFile(void *n1, LPWSTR n2, void *n3, void *n4);


typedef struct {
	DWORD d1[6];
} BITMAPDATA;

typedef struct {
	DWORD GdiplusVersion;
	DWORD DebugEventCallback;
	BOOL SuppressBackgroundThread;
	BOOL SuppressExternalCodecs;
} STARTUPINPUT;

int isTGA(unsigned char *buffer, unsigned int dwSize);

COLOR32 *gdipReadImage(LPWSTR lpszFileName, int *pWidth, int *pHeight);

void writeImage(COLOR32 *pixels, int width, int height, LPWSTR lpszFileName);

#endif

