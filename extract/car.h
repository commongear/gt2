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

////////////////////////////////////////////////////////////////////////////////
// Summary
////////////////////////////////////////////////////////////////////////////////
//
//  This code does a pretty thorough job of loading both cdo/cno and cdp/cnp
//  files correctly. The most interesting contribution (I haven't seen it
//  before) is the mapping-out of the Face and TexFace data. They have lots of
//  rendering flags related to tail lights, decals, reflections, rendering
//  order, etc. Understanding the normals and normal indices was a particularly
//  hairy endeavor, as they're stored little-endian and bit-packed into lots of
//  wacky, mis-aligned places.
//

////////////////////////////////////////////////////////////////////////////////
// Data types.
////////////////////////////////////////////////////////////////////////////////

// Encodes the vertex scale.
//  value = 16 matches up with the default wheel positions in a CarObject.
struct Scale16 {
  uint16_t value;

  // Multiply verts (or bounds) by this factor to match wheels in a CarObject.
  //  - Seems to work for values of 15, 16, 17, 18. NOT_VERIFIED for others.
  //  - UNKNOWN: are some of the 'value' bits use for something else?
  float factor() const {
    const int shift = 16 - static_cast<int>(value);
    if (shift < 0) {
      return static_cast<float>(1 << -shift);
    } else {
      return static_cast<float>(1.f / (1 << shift));
    }
  }
};
static_assert(sizeof(Scale16) == 2);

std::ostream& operator<<(std::ostream& os, const Scale16& s) {
  return os << "{Scale16 value:" << s.value << " factor:" << s.factor() << "}";
}

// 16-bit packed RGB color.
struct Color16 {
  uint16_t data;

  // 5-bit colors.
  uint8_t r5() const { return Unpack<uint8_t, /*bits=*/5, /*shift=*/0>(data); }
  uint8_t g5() const { return Unpack<uint8_t, /*bits=*/5, /*shift=*/5>(data); }
  uint8_t b5() const { return Unpack<uint8_t, /*bits=*/5, /*shift=*/10>(data); }

  // 8-bit colors.
  uint8_t r() const { return r5() << 3; }
  uint8_t g() const { return g5() << 3; }
  uint8_t b() const { return b5() << 3; }

  // Opacity:
  //  - Black is transparent by default.
  //  - The padding bit is set to force opaque rendering? NOT_VERIFIED.
  bool opaque() const { return data != 0; }
  uint8_t force_opaque() const {
    return Unpack<uint8_t, /*bits=*/1, /*shift=*/15>(data);
  }

  // Sets data from r,g,b,force-opaque.
  void SetRgb(uint8_t r, uint8_t g, uint8_t b, uint8_t force_opaque_bit = 0) {
    data = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) |
           ((force_opaque_bit & 0x1) << 15);
  }

  // Writes the color as a standard RGB hex string.
  void WriteHex(std::ostream& os) const {
    using std::setfill;
    using std::setw;
    os << setw(2) << setfill('0') << std::hex << static_cast<int>(r())
       << setw(2) << setfill('0') << std::hex << static_cast<int>(g())
       << setw(2) << setfill('0') << std::hex << static_cast<int>(b());
  }
} __attribute__((packed));
static_assert(sizeof(Color16) == 2);

std::ostream& operator<<(std::ostream& os, Color16 c) {
  return os << "{rgb " << static_cast<int>(c.r()) << " "
            << static_cast<int>(c.g()) << " " << static_cast<int>(c.b()) << " "
            << static_cast<int>(c.force_opaque()) << "}";
}

// 32-bit packed fixed-point normal.
// Multiply each element by 1/500 to get a vector with unit length.
struct Normal32 {
  // NOTE: data is stored litte-endian.
  // As a uint32_t, it looks like this:
  //  MSB [10 z][10 y][10 x][2 pad] LSB.
  // On disk, it would look like this:
  //  LSB [2 pad][10 x][10 y][10 z] MSB.
  uint32_t data;

