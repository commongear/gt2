// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#include <iomanip>
#include <iostream>
#include <regex>

#include "car.h"
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
    const std::string full_name = path + std::string(f.name());
    if (std::regex_match(full_name, regex)) {
      if (kAutoUnpackGz && EndsWith(full_name, ".gz")) {
        // Extract GZipped files.
        StringInStream zipped(f.ReadContents(s));
        const GzipMember z = GzipMember::FromStream(zipped);

        // Drop the '.gz' from the name, then save.
        std::string out_full_name = out_path + full_name;
        out_full_name.resize(out_full_name.size() - 3);
        Save(z.inflated, out_full_name);

        std::cout << "Unzipped and wrote " << out_full_name << std::endl;
      } else {
        // Write other files.
        const std::string out_full_name = out_path + full_name;
        Save(f.ReadContents(s), out_full_name);
        std::cout << "Wrote " << out_full_name << std::endl;
      }
    }
  }
}

// Pring what we understand about the file structure.
void InspectFiles(FileInStream& s, const Vol& vol, const std::string& pattern) {
  const std::regex regex(pattern);
  for (const auto& f : vol.files) {
    const std::string path = vol.PathOf(f);

    std::string full_name = path + std::string(f.name());
    if (std::regex_match(full_name, regex)) {
      std::cout << std::left << std::setw(12) << path << f << std::endl;

      // This is what's in the file.
      StringInStream file;

      // Extract GZ or read bare files.
      if (EndsWith(full_name, ".gz")) {
        // Extract GZipped files.
        StringInStream zipped(f.ReadContents(s));
        GzipMember z = GzipMember::FromStream(zipped);
        file = StringInStream(std::move(z.inflated));

        // Drop the '.gz' from the name, then save.
        full_name.resize(full_name.size() - 3);

        std::cout << "Unzipped " << full_name << std::endl;
      } else {
        file = StringInStream(f.ReadContents(s));
        std::cout << "Read " << full_name << std::endl;
      }

      // Print information about 
      if (EndsWith(full_name, ".cdo") || EndsWith(full_name, ".cno")) {
        const CarObject c = CarObject::FromStream(file);
        std::cout << c << std::endl;
      } else if (EndsWith(full_name, ".cdp") || EndsWith(full_name, ".cdo")) {
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
