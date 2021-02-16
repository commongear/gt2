// Copyright (c) 2021 commongear
// MIT License (see https://github.com/commongear/gt2/blob/master/LICENSE)

#ifndef GT2_EXTRACT_IO_H_
#define GT2_EXTRACT_IO_H_

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "inspect.h"

namespace gt2 {

// Loads an entire file.
inline std::string Load(const std::string& path) {
  std::ifstream s(path, std::ios::in | std::ios::binary);
  CHECK(s.good(), "Failed to open for read '", path, "'");

  s.seekg(0, std::ios::end);
  const int64_t size = s.tellg();
  s.seekg(0, std::ios::beg);

  std::string out(size, '\0');
  s.read(const_cast<char*>(out.data()), out.size());
  CHECK_EQ(s.tellg(), size, "Failed to read all of '", path, "'");
  return out;
}

// Saves an entire file.
inline void Save(const std::string& buffer, const std::string& path) {
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path());

  std::ofstream s(path, std::ios::out | std::ios::binary);
  CHECK(s.good(), "Failed to open for write '", path, "'");

  s.write(buffer.data(), buffer.size());
  s.flush();
  CHECK(s.good(), "Failed to write '", path, "'");
}

// NOTE: I implemented these streams because the C++ std library versions copy
// everything incessantly and have a clunky interface. These versions still copy
// some things, albeit somewhat less, and with more readable code.

// An input stream backed by an in-memory string.
class StringInStream {
 public:
  // Empty.
  StringInStream() = default;
  // Takes ownership of the input.
  explicit StringInStream(std::string&& data) : data_(std::move(data)) {}

  // Everything is good.
  bool ok() const { return pos_ < size(); }

  // Total size of the file.
  int64_t size() const { return data_.size(); }
  // Remaining bytes available.
  int64_t remain() const { return size() - pos(); }
  // Current position.
  int64_t pos() const { return pos_; }
  // Sets the current position.
  void set_pos(int64_t i) { pos_ = i; }

  // Reads one struct or basic data type.
  template <typename T>
  T Read() {
    static_assert(std::is_trivially_copyable<T>::value);
    CHECK_LE(sizeof(T), remain(), STR(T));
    T out;
    std::memcpy(&out, data_.data() + pos_, sizeof(T));
    pos_ += sizeof(T);
    return out;
  }

  // Reads 'n' structs or basic data types.
  template <typename T>
  std::vector<T> Read(int64_t n) {
    static_assert(std::is_trivially_copyable<T>::value);
    CHECK_LE(n * sizeof(T), remain(), STR(T));
    std::vector<T> out(n);
    std::memcpy(const_cast<T*>(out.data()), data_.data() + pos_, n * sizeof(T));
    pos_ += n * sizeof(T);
    return out;
  }

  // Reads 'n' bytes.
  std::string ReadData(int64_t n) {
    CHECK_LE(n, remain());
    pos_ += n;
    return data_.substr(pos_, n);
  }

  // Reads a null-terminated string.
  std::string ReadCString() {
    const int64_t rest = remain();
    const int64_t size = strnlen(data_.data() + pos_, rest);
    return ReadData(size + (size < rest ? 1 : 0));
  }

  // Reads 'n' bytes into the given buffer.
  void ReadInto(char* data, int64_t n) {
    CHECK_LE(n, remain());
    std::memcpy(data, data_.data() + pos_, n);
    pos_ += n;
  }

  // Reads 'n' bytes into the given buffer.
  void ReadInto(std::string& s) {
    return ReadInto(const_cast<char*>(s.data()), s.size());
  }

 private:
  std::string data_;
  uint64_t pos_ = 0;
};

// An input stream backed by a file.
class FileInStream {
 public:
  // Empty.
  FileInStream() = default;
  // Opens a path.
  explicit FileInStream(const std::string& path)
      : s_(path, std::ios::in | std::ios::binary) {
    s_.seekg(0, std::ios::end);
    size_ = s_.tellg();
    s_.seekg(0, std::ios::beg);
  }

  // Everything is good.
  bool ok() const { return s_.good(); }
  // Total size of the file.
  int64_t size() const { return size_; }
  // Remaining bytes available.
  int64_t remain() const { return size() - pos(); }
  // Current position.
  int64_t pos() const { return s_.tellg(); }
  // Sets the current position.
  void set_pos(int64_t i) { s_.seekg(i, std::ios::beg); }

  // Reads one struct or basic data type.
  template <typename T>
  T Read() {
    static_assert(std::is_trivially_copyable<T>::value);
    T out;
    s_.read(reinterpret_cast<char*>(&out), sizeof(T));
    return out;
  }

  // Reads N structs or basic data types.
  template <typename T>
  std::vector<T> Read(int64_t n) {
    static_assert(std::is_trivially_copyable<T>::value);
    std::vector<T> out(n);
    s_.read(reinterpret_cast<char*>(out.data()), sizeof(T) * n);
    return out;
  }

  // Reads 'n' bytes .
  std::string ReadData(int64_t n) {
    CHECK_LE(n, remain());
    std::string out(n, '\0');
    s_.read(const_cast<char*>(out.data()), n);
    return out;
  }

  // Reads a null-terminated string.
  std::string ReadCString() {
    std::vector<char> data;
    data.reserve(32);
    do {
      data.push_back(s_.get());
    } while (data.back() != '\0');
    return std::string(data.data(), data.size() - 1);
  }

  // Reads 'n' bytes into the given buffer.
  void ReadInto(char* data, int64_t n) {
    CHECK_LE(n, remain());
    s_.read(data, n);
  }

  // Reads 'n' bytes into the given buffer.
  void ReadInto(std::string& s) {
    return ReadInto(const_cast<char*>(s.data()), s.size());
  }

 private:
  mutable std::ifstream s_;  // Gross, but tellg() isn't const.
  int64_t size_ = 0;
};

// An output stream backed by a std::vector.
class VecOutStream {
 public:
  // Empty.
  VecOutStream() = default;

  // Resizes the backing container.
  void Resize(int64_t size) { data_.resize(size); }

  // Writes one POD type.
  template <typename T,
            typename = std::enable_if_t<std::is_trivially_copyable<T>::value>>
  void Write(const T& v) {
    const int64_t pos = data_.size();
    const int64_t size = sizeof(T);
    data_.resize(pos + size);
    std::memcpy(data_.data() + pos, &v, size);
  }

  // Writes many POD types.
  template <typename T,
            typename = std::enable_if_t<std::is_trivially_copyable<T>::value>>
  void Write(const std::vector<T>& v) {
    const int64_t pos = data_.size();
    const int64_t size = v.size() * sizeof(T);
    data_.resize(pos + size);
    std::memcpy(data_.data() + pos, v.data(), size);
  }

  // Gets all the data as a string.
  std::string GetData() const {
    return std::string(data_.data(), data_.size());
  }

 private:
  std::vector<char> data_;
};
}  // namespace gt2

#endif  // GT2_EXTRACT_IO_H_
