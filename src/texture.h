#pragma once
#include "color.h"

// ----- texture format names
#define GX_TEXFMT_NONE     0  // no texture
#define GX_TEXFMT_A3I5     1  // a3i5 format
#define GX_TEXFMT_PLTT4    2  // palette4 format
#define GX_TEXFMT_PLTT16   3  // palette16 format
#define GX_TEXFMT_PLTT256  4  // palette256 format
#define GX_TEXFMT_TEX4x4   5  // tex4x4 format
#define GX_TEXFMT_A5I3     6  // a5i3 format
#define GX_TEXFMT_DIRECT   7  // direct format

// ----- obsolete names (TODO: delete)
#define CT_A3I5     GX_TEXFMT_A3I5
#define CT_4COLOR   GX_TEXFMT_PLTT4
#define CT_16COLOR  GX_TEXFMT_PLTT16
#define CT_256COLOR GX_TEXFMT_PLTT256
#define CT_4x4      GX_TEXFMT_TEX4x4
#define CT_A5I3     GX_TEXFMT_A5I3
#define CT_DIRECT   GX_TEXFMT_DIRECT


// ----- tex4x4 palette index data layout macros
#define GX_TEX4x4_PIDX_PTY_INTERPOLATE  0x4000
#define GX_TEX4x4_PIDX_PTY_FULL         0x0000
#define GX_TEX4x4_PIDX_A_OPAQUE         0x8000
#define GX_TEX4x4_PIDX_A_XPNT           0x0000
#define GX_TEX4x4_PIDX_MODE_MASK        0xC000
#define GX_TEX4x4_PIDX_ADDR_MASK        0x3FFF
#define GX_TEX4x4_PIDX_ADDR(c)          ((unsigned int)(((c)&GX_TEX4x4_PIDX_ADDR_MASK)<<1))

// ----- tex4x4 palette index data layout macros (obsolete) (TODO: delete)
#define COMP_INTERPOLATE   GX_TEX4x4_PIDX_PTY_INTERPOLATE
#define COMP_FULL          GX_TEX4x4_PIDX_PTY_FULL
#define COMP_OPAQUE        GX_TEX4x4_PIDX_A_OPAQUE
#define COMP_TRANSPARENT   GX_TEX4x4_PIDX_A_XPNT
#define COMP_MODE_MASK     GX_TEX4x4_PIDX_MODE_MASK
#define COMP_INDEX_MASK    GX_TEX4x4_PIDX_ADDR_MASK
#define COMP_INDEX(c)      GX_TEX4x4_PIDX_ADDR(c)

// ----- a3i5 texel data layout macros
#define GX_A3I5_I_MASK       0x1F
#define GX_A3I5_A_MASK       0xE0
#define GX_A3I5_I_SHIFT         0
#define GX_A3I5_A_SHIT          5
#define GX_A3I5_A3_TO_A5(x)  ((x)<<2)|((x)>>1)

// ----- TEXIMAGE_PARAM layout macros
#define GX_TEXIMAGE_PARAM_ADDR_MASK 0x0000FFFF
#define GX_TEXIMAGE_PARAM_W_MASK    0x00700000
#define GX_TEXIMAGE_PARAM_H_MASK    0x03800000
#define GX_TEXIMAGE_PARAM_FMT_MASK  0x1C000000
#define GX_TEXIMAGE_PARAM_C0XP_MASK 0x20000000
#define GX_TEXIMAGE_PARAM_ADDR_SHIFT         0
#define GX_TEXIMAGE_PARAM_W_SHIFT           20
#define GX_TEXIMAGE_PARAM_H_SHIFT           23
#define GX_TEXIMAGE_PARAM_FMT_SHIFT         26
#define GX_TEXIMAGE_PARAM_C0XP_SHIFT        29

// ----- TEXIMAGE_PARAM layout macros (obsolete) (TODO: delete)
#define FORMAT(p)		(((p)>>GX_TEXIMAGE_PARAM_FMT_SHIFT)&7)
#define COL0TRANS(p)	(((p)>>GX_TEXIMAGE_PARAM_C0XP_SHIFT)&1)
#define OFFSET(p)		(((p)&GX_TEXIMAGE_PARAM_ADDR_MASK)<<3)
#define TEXW(p)			(8<<(((p)>>GX_TEXIMAGE_PARAM_W_SHIFT)&7))
#define TEXH(p)			(8<<(((p)>>GX_TEXIMAGE_PARAM_H_SHIFT)&7))


typedef struct {
	int texImageParam;
	int height;
	unsigned char *texel;
	uint16_t *cmp;
	char *name;
} TEXELS;

typedef struct {
	int nColors;
	COLOR *pal;
	char *name;
} PALETTE;

typedef struct {
	TEXELS texels;
	PALETTE palette;
} TEXTURE;

const char *TxNameFromTexFormat(int fmt);

void TxRenderRect(COLOR32 *px, unsigned int srcX, unsigned int srcY, unsigned int srcW, unsigned int srcH, TEXELS *texels, PALETTE *palette);

void TxRender(COLOR32 *px, TEXELS *texels, PALETTE *palette);

int TxGetTexelSize(int width, int height, int texImageParam);

int TxGetTextureVramSize(TEXELS *texels);

int TxGetIndexVramSize(TEXELS *texels);

int TxGetTexPlttVramSize(PALETTE *palette);

int TxDimensionIsValid(int x);
