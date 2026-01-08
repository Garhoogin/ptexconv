# ptexconv

This is the command line version of the background and texture converson functioanlity of NitroPaint. It can output textures in all of the DS's texture formats, as well as output Text mode BGs in 4 and 8 bit depth.

## Commane Line Usage

    Usage: ptexconv <option...> image [option...]
  	Global options:
  	   -gb     Generate BG (default)
  	   -gt     Generate texture
  	   -o      Specify output base name
  	   -ob     Output binary (default)
  	   -oc     Output as C header file
       -og     Output as GRIT GRF file
       -k  <c> Specify alpha key as 24-bit RRGGBB hex color
  	   -d  <n> Use dithering of n% (default 0%)
  	   -cm <n> Limit palette colors to n, regardless of bit depth
  	   -bb <n> Lightness-Color balance [1, 39] (default 20)
  	   -bc <n> Red-Green color balance [1, 39] (default 20)
  	   -be     Enhance colors in gradients (off by default)
  	   -s      Silent
  	   -h      Display help text
  	
  	BG Options:
       -bt4    Output BG data as text            (4bpp)
       -bt8    Output BG data as text            (8bpp)
       -ba     Output BG data as affine          (8bpp)
       -bA     Output BG data as affine extended (8bpp, default)
       -bB     Output BG data as bitmap (no BG screen output)
  	   -p  <n> Use n palettes in output
  	   -po <n> Use per palette offset n
  	   -pc     Use compressed palette
  	   -pb <n> Use palette base index n
       -p0o    Use color 0 as an opaque color slot
  	   -cb <n> Use character base index n
  	   -cc <n> Compress characters to a maximum of n (default is 1024)
  	   -cn     No character compression
  	   -wp <f> Use or overwrite an existing palette file (binary only)
  	   -wc <f> Use or append to an existing character file (binary only)
  	   -ns     Do not output screen data
  	   -se     Output screen only. Requires -wp and -wc (will not modify).
  	   -od     Output as DIB (disables character compression)
  	
  	Texture Options:
  	   -f      Specify format {palette4, palette16, palette256, a3i5, a5i3, tex4x4, direct}
       -ct <n> Set tex4x4 palette compression strength [0, 100] (default 0).
  	   -ot     Output as NNS TGA
       -tt     Trim the texture in the T axis if its height is not a power of 2
       -t0x    Color 0 is transparent (defuault: inferred)
       -t0o    Color 0 is opaque      (default: inferred)
       -da     Apply dithering in the alpha  channel (a3i5, a5i3)
       -fp <f> Specify fixed palette file
       -fpo    Outputs the fixed palette among other output files when used

    Compression Options:
       -cbios  Enable use of all BIOS compression types (valid for binary, C, GRF)
       -cno    Enable use of no/dummy compression       (valid for binary, C, GRF)
       -clz    Enable use of LZ compression             (valid for binary, C, GRF)
       -ch     Enable use of any Huffman compression    (valid for binary, C, GRF)
       -ch4    Enable use of 4-bit Huffman compression  (valid for binary, C, GRF)
       -ch8    Enable use of 8-bit Huffman compression  (valid for binary, C, GRF)
       -crl    Enablse use of RLE compression           (valid for binary, C, GRF)
       -clzx   Enable use of LZ extended compression    (valid for binary, C, GRF)
       -c8     Allow VRAM-unsafe compression            (valid for binary, C, GRF)

## General Options
The general options contain switches that may be useful regardless if you are generating a texture or BG graphics. Specify `-gt` to generate a texture, or specify `-gb` to generate BG graphics. Use the `-o` option followed by an output base name for file output (this gets suffixed on output). 

Also among the general options are those for controlling palette creation and indexing. Use the `-d` switch followed by a diffusion percentage (0-100) to specify the dithering level on the output image. Floyd-Steinberg dithering with a serpentine pattern is employed for this. To more specifically control the color reduction process, use the `-bb` and `-bc` followed by a number between 1 and 39 (default is 20 for both). These options control the Lightness-Color and Red-Green weighting respectively. Lastly, use the `be` switch to have palette generation try to favor gradient colors more strongly.

Last among the general options are `-s` which causes the program not to output any text unless in the case of a failure, and `-h` which prints the above usage information without processing any conversions.

## BG Conversion Options
First and foremost, the `-bt4`, `-bt8`, `-ba`, `-bA`, and `-bB` switches set the BG format of the output. These specify the following BG formats:
| Switch | BG Format |
| ------ | --------- |
| `-bt4` | Text (16 colors x 16 palettes) |
| `-bt8` | Text (256 colors x 1 palette) |
| `-ba`  | Affine (256 colors x 1 palette) |
| `-bA`  | Affine Extended (256 colors x 16 palettes) |
| `-bB`  | Bitmap (256 colors x 1 palette) |

To control the number of palettes to output, use `-p` followed by the number of palettes (between 1 and 16). Generating between 2 and 16 palettes takes more time than creating a single palette. The `-po` followed by a palette offset specifies the offset (into each palette) to start writing colors to. A palette offset of 1, for example, causes the first color of each palette to go unused (indexing will ignore these unused slots as well). The global `-cm` option now controls the number of colors in each palette. The `-pb` option followed by a number between 0 and 15 controls the index of the first palette to write to. The `-pc` option will cause the palette output to include only a subset of the total palette. For a single palette, it will output only the written-to colors of the single palette. For multiple palettes, it will output only those palettes written to, but in their entirety (regardless the palette offset setting). 

