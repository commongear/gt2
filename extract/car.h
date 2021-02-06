// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_CAR_H_
#define GT2_EXTRACT_CAR_H_

#include <cmath>
#include <ostream>
#include <vector>

#include "util/bitpack.h"
#include "util/image.h"
#include "util/io.h"
#include "util/vec.h"

namespace gt2 {

constexpr float kPif = 3.14159265;

////////////////////////////////////////////////////////////////////////////////
// CDO/CNO (3D OBJECT DATA FILES).
////////////////////////////////////////////////////////////////////////////////

// One solid color face (tri or quad).
struct Face {
  // Vertex indices.
  union {
    uint8_t i_vert[4];
    uint32_t i_vert_data;
  };

  union {
    struct {
      // Lots of packed data here. Bits are described MSB [---] LSB:
      // [3 zeros?] [9 normal-0] [5 FlagsA]
      uint16_t data_a;
      // [4 FlagsB] [12 zeros?]
      uint16_t data_b;
      // [4 zeros?] [9 normal-3] [9 normal-2] [9 normal-1] [1 zero?]
      uint32_t data_c;
      // [2 zeros?] [6 FlagsD] [24 Zeros?]
      uint32_t data_d;
    } __attribute__((packed));
    // Note, because the data is little endian, the order of bytes in each of
    // fields 'data_a' through 'data_d' will appear reversed, relative what is
    // observed through this field.
    uint8_t data[12];
  };

  // Flags A (Rendering order?):
  //    1----: Unknown. Always seems to be set?
  //    -0000: Most of the body and inward-looking spoiler faces.
  //    -0001: Front, side windows and wheel wells (these windows overlap the
  //           hood, so maybe they want to render it after the rest of the
  //           body?)
  //    -1100: Mirrors, spoiler (stuff sticking off the car?)
  //    -0111: Wheel well inners (so you can't see through to the other side)
  uint8_t flags_a() const { return LowBits<5>(data_a); }

  // Flags B (Palettes and reflections?):
  //    1100: Tail lights (these change palette when brakes are applied).
  //    1000: Body and decals (the decals are transparent, but they're rendered
  //          on top of other faces marked with 0000. I bet even the transparent
  //          parts of the decals receive reflections.) They have normals.
  //    0000: Blank body or window faces on top of which which decals are
  //          rendered, inside faces of spoilers, all untextured stuff. This is
  //          probably everything that doesn't receive a reflection. They don't
  //          have normals.
  uint8_t flags_b() const { return Unpack<uint8_t, 4, 12>(data_b); }

  // Flags D (Face type?):
  //    --100---: triangle
  //    --101---: quad
  //    -----000: untextured
  //    -----101: textured
  uint8_t flags_d() const { return Unpack<uint8_t, 8, 24>(data_d); }

  // Tail light palette index must be incremented when brakes are applied.
  bool is_tail_light() const { return (flags_b() >> 2) & 0x1; }

  // Not all faces have normals.
  bool has_normals() const { return flags_b() >> 3; }

  // NOTE: experimental. Not all face may be correctly marked as quads or tris.
  bool is_tri() const { return (flags_d() >> 3) == 0x4; }
  bool is_quad() const { return (flags_d() >> 3) == 0x5; }

  // Experimental: setters for various flags.
  void set_tri() { data_d |= (0x4 << 27); }
  void set_quad() { data_d |= (0x5 << 27); }
  void set_textured() { data_d |= (0x5 << 24); }

  // Extracts the normal index.
  uint16_t i_normal(int n) const {
    switch (n) {
      case 0:
        return Unpack<uint16_t, 9, 5>(data_a);
      case 1:
        return Unpack<uint16_t, 9, 1>(data_c);
      case 2:
        return Unpack<uint16_t, 9, 10>(data_c);
      case 3:
        return Unpack<uint16_t, 9, 19>(data_c);
    }
    return 0;
  }

