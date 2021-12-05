# ptexconv

NitroPaint texconv.

Simple converter for DS texture and BG formats, based on functionality offered by ntexconv.

BG files output as NBFP (palette), NBFC (character), and NBFS (screen). 

Texture files output as NTFT (texel), NTFI (index), and NTFP (palette). 

Uses GDI+ to read images on Windows, and stb_image on Linux.
