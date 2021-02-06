#ifndef GT2_EXTRACT_INSPECT_H_
#define GT2_EXTRACT_INSPECT_H_

#include <iomanip>
#include <iostream>
#include <sstream>
#include <tuple>

namespace gt2 {

////////////////////////////////////////////////////////////////////////////////
// Write varargs to ostream.
////////////////////////////////////////////////////////////////////////////////

// Calls os::operator<< on all arguments and returns the ostream.
template <int>
inline std::ostream& WriteToOstream(std::ostream& os) {
  return os;
}
template <int, typename T>
std::ostream& WriteToOstream(std::ostream& os, T&& arg) {
  return os << arg;
}
template <int num_spaces = 0, typename T, typename... U>
std::ostream& WriteToOstream(std::ostream& os, T&& arg, U&&... rest) {
  os << arg;
  for (int i = 0; i < num_spaces; ++i) os << " ";
  return WriteToOstream<num_spaces>(os, rest...);
}

////////////////////////////////////////////////////////////////////////////////
// Display things as hex. (Not particularly efficient, but ok for debugging.)
////////////////////////////////////////////////////////////////////////////////

// One byte to hex.
template <int width = 2>
inline std::string ToHex(uint8_t byte) {
  std::stringstream ss;
  ss << std::hex;
  ss << std::setw(width) << std::setfill(' ') << static_cast<int>(byte);
  return ss.str();
}

// A stream of bytes to hex.
template <int width = 2, int num_spaces = 1>
std::string ToHex(const uint8_t* data, int64_t size) {
  std::stringstream ss;
  ss << std::hex;
  if (size) {
    ss << std::setw(width) << std::setfill(' ') << static_cast<int>(*data);
  }
  for (int i = 1; i < size; ++i) {
    ss << std::string(num_spaces, ' ');
    ss << std::setw(width) << std::setfill(' ') << static_cast<int>(data[i]);
  }
  return ss.str();
}

// A fixed-length array to hex.
template <int width = 2, int num_spaces = 1, typename T, size_t N>
std::string ToHex(const T (&data)[N]) {
  return ToHex<width, num_spaces>(reinterpret_cast<const uint8_t*>(data),
                                  N * sizeof(T));
}

// A vector to hex.
template <int width = 2, int num_spaces = 1, typename T>
inline std::string ToHex(const std::vector<T>& data) {
  return ToHex<width, num_spaces>(reinterpret_cast<const uint8_t*>(data.data()),
                                  data.size() * sizeof(T));
}

////////////////////////////////////////////////////////////////////////////////
// Convert to string.
////////////////////////////////////////////////////////////////////////////////

// Calls ostream::operator<< on the arguments and concatenates the result.
template <int num_spaces = 0, typename... T>
std::string ToString(T&&... args) {
  std::stringstream ss;
  WriteToOstream<num_spaces>(ss, args...);
  return ss.str();
}

// Calls ostream::operator<< on the arguments and concatenates the result.
template <int num_spaces = 1, typename T>
std::string ToString(const std::vector<T>& v) {
  std::stringstream ss;
  const int64_t n = v.size();
  if (n) {
    if constexpr (sizeof(T) == 1) {
      ss << static_cast<int>(v.front());
    } else {
      ss << v.front();
    }
    for (int i = 1; i < n; ++i) {
      if constexpr (num_spaces) {
        ss << std::string(num_spaces, ' ');
      }
      if constexpr (sizeof(T) == 1) {
        if (n) ss << static_cast<int>(v[i]);
      } else {
        if (n) ss << v[i];
      }
    }
  }
  return ss.str();
}

////////////////////////////////////////////////////////////////////////////////
// Inspect strings.
////////////////////////////////////////////////////////////////////////////////

// True if 's' ends with the given suffix.
inline bool EndsWith(std::string_view s, std::string_view suffix) {
  if (suffix.size() > s.size()) return false;
  return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

// True if 's' begins with the given prefix.
inline bool StartsWith(std::string_view s, std::string_view prefix) {
  if (prefix.size() > s.size()) return false;
  return std::equal(prefix.begin(), prefix.end(), s.begin());
}

////////////////////////////////////////////////////////////////////////////////
// Check for data which is all zero.
////////////////////////////////////////////////////////////////////////////////

inline bool IsZero(const uint8_t* data, int64_t size) {
  for (int i = 0; i < size; ++i) {
    if (data[i]) return false;
  }
  return true;
}

template <typename T>
bool IsZero(const std::vector<T>& v) {
  return IsZero(reinterpret_cast<const uint8_t*>(v.data()),
                v.size() * sizeof(T));
}

template <typename T>
bool IsZero(const T& v) {
  return IsZero(reinterpret_cast<const uint8_t*>(&v), sizeof(T));
}

////////////////////////////////////////////////////////////////////////////////
// Exit the program with useful errors.
////////////////////////////////////////////////////////////////////////////////

// Logs the file, line, and any arguments and then calls exit(-1).
#define FAIL(...)                                                \
  do {                                                           \
    std::cerr << "FAIL(" << __FILE__ << ":" << __LINE__ << ") "; \
    WriteToOstream(std::cerr, ##__VA_ARGS__) << std::endl;       \
    std::exit(-1);                                               \
  } while (false);

// Converts a macro argument to a string.
#define STR(a) #a

// Checks if the following is true, converting the arguments to strings.
#define CHECK(cond, ...)                            \
  if (!(cond)) {                                    \
    FAIL(STR(cond), " ", ToString<1>(__VA_ARGS__)); \
  }

// Prints the names and values of the operands, separated by the operator.
#define BINARY_OP_STR(a, op, b) \
  ToString<1>("", STR(a), op, STR(b), " (", a, op, b, ") ")

// Checks (in)equality of the operands and prints them on failure.
#define CHECK_EQ(a, b, ...)                                    \
  if (!((a) == (b))) {                                         \
    FAIL(BINARY_OP_STR(a, "==", b), ToString<1>(__VA_ARGS__)); \
  }
#define CHECK_NE(a, b, ...)                                    \
  if (!((a) != (b))) {                                         \
    FAIL(BINARY_OP_STR(a, "!=", b), ToString<1>(__VA_ARGS__)); \
  }
#define CHECK_LE(a, b, ...)                                    \
  if (!((a) <= (b))) {                                         \
    FAIL(BINARY_OP_STR(a, "<=", b), ToString<1>(__VA_ARGS__)); \
  }
#define CHECK_GE(a, b, ...)                                    \
  if (!((a) >= (b))) {                                         \
    FAIL(BINARY_OP_STR(a, ">=", b), ToString<1>(__VA_ARGS__)); \
  }
#define CHECK_LT(a, b, ...)                                   \
  if (!((a) < (b))) {                                         \
    FAIL(BINARY_OP_STR(a, "<", b), ToString<1>(__VA_ARGS__)); \
  }
#define CHECK_GT(a, b, ...)                                   \
  if (!((a) > (b))) {                                         \
    FAIL(BINARY_OP_STR(a, ">", b), ToString<1>(__VA_ARGS__)); \
  }

}  // namespace gt2

#endif
