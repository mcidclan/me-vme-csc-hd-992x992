# CSC YCbCr HD Sample Code

This repository provides a sample implementation for converting YCbCr data to RGB using the CSC controller backed by the VME. It demonstrates how to construct a 992×992 image, which appears to be the maximum supported size.

The implementation uses double buffering with two complete sets of YCbCr source planes (Y1/Cb1/Cr1 and Y2/Cb2/Cr2) loaded directly into main memory and passed to the CSC controller. To achieve the capability to render at that large size, each source buffer set has its corresponding RGB output buffer (RGB1 and RGB2) with distinct memory offsets. The conversion is still performed in one step.

A minimal amount of setup is required on the Media Engine side. Note that this sample only works on actual physical hardware.

## Instructions

1. Run `make clean; make;` from your shell.
2. Place the `y_hd.bin`, `cb_hd.bin`, `cr_hd.bin`, and `kcall.prx` files along with the `EBOOT` in a `me` folder inside your `GAME` folder before launching.
3. Launch the homebrew.
4. Use the directional arrow keys to move the canvas.

## Special Thanks To
- The PSP homebrew community, for being an invaluable source of knowledge.
- All developers and contributors who have supported and continue to support the scene.

# resources:
- [psp wiki on PSDevWiki](https://www.psdevwiki.com/psp/)
- [pspdev on GitHub](https://github.com/pspdev)

*m-c/d*
