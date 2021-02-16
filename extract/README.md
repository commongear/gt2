This is a tool to inspect and extract files in the GT2 VOL format. I've tried to
make the code as easy to understand as possible, so others may use similar
techniques to enhance their own projects. Everything in here is a
work-in-progress, which will continue as long as I find it fun :-)

# Quickstart

1. Grab a Windows release from Github and extract it.
2. Open `cmd.exe` and `cd` to where you extracted the release.
3. `cd extract` and run `voltool.exe` to get usage instructions.
4. You'll need a GT2 VOL file to extract from. To extract your first model,
`voltool.exe [path-to-your-VOL] getobjs ../view/models '.*tsgtr.*'`
5. Follow the instructions to run the [OBJ viewer](../view/).

# Building

The code simple to build and has no external dependencies. Just clone the repo,
navigate to this folder (`gt2/extract`) and run `./build_voltool.sh`.

As long as you're on a little-endian system, the code should be cross platform
and standard. Please open an issue if this isn't the case.

### Windows

I recommend using MSYS2. Follow the directions at
[http://www.msys2.org](http://www.msys2.org), including the installation
instructions for `mingw-w64`, which you'll need. In the start menu, you should
find `MSYS2 MinGW 64-bit`. Run it to open a blue terminal. You'll then need to
install clang `pacman -S mingw-w64-x86_64-clang`. Then navigate to `gt2/extract`
and run `./build_voltool.sh`.

### Linux & OSX

Ensure `clang++` is installed on your machine. You can open a terminal and type
`clang++ -v` and it returns something, you're probably good. If not, you'll need
to look up a tutorial for installing it. Once done, Navigate to `gt2/extract`
and run `./build_voltool.sh`. You may need to `chmod +x build_voltool.sh` first.

# VOL Extraction

The tool is reasonably complete and (I think) correct at listing and extracting
GT2 VOL files. You can list, extract, unzip, and convert to OBJ.

Run `voltool` without arguments to get a basic usage message, or just read the
code to see what it can do.

# CDO/CNO format

- With normals! (see the comments in [car.h](car.h) for the format, and
[car_obj.h](car_obj.h) for practical fixups and a method for rendering as OBJ).
- Extraction to OBJ
- Some data in the files is still a mystery to me. If you know something that's
not implemented, perhaps open an issue?

# Broken things

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