  // Experimental: copies normals from the given face.
  void CopyNormalsFrom(const Face& f) {
    // TODO(commongear): we copy some other bits too, but they're usually zero.
    // May need to revisit with better masking.
    constexpr uint16_t flags_a_mask = LowBits<5, uint16_t>();
    data_a = (data_a & flags_a_mask) | (f.data_a & ~flags_a_mask);
    data_b = f.data_b;
    data_c = f.data_c;
  }
};
static_assert(sizeof(Face) == 16);

std::ostream& operator<<(std::ostream& os, const Face& f) {
  std::string_view name;
  if (f.is_tri()) {
    name = "tri";
  } else if (f.is_quad()) {
    name = "quad";
  } else {
    name = "UNKNOWN_FACE";
  }
  return os << "{" << name << " v:" << ToString(f.i_vert) << " n:"
            << StrCat(f.i_normal(0), f.i_normal(1), f.i_normal(2),
                      f.i_normal(3))
            << "}";
}

// One textured face (tri or quad).
struct TexFace : Face {
  Vec2<uint8_t> uv0;
  uint16_t palette_index;
  Vec2<uint8_t> uv1;
  uint16_t unknown3;
  Vec2<uint8_t> uv2;
  Vec2<uint8_t> uv3;
} __attribute__((packed));
static_assert(sizeof(TexFace) == 28);

std::ostream& operator<<(std::ostream& os, const TexFace& f) {
  std::string_view name;
  if (f.is_tri()) {
    name = "tex-tri";
  } else if (f.is_quad()) {
    name = "tex-quad";
  } else {
    name = "UNKNOWN_TEX_FACE";
  }
  return os << "{" << name << " v:" << ToString(f.i_vert) << " n:["
            << StrCat(f.i_normal(0), f.i_normal(1), f.i_normal(2),
                      f.i_normal(3))
            << "] p:" << f.palette_index << " uv:["
            << StrCat(f.uv0, f.uv1, f.uv2, f.uv3) << "]}";
}

// Header for a cdo/cno.
struct ObjectHeader {
  // Wheel dimensions *may* need to be divided by 2 to match car scale.
  struct WheelSize {
    uint16_t radius;
    uint16_t width;
  };
  char magic[4];
  uint8_t padding[20];  // Should be zero.
  // 0: front wheels. 1: rear wheels.
  WheelSize wheel_size[2];
  // These are wheel positions. They need to be divided by 2 to match car scale.
  // (x, y, z) are obvious. (w) is still a mystery.
  Vec4<int16_t> wheels[4];

  bool Validate() {
    return std::strncmp(magic, "GT\2\0", 4) == 0 && IsZero(padding);
  }
} __attribute__((packed));
static_assert(sizeof(ObjectHeader) == 64, "");

std::ostream& operator<<(std::ostream& os, const ObjectHeader::WheelSize& s) {
  return os << "{rad:" << s.radius << " width:" << s.width << "}";
}

std::ostream& operator<<(std::ostream& os, const ObjectHeader& h) {
  os << "magic: " << std::string_view(h.magic, 4)
     << "\nfront_wheel: " << h.wheel_size[0]
     << "\nrear_wheel: " << h.wheel_size[1] << "\nwheel_pos: [\n"
     << ToString<kSplitLines>(h.wheels) << "\n]\n";
  return os;
}

// One level-of-detail (LOD) from a cno/cdo.
struct Model {
  struct Header {
    uint16_t num_verts = 0;
    uint16_t num_normals = 0;
    // Solid-colored faces.
    uint16_t num_tris = 0;
    uint16_t num_quads = 0;
    // These appear to be padding (always zero?)
    uint16_t unknown_1 = 0;
    uint16_t unknown_2 = 0;
    // Textured faces.
    uint16_t num_tex_tris = 0;
    uint16_t num_tex_quads = 0;
    // Should be zero.
    uint16_t padding[32];
  } __attribute__((packed));
  static_assert(sizeof(Model::Header) == 80);

  // This is a fixed-point normal vector.
  // Multiply each element by 1/500 to normalize.
  struct Normal {
    // NOTE: data is stored litte-endian.
    // As a uint32_t, it looks like this:
    //  MSB [10 z][10 y][10 x][2 pad] LSB.
    // On disk, it would look like this:
    //  LSB [2 pad][10 x][10 y][10 z] MSB.
    uint32_t data;
    int16_t x() const { return Unpack<int16_t, 10, 2>(data); }
    int16_t y() const { return Unpack<int16_t, 10, 12>(data); }
    int16_t z() const { return Unpack<int16_t, 10, 22>(data); }

