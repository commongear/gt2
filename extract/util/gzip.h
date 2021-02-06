// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_GZIP_H_
#define GT2_EXTRACT_GZIP_H_

#include <cstring>
#include <iostream>

#include "io.h"

namespace miniz {
#include "../3p/miniz/miniz.h"
}

namespace gt2 {

// If set, hints that the output file is text.
constexpr uint8_t GZIP_FLAG_TEXT = 0x01;
// 16-bit header CRC is present immediately before the compressed data.
constexpr uint8_t GZIP_FLAG_HCRC = 0x02;
// "Extra" data field present after header.
constexpr uint8_t GZIP_FLAG_EXTRA = 0x04;
// ISO 8859-1 Latin-1 zero-terminated name after any "extra" fields.
constexpr uint8_t GZIP_FLAG_NAME = 0x08;
// ISO 8859-1 Latin-1 zero-terminated comment after the "name" field.
constexpr uint8_t GZIP_FLAG_COMMENT = 0x10;
// Remaining 3 bits reserved.

// OS ID values.
constexpr uint8_t GZIP_OS_FAT = 0;
constexpr uint8_t GZIP_OS_AMIGA = 1;
constexpr uint8_t GZIP_OS_VMS = 2;
constexpr uint8_t GZIP_OS_UNIX = 3;
constexpr uint8_t GZIP_OS_VM = 4;
constexpr uint8_t GZIP_OS_ATARI = 5;
constexpr uint8_t GZIP_OS_HPFS = 6;
constexpr uint8_t GZIP_OS_MACINTOSH = 7;
constexpr uint8_t GZIP_OS_ZSYSTEM = 8;
constexpr uint8_t GZIP_OS_CPM = 9;
constexpr uint8_t GZIP_OS_TOPS20 = 10;
constexpr uint8_t GZIP_OS_NTFS = 11;
constexpr uint8_t GZIP_OS_QDOS = 12;
constexpr uint8_t GZIP_OS_ACORN_RISCOS = 13;
constexpr uint8_t GZIP_OS_UNKNOWN = 255;

// https://www.infinitepartitions.com/art001.html
// GZIP: https://tools.ietf.org/html/rfc1952#page-11
// ZLIB: https://tools.ietf.org/html/rfc1950

// Inflates from a stream until the data end is reached.
template <typename Stream>
std::string Inflate(Stream& s) {
  constexpr int64_t kBufSize = 64 * 1024;

  const int64_t initial_pos = s.pos();
  int64_t remaining = s.remain();

  std::string in(kBufSize, '\0');
  std::string out(kBufSize, '\0');

  miniz::mz_stream mz;
  std::memset(&mz, 0, sizeof(mz));
  mz.next_in = reinterpret_cast<unsigned char*>(in.data());
  mz.avail_in = 0;
  mz.next_out = reinterpret_cast<unsigned char*>(out.data());
  mz.avail_out = kBufSize;

  // No zlib headers (we read them already).
  CHECK_EQ(mz_inflateInit2(&mz, -15), miniz::MZ_OK);
  while (true) {
    if (mz.avail_in == 0) {
      const int64_t to_read = std::min(kBufSize, remaining);
      s.ReadInto(in.data(), to_read);
      in.resize(to_read);
      remaining -= in.size();
      mz.next_in = reinterpret_cast<unsigned char*>(in.data());
      mz.avail_in = in.size();
    }
    const int status = mz_inflate(&mz, miniz::MZ_SYNC_FLUSH);
    if (status == miniz::MZ_STREAM_END) {
      break;
    } else if (mz.avail_out == 0) {
      int64_t old_size = out.size();
      out.resize(2 * old_size);
      mz.next_out = reinterpret_cast<unsigned char*>(out.data() + old_size);
      mz.avail_out = old_size;
    } else if (status != miniz::MZ_OK) {
      std::cerr << status << std::endl;
    }
    CHECK_EQ(status, miniz::MZ_OK);
  }

  // Bookkeeping.
  s.set_pos(initial_pos + mz.total_in);
  out.resize(mz.total_out);
  CHECK_EQ(miniz::mz_inflateEnd(&mz), miniz::MZ_OK);

  return out;
}

// Computes the CRC32 of some data.
uint32_t Crc32(const std::string& data) {
  return miniz::mz_crc32(0, reinterpret_cast<const unsigned char*>(data.data()),
                         data.size());
}

// One member of a GZip file (there may be multiple).
struct GzipMember {
  struct Header {
    uint8_t magic[2] = {0, 0};  // 1f 8b
    uint8_t compression = 0;    // 08 -> deflate. No other values valid.
    uint8_t flags = 0;
    uint32_t modified_time = 0;
    uint8_t compression_flags = 0;  // deflate: 0x02 best, 0x04 fastest.
    uint8_t os_id = 0;
  } __attribute__((packed));
  static_assert(sizeof(Header) == 10);

