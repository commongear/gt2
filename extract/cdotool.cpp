// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#include <filesystem>
#include <iostream>

#define STBI_ASSERT(x) CHECK(x)

#include "car.h"
#include "car_to_obj.h"
#include "util/io.h"

namespace miniz {
#include "3p/miniz/miniz.c"
}

#define STB_IMAGE_IMPLEMENTATION
#include "3p/stb/stb_image.h"

using namespace gt2;

static constexpr char kUsage[] =
    "Usage:  cdotool command [args...]\n"
    "  command:      [getobjs, getobjs-nowheels] details below\n"
    "  args...:      command arguments; details below\n"
    "\n"
    "Commands:\n"
    "  getobjs path-to-cdo output-path\n"
    "    Converts a CDO/CNO to several OBJs (one for each LOD), an MTL and "
    "PNGs.\n"
    "    path-to-cdo:  an extracted CDO or CNO file\n"
    "    output-path:  folder in which to store the OBJ files\n"
    "  getobjs-nowheels path-to-cdo output-path\n"
    "    Same as above, but doesn't build wheels for the model.";

// Prints the usage message.
void PrintUsage() { std::cerr << kUsage << std::endl; }

// Converts a CDO/CDP to OBJ.
void GetObjs(std::string cdo_path, std::string out_path, bool make_wheels) {
  CHECK(EndsWith(cdo_path, ".cdo") || EndsWith(cdo_path, ".cno"),
        "Input must be a .cdo/.cno file: ", cdo_path);

  // Make a path for the texture.
  const std::string base_path = cdo_path.substr(0, cdo_path.size() - 1);
  const std::string cdp_path = base_path + "p";

  // Read the object and texture data.
  FileInStream cdo_file(cdo_path);
  FileInStream cdp_file(cdp_path);

  // Parse the files.
  const CarObject cdo = CarObject::FromStream(cdo_file);
  const CarPix cdp = CarPix::FromStream(cdp_file);

  // Craft the output file base name.
  const std::string out_name =
      std::filesystem::path(base_path).filename().generic_string();

  // Write the OBJ data to the output path.
  SaveObj(cdo, cdp, out_path, out_name, make_wheels);
  std::cout << "Saved OBJ " << out_path + out_name + "..." << std::endl;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Need a command to execute.\n" << std::endl;
    PrintUsage();
    return -1;
  }

  const std::string command(argv[1]);
  if (command == "getobjs") {
    if (argc != 4) {
      std::cerr << "Need a CDO and an output path.\n" << std::endl;
      PrintUsage();
      return -1;
    }
    GetObjs(argv[2], argv[3], /*make_wheels=*/true);
  } else if (command == "getobjs-nowheels") {
    if (argc != 4) {
      std::cerr << "Need a CDO and an output path.\n" << std::endl;
      PrintUsage();
      return -1;
    }
    GetObjs(argv[2], argv[3], /*make_wheels=*/false);
  } else {
    std::cerr << "Unknown command: " << command << std::endl;
    PrintUsage();
    return -1;
  }

  return 0;
}
