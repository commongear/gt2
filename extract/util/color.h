// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_COLOR_H_
#define GT2_COLOR_H_

#include <limits>
#include <map>
#include <vector>

#include "../car.h"  // For Color16.

namespace gt2 {

constexpr int64_t kInt64Max = std::numeric_limits<int64_t>::max();

// Square distance between two colors.
inline int64_t ColorDistSq(Color16 a, Color16 b) {
  const int64_t dr = static_cast<int64_t>(a.r5()) - b.r5();
  const int64_t dg = static_cast<int64_t>(a.g5()) - b.g5();
  const int64_t db = static_cast<int64_t>(a.b5()) - b.b5();
  const int64_t da = (a.force_opaque() != b.force_opaque() ? 255 : 0);
  return dr * dr + dg * dg + db * db + da * da;
}

// Distance between two palettes.
struct PaletteDist {
  // Sum of square color distances between each best matching color pair.
  // Zero if one palette contains all the colors from the other.
  int64_t sum_sq_dist = 0;
  // Number of colors in the union of the two palettes.
  int64_t union_size = 0;
};
inline PaletteDist ComputePaletteDist(const std::map<Color16, uint16_t>& a,
                                      const std::map<Color16, uint16_t>& b) {
  std::vector<Color16> va;
  va.reserve(a.size());
  for (const auto& kv : a) va.push_back(kv.first);

  std::vector<Color16> vb;
  vb.reserve(b.size());
  for (const auto& kv : b) vb.push_back(kv.first);

  // Make sure 'a' is always smaller than 'b'.
  if (va.size() > vb.size()) std::swap(va, vb);
  const int a_size = va.size();
  const int b_size = vb.size();

  PaletteDist out;
  out.union_size = vb.size();

  // For each color in 'a'...
  for (int i = 0; i < a_size; ++i) {
    // Find the distance to the nearest color in 'b'...
    int64_t sq_dist = kInt64Max;
    for (int j = 0; j < b_size; ++j) {
      sq_dist = std::min(sq_dist, ColorDistSq(va[i], vb[j]));
    }
    // Tally the result.
    if (sq_dist > 0) ++out.union_size;
    out.sum_sq_dist += sq_dist;
  }
  return out;
}

// Expects a map of color -> num_pixels.
//   color: an entry in the output palette.
//   num_pixels: number of pixels in the original texture which are this color.
//   n: the maximum number of colors to retain.
inline void QuantizeColors(std::map<Color16, uint16_t>& inout_colors, int n) {
  constexpr float kInf = std::numeric_limits<float>::infinity();

  CHECK_GT(n, 0);
  if (inout_colors.size() <= n) return;

  struct Color {
    Color16 color;
    uint16_t num_pixels;
    float score;
  };

  // Track how many pixels are transparent.
  Color transparent = {.color = {.data = 0}, .num_pixels = 0, .score = 0.0};

  // Transfer colors to a vector, treating transparent ones separately.
  std::vector<Color> colors;
  colors.reserve(inout_colors.size());
  for (const auto& kv : inout_colors) {
    if (kv.first.data == 0) {
      transparent.num_pixels += kv.second;
    } else {
      colors.push_back(
          {.color = kv.first, .num_pixels = kv.second, .score = kInf});
    }
  }

  std::vector<Color> final;
  final.reserve(n);
  {
    // Save space for a transparent color.
    const int to_find = (transparent.num_pixels ? n - 1 : n);

    // Find the color with the most pixels.
    int c_next = 0;
    int c_num = colors[c_next].num_pixels;
    for (int i = 1; i < colors.size(); ++i) {
      const int num_pixels = colors[i].num_pixels;
      if (num_pixels < c_num) {
        c_num = num_pixels;
        c_next = i;
      }
    }

    // Build a final palette by choosing the most distance (in color-space)
    // color to any existing color in the final palette.
    while (true) {
      final.push_back(colors[c_next]);
      if (final.size() >= to_find) break;

      // Measure distance to each color.
      float highest_score = 0;
      for (int i = 0; i < colors.size(); ++i) {
        // Assign the minimum score to each color.
        const float score = ColorDistSq(final.back().color, colors[i].color);
        colors[i].score = std::min(score, colors[i].score);
        // Find the color with the highest score.
        if (colors[i].score > highest_score) {
          highest_score = colors[i].score;
          c_next = i;
        }
      }
    }

    // For colors which did not appear in the final palette...
    for (const auto& c : colors) {
      if (c.score > 0) {
        // Pick the nearest final palette color...
        int i_best = 0;
        int64_t d_best = std::numeric_limits<int64_t>::max();
        for (int i = 0; i < final.size(); ++i) {
          const int64_t d = ColorDistSq(c.color, final[i].color);
          if (d < d_best) {
            i_best = i;
            d_best = d;
          }
        }
        // and add this color's pixel count to it.
        final[i_best].num_pixels += c.num_pixels;
      }
    }

    // Push the transparent pixel, if any.
    if (transparent.num_pixels) {
      final.push_back(transparent);
    }
  }

  // Output resulting colors.
  inout_colors.clear();
  for (const auto& e : final) inout_colors[e.color] = e.num_pixels;
}

}  // namespace gt2

#endif  // GT2_COLOR_H_