  // Unpacks the raw components.
  int16_t x() const { return Unpack<int16_t, /*bits=*/10, /*shfit=*/2>(data); }
  int16_t y() const { return Unpack<int16_t, /*bits=*/10, /*shfit=*/12>(data); }
  int16_t z() const { return Unpack<int16_t, /*bits=*/10, /*shfit=*/22>(data); }

  // Sets the raw int16 x,y,z components.
  void set(int16_t x, int16_t y, int16_t z) {
    constexpr uint16_t m = LowBits<10, uint16_t>();
    data = ((x & m) << 2) | ((y & m) << 12) | ((z & m) << 22);
  }

  // Raw length.
  float len() const {
    return std::sqrt(1.f * x() * x() + 1.f * y() * y() + 1.f * z() * z());
  }

  // Returns unit-vector components.
  float xf() const { return x() * 1.f / 500.f; }
  float yf() const { return y() * 1.f / 500.f; }
  float zf() const { return z() * 1.f / 500.f; }

  // Expects a unit vector.
  void setf(float x, float y, float z) {
    // Scaling by 499 and rounding down puts the components in a similar range
    // as the original models.
    constexpr float k = 499.0;
    set(k * x, k * y, k * z);
  }

  // Unit length.
  float lenf() const {
    return std::sqrt(xf() * xf() + yf() * yf() + zf() * zf());
  }

  // The length should be 1.0.
  bool Validate() const {
    const float l = lenf();
    return (0.995 <= l && l <= 1.0);
  }
} __attribute__((packed));
static_assert(sizeof(Normal32) == 4);

std::ostream& operator<<(std::ostream& os, const Normal32& n) {
  return os << "{" << n.x() << " " << n.y() << " " << n.z() << "}";
}

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
  //    1----: UNKNOWN: Always seems to be set?
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
  uint8_t flags_b() const {
    return Unpack<uint8_t, /*bits=*/4, /*shift=*/12>(data_b);
  }

  // Flags D (Face type?):
  //    --100---: triangle
  //    --101---: quad
  //    -----000: untextured
  //    -----101: textured
  uint8_t flags_d() const {
    return Unpack<uint8_t, /*bits=*/8, /*shift=*/24>(data_d);
  }

  // Tail light palette index must be incremented when brakes are applied.
  bool is_tail_light() const { return (flags_b() >> 2) & 0x1; }

  // Not all faces have normals.
  bool has_normals() const { return flags_b() >> 3; }

  // NOTE: experimental. Not all faces may be correctly marked as quads or tris.
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
        return Unpack<uint16_t, /*bits=*/9, /*shift=*/5>(data_a);
      case 1:
        return Unpack<uint16_t, /*bits=*/9, /*shift=*/1>(data_c);
      case 2:
        return Unpack<uint16_t, /*bits=*/9, /*shift=*/10>(data_c);
      case 3:
        return Unpack<uint16_t, /*bits=*/9, /*shift=*/19>(data_c);
    }
    return 0;
  }

  // Sets the normal indices.
  void set_i_normals(uint16_t a, uint16_t b, uint16_t c, uint16_t d = 0) {
    // TODO(commongear): this assumes the data and flags are cleared first...
    data_a |= ((a & LowBits<9, uint16_t>()) << 5);
    data_c |= ((b & LowBits<9, uint16_t>()) << 1);
    data_c |= ((c & LowBits<9, uint16_t>()) << 10);
    data_c |= ((d & LowBits<9, uint16_t>()) << 19);
  }

  // Experimental: copies normal indices from the given face.
  void CopyNormalIndicesFrom(const Face& f) {
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
            << StrCat<1>(f.i_normal(0), f.i_normal(1), f.i_normal(2),
                         f.i_normal(3))
            << " a:" << std::bitset<5>(f.flags_a())
            << " b:" << std::bitset<4>(f.flags_b())
            << " c:" << std::bitset<6>(f.flags_d()) << "}";
}