    // The length should be 1.0.
    bool Validate() const {
      constexpr float k = 1.0f / 500.0f;
      const float x = k * this->x();
      const float y = k * this->y();
      const float z = k * this->z();
      const float l = std::sqrt(x * x + y * y + z * z);
      return (0.995 <= l && l <= 1.0);
    }
  } __attribute__((packed));
  static_assert(sizeof(Model::Normal) == 4);

  Model::Header header;
  std::vector<Vec4<int16_t>> verts;
  std::vector<Normal> normals;
  std::vector<Face> tris;
  std::vector<Face> quads;
  std::vector<TexFace> tex_tris;
  std::vector<TexFace> tex_quads;

  template <typename Stream>
  static Model FromStream(Stream& s) {
    Model out;
    out.header = s.template Read<Model::Header>();
    out.verts = s.template Read<Vec4<int16_t>>(out.header.num_verts);
    out.normals = s.template Read<Normal>(out.header.num_normals);
    for (int i = 0; i < out.header.num_normals; ++i) {
      const Normal& n = out.normals[i];
      CHECK(n.Validate(), "Bad normal. i=", i, n);
    }
    out.tris = s.template Read<Face>(out.header.num_tris);
    out.quads = s.template Read<Face>(out.header.num_quads);
    out.tex_tris = s.template Read<TexFace>(out.header.num_tex_tris);
    out.tex_quads = s.template Read<TexFace>(out.header.num_tex_quads);
    return out;
  }

