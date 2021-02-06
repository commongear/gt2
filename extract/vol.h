#ifndef GT2_EXTRACT_VOL_H_
#define GT2_EXTRACT_VOL_H_

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "util/inspect.h"

namespace gt2 {

constexpr uint8_t kVolFlagDir = 0x01;  // File is a directory.
constexpr uint8_t kVolFlagEnd = 0x80;  // File is the last of its directory.

constexpr int64_t kRootFolder = -1;

struct Vol {
  struct Header {
    char magic[8];
    int16_t num_files;
    int16_t num_file_infos;

    bool Verify() const {
      return std::strncmp(magic, "GTFS\0\0\0\0", 8) == 0 && num_files > 0 &&
             num_file_infos > 0 && num_files <= num_file_infos;
    }
  } __attribute__((packed));
  static_assert(sizeof(Header) == 12);

  // Positions and implicit sizes of files in the VOL.
  struct Offset {
    uint32_t value;
    // All file positions are 2048-byte aligned.
    uint32_t pos() const { return value & 0xFFFFF800; }
    // This is the number of bytes to ignore before the next "pos" marker.
    // I.e. the size of a file is (next_pos - pos - pad)
    // The size of the last file is (total_vol_size - pos - pad).
    uint32_t pad() const { return value & 0x7FF; }
  };
  static_assert(sizeof(Offset) == 4);

  // Info about one file.
  struct FileInfo {
    uint32_t datetime;
    uint16_t offset_index;
    uint8_t flags;
    char name_[25];
    std::string_view name() const {
      return std::string_view(name_, strnlen(name_, sizeof(name_)));
    }
  } __attribute__((packed));
  static_assert(sizeof(FileInfo) == 32);

  // Derived: combines information from FileInfo and Offsets.
  struct File : public FileInfo {
    // Index into the vol's list of files. Points to a folder entry.
    int64_t parent = kRootFolder;

    // Byte position and size of the file.
    int64_t pos;
    int64_t size;

    // Name of the file.
    std::string_view name() const {
      return std::string_view(name_, strnlen(name_, sizeof(name_)));
    }

    // Directory flags.
    bool is_dir() const { return flags & kVolFlagDir; }
    bool is_end() const { return flags & kVolFlagEnd; }

    // Reads the contents of the file into the given string.
    template <typename Stream>
    std::string ReadContents(Stream& s) const {
      s.set_pos(pos);
      return s.ReadData(size);
    }
  };

  // Read from the file.
  int64_t total_size;
  Header header;
  std::vector<Offset> offsets;
  std::vector<FileInfo> file_infos;

  // Derived.
  std::vector<File> files;

  // Computes the position of the given file.
  int32_t PositionOf(const FileInfo& f) {
    if (f.offset_index < offsets.size()) {
      return offsets[f.offset_index].pos();
    }
    // NOTE: sounds and replays use an out-of-bounds offset index.
    return 0;
  }

  // Computes the size of the given file.
  int32_t SizeOf(const FileInfo& f) {
    if (f.offset_index + 1 < offsets.size()) {
      return offsets[f.offset_index + 1].pos() - offsets[f.offset_index].value;
    } else if (f.offset_index < offsets.size()) {
      CHECK_LT(total_size, (1llu << 32));
      return total_size - offsets[f.offset_index].value;
    }
    // NOTE: sounds and replays use an out-of-bounds offset index.
    return 0;
  }

  // Returns the path of the given file.
  std::string PathOf(File f) const {
    std::string out;
    while (f.parent != kRootFolder) {
      f = files[f.parent];
      out = std::string(f.name()) + "/" + out;
    }
    return out;
  }

  // Verifies that the offsets are monotonically increasing.
  // If they're not, we've probably read the file wrong.
  static bool VerifyMonotonic(const std::vector<Offset>& v) {
    if (v.empty()) return true;
    const int size = v.size();
    for (int i = 1; i < size; ++i) {
      if (v[i - 1].pos() > v[i].pos()) {
        return false;
      }
    }
    return true;
  }

