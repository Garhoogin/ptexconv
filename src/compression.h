#pragma once

#include <stdint.h>


/*****************************************************************************\
*
* These flags control the encoder's preference for compression. Values are
* constructed by ORing desired flags. If strictly no compression should be
* used, then specify CX_COMPRESSION_NONE. The encoder will use the compression
* type that minimizes the data output size from the set of compression types
* specified. If the data is intended to be uncompressed into VRAM, also use
* CX_COMPRESSION_VRAM_SAFE to ensure correct operation at runtime.
*
\*****************************************************************************/
typedef enum GrfCompressionPolicy_ {
	CX_COMPRESSION_NONE              = 0x01,   // allow emitting uncompressed data
	CX_COMPRESSION_LZ                = 0x02,   // allow emitting LZ-compressed data
	CX_COMPRESSION_HUFFMAN4          = 0x04,   // enable emitting 4-bit Huffman compressed data
	CX_COMPRESSION_HUFFMAN8          = 0x08,   // enable emitting 8-bit Huffman compressed data
	CX_COMPRESSION_RLE               = 0x10,   // enable emitting RLE compressed data
	CX_COMPRESSION_VRAM_SAFE         = 0x80    // ensure VRAM-safe compression is applied
} CxCompressionPolicy;


#define CX_COMPRESSION_TYPES_MASK      0x1F
#define CX_COMPRESSION_FLAGS_MASK      0x80

#define CX_NUM_COMPRESSION_TYPES          5



unsigned char *CxCompressLZ16(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);

unsigned char *CxCompressLZ8(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);

unsigned char *CxCompressDummy(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);



unsigned char *CxCompress(const unsigned char *data, unsigned int size, unsigned int *pOutSize, CxCompressionPolicy compression);
