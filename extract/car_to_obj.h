// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_CAR_TO_OBJ_H_
#define GT2_EXTRACT_CAR_TO_OBJ_H_

#include <cmath>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "car.h"
#include "car_util.h"
#include "util/image.h"
#include "util/io.h"
#include "util/vec.h"

namespace gt2 {

// Counters for the current index of each type of element the OBJ output.
struct ObjState {
  int i_vert = 1;
  int i_normal = 1;
  int i_uv = 1;
};

// Rescales a model vertex and writes it to the stream.
inline void WriteObjVert(std::ostream& os, const float scale,
                         const Vec4<int16_t>& v) {
  // Cars are described in something like millimeters?
  const float k = scale;
  os << "v " << k * v.x << " " << k * v.y << " " << k * v.z << "\n";
}

// Rescales a model normal and writes it to the stream.
inline void WriteObjNorm(std::ostream& os, const Normal32& n) {
  os << "vn " << n.xf() << " " << n.yf() << " " << n.zf() << "\n";
}

// Extracts UVs from the given face and writes it to the OBJ.
inline void WriteObjUvs(std::ostream& os, const TexFace& f) {
  constexpr float kX = 1.0 / 256.0;
  constexpr float kY = 1.0 / 224.0;
  constexpr float dX = 0.5 * kX;
  constexpr float dY = 0.5 * kY;
  os << "vt " << kX * f.uv0.x + dX << " " << (1.0 - kY * f.uv0.y - dY) << "\n";
  os << "vt " << kX * f.uv1.x + dX << " " << (1.0 - kY * f.uv1.y - dY) << "\n";
  os << "vt " << kX * f.uv2.x + dX << " " << (1.0 - kY * f.uv2.y - dY) << "\n";
  if (f.is_quad()) {
    os << "vt " << kX * f.uv3.x + dX << " " << (1.0 - kY * f.uv3.y - dY)
       << "\n";
  }
}

// Writes a textured or untextured tri or quad into the stream.
inline void WriteObjFace(std::ostream& os, const ObjState& s, const Face& f) {
  // Function to write one face element (v/uv/n)
  const auto write = [&](int i) {
    os << " " << f.i_vert[i] + s.i_vert;
    if (f.is_textured()) os << "/" << s.i_uv + i;
    if (f.has_normals()) os << "/" << f.i_normal(i) + s.i_normal;
  };
  os << "f";
  write(0);
  write(1);
  write(2);
  if (f.is_quad()) write(3);
  os << "\n";
}

// Copies normals from faces that have them to matching faces that don't.
// This can occur if a face appers twice: once as a base paint layer, and once
// as a decal with transparency. See notes in 'WriteObj' below.
inline void TransferNormals(std::vector<TexFace>& faces) {
  std::unordered_map<uint32_t, Face*> need_normals;
  need_normals.reserve(faces.size() / 4);

  // Pick out faces that don't have normals, keyed by vertex-indices.
  //
  // NOTE: some models have hidden faces with duplicate geometry, which look
  // like errors that were missed by the original devs. By iterating over the
  // faces in drawing order, we skip transferring normals to the those faces.
  for (auto& f : faces) {
    if (!f.has_normals()) need_normals[f.i_vert_data] = &f;
  }

  // Copy normals from faces with matching vertex indices.
  //
  // Also, other models have multiple faces with normals, sometimes with
  // different UVs. Of these, the *first* face in the drawing order wins here.
  for (const auto& f : faces) {
    if (f.has_normals()) {
      const auto it = need_normals.find(f.i_vert_data);
      if (it != need_normals.end()) {
        it->second->CopyNormalIndicesFrom(f);
        need_normals.erase(it);
      }
    }
  }
}

// Writes a model to an OBJ file and updates the counts in 'state'.
// Multiple models can be written correctly to the same stream if the ObjState
// is reused between calls.
inline void WriteObj(std::ostream& os, ObjState& state, const Model& m) {
  const float scale = m.header.scale.to_meters();

  // Lots of cars have decals with transparency applied to some of the faces.
  // We do some gymastics to get these to render properly on modern hardware.

  // The decals share geometry with the face they're applied to, but obviously
  // have a different texture, part of which is transparent. The decal faces
  // always seem to come before the base paint faces in CDO/CNO files, so if we
  // reverse the face ordering, we can get the decals to render on top of
  // the base paint.
  auto tex_tris = m.tex_tris;
  auto tex_quads = m.tex_quads;
  std::reverse(std::begin(tex_tris), std::end(tex_tris));
  std::reverse(std::begin(tex_quads), std::end(tex_quads));

  // Curiously, in the CDO/CNO format, reflections appear to be rendered on the
  // *decal* faces (even where they are transparent), not the base paint faces.
  // As a result, the base paint faces have no normals! This probably makes
  // sense given the hardware they had, but it doesn't work for us, so copy the
  // normals from the decal faces to the matching base-paint faces.
  TransferNormals(tex_tris);
  TransferNormals(tex_quads);

  // Write the OBJ vertex data (positions, normals, uvs).
  for (const auto& v : m.verts) WriteObjVert(os, scale, v);
  for (const auto& n : m.normals) WriteObjNorm(os, n);
  for (const auto& f : tex_tris) WriteObjUvs(os, f);
  for (const auto& f : tex_quads) WriteObjUvs(os, f);

  // Write untextured faces.
  os << "usemtl Untextured\n";
  for (const auto& f : m.tris) WriteObjFace(os, state, f);
  for (const auto& f : m.quads) WriteObjFace(os, state, f);

  // Write textured faces with NO NORMALS as 'Diffuse'.
  os << "usemtl Diffuse\n";
  const int i_uv_start = state.i_uv;
  for (const auto& f : tex_tris) {
    if (!f.has_normals()) WriteObjFace(os, state, f);
    state.i_uv += 3;
  }
  for (const auto& f : tex_quads) {
    if (!f.has_normals()) WriteObjFace(os, state, f);
    state.i_uv += 4;
  }
  const int i_uv_end = state.i_uv;

  // Write textured faces that have normals using the 'Reflective' material.
  os << "usemtl Reflective\n";
  state.i_uv = i_uv_start;
  for (const auto& f : tex_tris) {
    if (f.has_normals()) WriteObjFace(os, state, f);
    state.i_uv += 3;
  }
  for (const auto& f : tex_quads) {
    if (f.has_normals()) WriteObjFace(os, state, f);
    state.i_uv += 4;
  }
  CHECK_EQ(state.i_uv, i_uv_end);

  // Update the other counts with what we wrote.
  state.i_vert += m.verts.size();
  state.i_normal += m.normals.size();
}

// Writes the car object and pix files to an OBJ.
inline void SaveObj(const CarObject& cdo, const CarPix& cdp,
                    const std::string& path, const std::string& name,
                    bool make_wheels) {
  CHECK(EndsWith(name, ".cd") || EndsWith(name, ".cn"),
        "Output name should end with '.cd' or '.cn' to avoid name collisions "
        "between models.",
        name);

  const Image pixels = cdp.Pixels();
  const CarObject::UvPalette uv_palette = cdo.DrawUvPalette();

  // Debugging images.
  Save(pixels.ToPng(), path + name + "p.pixels.png");
  Save(uv_palette.index.ToPng(), path + name + "p.uv_palette.png");
  Save(uv_palette.mask.ToPng(), path + name + "p.uv_palette_mask.png");

  // Save the textures using each of the palettes.
  for (int i = 0; i < cdp.palettes.size(); ++i) {
    const std::string texture_path = StrCat(path, name, "p.", i);

    const Image palette = cdp.PaletteImage(i);
    Save(palette.ToPng(), texture_path + ".palette.png");

    const Image flags = cdp.FlagDebugTexture(i, uv_palette.index);
    Save(flags.ToPng(), texture_path + ".flags.png");

    const Image texture = cdp.Texture(i, uv_palette.index, uv_palette.mask);
    Save(texture.ToPng(), texture_path + ".png");

    const Image brake_texture = cdp.BrakeLightTexture(i, uv_palette.index);
    Save(brake_texture.ToPng(), texture_path + ".brake.png");
  }

  {  // Write the MTL file.
    std::fstream f;
    f.open(path + name + "o.mtl", std::ios::out);
    f << "newmtl Reflective\n";
    f << "  Ka 0.0 0.0 0.0\n";
    f << "  Kd 1.0 1.0 1.0\n";
    f << "  Ks 1.0 1.0 1.0\n";
    f << "  illum 3\n";
    f << "  Ns 5000.0\n";
    f << "  map_Kd " << name + "p.0.png\n";
    f << "\n";
    f << "newmtl Diffuse\n";
    f << "  Ka 0.0 0.0 0.0\n";
    f << "  Kd 1.0 1.0 1.0\n";
    f << "  Ks 0.0 0.0 0.0\n";
    f << "  illum 1\n";
    f << "  map_Kd " << name + "p.0.png\n";
    f << "\n";
    f << "newmtl Untextured\n";
    f << "  Ka 0.0 0.0 0.0\n";
    f << "  Kd 0.0 0.0 0.0\n";
    f << "  Ks 0.0 0.0 0.0\n";
    f << "  illum 1\n";
    f << "\n";
  }

  {  // Write the JSON manifest (this is read by the three.js viewer).
    std::fstream f;
    f.open(path + name + "o.json", std::ios::out);
    f << "{\n";
    f << "  \"lods\": " << cdo.num_lods << ",\n";
    f << "  \"palettes\": " << cdp.palettes.size() << "\n";
    f << "}\n";
  }

  // Make some wheels.
  std::vector<Model> wheels;
  if (make_wheels) {
    wheels.reserve(4);
    for (int i = 0; i < 4; ++i) {
      wheels.push_back({});
      MakeWheel(cdo.header.wheel_pos[i], cdo.header.wheel_size[i / 2],
                wheels.back());
    }
  }

  // Write an OBJ file for each LOD.
  for (int i = 0; i < cdo.num_lods; ++i) {
    const std::string lod_name = StrCat(path, name, "o.", i, ".obj");

    std::fstream f(lod_name, std::ios::out | std::ios::binary);
    f << "mtllib " << name + "o.mtl\n";

    ObjState state;
    WriteObj(f, state, cdo.lods[i]);
    for (const auto& w : wheels) WriteObj(f, state, w);
  }

  // TODO(commongear): write the shadow to the OBJ.
}

}  // namespace gt2

#endif  // GT2_EXTRACT_CAR_TO_OBJ_H_