  // Each face has a palette index used for color lookup.
  // We can draw the palette indices into a UV map the size of the texture.
  //  'palette' contains the palette_index for each texel.
  //  'mask' is 255 wherever palette values were set, 0 otherwise.
  void DrawPaletteUvs(Image& palette, Image& mask) const {
    CHECK_EQ(palette.width, 256);
    CHECK_EQ(palette.height, 256);
    CHECK_EQ(palette.channels, 1);
    CHECK_EQ(mask.width, 256);
    CHECK_EQ(mask.height, 256);
    CHECK_EQ(mask.channels, 1);
    for (const auto& f : tex_tris) {
      // TODO(commongear): We're always turning on the brake lights...
      const uint8_t value = f.palette_index == 194 ? 195 : f.palette_index;
      palette.DrawTriangle(f.uv0, f.uv1, f.uv2, value);
      mask.DrawTriangle(f.uv0, f.uv1, f.uv2, 255);
    }
    for (const auto& f : tex_quads) {
      const uint8_t value = f.palette_index == 194 ? 195 : f.palette_index;
      palette.DrawTriangle(f.uv0, f.uv1, f.uv2, value);
      palette.DrawTriangle(f.uv0, f.uv2, f.uv3, value);
      mask.DrawTriangle(f.uv0, f.uv1, f.uv2, 255);
      mask.DrawTriangle(f.uv0, f.uv2, f.uv3, 255);
    }
  }
};

std::ostream& operator<<(std::ostream& os, const Model::Header& h) {
  return os << "verts: " << h.num_verts << "\nnum_norms: " << h.num_normals
            << "\ntris: " << h.num_tris << "\nquads: " << h.num_quads
            << "\n?? 1: " << h.unknown_1 << "\n?? 2: " << h.unknown_2
            << "\ntex_tris: " << h.num_tex_tris
            << "\ntex_quads: " << h.num_tex_quads;
}

std::ostream& operator<<(std::ostream& os, const Model::Normal& n) {
  return os << "{" << n.x() << " " << n.y() << " " << n.z() << "}";
}

std::ostream& operator<<(std::ostream& os, const Model& m) {
  os << m.header << std::endl;
  os << ToString<kSplitLines>(m.verts) << std::endl;
  os << ToString<kSplitLines>(m.normals) << std::endl;
  os << ToString<kSplitLines>(m.tris) << std::endl;
  os << ToString<kSplitLines>(m.quads) << std::endl;
  os << ToString<kSplitLines>(m.tex_tris) << std::endl;
  os << ToString<kSplitLines>(m.tex_quads) << std::endl;
  return os;
}

// Builds a rudimentary wheel and stores it in 'm'.
void MakeWheel(Vec4<int16_t> pos, ObjectHeader::WheelSize size, Model& m) {
  const float r_tire = size.radius;
  const float r_rim = 0.75 * r_tire;
  const float width = size.width * (pos.w < 0 ? 1 : -1);
  const int n = 16;
  const float dth = 2.f * kPif / n * (pos.w < 0 ? 1 : -1);
  // Center of the inside.
  m.verts.emplace_back(pos.x + width, pos.y, pos.z, 0);
  // Center of the outside.
  m.verts.emplace_back(pos.x + 0.07f * width, pos.y, pos.z, 0);
  const int vert = m.verts.size();
  // Wheel rim.
  for (int i = 0; i < n; ++i) {
    const float th = i * dth;
    const float x = pos.x + 0.5 + 0.07f * width;
    const float y = pos.y + r_rim * std::sin(th) + 0.5;
    const float z = pos.z + r_rim * std::cos(th) + 0.5;
    m.verts.emplace_back(x, y, z, 0);
  }
  // Tire rim.
  for (int i = 0; i < n; ++i) {
    const float th = i * dth;
    const float x = pos.x + 0.5;
    const float y = pos.y + r_rim * std::sin(th) + 0.5;
    const float z = pos.z + r_rim * std::cos(th) + 0.5;
    m.verts.emplace_back(x, y, z, 0);
  }
  // Tire outer.
  for (int i = 0; i < n; ++i) {
    const float th = i * dth;
    const float x = pos.x + 0.5;
    const float y = pos.y + r_tire * std::sin(th) + 0.5;
    const float z = pos.z + r_tire * std::cos(th) + 0.5;
    m.verts.emplace_back(x, y, z, 0);
  }
  // Tire inner.
  for (int i = 0; i < n; ++i) {
    const float th = i * dth;
    const float x = pos.x + 0.5 + width;
    const float y = pos.y + r_tire * std::sin(th) + 0.5;
    const float z = pos.z + r_tire * std::cos(th) + 0.5;
    m.verts.emplace_back(x, y, z, 0);
  }
  // Wheel.
  for (int i = 0; i < n; ++i) {
    m.tex_tris.push_back({});
    TexFace& f = m.tex_tris.back();
    f.set_textured();
    f.i_vert[0] = vert - 1;
    f.i_vert[1] = vert + i;
    f.i_vert[2] = vert + (i + 1) % n;

    const float th = i * dth;
    const float thp = (i + 1) * dth;
    const float kR = 23.f;
    f.uv0.x = 0.5f + kR;
    f.uv0.y = 0.5f + kR;
    f.uv1.x = 0.5f + kR + kR * std::cos(th);
    f.uv1.y = 0.5f + kR + kR * std::sin(th);
    f.uv2.x = 0.5f + kR + kR * std::cos(thp);
    f.uv2.y = 0.5f + kR + kR * std::sin(thp);
  }
  // Rim lip.
  for (int i = 0; i < n; ++i) {
    m.tex_quads.push_back({});
    TexFace& f = m.tex_quads.back();
    f.set_textured();
    f.set_quad();
    f.i_vert[0] = vert + i + n;
    f.i_vert[1] = vert + (i + 1) % n + n;
    f.i_vert[2] = vert + (i + 1) % n;
    f.i_vert[3] = vert + i;

    const float th = i * dth;
    const float thp = (i + 1) * dth;
    const float kR = 23.f;
    f.uv0.x = 0.5f + kR + kR * std::cos(th);
    f.uv0.y = 0.5f + kR + kR * std::sin(th);
    f.uv1.x = 0.5f + kR + kR * std::cos(thp);
    f.uv1.y = 0.5f + kR + kR * std::sin(thp);
    f.uv2 = f.uv1;
    f.uv3 = f.uv0;
  }
  // Tire wall.
  for (int i = 0; i < n; ++i) {
    m.quads.push_back({});
    Face& f = m.quads.back();
    f.set_quad();
    f.i_vert[0] = vert + i + 2 * n;
    f.i_vert[1] = vert + (i + 1) % n + 2 * n;
    f.i_vert[2] = vert + (i + 1) % n + n;
    f.i_vert[3] = vert + i + n;
  }
  // Tire tread.
  for (int i = 0; i < n; ++i) {
    m.quads.push_back({});
    Face& f = m.quads.back();
    f.set_quad();
    f.i_vert[0] = vert + 2 * n + i + n;
    f.i_vert[1] = vert + 2 * n + (i + 1) % n + n;
    f.i_vert[2] = vert + 2 * n + (i + 1) % n;
    f.i_vert[3] = vert + 2 * n + i;
  }
  // Wheel/tire inside.
  for (int i = 0; i < n; ++i) {
    m.tris.push_back({});
    Face& f = m.tris.back();
    f.i_vert[0] = vert - 2;
    f.i_vert[1] = vert + 3 * n + (i + 1) % n;
    f.i_vert[2] = vert + 3 * n + i;
  }
}

struct ObjectFooter {
  Vec4<int16_t> unknown;
  // Appears to be the collision bounds at road-level?
  Vec4<int16_t> lower_bound;
  Vec4<int16_t> upper_bound;
};

std::ostream& operator<<(std::ostream& os, const ObjectFooter& f) {
  return os << "{unknown:" << f.unknown << " lo:" << f.lower_bound
            << " hi:" << f.upper_bound << "}";
}

// Stored in a 'cdo' (daytime tracks) or 'cno' (nighttime tracks).
struct ObjectFile {
  // As-read from the file.
  ObjectHeader header;
  std::vector<uint16_t> padding;  // All zeros?
  uint16_t num_lods;
  std::vector<uint16_t> unknown1;  // Lots of stuff in here.
  std::vector<Model> lods;
  ObjectFooter footer;
  uint16_t unknown2;              // Looks to be some kind of flags (+scale?).
  std::vector<uint8_t> unknown3;  // Looks to be 4-byte chunks.
  std::vector<int16_t> unknown4;  // Interesting symmetry in here...