In addition, there are a few options for controlling output of graphics data as well. Use the `-cb` option followed by an integer to set the character base index. This is for loading graphics to a nonzero offset into a BG VRAM slot. This number is added to the character index of each screen tile. Next, the `-cc` and `-cn` options control character compression. By default, a maximum of 1024 is used. Identical characters (with H/V flip) are merged, and if there are more than the specified amount, it will merge them until the number specified remain. The `-cn` option disables character compression altogether, resulting in no identical characters being merged. 

There exist a couple of options for writing to cumulative files. Use the `-wp` option to specify a (binary) palette file to read in before conversion. The generated palette will write over this palette, but not replace any other color slots. This allows for a single palette file to be built up over the course of multiple conversions. Then there is the `-wc` option, which specifies a character graphics file to append to. When this is specified and the character base index has not been specified with `-cb`, it will use the end of the file as the character base index (assuming this file uses a base index of 0). If a base index *was* specified with `-cb`, then data in the file will be overwritten with the output of conversion.

BG conversion may use color 0 as an opaque color slot. By default, color 0 is reserved for use by transparent pixels. Specify the `-p0o` switch to enable its use as an opaque color. Specifying this option loses the ability to use transparent pixels in the BG conversion. This option is only effective when the palette offset is 0. For character map color reduction, one opaque color is distributed across color 0 of each created color palette. For this kind of BG to display correctly, ensure that this color 0 ends up in the backdrop color slot in the background palette.

BG conversion also allows for using existing graphics data to create screen files from a source image. Use the `-se` option to generate a screen file exclusively. This option requires specifying palette and character graphics files with the `-wp` and `-wc` options. With `-se` enabled, however, these files are not written to, only read from. The only output file will be the resulting screen file.

Lastly, to do BG color reduction but output as a standard BMP file, use the `-od` option. This will produce an indexed BMP file with the same palette layout as would have been output otherwise. This option disables character compression.

## Texture Conversion Options
Texture conversion has a couple switches of its own. Use `-f` followed by a format name (or number) to select a texture format to use. Use the `-fp` option followed by a path to a (raw) palette file to instruct the program to use this palette file when generating texture data. Lastly, the `-ot` option tells ptexconv to output a file as an NNS TGA file (for use with NNS plugins).

The options for texture format are summarized:
| Format     | Bit Depth | Max Palette Colors | Transparency |
| ---------- | --------- | ------------------ | ------------ |
| palette4   | 2bpp      | 4                  | 1 color      |
| palette16  | 4bpp      | 16                 | 1 color      |
| palette256 | 8bpp      | 256                | 1 color      |
| a3i5       | 8bpp      | 32                 | 3-bit        |
| a5i3       | 8bpp      | 8                  | 5-bit        |
| tex4x4     | 2+1bpp    | 32768              | 1 color      |
| direct     | 16bpp     | --                 | 1-bit        |

Texture formats a3i5 and a5i3 have a full alpha channel per pixel. In these formats, you may choose to enable dithering of the alpha channel by enabling the `-da` switch. Without this option, the alpha value is rounded to nearest and not dithered. To use alpha dithering, the `-d` option must also be specified with a dithering level. This dithering level is then used for both the color and alpha channels.

When using a fixed palette, the palette file is read (and assumed not to be compressed) and used for the color reduction process without being modified. By default, a palette file is not output when outputting raw binary data with a fixed palette. If output of the palette file is required, you may additionally use the `-fpo` option.

When using palette4, palette16 or palette256 texture formats, color index 0 is made the transparent color if there exists at least one pixel in the input image with an alpha value of less than half. The color 0 mode can be overridden using the `-t0x` or `-t0o` options. Specify `-t0x` to reserve color 0 for transparency, or `-t0o` to use it as an opaque color.

## Compression Options
By default, output files are not compresed. Compression settings are valid for binary files, C source files, and GRF files. For C source files, the compression is applied to the data before writing C source output. For binary files, the whole file is compressed. For GRF files, the file's binary blocks are independently compressed.

By default, no compression is used. For C and binary output, this means files are output exactly as they should be copied to VRAM. For GRF files, since a compression header is required for binary blocks, a dummy compression is used (as thought `-cno` was specified). 

ptexconv uses a system of opt-in compression. Specifying a compression type on the command line opts in that compression type for output. When multiple compression types are opted in, ptexconv selects the smallest one. 

To use any compression supported by the DS/GBA's BIOS, use the `-cbios` option, which has the same effect as specifying `-clz -ch -crl`. The `-ch` option enables use of either Huffman format, and is equivalent to specifying `-ch4 -ch8`. The extended LZ compression format is not supported by the BIOS, and must be enabled independently to use it. If data is not meant to be decompressed into VRAM directly, then you may specify the `-c8` flag, which affects the LZ compression to allow for a better compression rate.

## Usage Examples
**Example 1**: creating a background using 4 16-color palettes: 
```
ptexconv -gb -b 4 -p 4 background.png -o background
```

**Example 2**: creating two backgrounds that use the same graphics space, with one image getting the first 8 palettes, and the other getting the 9th palette, allotting up to 512 characters of graphics to each:
```
ptexconv -gb -b 4 -p 8 main_bg.png -cc 512 -o background
ptexconv -gb -b 4 -pb 8 bg2.png -cc 512 -wp background_pal.bin -wc background_chr.bin -o bg2
```

**Example 3**: creating an extended palette background using 4 palettes starting at palette index 2, but only output the portion of the palette that should be loaded (palette that can be loaded directly to extended palette slot 2, rather than the start of the palette memory):
```
ptexconv -gb -b 8 -p 4 -pb 2 -pc background.png -o background
```

**Example 4**: using an existing 4bpp tileset with 12 palettes to construct a background:
```
ptexconv -gb -b 4 -p 12 image.png -wp tileset_pal.bin -wc tileset_chr.bin -se -o bg_constructed
```
