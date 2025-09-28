#pragma once
#include "color.h"

#ifdef _MSC_VER
#include <Windows.h>


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

#endif // _MSC_VER




// -----------------------------------------------------------------------------------------------
// Name: enum ImgScaleSetting
//
// Image resize modes
// -----------------------------------------------------------------------------------------------
typedef enum ImgScaleSetting_ {
	IMG_SCALE_FILL,                // stretch to fill whole output, destryoing aspect ratio
	IMG_SCALE_COVER,               // stretch to cover the whole output, preserving the aspect ratio
	IMG_SCALE_FIT                  // stretch to maximize the size while keeping full visibility and aspect ratio
} ImgScaleSetting;

typedef enum ImgCountColorsMode_ {
	IMG_CCM_IGNORE_ALPHA                = 0x01, // ignores the alpha value for purposes of color counting
	IMG_CCM_BINARY_ALPHA                = 0x02, // rounds off alpha levels by a threshold
	IMG_CCM_NO_COUNT_TRANSPARENT        = 0x04, // do not count transparency as a color
	IMG_CCM_NO_IGNORE_TRANSPARENT_COLOR = 0x08, // do not treat all alpha=0 with differing RGB as identical
} ImgCountColorsMode;


// -----------------------------------------------------------------------------------------------
// Name: ImgFlip
//
// Flip an image horizontally and/or vertically.
//
// Parameters:
//   px            The input pixel buffer.
//   width         The image width.
//   height        The image height.
//   hFlip         Set to 1 to flip the image horizontally.
//   vFlip         Set to 1 to flip the image vertically.
// -----------------------------------------------------------------------------------------------
void ImgFlip(
	COLOR32     *px,
	unsigned int width,
	unsigned int height,
	int          hFlip,
	int          vFlip
);

// -----------------------------------------------------------------------------------------------
// Name: ImgSwapRedBlue
//
// Swap red and blue color channels in an image.
//
// Parameters:
//   px            The input pixel buffer.
//   width         The image width.
//   height        The image height.
// -----------------------------------------------------------------------------------------------
void ImgSwapRedBlue(
	COLOR32     *px,
	unsigned int width,
	unsigned int height
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCountColors
//
// Count the number of unique colors in an image. This function treats full transparency as one
// color, regardless of what the values of RGB are for those pixels. Pixels with nonzero alpha
// values are treated as nondistinct when their RGB values are equal in value.
//
// Parameters:
//   px            The input pixel buffer.
//   nPx           The number of pixels (width*height).
//
// Returns:
//   The number of disctinct RGB colors in the input image.
// -----------------------------------------------------------------------------------------------
unsigned int ImgCountColors(
	const COLOR32 *px,
	unsigned int   nPx
);

unsigned int ImgCountColorsEx(
	const COLOR32     *px,
	unsigned int       width,
	unsigned int       height,
	ImgCountColorsMode mode
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCrop
//
// Crop an image with a source bounding box. The sampling region is specified as a rectangle whose
// starting coordinates may be negative. Points sampled from out of the bounds of the input image
// are rendered as transparent pixels in the output buffer.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   srcX          The source X to begin sampling (may be negative)
//   srcY          The source Y to begin sampling (may be negative)
//   srcWidth      The source rectangle X
//   srcHeight     The source rectangle Y
//
// Returns:
//   The cropped pixel buffer. The output pixel buffer will have a size of srcWidth x srcHeight.
// -----------------------------------------------------------------------------------------------
COLOR32 *ImgCrop(
	const COLOR32 *px,
	unsigned int   width,
	unsigned int   height,
	int            srcX,
	int            srcY,
	unsigned int   srcWidth,
	unsigned int   srcHeight
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCropInPlace
//
// Crop an image with a source bounding box and write pixel data to a buffer. The sampling region
// is specified as a rectangle whose starting coordinates may be negative. Points sampled from
// out of the bounds of the input image are rendered as transparent pixels in the output buffer.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   out           The output pixel buffer (sized srcWidth x srcHeight)
//   srcX          The source X to begin sampling (may be negative)
//   srcY          The source Y to begin sampling (may be negative)
//   srcWidth      The source rectangle X
//   srcHeight     The source rectangle Y
// -----------------------------------------------------------------------------------------------
void ImgCropInPlace(
	const COLOR32 *px,
	unsigned int   width,
	unsigned int   height,
	COLOR32       *out,
	int            srcX,
	int            srcY,
	unsigned int   srcWidth,
	unsigned int   srcHeight
);

// -----------------------------------------------------------------------------------------------
// Name: ImgComposite
//
// Composite two translucent images.
//
// Parameters:
//   back          The back layer pixels
//   backWidth     The back layer width
//   backHeight    The back layer height
//   front         The front layer pixels
//   frontWidth    The front layer width
//   frontHeight   The front layer height
//   outWidth      The composited width
//   outHeight     The composited height
//
// Returns:
//   The compositde pixels.
// -----------------------------------------------------------------------------------------------
COLOR32 *ImgComposite(
	const COLOR32 *back,
	unsigned int   backWidth,
	unsigned int   backHeight,
	const COLOR32 *front,
	unsigned int   frontWidth,
	unsigned int   frontHeight,
	unsigned int  *outWidth,
	unsigned int  *outHeight
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCreateAlphaMask
//
// Create an alpha mask for rendering an image with a transparent region.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   pRows         The output mask number of rows
//   pStride       The output mask stride
//
// Returns:
//   The created alpha mask.
// -----------------------------------------------------------------------------------------------
unsigned char *ImgCreateAlphaMask(
	const COLOR32 *px,
	unsigned int   width,
	unsigned int   height,
	unsigned int   threshold,
	unsigned int  *pRows,
	unsigned int  *pStride
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCreateColorMask
//
// This function takes an RGBA image as input and produces a bitmap color mask as a result. The
// created color mask is in 24-bit per pixel format.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   pRows         The output mask number of rows
//   pStride       The output mask stride
//
// Returns:
//   The created color mask.
// -----------------------------------------------------------------------------------------------
unsigned char *ImgCreateColorMask(
	const COLOR32 *px, 
	unsigned int   width,
	unsigned int   height,
	unsigned int  *pRows,
	unsigned int  *pStride
);

// -----------------------------------------------------------------------------------------------
// Name: ImgScale
//
// Resize an image. When downscaling, the pixels are resampled to lose as little image information
// as possible. When upscaling, pixels are preserved.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   outWidth      Width after scaling
//   outHeight     Height after scaling
//
// Returns:
//   The resulting scaleed image pixels.
// -----------------------------------------------------------------------------------------------
COLOR32 *ImgScale(
	const COLOR32 *px,
	unsigned int   width,
	unsigned int   height,
	unsigned int   outWidth,
	unsigned int   outHeight
);

// -----------------------------------------------------------------------------------------------
// Name: ImgScaleEx
//
// Scales an image, with additional options to specify how the aspect ratio should be handled.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   outWidth      Width after scaling
//   outHeight     Height after scaling
//   mode          Image scaling mode (see enum ImgScaleSetting).
//
// Returns:
//   The resulting scaleed image pixels.
// -----------------------------------------------------------------------------------------------
COLOR32 *ImgScaleEx(
	const COLOR32  *px,
	unsigned int    width,
	unsigned int    height,
	unsigned int    outWidth,
	unsigned int    outHeight,
	ImgScaleSetting mode
);