  // Derived.
  // TODO(commongear): this is not necessary here.
  std::vector<Model> wheels;

  // Multiply each vertex by this value to scale the body correctly relative to
  // the wheels. Are there more than just two scales?
  float body_scale() const { return (unknown2 & 0x1) ? 2.0f : 1.0f; }

  template <typename Stream>
  static ObjectFile FromStream(Stream& s) {
    ObjectFile out;
    out.header = s.template Read<ObjectHeader>();
    CHECK(out.header.Validate());
    out.padding = s.template Read<uint16_t>(0x828 / 2);
    CHECK(IsZero(out.padding));
    out.num_lods = s.template Read<uint16_t>();
    out.unknown1 = s.template Read<uint16_t>(13);

    for (int i = 0; i < out.num_lods; ++i) {
      out.lods.push_back(Model::FromStream(s));
    }

    out.footer = s.template Read<ObjectFooter>();
    out.unknown2 = s.template Read<uint16_t>();
    out.unknown3 = s.template Read<uint8_t>(16);
    out.unknown4 = s.template Read<int16_t>(s.remain() / sizeof(int16_t));

    // Let's make some wheels!
    // TODO(commongear): this is not necessary here...
    for (int i = 0; i < 4; ++i) {
      out.wheels.push_back({});
      Model& w = out.wheels.back();
      MakeWheel(out.header.wheels[i], out.header.wheel_size[i / 2], w);
    }

    return out;
  }

