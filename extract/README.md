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
[car_obj.h](car_obj.h) for practical fixups and a method for rendering as OBJ).
- Extraction to OBJ
- Some data in the files is still a mystery to me. If you know something that's
not implemented, perhaps open an issue?

## Broken things

There's probably more, but here's what I know about:

- No other formats are supported yet. (I'm working on `.tim`s and I have a
partial understanding of how the sky backgrounds work).
- Some cars have slightly wrong-looking wheel positions.
- Rare: decals occasionally flicker.
- I completely made up the geometry for wheels ;-)

I've begun annotating the repo with tags in comments:

- `TODO` something left to implement
- `UNKNOWN` usually data, is something of a mystery
- `NOT_VERIFIED` if it *might* work, but I haven't exhaustively checked.
