// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_CAR_UTIL_H_
#define GT2_EXTRACT_CAR_UTIL_H_

#include <cmath>
#include <vector>

#include "util/vec.h"

namespace gt2 {

constexpr float kPif = 3.14159265;

////////////////////////////////////////////////////////////////////////////////
// Nothing that's part of the car file formats, but still useful.
////////////////////////////////////////////////////////////////////////////////

// Builds a rudimentary wheel and stores it in 'm'.
// 'pos' is a wheel position in a Model (see car.h).
void MakeWheel(Vec4<int16_t> pos, CarObject::Header::WheelSize size, Model& m) {
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

}  // namespace gt2

#endif  // GT2_EXTRACT_CAR_UTIL_H_
