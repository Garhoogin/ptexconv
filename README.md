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
  	   -d  <n> Use dithering of n% (default 0%)
  	   -cm <n> Limit palette colors to n, regardless of bit depth
  	   -bb <n> Lightness-Color balance [1, 39] (default 20)
  	   -bc <n> Red-Green color balance [1, 39] (default 20)
  	   -be     Enhance colors in gradients (off by default)
  	   -s      Silent
  	   -h      Display help text
  	
  	BG Options:
  	   -b  <n> Specify output bit depth {4, 8}
  	   -p  <n> Use n palettes in output
  	   -po <n> Use per palette offset n
  	   -pc     Use compressed palette
  	   -pb <n> Use palette base index n
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
  	   -ot     Output as NNS TGA
       -fp <f> Specify fixed palette file

## General Options
The general options contain switches that may be useful regardless if you are generating a texture or BG graphics. Specify `-gt` to generate a texture, or specify `-gb` to generate BG graphics. Use the `-o` option followed by an output base name for file output (this gets suffixed on output). 

Also among the general options are those for controlling palette creation and indexing. Use the `-d` switch followed by a diffusion percentage (0-100) to specify the dithering level on the output image. Floyd-Steinberg dithering with a serpentine pattern is employed for this. To more specifically control the color reduction process, use the `-bb` and `-bc` followed by a number between 1 and 39 (default is 20 for both). These options control the Lightness-Color and Red-Green weighting respectively. Lastly, use the `be` switch to have palette generation try to favor gradient colors more strongly.

Last among the general options are `-s` which causes the program not to output any text unless in the case of a failure, and `-h` which prints the above usage information without processing any conversions.

## BG Conversion Options
First and foremost, the `-b` switch sets the bit depth of the output. Follow it with 4 or 8 for 4-bit and 8-bit graphics respectively. 

To control the number of palettes to output, use `-p` followed by the number of palettes (between 1 and 16). Generating between 2 and 16 palettes takes more time than creating a single palette. The `-po` followed by a palette offset specifies the offset (into each palette) to start writing colors to. A palette offset of 1, for example, causes the first color of each palette to go unused (indexing will ignore these unused slots as well). The global `-cm` option now controls the number of colors in each palette. The `-pb` option followed by a number between 0 and 15 controls the index of the first palette to write to. The `-pc` option will cause the palette output to include only a subset of the total palette. For a single palette, it will output only the written-to colors of the single palette. For multiple palettes, it will output only those palettes written to, but in their entirety (regardless the palette offset setting). 

In addition, there are a few options for controlling output of graphics data as well. Use the `-cb` option followed by an integer to set the character base index. This is for loading graphics to a nonzero offset into a BG VRAM slot. This number is added to the character index of each screen tile. Next, the `-cc` and `-cn` options control character compression. By default, a maximum of 1024 is used. Identical characters (with H/V flip) are merged, and if there are more than the specified amount, it will merge them until the number specified remain. The `-cn` option disables character compression altogether, resulting in no identical characters being merged. 

There exist a couple of options for writing to cumulative files. Use the `-wp` option to specify a (binary) palette file to read in before conversion. The generated palette will write over this palette, but not replace any other color slots. This allows for a single palette file to be built up over the course of multiple conversions. Then there is the `-wc` option, which specifies a character graphics file to append to. When this is specified and the character base index has not been specified with `-cb`, it will use the end of the file as the character base index (assuming this file uses a base index of 0). If a base index *was* specified with `-cb`, then data in the file will be overwritten with the output of conversion.

BG conversion also allows for using existing graphics data to create screen files from a source image. Use the `-se` option to generate a screen file exclusively. This option requires specifying palette and character graphics files with the `-wp` and `-wc` options. With `-se` enabled, however, these files are not written to, only read from. The only output file will be the resulting screen file.

Lastly, to do BG color reduction but output as a standard BMP file, use the `-od` option. This will produce an indexed BMP file with the same palette layout as would have been output otherwise. This option disables character compression.

## Texture Conversion Options
Texture conversion has a couple switches of its own. Use `-f` followed by a format name (or number) to select a texture format to use. Use the `-fp` option followed by a path to a (raw) palette file to instruct the program to use this palette file when generating texture data. Lastly, the `-ot` option tells ptexconv to output a file as an NNS TGA file (for use with NNS plugins).