  // Each model face contains a palette index. We can extract it to a UV map.
  struct UvPalette {
    Image index;  // Palette index for every texel.
    Image mask;   // Zero where there are no UVs.
  };
  UvPalette DrawUvPalette() const {
    UvPalette out{
        Image(256, 256, 1),
        Image(256, 256, 1),
    };
    for (const auto& model : lods) {
      model.DrawPaletteUvs(out.index, out.mask);
    }
    {                           // Draw the palette index for the wheel.
      const uint8_t value = 0;  // TODO(commongear): is it always zero?
      int x0 = 0;
      for (int i = 0; i < 44; ++i) {
        for (int x = 0; x < 44; ++x) {
          // Re-pack the wacky palette index.
          out.index.pixels[x0 + x] = ((value << 4) | value) & 0xC3;
          out.mask.pixels[x0 + x] = 255;
        }
        x0 += 256;
      }
    }
    // Our drawing algos leave gaps. Grow the index and mask regions.
    out.index.GrowBorders(out.mask);
    // out.index.GrowBorders(out.mask);
    return out;
  }
};

std::ostream& operator<<(std::ostream& os, const ObjectFile& f) {
  os << f.header << "\n"
     << "unknown1:" << ToString(f.unknown1) << "\nnum_lods: " << f.num_lods
     << "\n";
  for (const auto& m : f.lods) {
    os << m.header << "\n-----------------------\n";
  }
  os << f.footer;
  os << "\nunknown2: " << f.unknown2;
  os << "\nunknown3: " << ToString(f.unknown3);
  os << "\nunknown4: " << ToString(f.unknown4);
  return os << "\n";
}

////////////////////////////////////////////////////////////////////////////////
// CDP/CNP (PICTURE DATA FILES).
////////////////////////////////////////////////////////////////////////////////

union Color16 {
  // TODO(commongear): c++ doesn't specify the memory layout of struct
  // bitfields, so this could break on different platforms. Switch to Unpack.
  struct {
    uint16_t r : 5;
    uint16_t g : 5;
    uint16_t b : 5;
    // TODO(commongear): This doesn't appear to be alpha. Is it padding?
    // Extremely few palette colors have this set, and it doesn't seem related
    // to their intended transparency.
    uint16_t a : 1;
  };
  uint16_t data;
};

std::ostream& operator<<(std::ostream& os, Color16 c) {
  return os << "{rgb " << (c.r << 3) << " " << (c.g << 3) << " " << (c.b << 3)
            << " " << c.a << "}";
}

// Stored in a 'cdp' (daytime tracks) or 'cnp' (nighttime tracks).
struct Picture {
  struct Palette {
    Color16 data[256];
    uint8_t unknown[64];
  } __attribute__((packed));
  static_assert(sizeof(Palette) == 576);

  // Currently guessed by me...
  int width = 0;
  int height = 0;

  // As-read from the file.
  uint8_t num_palettes = 0;
  std::vector<uint8_t> unknown1;  // Some data in here...
  std::vector<Palette> palettes;
  // If we've read the palettes right, this should be all zeros.
  std::vector<uint8_t> padding;
  // 4-bits per pixel, indices into a sub-palette of 16 colors.
  std::vector<uint8_t> data;
  std::vector<uint8_t> unknown4;  // Should be empty.

  // Returns the 4 MSB of the palette index. The four LSB come from the 4bpp
  // texture data. See Texture().
  // QUIRK: Palette indices are stored very oddly. They appear to be in the two
  // MSB and two LSB of an unsigned 8-bit number. The four middle bits appear to
  // be zero.
  static uint8_t PaletteToHighBits(uint8_t i) { return (i | (i << 4)) & 0xF0; }

  template <typename Stream>
  static Picture FromStream(Stream& s) {
    Picture out;
    // TODO(commongear): read the actual correct size.
    // TODO(commongear): UVs aren't generated correctly for non-square images.
    out.width = 256;
    out.height = 256;  // TODO(commongear): Is this actually 220 or something?
    // TODO(commongear): lots of data in here...
    out.num_palettes = s.template Read<uint8_t>();
    out.unknown1 = s.template Read<uint8_t>(31);

    // Read palettes. There's always space for 30 in the file.
    constexpr int kMaxPalettes = 30;
    CHECK_LE(out.num_palettes, kMaxPalettes);
    out.palettes = s.template Read<Palette>(out.num_palettes);
    const int pad_size = (kMaxPalettes - out.num_palettes) * sizeof(Palette);
    out.padding = s.template Read<uint8_t>(pad_size);
    CHECK(IsZero(out.padding));

    // Image data begins here.
    CHECK_EQ(s.pos(), 17312);
    // 4 bits per pixel.
    // TODO(commongear): how much data is actually here???
    out.data = s.template Read<uint8_t>(out.width * 224 / 2);
    out.data.resize(out.width * out.height / 2, 255);
    // TODO(commongear): Anything here?
    if (s.remain() > 0) out.unknown4 = s.template Read<uint8_t>(s.remain());

    return out;
  }

