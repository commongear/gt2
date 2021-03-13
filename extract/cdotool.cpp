// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#define STBI_ASSERT(x) CHECK(x)

#include "car.h"
#include "car_from_obj.h"
#include "car_to_obj.h"
#include "util/color.h"
#include "util/io.h"
#include "util/obj.h"

namespace miniz {
#include "3p/miniz/miniz.c"
}

#define STB_IMAGE_IMPLEMENTATION
#include "3p/stb/stb_image.h"

using namespace gt2;

static constexpr char kUsage[] =
    "Usage:  cdotool command [args...]\n"
    "  command:      [getobjs, getobjs-nowheels, packcdo, packcno]\n"
    "                details below\n"
    "  args...:      command arguments; details below\n"
    "\n"
    "Commands:\n"
    "  getobjs path-to-cdo output-path\n"
    "    Converts a CDO/CNO to several OBJs (one for each LOD), MTL and PNGs.\n"
    "    path-to-cdo:  an extracted CDO or CNO file\n"
    "    output-path:  folder in which to store the OBJ files\n"
    "  getobjs-nowheels path-to-cdo output-path\n"
    "    Same as above, but doesn't build wheels for the model.\n"
    "  packcdo path-to-base-cdo path-to-obj output-path\n"
    "    Converts an OBJ and supporting files to a CDO/CDP\n"
    "    path-to-base-cdo:  A CDO or CNO file to use as the base. There are\n"
    "                       several unknown fields in CDO format, so a valid\n"
    "                       base file is required. This can be one extracted\n"
    "                       from the VOL using voltool, for instance.\n"
    "    path-to-obj:       An OBJ file and supporting files to convert.\n"
    "    output-path:       Folder in which to store the CDO/CDP files.\n"
    "  packcno path-to-base-cdo path-to-obj output-path\n"
    "    Same as above, but outputs a .CNO file.\n";

// Prints the usage message.
void PrintUsage() { std::cerr << kUsage << std::endl; }

