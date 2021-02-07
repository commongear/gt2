This is a tool to inspect and extract files in the GT2 VOL format. I've tried to
make the code as easy to understand as possible, so others may use similar
techniques to enhance their own projects. Everything in here is a
work-in-progress, which will continue as long as I find it fun :-)

## Build System

None! Run `build.sh` or use your favorite compiler to build `voltool.cpp`. As
long as you're on a little-endian system, the code should be cross platform and
standard. Please open an issue if this isn't the case.

## VOL Extraction

The tool is reasonably complete and (I think) correct at listing and extracting
GT2 VOL files. You can list, extract, unzip, and convert to OBJ.

Run `voltool` without arguments to get a basic usage message, or just read the
code to see what it can do.

## CDO/CNO format

- With normals! (see the comments in [car.h](car.h) for the format, and
[car_obj.h](car_obj.h) for practical fixups and guidance for rendering as an
OBJ).
- Extraction to OBJ
- Some data in the files is still a mystery to me. If you know something that's
not implemented, perhaps open an issue?

## Broken things

There's probably more, but here's what I know about:

- No other formats are supported yet. (I'm working on `.tim`s and I have a
partial understanding of how the sky backgrounds work).
- A few cars have the wrong scale. (e.g. some of the Miatas)
- Some cars have slightly wrong-looking wheel positions.
- Rare: decals occasionally flicker.
- Texture extraction is not pixel perfect. Occasionally, the wrong palette is
used when there are nearby UV islands in the texture.
- I completely made up the geometry for wheels ;-)