  template <typename Stream>
  static Vol FromStream(Stream& s) {
    const int64_t init_pos = s.pos();

    Vol out;
    out.total_size = s.remain();

    out.header = s.template Read<Header>();
    CHECK(out.header.Verify());

    s.set_pos(init_pos + 0x10);
    out.offsets = s.template Read<Offset>(out.header.num_files);
    CHECK(VerifyMonotonic(out.offsets));

    // Check that the number of offsets read was the right number.
    const double num_offsets = static_cast<double>(out.offsets[1].pos() - 0x10 -
                                                   out.offsets[0].pad()) /
                               sizeof(Offset);
    CHECK_EQ(num_offsets, out.header.num_files);

    // Check that the number of file info in 'offsets' matches the header.
    const double num_file_infos =
        static_cast<double>(out.offsets[2].pos() - out.offsets[1].pos() -
                            out.offsets[1].pad()) /
        sizeof(FileInfo);
    CHECK_EQ(num_file_infos, out.header.num_file_infos);

    s.set_pos(init_pos + out.offsets[1].pos());
    out.file_infos = s.template Read<FileInfo>(out.header.num_file_infos);

    // Data structures for reading folder hierarchy.
    struct Range {
      int64_t begin, current, end;
    };
    std::vector<int64_t> dirs = {-1};
    std::vector<Range> stack = {{.begin = 0, .current = 0, .end = 1}};
    int64_t next_begin = 1;

    // Read the folder hierarchy and decode offsets into pos, size.
    out.files.reserve(out.file_infos.size());
    for (int i = 0; i < num_file_infos; ++i) {
      const FileInfo& info = out.file_infos[i];

      // Copy the common part of FileInfo.
      out.files.push_back({});
      File& f = out.files.back();
      std::memcpy(&f, &info, sizeof(info));
      static_assert(sizeof(info) == sizeof(FileInfo), "");
      CHECK(!stack.empty());

      // Set derived fields.
      f.parent = dirs[stack.back().current];
      f.pos = out.PositionOf(info);
      f.size = out.SizeOf(info);

      // Keep track of each folder.
      if (f.is_dir() && f.name() != "..") dirs.push_back(i);

      // At the end of a folder, start reading the next one.
      if (f.is_end()) {
        // We're done with this folder.
        ++stack.back().current;
        if (dirs.size() != next_begin) {
          // Added children to the last folder. They come next in the file.
          stack.push_back(
              {.begin = next_begin, .current = next_begin, .end = dirs.size()});
        } else {
          // No children to parse. Go back to where we left off.
          while (stack.back().current == stack.back().end) {
            CHECK_EQ(stack.back().end, dirs.size());
            dirs.resize(stack.back().begin);
            stack.pop_back();
          }
        }
        next_begin = dirs.size();
      }
    }
    CHECK(stack.empty());
    CHECK(s.ok());
    return out;
  }
};

std::ostream& operator<<(std::ostream& os, const Vol::Header& h) {
  return os << "{" << h.magic << " files:" << h.num_files
            << " file-infos:" << h.num_file_infos << "}";
}

std::ostream& operator<<(std::ostream& s, const Vol::Offset& o) {
  return s << o.pos() << ":" << o.pad();
}

std::ostream& operator<<(std::ostream& os, const Vol::FileInfo& f) {
  return os << " " << std::left << std::setw(25) << f.name() << " "
            << std::right << std::setw(5) << f.offset_index << " "
            << std::setw(10) << f.datetime << " " << ToHex(f.flags);
}

std::ostream& operator<<(std::ostream& os, const Vol::File& f) {
  return os << " " << std::left << std::setw(26) << f.name()
            << (f.is_dir() ? "d" : " ") << (f.is_end() ? "e" : " ") << " "
            << ToHex(f.flags) << " " << std::right << std::setw(10)
            << f.datetime << " " << std::setw(8) << f.size;
}

}  // namespace gt2

#endif  // GT2_EXTRACT_VOL_H_
