// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_OBJ_H_
#define GT2_OBJ_H_

// Extremely slow and unoptimized OBJ loading. Would not recommend general use.

#include <cstring>
#include <sstream>
#include <string>

#include "inspect.h"
#include "vec.h"

namespace gt2 {

// One element of an OBJ face. E.g. '1/2/3' of 'f 1/2/3 4/5/6 7/8/9'.
struct ObjFaceElement {
  int indices[3] = {0, 0, 0};

  int i_vert() const { return indices[0]; }
  int i_uv() const { return indices[1]; }
  int i_norm() const { return indices[2]; }

  static ObjFaceElement FromString(std::string_view element) {
    ObjFaceElement out;
    auto tokens = Split(element, "/");
    for (int i = 0; i < 3 && i < tokens.size(); ++i) {
      std::stringstream ss((std::string(tokens[i])));
      ss >> out.indices[i];
    }
    return out;
  }
};

// Stringifies a ObjFaceElement as if it were part of an OBJ.
std::ostream& operator<<(std::ostream& os, const ObjFaceElement& e) {
  os << e.i_vert();
  if (e.i_uv() || e.i_norm()) {
    os << "/";
    if (e.i_uv()) os << e.i_uv();
    if (e.i_norm()) os << "/" << e.i_norm();
  }
  return os;
}

// One OBJ face. E.g. 'f 1/2/3 4/5/6 7/8/9'.
struct ObjFace {
  ObjFaceElement elements[4];
  bool is_quad = false;

  bool has_verts() const {
    const int n = is_quad ? 4 : 3;
    for (int i = 0; i < n; ++i) {
      if (elements[i].i_vert() == 0) return false;
    }
    return true;
  }

  bool has_uvs() const {
    const int n = is_quad ? 4 : 3;
    for (int i = 0; i < n; ++i) {
      if (elements[i].i_uv() == 0) return false;
    }
    return true;
  }

  bool has_norms() const {
    const int n = is_quad ? 4 : 3;
    for (int i = 0; i < n; ++i) {
      if (elements[i].i_norm() == 0) return false;
    }
    return true;
  }
};

// Stringifies a ObjFace as if it were part of an OBJ.
std::ostream& operator<<(std::ostream& os, const ObjFace& f) {
  os << "f";
  for (int i = 0; i < 3; ++i) os << " " << f.elements[i];
  if (f.is_quad) os << " " << f.elements[3];
  return os;
}

// One (very basic) decoded OBJ.
struct Obj {
  std::vector<Vec4<float>> verts;
  std::vector<Vec4<float>> normals;
  std::vector<Vec2<float>> uvs;
  std::vector<ObjFace> faces;

  static Obj FromString(const std::string& data) {
    Obj o;
    o.verts.reserve(256);
    o.normals.reserve(512);
    o.uvs.reserve(512);
    o.faces.reserve(512);

    std::stringstream ss(data);
    std::string token;
    ss >> token;
    while (!token.empty()) {
      if (token == "v") {
        Vec4<float> v;
        v.w = 0;
        ss >> v.x >> v.y >> v.z;
        o.verts.push_back(v);
        if (!(ss >> token)) break;
      } else if (token == "vt") {
        Vec2<float> uv;
        ss >> uv.x >> uv.y;
        o.uvs.push_back(uv);
        if (!(ss >> token)) break;
      } else if (token == "vn") {
        Vec4<float> n;
        n.w = 0;
        ss >> n.x >> n.y >> n.z;
        o.normals.push_back(n);
        if (!(ss >> token)) break;
      } else if (token == "f") {
        std::string face_str;
        std::getline(ss, face_str);

        std::stringstream face_ss(face_str);
        std::string a, b, c, d;
        face_ss >> a >> b >> c >> d;

        ObjFace f;
        f.elements[0] = ObjFaceElement::FromString(a);
        f.elements[1] = ObjFaceElement::FromString(b);
        f.elements[2] = ObjFaceElement::FromString(c);
        if (d.empty()) {
          f.is_quad = false;
        } else {
          f.is_quad = true;
          f.elements[3] = ObjFaceElement::FromString(d);
        }
        o.faces.push_back(f);

        if (!(ss >> token)) break;
      } else {
        std::cerr << "Unknown token '" << token << "' skipping line."
                  << std::endl;
        std::getline(ss, token);
        if (!(ss >> token)) break;
      }
    }

    return o;
  }
};

}  // namespace gt2

#endif  // GT2_OBJ_H_
