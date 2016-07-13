BPG Image library and utilities
-------------------------------

1) Quick introduction
---------------------

- Edit the Makefile to change the compile options (the default compile
  options should be OK for Linux). Type 'make' to compile and 'make
  install' to install the compiled binaries.

- bpgview: in order to compile it you need to install the SDL and
  SDL_image libraries.

- Emscripten usage: in order to generate the Javascript decoder, you
  must install Emscripten and enable its use in the Makefile.

- An HTML demonstration (with a precompiled Javascript decoder) is
  available in html/index.html (if you use Chrome and want to use
  file:// to access it, launch Chrome with the option
  --allow-file-access-from-files). 

- The BPG file format is specified in doc/bpg_spec.txt.

2) Compilation and Installation Notes
-------------------------------------

2.1) Linux
----------

  - Edit the Makefile to change the compile options (the default
  compile options should be OK). Type 'make' to compile and 'make
  install' to install the compiled binaries.
 
  - Use 'make -j N' where N is the number of CPU cores to compile faster.

  - The following packages must be installed: SDL-devel
  SDL_image-devel yasm. It is recommended to use yasm version >= 1.3.0
  to have a faster compilation.
 
  - Only a 64 bit target is supported because x265 needs it for bit
    depths > 8.

2.2) Windows
------------

  - Only cross-compilation from Linux is supported.

  - The following packages need to be installed: mingw64-gcc
    mingw64-libpng mingw64-libjpeg-turbo mingw64-SDL mingw64-SDL_image
    yasm. It is recommended to use yasm version >= 1.3.0 to have a
    faster compilation.

  - Only a 64 bit target is supported because x265 needs it for bit
    depths > 8.

3) BPG encoder
--------------

The BPG command line encoder is 'bpgenc'. It takes JPEG or PNG images
as input.

- Speed: by default bpgenc uses the x265. You can compile the much
  slower but more efficient JCTVC encoder and select it with the '-e
  jctvc' option. With x265 you can select the encoding speed with the
  '-m' option (1 = fast, but larger image, 9 = slower but smaller
  image).

- Bit depth: the default bit depth is 8. You can increase it to 10
  ('-b 10' option) to slightly increase the compression ratio. For web
  publishing it is generally not a good idea because the Javascript
  decoder uses more memory. The compiled x265 encoder supports the bit
  depth of 8, 10 and 12. The slower JCTVC encoder can be compiled to
  support higher bit depths (up to 14) by enabling the Makefile
  define: USE_JCTVC_HIGH_BIT_DEPTH.

- Lossless compression is supported as a bonus thru the HEVC lossless
  capabilities. Use a PNG input in this case unless you know what you
  do ! In case of a JPEG input, the compression is lossless related to
  the JPEG YCbCr data, not the RGB data. In any case, the bit depth
  should match the one of your picture otherwise the file size
  increases a lot. By default the lossless mode sets the bit depth to
  8 bits. The prefered color space is set to "rgb". Notes: 
  
    - lossless mode is less tested that the lossy mode but it usually
      gives better results that PNG on photographic images.

    - the JCTVC encoder gives smaller images than the x265 encoder
      with lossless compression.

- There is a small difference of interpretation of the quantizer
  parameter (-q option) between the x265 and JCTVC encoder.

- Color space and chroma format:

    * For JPEG input, the color space of the input image is not
      modified (it is YCbCr, RGB, YCbCrK or CMYK). The chroma is
      subsampled according to the preferred chroma format ('-f'
      option).

    * For PNG input, the input image is converted to the preferred
      color space ('-c' option). Its chroma is then subsampled
      according to the preferred chroma format.

    * grayscale images are kept unmodified.

- Premultiplied alpha: by default bpgenc uses non-premultiplied alpha
  to preserve the color components. However, premultiplied alpha
  ('-premul' option) usually gives a better compression at the expense
  of a loss in the color components. This loss is not an issue if the
  image is not edited.

