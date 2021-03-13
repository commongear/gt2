// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_CAR_FROM_OBJ_H_
#define GT2_EXTRACT_CAR_FROM_OBJ_H_

#include <cmath>
#include <map>
#include <ostream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "car.h"
#include "util/color.h"
#include "util/image.h"
#include "util/io.h"
#include "util/obj.h"
#include "util/vec.h"

namespace gt2 {

// Converts regular RGBA to gt2's 16-bit colors with opacity flag.
//  - If 'a' is zero, the color is considered transparent.
//  - Fully opaque otherwise.
//  - The explicit opacity flag is only set for black.
inline Color16 RgbaToColor16(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (a == 0) return Color16(); // Empty if transparent.
  const uint8_t force_opaque = (r == 0 && g == 0 && b == 0);
  return Color16::FromRgb8(r, g, b, force_opaque);
}

// Converts an OBJ vector to a Car vector.
inline Vec4<int16_t> ToCarVec(const Vec4<float>& meters, float scale) {
  return Vec4<int16_t>(scale * meters.x + 0.5f, scale * meters.y + 0.5f,
                       scale * meters.z + 0.5f, 0);
}

// Converts an OBJ UV to a CDO UV within texture dimsmensions (256, 224).
inline Vec2<uint8_t> ToCarUv(const Vec2<float>& uv) {
  return Vec2<uint8_t>(std::max(0.f, std::min(255.f, 256.f * uv.x)),
                       std::max(0.f, std::min(223.f, 224.f - 224.f * uv.y)));
}

// Converts an OBJ Face to a CDO face, given the UVs from the OBJ.
// NOTE: CDO faces store the UV coordinates directly, wheres OBJ stores just the
// UV indices in each face.
inline TexFace ToCarFace(const ObjFace& f,
                         const std::vector<Vec2<float>>& uvs) {
  TexFace out;

  // Flags -------------------------------------------------------------------

  out.set_i_palette(1);   // A reasonable default. We'll need to reset later.
  out.data_a = (1 << 4);  // TODO(commongear): verify rendering order?
  if (f.has_norms()) {
    out.data_b = (1 << 15);  // "has normals"
  } else {
    out.data_b = 0;
  }
  out.data_c = 0;
  out.data_d = 0;

  // Verts -------------------------------------------------------------------

  CHECK(f.has_verts());

  out.i_vert[0] = f.elements[0].i_vert() - 1;
  out.i_vert[1] = f.elements[1].i_vert() - 1;
  out.i_vert[2] = f.elements[2].i_vert() - 1;

  if (f.is_quad) {
    out.i_vert[3] = f.elements[3].i_vert() - 1;
    out.set_quad();
  } else {
    out.i_vert[3] = 0;
    out.set_tri();
  }

  // UVs ---------------------------------------------------------------------

  if (f.has_uvs()) {
    out.uv0 = ToCarUv(uvs[f.elements[0].i_uv() - 1]);
    out.uv1 = ToCarUv(uvs[f.elements[1].i_uv() - 1]);
    out.uv2 = ToCarUv(uvs[f.elements[2].i_uv() - 1]);

    if (f.is_quad) {
      out.uv3 = ToCarUv(uvs[f.elements[3].i_uv() - 1]);
    } else {
      out.uv3.x = out.uv3.y = 0;
    }
    out.set_textured();
  } else {
    out.set_untextured();
  }

  // Normals -----------------------------------------------------------------

  if (f.has_norms()) {
    const uint16_t n0 = f.elements[0].i_norm() - 1;
    const uint16_t n1 = f.elements[1].i_norm() - 1;
    const uint16_t n2 = f.elements[2].i_norm() - 1;

    if (f.is_quad) {
      const uint16_t n3 = f.elements[3].i_norm() - 1;
      out.set_i_normals(n0, n1, n2, n3);
    } else {
      out.set_i_normals(n0, n1, n2);
    }
  }

  return out;
}

// Updates a CDO LOD from the given OBJ.
inline void UpdateFromObj(const Obj& o, Model& m) {
  m.verts.clear();
  m.normals.clear();
  m.tris.clear();
  m.quads.clear();
  m.tex_tris.clear();
  m.tex_quads.clear();

  // Compute Bounds. This should be in meters.
  CHECK_GT(o.verts.size(), 0);
  Vec4<float> lo = o.verts[0];
  Vec4<float> hi = o.verts[0];
  for (const auto& v : o.verts) {
    lo.x = std::min(lo.x, v.x);
    lo.y = std::min(lo.y, v.y);
    lo.z = std::min(lo.z, v.z);
    lo.w = std::min(lo.w, v.w);
    hi.x = std::max(hi.x, v.x);
    hi.y = std::max(hi.y, v.y);
    hi.z = std::max(hi.z, v.z);
    hi.w = std::max(hi.w, v.w);
  }

  // Pick a scale factor. I haven't seen any values > 8192, so we'll be safe.
  const float absmax = std::max(
      std::max(std::max(std::abs(lo.x), std::abs(lo.y)), std::abs(lo.z)),
      std::max(std::max(std::abs(hi.x), std::abs(hi.y)), std::abs(hi.z)));
  Scale16 scale;
  while (scale.value < 32 && absmax / scale.to_meters() > 8192) ++scale.value;
  CHECK_LT(scale.value, 32);
  const float to_car_scale = 1.f / scale.to_meters();
  m.header.scale = scale;

  // Convert the bounds.
  m.header.lo_bound = ToCarVec(lo, to_car_scale);
  m.header.hi_bound = ToCarVec(hi, to_car_scale);

  // Convert verts.
  m.verts.reserve(o.verts.size());
  for (const auto& v : o.verts) m.verts.push_back(ToCarVec(v, to_car_scale));

  // Convert normals.
  int num_bad_normals = 0;
  m.normals.reserve(o.normals.size());
  for (const auto& n : o.normals) {
    Normal32 mn;
    mn.setf(n.x, n.y, n.z);
    m.normals.push_back(mn);
    if (!mn.Validate()) {
      std::cout << mn << " has bad length: " << mn.lenf() << std::endl;
      ++num_bad_normals;
    }
  }
  if (num_bad_normals) {
    std::cerr << "Bad normals: " << num_bad_normals << " of "
              << m.normals.size() << std::endl;
  }

  // Convert faces and UVs.
  for (const auto& f : o.faces) {
    if (f.is_quad) {
      if (f.has_uvs()) {
        m.tex_quads.push_back(ToCarFace(f, o.uvs));
      } else {
        m.quads.push_back(ToCarFace(f, o.uvs));
      }
    } else {
      if (f.has_uvs()) {
        m.tex_tris.push_back(ToCarFace(f, o.uvs));
      } else {
        m.tris.push_back(ToCarFace(f, o.uvs));
      }
    }
  }

  // Update the header.
  m.header.num_verts = m.verts.size();
  m.header.num_normals = m.normals.size();
  m.header.num_tris = m.tris.size();
  m.header.num_quads = m.quads.size();
  m.header.num_tex_tris = m.tex_tris.size();
  m.header.num_tex_quads = m.tex_quads.size();
}

// Extracts the color palette from the given CDO face and texture.
//  - Returned values are the number of pixels with the given color.
//  - 'face_index' is written to each affected pixel of 'face_indices' (over-
//    writing any data which already exists there.)
inline std::map<Color16, uint16_t> ExtractFacePalette(
    const TexFace& f, const Image8& tex, uint16_t face_index,
    Image<uint16_t>& face_indices) {
  CHECK_EQ(face_indices.width, tex.width);
  CHECK_EQ(face_indices.height, tex.height);
  CHECK_EQ(face_indices.channels, 1);
  CHECK_EQ(tex.channels, 4);

  // UVs are 8-bit.
  CHECK_LE(tex.width, 256);
  CHECK_LE(tex.height, 256);

  // Bounds of the face UVs.
  auto lo = f.uv0.Min(f.uv1).Min(f.uv2);
  auto hi = f.uv0.Max(f.uv1).Max(f.uv2);

  // Quads have 4 uvs.
  if (f.is_quad()) {
    lo = lo.Min(f.uv3);
    hi = hi.Max(f.uv3);
  }

  // Make sure 'lo' and 'hi' are within the texture bounds.
  lo = lo.Max(Vec2<uint8_t>(0, 0));
  hi = hi.Min(Vec2<uint8_t>(tex.width - 1, tex.height - 1));

  // Find the pixels which are part of this triangle.
  face_indices.DrawTriangle(f.uv0, f.uv1, f.uv2, face_index);
  if (f.is_quad()) face_indices.DrawTriangle(f.uv0, f.uv2, f.uv3, face_index);

  // Assign each pixel to the face's palette.
  std::map<Color16, uint16_t> p;
  for (int y = lo.y; y <= hi.y; ++y) {
    const int x0 = y * tex.width;
    const int tx0 = y * tex.width * 4;
    for (int x = lo.x; x <= hi.x; ++x) {
      if (face_indices.pixels[x + x0] != face_index) continue;
      const uint8_t r = tex.pixels[x * 4 + tx0 + 0];
      const uint8_t g = tex.pixels[x * 4 + tx0 + 1];
      const uint8_t b = tex.pixels[x * 4 + tx0 + 2];
      const uint8_t a = tex.pixels[x * 4 + tx0 + 3];
      const auto color = RgbaToColor16(r, g, b, a);
      ++p[color];
    }
  }

  return p;
}

// A palette, and its associated Model faces.
//  - Face index 0 is the first TexTri face from the model.
//  - TexQuad face indices start immediately after TexTri faces.
struct PaletteData {
  // Color -> num_pixels.
  std::map<Color16, uint16_t> colors;
  // Face indices with this palette.
  std::unordered_set<uint16_t> face_index;
};

// Colors from a given texture, and their associated face indices.
struct TexturePaletteData {
  // All the palettes from the texture.
  std::vector<PaletteData> palettes;
  // Face index of each pixel in UV coordinates.
  //  - Several faces may map to the same area in the texture. In this case,
  //    only one face index is recorded (not specified which one).
  //  - Index 0 is the first TexTri face.
  //  - TexQuad face indices start immediately after TexTri faces.
  Image16 face_indices = Image16(0, 0, 0);
};

// Merges all the colors and faces from 'b' into 'a'.
inline void PaletteDataUnion(PaletteData& a, const PaletteData& b) {
  for (const auto& kv : b.colors) a.colors[kv.first] += kv.second;
  for (const auto& f : b.face_index) a.face_index.insert(f);
}

// Extracts the color palette from 'tex' for each face of Model 'm'.
inline TexturePaletteData ExtractFacePalettes(const Image8& tex,
                                              const Model& m) {
  CHECK_EQ(tex.channels, 4);

  const int num_faces = m.tex_tris.size() + m.tex_quads.size();

  TexturePaletteData out;
  out.palettes.resize(num_faces);
  out.face_indices = Image16(tex.width, tex.height, /*channels=*/1);
  constexpr auto kInvalidFace = std::numeric_limits<uint16_t>::max();
  for (int i = 0; i < out.face_indices.pixels.size(); ++i) {
    out.face_indices.pixels[i] = kInvalidFace;
  }

  // Make a palette per face.
  int face_index = 0;
  for (const auto& f : m.tex_tris) {
    CHECK(!f.is_quad());
    auto& p = out.palettes[face_index];
    p.colors = ExtractFacePalette(f, tex, face_index, out.face_indices);
    p.face_index.insert(face_index);
    ++face_index;
  }
  for (const auto& f : m.tex_quads) {
    CHECK(f.is_quad());
    auto& p = out.palettes[face_index];
    p.colors = ExtractFacePalette(f, tex, face_index, out.face_indices);
    p.face_index.insert(face_index);
    ++face_index;
  }
  CHECK_EQ(face_index, num_faces);

  return out;
}

// Extracts the color palette from 'tex' for the 48x48 px wheel area.
inline TexturePaletteData ExtractWheelPalette(const Image8& tex) {
  CHECK_EQ(tex.channels, 4);

  // Only one palette to return.
  TexturePaletteData out;
  out.palettes.resize(1);
  out.face_indices = Image16(tex.width, tex.height, /*channels=*/1);
  constexpr auto kInvalidFace = std::numeric_limits<uint16_t>::max();
  for (int i = 0; i < out.face_indices.pixels.size(); ++i) {
    out.face_indices.pixels[i] = kInvalidFace;
  }

  // Hallucinate a face for the wheel.
  TexFace f;
  f.set_quad();
  f.uv0 = {0, 0};
  f.uv1 = {0, 48};
  f.uv2 = {48, 48};
  f.uv3 = {48, 0};

  // Extract the colors, write the index image, and get outa dodge.
  auto& p = out.palettes[0];
  p.colors = ExtractFacePalette(f, tex, /*face_index=*/0, out.face_indices);
  p.face_index.insert(0);

  return out;
}

// Compresses and quantizes the palette data to fit constraints.
//  - Result will contain no more than 'max_palettes' entries.
//  - Each palette will contain no more than 'max_colors'.
//  - Palettes with no faces are removed.
inline void MergePalettes(std::vector<PaletteData>& palettes, int max_palettes,
                          int max_colors) {
  // Get rid of palettes that have no face.
  std::sort(palettes.begin(), palettes.end(),
            [](const PaletteData& a, const PaletteData& b) {
              return a.face_index.size() > b.face_index.size();
            });
  while (!palettes.empty() && palettes.back().face_index.empty()) {
    palettes.pop_back();
  }

  // Quantize each palette if it has too many colors.
  for (auto& p : palettes) QuantizeColors(p.colors, max_colors);

  // Sort by number of colors (largest first).
  std::sort(palettes.begin(), palettes.end(),
            [](const PaletteData& a, const PaletteData& b) {
              return a.colors.size() > b.colors.size();
            });

  // Merge palettes which are exactly overlapping.
  for (int i = 0; i < palettes.size(); ++i) {
    auto& target = palettes[i];
    for (int j = i + 1; j < palettes.size();) {
      auto& source = palettes[j];
      const PaletteDist dist = ComputePaletteDist(target.colors, source.colors);
      if (dist.sum_sq_dist == 0) {
        PaletteDataUnion(target, source);
        CHECK_LE(target.colors.size(), max_colors);
        std::swap(source, palettes.back());
        palettes.pop_back();
      } else {
        ++j;
      }
    }
  }

  // Merge palettes which can be merged without exceeding 'max_colors'.
  for (int i = 0; i < palettes.size(); ++i) {
    auto& target = palettes[i];
    for (int j = i + 1; j < palettes.size();) {
      auto& source = palettes[j];
      const PaletteDist dist = ComputePaletteDist(target.colors, source.colors);
      if (dist.union_size <= max_colors) {
        PaletteDataUnion(target, source);
        CHECK_LE(target.colors.size(), max_colors);
        std::swap(source, palettes.back());
        palettes.pop_back();
      } else {
        ++j;
      }
    }
  }

  // Sort by number of colors (largest first).
  std::sort(palettes.begin(), palettes.end(),
            [](const PaletteData& a, const PaletteData& b) {
              return a.colors.size() > b.colors.size();
            });

  // Destructive: quantize colors until 'max_palettes' is satisfied.
  for (int i = palettes.size() - 1; i >= max_palettes; --i) {
    const auto& source = palettes[i];

    int j_best = 0;
    int64_t dist_best = kInt64Max;

    // Find the best target palette.
    for (int j = 0; j < i; ++j) {
      const PaletteDist dist =
          ComputePaletteDist(palettes[j].colors, source.colors);
      if (dist.sum_sq_dist < dist_best) {
        dist_best = dist.sum_sq_dist;
        j_best = j;
      }
    }

    // Merge and quantize.
    PaletteDataUnion(palettes[j_best], source);
    QuantizeColors(palettes[j_best].colors, max_colors);
    palettes.pop_back();
  }
}

inline void AssignPaletteIndicesToFaces(
    const std::vector<PaletteData>& palettes, int first_palette_index,
    Model& m) {
  // Reset all palette indices, in case some faces are unaccounted-for.
  for (auto& f : m.tex_tris) f.set_i_palette(0);
  for (auto& f : m.tex_quads) f.set_i_palette(0);

  // Loop over all palettes.
  const int i0 = first_palette_index;
  const int num_tex_tris = m.tex_tris.size();
  for (int i = 0; i < palettes.size(); ++i) {
    const auto& p = palettes[i];
    // For each face index in the palette, lookup the face and assign the index.
    for (const auto& i_face : p.face_index) {
      if (i_face < num_tex_tris) {
        m.tex_tris[i_face].set_i_palette(i0 + i);
      } else {
        m.tex_quads[i_face - num_tex_tris].set_i_palette(i0 + i);
      }
    }
  }
}

// Returns an initialized CDP with a cleared palette and empty texture data.
inline CarPix InitCarPix() {
  CarPix out;

  // Initialize header palette data.
  out.header.num_palettes = 1;
  out.header.palette_id[0] = 1;
  for (int i = 1; i < 30; ++i) out.header.palette_id[i] = 0;

  // Create one cleared palette.
  out.palettes.push_back({});
  for (int i = 0; i < 256; ++i) out.palettes[0].data[i] = {};
  for (int i = 0; i < 16; ++i) out.palettes[0].is_emissive_data[i] = 0;
  for (int i = 0; i < 16; ++i) out.palettes[0].is_painted_data[i] = 0;

  // Initialize (and clear) data for 8bpp.
  out.data.resize(out.width * out.height);

  return out;
}

// Updates rows of the sub-palette starting with the given index.
inline void UpdateCarPixSubPalettes(
    const std::vector<PaletteData>& palettes,
    int first_sub_palette_index, CarPix::Palette& cdp_palette) {

  const int i0 = first_sub_palette_index;
  CHECK_LE(i0 + palettes.size(), 16);

  // Push all palette colors into the CDP palette.
  for (int i = 0; i < palettes.size(); ++i) {
    const auto& p = palettes[i];
    CHECK_LE(p.colors.size(), 16);
    int j = 0;
    for (const auto& kv : p.colors) {
      cdp_palette.data[16 * (i0 + i) + j] = kv.first;
      ++j;
    }
  }
}

// Updates color index data for a CarPix based on texture and palettes.
//  - Only updates 'out_index' where 'data.face_indices' is valid.
//  - 'out_mask' set to 255 for each updated pixel.
inline void UpdateCarPixColorIndex(const Image8& texture,
                                     const TexturePaletteData& data,
                                     Image8& out_index, Image8& out_mask) {
  CHECK_EQ(texture.width, texture.width);
  CHECK_EQ(texture.height, texture.height);
  CHECK_EQ(texture.width, texture.width);
  CHECK_EQ(texture.height, texture.height);
  CHECK_EQ(texture.width, data.face_indices.width);
  CHECK_EQ(texture.height, data.face_indices.height);

  // Make a mapping from face to CDO palette index.
  std::unordered_map<uint16_t, uint16_t> face_to_palette;
  face_to_palette.reserve(256);
  for (int i = 0; i < data.palettes.size(); ++i) {
    for (const auto& f : data.palettes[i].face_index) {
      face_to_palette[f] = i;
    }
  }

  // For each texture pixel, lookup the palette and assign the nearest color.
  // NOTE: we do this at 8bpp. We'll pack to 4bpp later.
  const int num_pixels = data.face_indices.pixels.size();
  CHECK_EQ(num_pixels, texture.width * texture.height);

  CHECK_EQ(texture.channels, 4);
  for (int i = 0; i < num_pixels; ++i) {
    // Read the texture.
    const uint8_t r = texture.pixels[4 * i + 0];
    const uint8_t g = texture.pixels[4 * i + 1];
    const uint8_t b = texture.pixels[4 * i + 2];
    const uint8_t a = texture.pixels[4 * i + 3];

    // Color is zero if transparent.
    const auto color = RgbaToColor16(r, g, b, a);

    // Pick the sub-palette.
    const uint16_t index = data.face_indices.pixels[i];

    // If the index is invalid, skip this pixel.
    const auto it = face_to_palette.find(index);
    if (it == face_to_palette.end()) continue;
    const uint16_t i_palette = it->second;

    // Find the palette.
    const std::map<Color16, uint16_t>& colors = data.palettes[i_palette].colors;

    // Scan the palette row to find the best matching color for this pixel.
    int c_best = 0;
    int64_t d_best = kInt64Max;
    int c = 0;
    for (const auto& kv : colors) {
      const int64_t d = ColorDistSq(color, kv.first);
      if (d < d_best) {
        c_best = c;
        d_best = d;
      }
      ++c;
    }
    out_index.pixels[i] = c_best;
    out_mask.pixels[i] = 255;
  }
}

// Packs a CarPix color indices from 8bpp to 4bpp.
void PackCarPixData(const Image8& index, CarPix& cdp) {
  CHECK_EQ(index.width, cdp.width);
  CHECK_EQ(index.height, cdp.height);
  CHECK_EQ(index.channels, 1);

  const int num_pixels = index.pixels.size();
  CHECK_EQ(num_pixels, cdp.width * cdp.height);
  for (int i = 0; i < num_pixels; i += 2) {
    cdp.data[i / 2] = (index.pixels[i + 1] << 4) | index.pixels[i];
  }
  cdp.data.resize(num_pixels / 2);
}

}  // namespace gt2

#endif  // GT2_EXTRACT_CAR_FROM_OBJ_H_