// Converts a CDO/CDP to OBJ.
void GetObjs(std::string cdo_path, std::string out_path, bool make_wheels) {
  CHECK(EndsWith(cdo_path, ".cdo") || EndsWith(cdo_path, ".cno"),
        "Input must be a .cdo/.cno file: ", cdo_path);

  // Make a path for the texture.
  const std::string base_path = cdo_path.substr(0, cdo_path.size() - 1);
  const std::string cdp_path = base_path + "p";

  CHECK(std::filesystem::exists(cdo_path), cdo_path);
  CHECK(std::filesystem::exists(cdp_path), cdp_path);

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

// Converts a set of OBJs and PNGs to CDO/CDP.
void PackCdo(const std::string& base_cdo_path, std::string obj_path,
             std::string out_path, bool is_day) {
  CHECK(EndsWith(base_cdo_path, ".cdo") || EndsWith(base_cdo_path, ".cno"),
        "Base must be a .cdo/.cno file: ", base_cdo_path);
  CHECK(EndsWith(obj_path, ".obj"), "Input must be a .obj file: ", obj_path);

  // Sanitize the output path.
  out_path = std::filesystem::path(out_path).parent_path().generic_string();
  std::cout << "PARENT PATH " << out_path << std::endl;

  // Create paths for everything.
  const std::filesystem::path obj_fspath(obj_path);
  const std::string obj_stem = obj_fspath.stem().stem().generic_string();
  const std::string obj_name =
      (obj_fspath.has_parent_path()
           ? obj_fspath.parent_path().generic_string() + "/"
           : "") +
      obj_stem;

  std::string tex_path = obj_name + ".0.png";
  if (EndsWith(obj_name, ".cdo") || EndsWith(obj_name, ".cno")) {
    tex_path = obj_name.substr(0, obj_name.size() - 1) + "p.0.png";
  }

  // Find the input files.
  CHECK(std::filesystem::exists(base_cdo_path), "Not found:", base_cdo_path);
  CHECK(std::filesystem::exists(obj_path), "Not found:", obj_path);
  CHECK(std::filesystem::exists(tex_path), "Not found:", tex_path);

  // Create paths for each LOD.
  std::vector<std::string> obj_paths = {obj_path};
  for (int i = 1; i <= 2; ++i) {
    std::stringstream s;
    s << obj_name << "." << i << ".obj";
    const std::string lod_path = s.str();
    if (std::filesystem::exists(lod_path)) {
      obj_paths.push_back(lod_path);
    } else {
      std::cout << "LOD " << i << " file doesn't exist: '" << lod_path
                << "'" << std::endl;
    }
  }

  // Read the base object.
  FileInStream base_cdo_file(base_cdo_path);
  CarObject cdo = CarObject::FromStream(base_cdo_file);
  CHECK_GT(cdo.lods.size(), 0);

  // Read the texture.
  const std::string png_data = Load(tex_path);
  Image texture = Image8::FromPng(png_data);
  std::cout << "Loaded " << tex_path << " " << texture.width << " x "
            << texture.height << "\n";

  // Init the CDP with a cleared palette and 8bpp data.
  CarPix cdp = InitCarPix();
  CHECK_EQ(cdp.header.num_palettes, 1);
  CHECK_EQ(cdp.palettes.size(), 1);

  // This is the data we'll eventually store in the CDP.
  Image8 color_index(texture.width, texture.height, 1);
  Image8 color_mask(texture.width, texture.height, 1);

  // Extract and update the palette and data from the wheel area (48 x 48 px).
  TexturePaletteData wheel_texpal = ExtractWheelPalette(texture);
  CHECK_EQ(wheel_texpal.palettes.size(), 1);
  QuantizeColors(wheel_texpal.palettes[0].colors, /*max_colors=*/16);

  // Update the 0th sub-palette of the 0th palette in the data for the wheel.
  UpdateCarPixSubPalettes(wheel_texpal.palettes, /*first_palette_index=*/0,
                          cdp.palettes[0]);
  UpdateCarPixColorIndex(texture, wheel_texpal, color_index, color_mask);

  // TODO(commongear): read the brake light texture and palette.

  // TODO(commongear): what's in the unknown areas?
  for (auto& x : cdo.padding) x = 0;
  for (auto& x : cdo.unknown1) x = 0;

  // Load the OBJ for each LOD.
  int first_palette_index = 3;
  for (int i = 0; i < 3; ++i) {
    // If there is no OBJ for this lod, clear it.
    // We'd like to copy, but CDOs have a 20K limit.
    if (i >= obj_paths.size()) {
      std::cout << "No LOD " << i << " obj found. Skipping." << std::endl;
      cdo.lods[i] = Model();
      continue;
    }

    // Read the OBJ.
    const std::string obj_data = Load(obj_paths[i]);
    Obj obj = Obj::FromString(obj_data);
    std::cout << "Loaded " << obj_path << "\n";
    std::cout << " verts " << obj.verts.size() << "\n";
    std::cout << " norms " << obj.normals.size() << "\n";
    std::cout << "   uvs " << obj.uvs.size() << "\n";
    std::cout << " faces " << obj.faces.size() << std::endl;

    // Check the OBJ.
    CHECK_LE(obj.verts.size(), 256, "CDO only supports 255 verts.");
    CHECK_LE(obj.normals.size(), 512, "CDO only supports 512 normals.");

    // Reverse the faces, to get the original CDO rendering order.
    std::reverse(obj.faces.begin(), obj.faces.end());

    // Convert to CDO LOD.
    Model& m = cdo.lods[i];

    // TODO(commongear): we don't know what these fields are in the model, but
    // leaving them nonzero sometimes prevents loading the model into a race.
    // They probably have something to do with the faces, but zeroing them seems
    // to at least be safe-ish...
    for (int i = 0; i < 44; ++i) m.header.unknown3[i] = 0;
    m.header.unknown4 = 0;
    m.header.unknown5 = 0;

    UpdateFromObj(obj, m);
    std::cout << "Converted to CDO LOD " << i << "\n" << m.header << std::endl;

    // The 0-th LOD has 12 palettes; the other LODs have 1.
    const int max_palettes = (i == 0 ? 12 : 1);

    // Extract and quantize the palette from the texture.
    TexturePaletteData texpal = ExtractFacePalettes(texture, m);
    MergePalettes(texpal.palettes, max_palettes, /*max_colors=*/16);
    CHECK_LE(texpal.palettes.size(), max_palettes);

    // Store palette indices back in each LOD.
    AssignPaletteIndicesToFaces(texpal.palettes, first_palette_index, m);

    // Update the CDP (data and palette) with the texture for this LOD.
    UpdateCarPixSubPalettes(texpal.palettes, first_palette_index,
                            cdp.palettes[0]);
    UpdateCarPixColorIndex(texture, texpal, color_index, color_mask);

    // Each successive LOD palette starts one index lower.
    --first_palette_index;
  }
  color_index.GrowBorders(color_mask);
  PackCarPixData(color_index, cdp);
  // PackCarPixTo4bpp(cdp);

  {  // Save the CDP file.
    VecOutStream cdp_data;
    cdp.Serialize(cdp_data);

    std::string out_cdp_path = out_path + obj_stem;
    if (EndsWith(out_cdp_path, ".cdo") || EndsWith(out_cdp_path, ".cno")) {
      out_cdp_path.resize(out_cdp_path.size() - 4);
    }
    out_cdp_path += (is_day ? ".cdp" : ".cnp");
    Save(cdp_data.GetData(), out_cdp_path);
  }

  {  // Save the CDO file.
    VecOutStream cdo_data;
    cdo.Serialize(cdo_data);
    std::string out_cdo_path = out_path + obj_stem;
    if (is_day) {
      if (!EndsWith(out_cdo_path, ".cdo")) out_cdo_path += ".cdo";
    } else {
      if (!EndsWith(out_cdo_path, ".cno")) out_cdo_path += ".cno";
    }
    const auto cdo_data_str = cdo_data.GetData();
    Save(cdo_data_str, out_cdo_path);
    CHECK_LE(cdo_data_str.size(), 20480,
             "Output CDO is too large; may crash when loaded into a race.");
  }
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
  } else if (command == "packcdo") {
    if (argc != 5) {
      std::cerr << "Need base CDO, OBJ, and an output path.\n" << std::endl;
      PrintUsage();
      return -1;
    }
    PackCdo(argv[2], argv[3], argv[4], /*is_day=*/true);
  } else {
    std::cerr << "Unknown command: " << command << std::endl;
    PrintUsage();
    return -1;
  }

  return 0;
}