- Animations: with the '-a' option, animations can be encoded from a
  sequence of PNG or JPEG images, indexed from 1 or 0. For example:

  ./bpgenc -a anim%2d.png -fps 25 -loop 0 -o anim.bpg

  generates an animation from anim01.png, anim02.png, etc... The frame
  rate is specified with '-fps' and the number of loops with '-loop'
  (0 = infinite). If a different delay per image is needed as in some
  animated GIFs, a text file can be specified with the '-delayfile'
  option. It contains one number per image giving its duration in
  centiseconds. All durations are rounded to a multiple of '1/fps', so
  it is important to set a consistent frame rate.
  
  The necessary frames and delay file can be generated from animated
  GIFs with the ImageMagick tools:
  
  convert -coalesce anim.gif anim%d.png
 
  identify -format "%T\n" anim.gif > anim.txt
  
  In order to reduce the file size, the frame rate can be choosen so
  that most frames have a frame period of 1 (hence if anim.txt
  contains only frame durations of 5 centiseconds, then choose a frame
  rate of 20 frames/s).

  As GIFs use paletted colors and 1 bit transparency, it is always
  better to start from the source material (e.g. PNG files) to have
  the best quality.

  A BPG decoder not supporting animations only displays the first
  frame.

- By default, bpgenc does not copy the metadata. You can copy them
  with the '-keepmetadata' option. For JPEG input, EXIF, ICCP and XMP
  are copied. For PNG input, ICCP is copied.

- Objective comparisons: x265 is tuned by default for SSIM. the JCTVC
  encoder is tuned for PSNR only, not for SSIM, so you should use PSNR
  when making objective comparison with other formats.

4) BPG decoder
--------------

The BPG command line decoder is bpgdec. It outputs a PNG or PPM
image. Use a PPM output to get the fastest speed.

- With the '-i' option, you have information about the BPG image (and
no decoded image is output).

- The '-b' option selects the bit depth (8 or 16) of the PNG
  output. It is independent of the internal BPG bit depth.

5) BPG viewer
-------------

The BPG image viewer uses the SDL library to display BPG images and
other image formats supported by the SDL_image library. The available
keys are displayed by launching bpgview without parameters. bpgview
supports BPG animations.

6) BPG decoding library
-----------------------

BPG images can be decoded in any program with the libbpg
library.

The API is not considered stable yet so that the library is only
provided as a static one.

Currently there is no similar library for encoding so you should
invoke the bpgenc utility.

7) Javascript decoder
---------------------

The following Javascript decoders are available, sorted by increasing size:

             > 8 bits   animations
bpgdec8.js   no         no
bpgdec.js    yes        no
bpgdec8a.js  no         yes


The 8 bit only decoders are a little faster and consumes less memory
(16 MB instead of 32 MB by default, you can change the memory
configuration in the Makefile if you want to handle larger images).

The Javascript decoder substitutes all the <img> tags with a source
having a .bpg extension with a <canvas> tag and decodes the BPG image
into it. Stylesheets are supported (the 'id' and 'class' attributes
are preserved). The 'width' and 'height' attributes are supported only
with pixel units.

The image data is downloaded with the XMLHttpRequest object. So the
BPG images and the BPG Javascript decoder must be in the same domain
unless Cross-Origin Resource Sharing is used.

When animations are displayed, all the frames are stored in memory, so
animations with a large number of frames and large resolutions should
be avoided, as with animated GIFs.

asm.js gives an interesting speed boost, so we hope that more browsers
will support this Javascript subset.

8) FFmpeg modifications
-----------------------

- Completed support of chroma_format_idc = 0 (monochrome mode).

- Fixed RDPCM support (intra predictions).

- Reduced memory usage for the SAO loop filter.

- Generated the IDCT coefficients dynamically to reduce the code size.

- Added a 'dynamic bit depth' mode where all the bit depths from 8 to
  14 are supported without code duplication but slower decoding.

- Added a modified SPS header to reduce the size of the BPG decoder
  (an alternate solution is to generate standard VPS and SPS headers
  from the BPG header).

- Added defines to keep only the HEVC intra code and suppress the
  parsing of all the irrelevant NAL units.

- Stripped FFmpeg from all codecs except HEVC and the necessary
  support code.

9) x265 modifications
---------------------

- Support of monochrome format (some parts not used by BPG may be
  missing).

- Support of static build.

10) Licensing
-------------

- libbpg and bpgdec are released under the LGPL license (the FFmpeg
  part is under the LGPL, the BPG specific part is released under the
  BSD license).

- bpgenc is released under the GPL version 2 license. The BPG specific
  code is released under the BSD license. The JCTVC code is released
  under the BSD license. The x265 code is released under the GPL
  version 2 license.

- BPG relies on the HEVC compression technology which may be protected
  by patents in some countries. Most devices already include or will
  include hardware HEVC support, so we suggest to use it if patents
  are an issue.
