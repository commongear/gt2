// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_BITPACK_H_
#define GT2_EXTRACT_BITPACK_H_

namespace gt2 {

// Returns a mask of the 'num_bits' least significant bits of type 'T'.
// For example LowBits<3, uint8_t> -> 0000 0111
template <int num_bits, typename T>
constexpr T LowBits() {
  static_assert(num_bits < 8 * sizeof(T));
  const std::make_unsigned_t<T> one = 1;
  return (one << num_bits) - one;
}

// Returns 'num_bits' least-signfificant bits from 'x'.
// For example, LowBits<3, uint8_t>(1100 1100) -> 0000 0100
template <int num_bits, typename T>
constexpr T LowBits(T x) {
  return x & LowBits<num_bits, T>();
}

// Unpacks type 'T' from some number of data bits and trailing bits in x.
// If 'T' is signed, performs sign extension. otherwise, unpacks as-is.
// In other words, if input 'x' looks like:
//    MSB [leading-bits][data-bits][trailing-bits] LSB.
// Then Unpack<T, data-bits, trailing-bits>(x) will return:
//    MSB [sign-bits][data-bits] LSB
template <typename T, int data_bits, int trailing_bits, typename T_in>
constexpr T Unpack(T_in x) {
  static_assert(data_bits + trailing_bits <= 8 * sizeof(T_in));
  if constexpr (std::numeric_limits<T>::is_signed) {
    // Perform sign extension.
    // https://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend
    constexpr T signbit = 1u << (data_bits - 1);
    const T y = LowBits<data_bits>(x >> trailing_bits);
    return (y ^ signbit) - signbit;
  } else {
    return LowBits<data_bits>(x >> trailing_bits);
  }
}

}  // namespace gt2

#endif
