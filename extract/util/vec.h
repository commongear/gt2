// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_VECTOR_H_
#define GT2_EXTRACT_VECTOR_H_

#include <cstring>

namespace gt2 {

template <typename T>
union Vec2 {
  struct {
    T x, y;
  };
  T data[2];
  Vec2() = default;
  Vec2(const T& _x, const T& _y) : x(_x), y(_y) {}
  bool operator==(const Vec2<T>& other) const {
    return std::memcmp(data, other.data, sizeof(data) * sizeof(T)) == 0;
  }
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const Vec2<T> v) {
  return os << std::vector<T>(v.data, v.data + 2);
}

template <typename T>
union Vec4 {
  struct {
    T x, y, z, w;
  };
  T data[4];
  Vec4() = default;
  Vec4(const T& _x, const T& _y, const T& _z, const T& _w)
      : x(_x), y(_y), z(_z), w(_w) {}
  bool operator==(const Vec2<T>& other) const {
    return std::memcmp(data, other.data, sizeof(data) * sizeof(T)) == 0;
  }

  Vec4<T>& operator+=(const Vec4<T>& other) {
    for (int i = 0; i < 4; ++i) data[i] += other.data[i];
    return *this;
  }

  Vec4<T>& operator*=(double k) {
    for (int i = 0; i < 4; ++i) data[i] *= k;
    return *this;
  }

  template <typename U>
  Vec4<double> operator-(const Vec4<U>& other) const {
    Vec4<double> out(x, y, z, w);
    for (int i = 0; i < 4; ++i) out.data[i] -= other.data[i];
    return out;
  }

  template <typename U>
  Vec4<double> operator-(const U& other) const {
    Vec4<double> out(x, y, z, w);
    for (int i = 0; i < 4; ++i) out.data[i] -= other;
    return out;
  }

  template <typename U>
  Vec4<double> operator+(const Vec4<U>& other) const {
    Vec4<double> out(x, y, z, w);
    for (int i = 0; i < 4; ++i) out.data[i] += other.data[i];
    return out;
  }

  Vec4<double> operator*(double k) const {
    Vec4<double> out(x, y, z, w);
    for (int i = 0; i < 4; ++i) out.data[i] *= k;
    return out;
  }

  template <typename U>
  Vec4<double> Cross(const Vec4<U>& other) const {
    Vec4<double> temp(x, y, z, w);
    return {
        temp.y * other.z - temp.z * other.y,
        temp.z * other.x - temp.x * other.z,
        temp.x * other.y - temp.y * other.x,
        0.0,
    };
  }

  template <typename U>
  double Dot(const Vec4<U>& other) const {
    Vec4<double> temp(x, y, z, w);
    return temp.x * other.x + temp.y * other.y + temp.z * other.z;
  }

  double LengthSq() const { return 1.0 * x * x + 1.0 * y * y + 1.0 * z * z; }

  void Normalize() {
    const double k = 1.0 / std::sqrt(LengthSq());
    x *= k;
    y *= k;
    z *= k;
    w = 0;
  }
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const Vec4<T> v) {
  return os << std::vector<T>(v.data, v.data + 4);
}

}  // namespace gt2

#endif  // GT2_EXTRACT_VECTOR_H_