// One textured face (tri or quad).
struct TexFace : Face {
  Vec2<uint8_t> uv0;
  uint16_t pal_data;
  Vec2<uint8_t> uv1;
  uint16_t unknown3;  // UNKNOWN
  Vec2<uint8_t> uv2;
  Vec2<uint8_t> uv3;

  // Palette index is stored in a very odd format: [2 MSB, 4 zero, 2 LSB].
  //  - It picks a 16 color chunk of the palette in the CDP.
  //  - The 4-bit color index within the sub-palette is in the CDP as a texture.
  uint8_t i_palette() const { return (pal_data >> 4 | pal_data) & 0xF; }
  void set_i_palette(uint8_t i) { pal_data = (((i << 4) | i) & 0xC3); }

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
            << StrCat<1>(f.i_normal(0), f.i_normal(1), f.i_normal(2),
                         f.i_normal(3))
            << "] p:" << f.i_palette() << " uv:["
            << StrCat(f.uv0, f.uv1, f.uv2, f.uv3)
            << "] a:" << std::bitset<5>(f.flags_a())
            << " b:" << std::bitset<4>(f.flags_b())
            << " c:" << std::bitset<6>(f.flags_d()) << "}";
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
    uint16_t unknown1 = 0;
    uint16_t unknown2 = 0;
    // Textured faces.
    uint16_t num_tex_tris = 0;
    uint16_t num_tex_quads = 0;

    // Not zero!
    uint8_t unknown3[44];  // UNKNOWN: Some mystery data in here...

    Vec4<int16_t> lo_bound;
    Vec4<int16_t> hi_bound;
    Scale16 scale;

    uint8_t unknown4;  // UNKNOWN: Mystery data here too...
    uint8_t unknown5;  // UNKNOWN: And here...
  } __attribute__((packed));
  static_assert(sizeof(Model::Header) == 80);

  Model::Header header;
  std::vector<Vec4<int16_t>> verts;
  std::vector<Normal32> normals;
  std::vector<Face> tris;
  std::vector<Face> quads;
  std::vector<TexFace> tex_tris;
  std::vector<TexFace> tex_quads;

  template <typename Stream>
  static Model FromStream(Stream& s) {
    Model out;
    out.header = s.template Read<Model::Header>();
    out.verts = s.template Read<Vec4<int16_t>>(out.header.num_verts);
    out.normals = s.template Read<Normal32>(out.header.num_normals);
    for (int i = 0; i < out.header.num_normals; ++i) {
      const Normal32& n = out.normals[i];
      CHECK(n.Validate(), "Bad normal. i=", i, n, n.len());
    }
    out.tris = s.template Read<Face>(out.header.num_tris);
    out.quads = s.template Read<Face>(out.header.num_quads);
    out.tex_tris = s.template Read<TexFace>(out.header.num_tex_tris);
    out.tex_quads = s.template Read<TexFace>(out.header.num_tex_quads);
    return out;
  }

  void Serialize(VecOutStream& out) const {
    out.Write(header);
    out.Write(verts);
    out.Write(normals);
    out.Write(tris);
    out.Write(quads);
    out.Write(tex_tris);
    out.Write(tex_quads);
  }

  // Each face has a palette index used for color lookup.
  // We can draw the palette indices into a UV map the size of the texture.
  //  'palette' contains the 4-msb of the palette_index for each texel.
  //  'mask' is 255 wherever palette values were set, 0 otherwise.
  void DrawPaletteUvs(Image& palette, Image& mask) const {
    CHECK_EQ(palette.width, 256);
    CHECK_EQ(palette.height, 256);
    CHECK_EQ(palette.channels, 1);
    CHECK_EQ(mask.width, 256);
    CHECK_EQ(mask.height, 256);
    CHECK_EQ(mask.channels, 1);
    for (const auto& f : tex_tris) {
      uint8_t value = (f.i_palette() << 4);
      // TODO(commongear): this always turns the brake lights on.
      if (value == 224) value = 240;
      palette.DrawTriangle(f.uv0, f.uv1, f.uv2, value);
      mask.DrawTriangle(f.uv0, f.uv1, f.uv2, 255);
    }
    for (const auto& f : tex_quads) {
      uint8_t value = (f.i_palette() << 4);
      // TODO(commongear): this always turns the brake lights on.
      if (value == 224) value = 240;
      palette.DrawTriangle(f.uv0, f.uv1, f.uv2, value);
      palette.DrawTriangle(f.uv0, f.uv2, f.uv3, value);
      mask.DrawTriangle(f.uv0, f.uv1, f.uv2, 255);
      mask.DrawTriangle(f.uv0, f.uv2, f.uv3, 255);
    }
  }
};

