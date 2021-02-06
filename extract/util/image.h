// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_IMAGE_H_
#define GT2_EXTRACT_IMAGE_H_

namespace miniz {
#include "../3p/miniz/miniz.h"
}

#include "inspect.h"
#include "vec.h"

namespace gt2 {

// Draws a line from 'a' to 'b'.
template <typename T, typename R>
void DrawLine(Vec2<T> a, const Vec2<T> b, R& result) {
  const int dx = std::abs(b.x - a.x);
  const int sx = a.x < b.x ? 1 : -1;
  const int dy = -std::abs(b.y - a.y);
  const int sy = a.y < b.y ? 1 : -1;
  int err = dx + dy; /* error value e_xy */
  while (true) {
    result.Set(a);
    if (a == b) break;
    int e2 = 2 * err;
    if (e2 >= dy) { /* e_xy+e_x > 0 */
      err += dy;
      a.x += sx;
    }
    if (e2 <= dx) { /* e_xy+e_y < 0 */
      err += dx;
      a.y += sy;
    }
  }
}

// A very basic image.
struct Image {
  int width = 0;
  int height = 0;
  int channels = 0;
  std::vector<uint8_t> pixels;

  // Allocate a blank image of the given size.
  Image(int w, int h, int c)
      : width(w), height(h), channels(c), pixels(w * h * c) {}

  // Get pixel data.
  uint8_t& at(int x, int y) { return pixels[x + y * width]; }
  const uint8_t& at(int x, int y) const { return pixels[x + y * width]; }

  // Draws a line between pixel coordinates 'a' and 'b'.
  // No bounds checking.
  template <typename T>
  void DrawLine(const Vec2<T> a, const Vec2<T> b, uint8_t value) {
    struct Setter {
      void Set(const Vec2<T> v) { im->at(v.x, v.y) = value; }
      Image* im;
      uint8_t value;
    } s = {this, value};
    ::gt2::DrawLine(a, b, s);
  }

  // Draws a filled triangle between pixel coordinates 'a', 'b', and 'c'.
  // No specific vertex ordering required.
  // No bounds checking.
  template <typename T>
  void DrawTriangle(const Vec2<T> a, const Vec2<T> b, const Vec2<T> c,
                    uint8_t value) {
    constexpr int kMin = std::numeric_limits<int>::min();
    constexpr int kMax = std::numeric_limits<int>::max();
    struct Bounds {
      int lo = kMax;
      int hi = kMin;
    };
    std::vector<Bounds> to_draw(height);
    struct Lines {
      void Set(Vec2<T> v) {
        to_draw[v.y].lo = std::min(to_draw[v.y].lo, static_cast<int>(v.x));
        to_draw[v.y].hi = std::max(to_draw[v.y].hi, static_cast<int>(v.x));
      }
      std::vector<Bounds>& to_draw;
    } s = {to_draw};
    ::gt2::DrawLine(a, b, s);
    ::gt2::DrawLine(a, c, s);
    ::gt2::DrawLine(b, c, s);
    for (int y = 0; y < height; ++y) {
      const Bounds& b = to_draw[y];
      const int hi = std::min(width, b.hi);
      for (int x = b.lo; x < hi; ++x) at(x, y) = value;
    }
  }

  // Flood fills an enclosed area starting at 'p'.
  void Fill(const Vec2<float> p, uint8_t value) {
    CHECK_EQ(channels, 1);
    const int w = width;
    const int size = w * height;
    const int x = static_cast<int>(p.x + 0.5f);
    const int y = static_cast<int>(p.y + 0.5f);
    std::vector<uint8_t> closed(size);
    std::vector<int> next = {x + w * y};
    int i = 0;
    while (i < next.size()) {
      const int index = next[i++];

      uint8_t& is_closed = closed[index];
      if (is_closed) continue;
      is_closed = 1;

      uint8_t& out_px = pixels[index];
      if (out_px == value) continue;
      if (out_px == 0) out_px = value;

      if (index > 0) next.push_back(index - 1);
      if (index > w) next.push_back(index - w);
      if (index + 1 < size) next.push_back(index + 1);
      if (index + w < size) next.push_back(index + w);
    }
  }

  // Expands masked areas in the image by one pixel.
  // For each pair of adjacent pixels (a, b):
  //  - If mask(a) is '255' and mask(b) is '0':
  //     - Copy the pixel value from 'a' to 'b'.
  //     - Set mask(b) = 255.
  // Returns 'true' if any pixels were changed.
  bool GrowBorders(Image& mask) {
    CHECK_EQ(channels, 1);
    CHECK_EQ(width, mask.width);
    CHECK_EQ(height, mask.height);
    CHECK_EQ(channels, mask.channels);

    const int w = width;
    const int h = height;

    bool changed = false;

    const auto pick = [&](int x, int y, int dx, int dy) {
      if (mask.at(x, y) == 0 && mask.at(x + dx, y + dy) > 0) {
        at(x, y) = at(x + dx, y + dy);
        mask.at(x, y) = 255;
        changed = true;
      }
    };

    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w - 1; ++x) pick(x, y, 1, 0);
      for (int x = w - 1; x > 0; --x) pick(x, y, -1, 0);
    }
    for (int y = 0; y < h - 1; ++y) {
      for (int x = 0; x < w; ++x) pick(x, y, 0, 1);
    }
    for (int y = h - 1; y > 0; --y) {
      for (int x = 0; x < w; ++x) pick(x, y, 0, -1);
    }

    return changed;
  }

  // Converts this image to a PNG.
  std::string ToPng() const {
    size_t png_size = 0;
    void* png = miniz::tdefl_write_image_to_png_file_in_memory_ex(
        pixels.data(), width, height, channels, &png_size, 6, MZ_FALSE);
    std::string out(reinterpret_cast<const char*>(png), png_size);
    miniz::mz_free(png);
    return out;
  }
};

}  // namespace gt2

#endif  // GT2_IMAGE_H
