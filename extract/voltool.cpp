// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#include <iomanip>
#include <iostream>
#include <regex>

#include "car.h"
#include "car_obj.h"
#include "util/gzip.h"
#include "util/io.h"
#include "vol.h"

namespace miniz {
#include "3p/miniz/miniz.c"
}

using namespace gt2;

// If 'true', GZip files will be unpacked when extracting from the vol.
constexpr bool kAutoUnpackGz = true;

// Prints the usage message.
void PrintUsage() {
  std::cerr << "Usage:  vol path-to-vol command [regex-pattern]" << std::endl;
}

// Extracts the contents of an optionally zipped file.
std::string GetFileContents(FileInStream& s, const Vol::File& f, bool unzip) {
  if (unzip) {
    StringInStream file(f.ReadContents(s));
    return GzipMember::FromStream(file).inflated;
  } else {
    return f.ReadContents(s);
  }
}

// Scans the index for the file with the given path.
const Vol::File* FindFile(const Vol& v, std::string_view path) {
  for (const auto& f : v.files) {
    if (StrCat(v.PathOf(f), f.name()) == path) return &f;
  }
  return nullptr;
}

// List dirs from the VOL.
void ListDirs(const Vol& vol) {
  for (const auto& f : vol.files) {
    if (f.is_dir()) {
      const std::string path = vol.PathOf(f);
      std::cout << std::left << std::setw(12) << path << f << std::endl;
    }
  }
}

// List files from the VOL.
void ListFiles(const Vol& vol, const std::string& pattern) {
  const std::regex regex(pattern);
  for (const auto& f : vol.files) {
    const std::string path = vol.PathOf(f);
    const std::string full_name = path + std::string(f.name());
    if (std::regex_match(full_name, regex)) {
      std::cout << std::left << std::setw(12) << path << f << std::endl;
    }
  }
}

// Read individual file from the VOL and unpack them.
void GetFiles(FileInStream& s, const Vol& vol, const std::string& out_path,
              const std::string& pattern) {
  const std::regex regex(pattern);
  for (const auto& f : vol.files) {
    const std::string path = vol.PathOf(f);
    std::string full_name = path + std::string(f.name());
    if (std::regex_match(full_name, regex)) {
      const bool unzip = kAutoUnpackGz && EndsWith(full_name, ".gz");
      if (unzip) full_name.resize(full_name.size() - 3);
      Save(GetFileContents(s, f, unzip), out_path + full_name);
      if (unzip) {
        std::cout << "Unzipped and wrote " << full_name << std::endl;
      } else {
        std::cout << "Wrote " << full_name << std::endl;
      }
    }
  }
}

// Extract OBJs for all known model types.
void GetObjs(FileInStream& s, const Vol& vol, const std::string& out_path,
             const std::string& pattern) {
  const std::regex regex(pattern);
  for (const auto& f : vol.files) {
    const std::string path = vol.PathOf(f);

    std::string full_name = path + std::string(f.name());
    if (std::regex_match(full_name, regex)) {
      const bool unzip = EndsWith(full_name, ".gz");
      if (unzip) full_name.resize(full_name.size() - 3);

      // Extract cars.
      if (EndsWith(full_name, ".cdo") || EndsWith(full_name, ".cno")) {
        full_name.resize(full_name.size() - 1);  // Drop the 'o'.

        // Find the texture.
        const Vol::File* f_pix = FindFile(vol, full_name + "p.gz");
        if (!f_pix) {
          std::cerr << "Failed to find pix file for " << full_name << std::endl;
          continue;
        }

        // Read the object and texture data.
        StringInStream cdo_file(GetFileContents(s, f, unzip));
        StringInStream cdp_file(GetFileContents(s, *f_pix, unzip));

        // Parse the files.
        const CarObject cdo = CarObject::FromStream(cdo_file);
        const CarPix cdp = CarPix::FromStream(cdp_file);

        // Craft the output file base name.
        const std::string out_name =
            std::filesystem::path(full_name).filename().generic_string();

        // Write the OBJ data to the output path.
        SaveObj(cdo, cdp, out_path, out_name);
        std::cout << "Saved OBJ " << out_path + out_name + "..." << std::endl;
      } else if (EndsWith(full_name, ".cdp") || EndsWith(full_name, ".cnp")) {
        std::cout << "Use the .cdo/.cno filename to extract cars: " << full_name
                  << std::endl;
      } else {
        std::cout << "Can't convert " << full_name << std::endl;
      }
    }
  }
}

// Print what we understand about the file structure.
void InspectFiles(FileInStream& s, const Vol& vol, const std::string& pattern) {
  const std::regex regex(pattern);
  for (const auto& f : vol.files) {
    const std::string path = vol.PathOf(f);

    std::string full_name = path + std::string(f.name());
    if (std::regex_match(full_name, regex)) {
      std::cout << std::left << std::setw(12) << path << f << std::endl;
      const bool unzip = EndsWith(full_name, ".gz");
      if (unzip) full_name.resize(full_name.size() - 3);

      // Print information about known files.
      if (EndsWith(full_name, ".cdo") || EndsWith(full_name, ".cno")) {
        auto file = StringInStream(GetFileContents(s, f, unzip));
        const CarObject c = CarObject::FromStream(file);
        std::cout << c << std::endl;
      } else if (EndsWith(full_name, ".cdp") || EndsWith(full_name, ".cnp")) {
        auto file = StringInStream(GetFileContents(s, f, unzip));
        const CarPix c = CarPix::FromStream(file);
        std::cout << c << std::endl;
      } else {
        std::cout << "We don't know much about this file yet..." << std::endl;
      }
    }
  }
}

int main(int argc, char** argv) {
  if (argc <= 1) {
    std::cerr << "\nNeed a vol file to open.\n" << std::endl;
    PrintUsage();
    return -1;
  }

  // Read the VOL.
  FileInStream s(argv[1]);
  CHECK(s.ok(), "Failed to open '", argv[1], "'");
  const Vol vol = Vol::FromStream(s);
  std::cout << "Read vol file " << vol.total_size << " bytes." << std::endl;

  if (argc <= 2) {
    std::cerr << "\nNeed a command to complete.\n" << std::endl;
    PrintUsage();
    return -1;
  }

  const std::string_view command(argv[2]);
  if (command == "dirs") {
    // List directories in the VOL.
    ListDirs(vol);
  } else if (command == "list") {
    // List files in the VOL.
    const std::string pattern(argc > 3 ? argv[3] : ".*");
    ListFiles(vol, pattern);
  } else if (command == "get") {
    // Pull files out of the VOL.
    if (argc <= 4) {
      std::cerr << "\nNeed output-path and regex-pattern.\n" << std::endl;
      PrintUsage();
      return -1;
    }
    const std::string out_path(argv[3]);
    const std::string pattern(argv[4]);
    GetFiles(s, vol, out_path, pattern);
  } else if (command == "getobjs") {
    if (argc <= 4) {
      std::cerr << "\nNeed output-path and regex-pattern.\n" << std::endl;
      PrintUsage();
      return -1;
    }
    const std::string out_path(argv[3]);
    const std::string pattern(argv[4]);
    GetObjs(s, vol, out_path, pattern);
  } else if (command == "inspect") {
    // Get better information about files.
    if (argc <= 3) {
      std::cerr << "\nNeed a regex-pattern.\n" << std::endl;
      PrintUsage();
      return -1;
    }
    const std::string pattern(argv[3]);
    InspectFiles(s, vol, pattern);
  } else {
    // Fail.
    std::cerr << "Unknown command '" << command << "'" << std::endl;
    PrintUsage();
    return -1;
  }

  return 0;
}
