// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_VEC_H_
#define GT2_EXTRACT_VEC_H_

#include <cstring>

#include "inspect.h"

namespace gt2 {

template <typename T>
union Vec2 {
  struct {
    T x, y;
  };
  T data[2];
  uint16_t data2;

  Vec2() = default;
  Vec2(const T& _x, const T& _y) : x(_x), y(_y) {}
  bool operator==(const Vec2<T>& v) const {
    return std::memcmp(data, v.data, sizeof(data) * sizeof(T)) == 0;
  }
  bool operator<(const Vec2<T>& v) const { return data2 < v.data2; }

  // Element-wise min/max.
  Vec2<T> Min(const Vec2<T>& v) const {
    return {std::min(x, v.x), std::min(y, v.y)};
  }
  Vec2<T> Max(const Vec2<T>& v) const {
    return {std::max(x, v.x), std::max(y, v.y)};
  }
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const Vec2<T> v) {
  return os << "[" << ToString(v.data) << "]";
}

template <typename T>
union Vec4 {
  struct {
    T x, y, z, w;
  };
  T data[4];
  uint32_t data4;

  Vec4() = default;
  Vec4(const T& _x, const T& _y, const T& _z, const T& _w)
      : x(_x), y(_y), z(_z), w(_w) {}
  bool operator==(const Vec2<T>& v) const {
    return std::memcmp(data, v.data, sizeof(data) * sizeof(T)) == 0;
  }
  bool operator<(const Vec4<T>& v) const { return data4 < v.data4; }

  Vec4<T>& operator+=(const Vec4<T>& v) {
    for (int i = 0; i < 4; ++i) data[i] += v.data[i];
    return *this;
  }

  Vec4<T>& operator*=(double k) {
    for (int i = 0; i < 4; ++i) data[i] *= k;
    return *this;
  }

  template <typename U>
  Vec4<double> operator-(const Vec4<U>& v) const {
    Vec4<double> out(x, y, z, w);
    for (int i = 0; i < 4; ++i) out.data[i] -= v.data[i];
    return out;
  }

  template <typename U>
  Vec4<double> operator-(const U& v) const {
    Vec4<double> out(x, y, z, w);
    for (int i = 0; i < 4; ++i) out.data[i] -= v;
    return out;
  }

  template <typename U>
  Vec4<double> operator+(const Vec4<U>& v) const {
    Vec4<double> out(x, y, z, w);
    for (int i = 0; i < 4; ++i) out.data[i] += v.data[i];
    return out;
  }

  Vec4<double> operator*(double k) const {
    Vec4<double> out(x, y, z, w);
    for (int i = 0; i < 4; ++i) out.data[i] *= k;
    return out;
  }

  template <typename U>
  Vec4<double> Cross(const Vec4<U>& v) const {
    Vec4<double> temp(x, y, z, w);
    return {
        temp.y * v.z - temp.z * v.y,
        temp.z * v.x - temp.x * v.z,
        temp.x * v.y - temp.y * v.x,
        0.0,
    };
  }

  template <typename U>
  double Dot(const Vec4<U>& v) const {
    Vec4<double> temp(x, y, z, w);
    return temp.x * v.x + temp.y * v.y + temp.z * v.z;
  }

  double LengthSq() const { return 1.0 * x * x + 1.0 * y * y + 1.0 * z * z; }

  void Normalize() {
    const double k = 1.0 / std::sqrt(LengthSq());
    x *= k;
    y *= k;
    z *= k;
    w = 0;
  }

  // Element-wise min/max.
  Vec4<T> Min(const Vec4<T>& v) const {
    return {std::min(x, v.x), std::min(y, v.y), std::min(z, v.z),
            std::min(w, v.w)};
  }
  Vec4<T> Max(const Vec4<T>& v) const {
    return {std::max(x, v.x), std::max(y, v.y), std::max(z, v.z),
            std::max(w, v.w)};
  }
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const Vec4<T> v) {
  return os << "[" << ToString(v.data) << "]";
}

}  // namespace gt2

#endif  // GT2_EXTRACT_VEC_H_