  struct Footer {
    uint32_t crc = 0;
    uint32_t uncompressed_size = 0;
  } __attribute__((packed));
  static_assert(sizeof(Footer) == 8);

  struct Extra {
    struct Subfield {
      struct Header {
        uint8_t id[2];
        uint16_t data_size;
      } __attribute__((packed));
      static_assert(sizeof(Header) == 4);

      Header header;
      std::vector<uint8_t> data;

      template <typename Stream>
      static Subfield FromStream(Stream& s) {
        Subfield out;
        out.header = s.template Read<Header>();
        out.data = s.template Read<uint8_t>(out.header.data_size);
        return out;
      }
    };

    uint16_t subfield_data_size = 0;
    std::vector<Subfield> subfields;

    template <typename Stream>
    static Extra FromStream(Stream& s) {
      Extra out;
      out.subfield_data_size = s.template Read<uint16_t>();
      const int64_t end = out.subfield_data_size + s.pos();
      while (end < s.pos()) out.subfields.push_back(Subfield::FromStream(s));
      CHECK_EQ(s.pos(), end, "GZip: subfield data was misaligned.");
      return out;
    }
  };

  Header header;
  Extra extra;
  std::string name;
  std::string comment;
  uint16_t header_crc = 0;
  std::string inflated;
  Footer footer;

  template <typename Stream>
  static GzipMember FromStream(Stream& s) {
    GzipMember out;
    out.header = s.template Read<Header>();
    if (out.header.flags & GZIP_FLAG_EXTRA) out.extra = Extra::FromStream(s);
    if (out.header.flags & GZIP_FLAG_NAME) out.name = s.ReadCString();
    if (out.header.flags & GZIP_FLAG_COMMENT) out.comment = s.ReadCString();
    if (out.header.flags & GZIP_FLAG_HCRC) {
      out.header_crc = s.template Read<uint16_t>();
    }
    out.inflated = Inflate(s);
    out.footer = s.template Read<Footer>();
    CHECK_EQ(out.footer.crc, Crc32(out.inflated));
    return out;
  }
};

std::ostream& operator<<(std::ostream& os, const GzipMember::Header& h) {
  return os << "{" << ToHex<2>(h.magic)
            << "compression:" << ToHex(h.compression)
            << " flags:" << ToHex(h.flags) << " mtime:" << h.modified_time
            << " cflgs:" << ToHex(h.compression_flags)
            << " os:" << ToHex(h.os_id) << "}";
}

std::ostream& operator<<(std::ostream& os, const GzipMember::Footer& f) {
  return os << "{crc:" << f.crc << " uncompressed_size:" << f.uncompressed_size
            << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const GzipMember::Extra::Subfield::Header& h) {
  return os << "{" << ToHex<2>(h.id) << "size:" << h.data_size << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const GzipMember::Extra::Subfield& f) {
  return os << f.header << ToString(f.data);
}

std::ostream& operator<<(std::ostream& os, const GzipMember::Extra& e) {
  for (const auto& s : e.subfields) os << s;
  return os;
}

std::ostream& operator<<(std::ostream& os, const GzipMember& m) {
  os << "GzipMember: " << m.header << "\n";
  if (m.header.flags & GZIP_FLAG_EXTRA) os << m.extra << "\n";
  if (m.header.flags & GZIP_FLAG_NAME) os << "name: '" << m.name << "'\n";
  if (m.header.flags & GZIP_FLAG_COMMENT)
    os << "comment: '" << m.comment << "'\n";
  if (m.header.flags & GZIP_FLAG_HCRC)
    os << "header_crc: " << m.header_crc << "\n";
  return os << "inflated_size: " << m.inflated.size() << "\n" << m.footer;
}

}  // namespace gt2

#endif  // GT2_EXTRACT_GZIP_H_