  // Unpacks 4-bit pixel data into a renderable 8-bit image.
  // Values are stored in the 4 MSB of the resulting pixels for better
  // visualization. However, the data actually forms the 4 LSB of an index into
  // the palette.
  Image Pixels() const {
    Image out(width, height, 1);
    out.pixels.clear();
    for (const uint8_t pixel : data) {
      const uint8_t a = (pixel << 4) & 0xF0;
      out.pixels.push_back(a);
      const uint8_t b = pixel & 0xF0;
      out.pixels.push_back(b);
    }
    return out;
  }

  // Gets palette 'p' as an NxN image.
  // The 'palette_index' from the obj data point to a row in this palette.
  // The 4-bit value in the pixels of this image select a column.
  Image PaletteImage(int p) const {
    const int dim = std::ceil(
        std::sqrt(sizeof(palettes[p].data) / sizeof(palettes[p].data[0])));
    Image out(dim, dim, 3);
    out.pixels.clear();
    for (const Color16 c : palettes[p].data) {
      out.pixels.push_back(c.r << 3);
      out.pixels.push_back(c.g << 3);
      out.pixels.push_back(c.b << 3);
    }
    out.pixels.resize(dim * dim * 3);
    return out;
  }

  // Unpacks the 32-bit RGBA texture stored in this picture using palette 'p'.
  // The 'palette_index' must be the 8-bit value from each face of the 3d model,
  // written into a UV-space image.
  Image Texture(int p, const Image& palette_index) const {
    Image texture(width, height, 4);
    texture.pixels.clear();
    int i = 0;
    for (const uint8_t pixel : data) {
      {
        const uint8_t pal = palette_index.pixels[i];
        const uint8_t lo = pixel & 0xF;
        const uint8_t hi = PaletteToHighBits(pal);
        const Color16 c = palettes[p].data[lo | hi];
        texture.pixels.push_back(c.r << 3);
        texture.pixels.push_back(c.g << 3);
        texture.pixels.push_back(c.b << 3);
        // NOTE(commongear): zero appears to be rendered as transparent. Is this
        // always supposed to be true?
        texture.pixels.push_back(c.data ? 255 : 0);
        ++i;
      }
      {
        const uint8_t pal = palette_index.pixels[i];
        const uint8_t lo = (pixel >> 4);
        const uint8_t hi = PaletteToHighBits(pal);
        const Color16 c = palettes[p].data[lo | hi];
        texture.pixels.push_back(c.r << 3);
        texture.pixels.push_back(c.g << 3);
        texture.pixels.push_back(c.b << 3);
        texture.pixels.push_back(c.data ? 255 : 0);
        ++i;
      }
    }
    // We'll use the first pixel of the last line to render un-textured faces.
    const int last_line = 4 * width * (height - 1);
    texture.pixels[last_line + 0] = 0;
    texture.pixels[last_line + 1] = 0;
    texture.pixels[last_line + 2] = 0;
    texture.pixels[last_line + 3] = 255;
    return texture;
  }
};

std::ostream& operator<<(std::ostream& os, const Picture::Palette& p) {
  return os << "data:\n"
            << ToString<kSplitLines>(p.data)
            << "\nunknown: " << ToString(p.unknown) << "\n";
}

std::ostream& operator<<(std::ostream& os, const Picture& p) {
  os << "num_palettes: " << static_cast<int>(p.num_palettes)
     << "\nunknown1: " << ToString(p.unknown1);
  if (!p.unknown4.empty()) os << "\nunknown4: " << ToString(p.unknown4);
  return os;
}

}  // namespace gt2

#endif  // GT2_EXTRACT_CAR_H_
