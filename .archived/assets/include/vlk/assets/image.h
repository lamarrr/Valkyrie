#pragma once

#include <filesystem>

#include "stx/result.h"
#include "stx/span.h"

namespace vlk {
namespace desc {

// TODO(lamarrr): what can we do to separate this? constrain some? it is rarely
// possible that we would want the image to reside in memory forever, we need an
// image view and an image type with ticking managed by an asset manager
// count references or count ticks?

struct Image {
  enum class Format : uint8_t {
    Internal = 0,
    Grey = 1,
    GreyAlpha = 2,
    RGB = 3,
    RGBA = 4
  };

  std::filesystem::path path;
  Format target_format = Format::Internal;
  bool flip_vertically = false;
};

}  // namespace desc

namespace data {

enum class Error : uint8_t { InvalidPath, Internal };

// stored in h x w x c memory order.
// each channel is representy by a byte
// TODO(lamarrr): make better by adding support for new types
// TODO(lamarrr): ensure only malloc is ever used here
struct [[nodiscard]] Image {
  enum class Format : uint8_t { Grey = 1, GreyAlpha = 2, RGB = 3, RGBA = 4 };

  static stx::Result<Image, Error> load(desc::Image const& desc);
  static stx::Result<Image, Error> load(std::filesystem::path const& path,
                                        desc::Image::Format format) {
    return load(desc::Image{path, format});
  }
  static stx::Result<Image, Error> load(std::filesystem::path const& path,
                                        desc::Image::Format format,
                                        bool flip_vertically) {
    return load(desc::Image{path, format, flip_vertically});
  }

  // void take();
  // void ref();

  Image() : pixel_data_{}, width_{}, height_{}, format_{Format::RGBA} {}
  Image(Image const&) = delete;
  Image& operator=(Image const&) = delete;
  Image(Image && other) noexcept;
  Image& operator=(Image&& other) noexcept;
  ~Image() noexcept;

  uint32_t width() const noexcept { return width_; }
  uint32_t height() const noexcept { return height_; }
  Format format() const noexcept { return format_; }
  uint32_t channels() const noexcept { return static_cast<uint32_t>(format()); }

  size_t size() const noexcept {
    return static_cast<size_t>(width()) * static_cast<size_t>(height()) *
           static_cast<size_t>(format());
  }

  stx::Span<uint8_t const> bytes() const noexcept {
    return stx::Span<uint8_t const>(pixel_data_, pixel_data_ + size());
  }

  bool is_valid() const noexcept { return size() != 0; }

 private:
  uint8_t* pixel_data_;
  uint32_t width_, height_;
  Format format_;
};

}  // namespace data
}  // namespace vlk