std::ostream& operator<<(std::ostream& os, const Model::Header& h) {
  return os << "{ Model "                                          //
            << std::setw(3) << h.num_verts << " verts  "           //
            << std::setw(3) << h.num_normals << " norms  "         //
            << std::setw(3) << h.num_tris << " tris   "            //
            << std::setw(3) << h.num_quads << " quads  "           //
            << std::setw(3) << h.num_tex_tris << " tex-tris   "    //
            << std::setw(3) << h.num_tex_quads << " tex-quads  "   //
            << "\n ?? 1: " << h.unknown1                           //
            << "\n ?? 2: " << h.unknown2                           //
            << "\n unknown3: " << ToHex(h.unknown3)                //
            << "\n lo: " << h.lo_bound << "   hi: " << h.hi_bound  //
            << "\n scale: " << h.scale                             //
            << "\n ?? 4: " << static_cast<int>(h.unknown4)
            << "\n ?? 5: " << static_cast<int>(h.unknown5) << "\n}";
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

////////////////////////////////////////////////////////////////////////////////
// CDO/CNO (Car 3d model data files).
////////////////////////////////////////////////////////////////////////////////

// Stored in a 'cdo' (daytime tracks) or 'cno' (nighttime tracks).
struct CarObject {
  struct Header {
    struct WheelSize {
      uint16_t radius;
      uint16_t width;
    };
    char magic[4];
    uint8_t padding[20];  // Should be zero.
    // 0: front wheels. 1: rear wheels.
    WheelSize wheel_size[2];
    // These are wheel positions.(x, y, z) are obvious. (w) is still a mystery.
    Vec4<int16_t> wheel_pos[4];

    bool Validate() {
      return std::strncmp(magic, "GT\2\0", 4) == 0 && IsZero(padding);
    }
  } __attribute__((packed));
  static_assert(sizeof(Header) == 64);

  // The shadow model of the car.
  // It also has scale information for interpreting the rest of the car verts.
  struct Shadow {
    struct Header {
      uint16_t num_verts;
      uint16_t num_tris;
      uint16_t num_quads;
      uint16_t unknown1;

      Vec4<int16_t> lo_bound;
      Vec4<int16_t> hi_bound;
      Scale16 scale;

      uint8_t unknown3;  // UNKNOWN: mystery data
      uint8_t unknown4;  // UNKNOWN: mystery data
    };
    static_assert(sizeof(Header) == 28);

    struct Face {
      uint32_t data;

      // 0x80 seems to indicate 'gradient', otherwise zero.
      uint8_t flags() const { return Unpack<uint32_t, /*bits=*/8, 24>(data); }

      // Vertex indices.
      uint8_t i_vert(int n) const {
        switch (n) {
          case 0:
            return Unpack<uint32_t, /*bits=*/6, /*shift=*/0>(data);
          case 1:
            return Unpack<uint32_t, /*bits=*/6, /*shift=*/6>(data);
          case 2:
            return Unpack<uint32_t, /*bits=*/6, /*shift=*/12>(data);
          case 3:
            return Unpack<uint32_t, /*bits=*/6, /*shift=*/18>(data);
        }
        return 0;
      }
    };
    static_assert(sizeof(Face) == 4);

    // Shadow data, as read from the file.
    Header header;
    std::vector<Vec2<int16_t>> verts;  // x,z only.
    std::vector<Face> tris;
    std::vector<Face> quads;

    template <typename Stream>
    static Shadow FromStream(Stream& s) {
      Shadow out;
      out.header = s.template Read<Header>();
      out.verts = s.template Read<Vec2<int16_t>>(out.header.num_verts);
      out.tris = s.template Read<Face>(out.header.num_tris);
      out.quads = s.template Read<Face>(out.header.num_quads);
      return out;
    }

    void Serialize(VecOutStream& out) const {
      out.Write(header);
      out.Write(verts);
      out.Write(tris);
      out.Write(quads);
    }
  };

  // As-read from the file.
  Header header;
  std::vector<uint16_t> padding;  // All zeros?
  uint16_t num_lods;
  std::vector<uint16_t> unknown1;  // UNKNOWN: Lots of stuff in here.
  std::vector<Model> lods;
  Shadow shadow;

  template <typename Stream>
  static CarObject FromStream(Stream& s) {
    CarObject out;

    out.header = s.template Read<Header>();
    CHECK(out.header.Validate());

    out.padding = s.template Read<uint16_t>(0x828 / 2);
    CHECK(IsZero(out.padding));

    out.num_lods = s.template Read<uint16_t>();
    out.unknown1 = s.template Read<uint16_t>(13);

    for (int i = 0; i < out.num_lods; ++i) {
      out.lods.push_back(Model::FromStream(s));
    }

    out.shadow = Shadow::FromStream(s);

    CHECK_EQ(s.remain(), 0);
    return out;
  }

  void Serialize(VecOutStream& out) const {
    out.Write(header);
    out.Write(padding);
    out.Write(num_lods);
    out.Write(unknown1);
    for (const auto& m : lods) m.Serialize(out);
    shadow.Serialize(out);
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
    // Grow the index and mask regions to cover any jagged, ambiguous edges.
    out.index.GrowBorders(out.mask);
    return out;
  }
};

std::ostream& operator<<(std::ostream& os,
                         const CarObject::Header::WheelSize& s) {
  return os << "{rad:" << s.radius << " width:" << s.width << "}";
}

std::ostream& operator<<(std::ostream& os, const CarObject::Header& h) {
  os << "{ CarObject\n magic: " << std::string_view(h.magic, 4)
     << "\n front_wheel_size: " << h.wheel_size[0]
     << "\n rear_wheel_size: " << h.wheel_size[1] << "\n wheel_pos: [\n"
     << ToString<kSplitLines>(h.wheel_pos) << "\n ]\n}";
  return os;
}

std::ostream& operator<<(std::ostream& os, const CarObject::Shadow::Header& h) {
  return os << "{ CarObject::Shadow  " << std::setw(2) << h.num_verts
            << " verts  " << std::setw(2) << h.num_tris << " tris   "
            << std::setw(2) << h.num_quads << " quads  "
            << "\n unknown1: " << h.unknown1 << "\n lo: " << h.lo_bound
            << "  hi: " << h.hi_bound << "\n scale: " << h.scale
            << "\n unknown3: " << static_cast<int>(h.unknown3)
            << "\n unknown4: " << static_cast<int>(h.unknown4) << "\n}";
}

std::ostream& operator<<(std::ostream& os, const CarObject& f) {
  os << f.header << "\nunknown1: " << ToString(f.unknown1)
     << "\nnum_lods: " << f.num_lods << "\n";
  for (const auto& m : f.lods) {
    os << m.header << "\n";
  }
  return os << f.shadow.header << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
// CDP/CNP (Car picture? (what the heck does the 'p' stand for?) data files).
////////////////////////////////////////////////////////////////////////////////

// Stored in a 'cdp' (daytime tracks) or 'cnp' (nighttime tracks).
struct CarPix {
  struct Header {
    uint16_t num_palettes;
    // One for each palette.
    uint8_t palette_id[30];
  };
  static_assert(sizeof(Header) == 32);

  struct Palette {
    Color16 data[256];
    // 1-bit/color: if set, color is unaffected by lighting.
    uint16_t is_emissive_data[16];
    // 1-bit/color: if set, part of a "painted" section (i.e. not windows).
    uint16_t is_painted_data[16];
    // True if the color at index 'i' is emissive.
    bool is_emissive(int i) const {
      return (is_emissive_data[i / 16] >> (i % 16)) & 0x1;
    }
    // True if the color at index 'i' is part of a "painted" section.
    bool is_painted(int i) const {
      return (is_painted_data[i / 16] >> (i % 16)) & 0x1;
    }
  } __attribute__((packed));
  static_assert(sizeof(Palette) == 576);

  // Always the same.
  const int width = 256;
  const int height = 256;  // TODO(commongear): 224.

  // As-read from the file.
  Header header;
  std::vector<Palette> palettes;
  // If we've read the palettes right, this should be all zeros.
  std::vector<uint8_t> padding;
  // 4-bpp: stores the low bits of the palette index for each texel.
  // High bits are stored on each face in the CDO.
  std::vector<uint8_t> data;

  template <typename Stream>
  static CarPix FromStream(Stream& s) {
    CarPix out;
    out.header = s.template Read<Header>();

    // There are always slots for 30 palettes in the file.
    constexpr int kMaxPalettes = 30;
    CHECK_LE(out.header.num_palettes, kMaxPalettes);

    // Read them.
    out.palettes = s.template Read<Palette>(out.header.num_palettes);
    const int pad_size =
        (kMaxPalettes - out.header.num_palettes) * sizeof(Palette);

    // If we read the palettes correctly, the remaining slots should be zero.
    out.padding = s.template Read<uint8_t>(pad_size);
    CHECK(IsZero(out.padding));
    CHECK_EQ(s.pos(), 17312);

    // 4 bits per pixel, so divide by 2.
    out.data = s.template Read<uint8_t>(out.width * 224 / 2);

    // TODO(commongear): This should be done in the OBJ exporter, or not at all.
    out.data.resize(256 * 128);

    CHECK_EQ(s.remain(), 0);
    return out;
  }

  void Serialize(VecOutStream& out) {
    // There are always slots for 30 palettes in the file.
    constexpr int kMaxPalettes = 30;
    CHECK_LE(header.num_palettes, kMaxPalettes);
    CHECK_EQ(header.num_palettes, palettes.size());
    CHECK_EQ(data.size(), 256 * 224 / 2);

    out.Write(header);
    out.Write(palettes);
    out.Resize(sizeof(Header) + kMaxPalettes * sizeof(Palette));
    out.Write(data);
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
    constexpr int kDim = 16;
    static_assert(kDim * kDim * sizeof(uint16_t) == sizeof(Palette::data));
    Image out(kDim, kDim, 3);
    out.pixels.clear();
    for (const Color16 c : palettes[p].data) {
      out.pixels.push_back(c.r());
      out.pixels.push_back(c.g());
      out.pixels.push_back(c.b());
    }
    CHECK_EQ(out.pixels.size() * 2, sizeof(Palette::data) * 3);
    return out;
  }

  // Unpacks the 32-bit RGBA texture stored in this CarPix using palette 'p'.
  // The 'palette_msb' must be the 4-bit value from each face of the 3d model,
  // stored in the 4 MSB of each pixel in a UV-space image.
  Image Texture(int p, const Image& palette_msb) const {
    const Palette& palette = palettes[p];
    Image texture(width, height, 4);
    texture.pixels.clear();
    int i = 0;
    for (const uint8_t pixel : data) {
      {
        const uint8_t p_msb = palette_msb.pixels[i];
        const uint8_t p_lsb = pixel & 0xF;
        const uint8_t ic = p_msb | p_lsb;
        const Color16 c = palette.data[ic];
        texture.pixels.push_back(c.r());
        texture.pixels.push_back(c.g());
        texture.pixels.push_back(c.b());
        texture.pixels.push_back(c.opaque() ? 255 : 0);
        ++i;
      }
      {
        const uint8_t p_msb = palette_msb.pixels[i];
        const uint8_t p_lsb = (pixel >> 4);
        const uint8_t ic = p_msb | p_lsb;
        const Color16 c = palette.data[ic];
        texture.pixels.push_back(c.r());
        texture.pixels.push_back(c.g());
        texture.pixels.push_back(c.b());
        texture.pixels.push_back(c.opaque() ? 255 : 0);
        ++i;
      }
    }
    // TODO(commongear): This should be done in the OBJ exporter.
    // We'll use the first pixel of the last line to render un-textured faces.
    const int last_line = 4 * width * (height - 1);
    texture.pixels[last_line + 0] = 0;
    texture.pixels[last_line + 1] = 0;
    texture.pixels[last_line + 2] = 0;
    texture.pixels[last_line + 3] = 255;
    return texture;
  }

  // Creates a 32-bit RGBA texture for debugging color flags in palette 'p'.
  //  'palette_msb': see comments on Texture().
  // Output:
  //   red:   255 if emissive, 0 otherwise.
  //   red:   255 if emissive, 0 otherwise.
  //   blue:  255 if painted, 0 otherwise.
  Image FlagDebugTexture(int p, const Image& palette_msb) const {
    const Palette& palette = palettes[p];
    Image texture(width, height, 4);
    texture.pixels.clear();
    int i = 0;
    for (const uint8_t pixel : data) {
      {
        const uint8_t p_msb = palette_msb.pixels[i];
        const uint8_t p_lsb = pixel & 0xF;
        const uint8_t ic = p_msb | p_lsb;
        const Color16 c = palette.data[ic];
        texture.pixels.push_back(palette.is_emissive(ic) ? 255 : 0);
        texture.pixels.push_back(palette.is_emissive(ic) ? 255 : 0);
        texture.pixels.push_back(palette.is_painted(ic) ? 255 : 0);
        texture.pixels.push_back(c.opaque() ? 255 : 0);
        ++i;
      }
      {
        const uint8_t p_msb = palette_msb.pixels[i];
        const uint8_t p_lsb = (pixel >> 4);
        const uint8_t ic = p_msb | p_lsb;
        const Color16 c = palette.data[ic];
        texture.pixels.push_back(palette.is_emissive(ic) ? 255 : 0);
        texture.pixels.push_back(palette.is_emissive(ic) ? 255 : 0);
        texture.pixels.push_back(palette.is_painted(ic) ? 255 : 0);
        texture.pixels.push_back(c.opaque() ? 255 : 0);
        ++i;
      }
    }
    // TODO(commongear): This should be done in the OBJ exporter.
    // We'll use the first pixel of the last line to render un-textured faces.
    const int last_line = 4 * width * (height - 1);
    texture.pixels[last_line + 0] = 0;
    texture.pixels[last_line + 1] = 0;
    texture.pixels[last_line + 2] = 0;
    texture.pixels[last_line + 3] = 255;
    return texture;
  }
};

std::ostream& operator<<(std::ostream& os, const CarPix::Header& h) {
  return os << "{CarPix  num_palettes: " << h.num_palettes << "\n"
            << " palette_ids: " << ToString(h.palette_id) << "}";
}

std::ostream& operator<<(std::ostream& os, const CarPix::Palette& p) {
  for (int i = 0; i < 256; ++i) {
    os << (i % 16 == 0 ? "\n" : " ");
    p.data[i].WriteHex(os);
    os << (p.data[i].force_opaque() ? "o" : "_")
       << (p.is_emissive(i) ? "e" : "_") << (p.is_painted(i) ? "p" : "_");
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const CarPix& p) {
  os << p.header << "\n";
  // We don't need to write each palette color, but we can if we want to.
  // for (int i = 0; i < p.header.num_palettes; ++i) {
  //   os << p.palettes[i] << "\n";
  // }
  return os;
}

}  // namespace gt2

#endif  // GT2_EXTRACT_CAR_H_
