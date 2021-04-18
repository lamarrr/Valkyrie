

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "stx/Option.h"

#include "vlk/ui/primitives.h"
#include "vlk/ui/raster_cache.h"
#include "vlk/ui/raster_tiles.h"

namespace vlk {
namespace ui {

// how do we manage already performed work?
struct RasterTaskScheduler {
  // TODO(lamarrr): add synchronization across frames to prevent overdrawing
  // guarantess: tile must not be modified whilst submitted,
  // we need resubmission/cancelation logic. if a widget has ticked out after
  // submission for example, we need to be able to also send a cancel request

  struct VectorSemaphore {
    std::vector<RasterCache *> *queue_;
    std::mutex *mutex_;
    std::atomic_bool *should_stop_;

    VectorSemaphore();

    void push(RasterCache &tile) {
      std::lock_guard guard(*mutex_);
      queue_->push_back(&tile);
    }

    // returns none if exit is requested
    stx::Option<RasterCache *> await_task() {
      for (size_t i = 0; !should_stop_->load(std::memory_order_acquire);) {
        mutex_->lock();
        if (queue_->size() == 0) {
          mutex_->unlock();
          if (i < 64) {
            std::this_thread::yield();
            i++;
            continue;
          } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            i++;
            continue;
          }
        }

        i = 0;

        RasterCache *tile = queue_->back();
        queue_->pop_back();

        mutex_->unlock();

        return tile;
      }

      return stx::None;
    }

  } semaphore;
  Ticks current_ticks_;

  RasterTaskScheduler() {
    size_t max_threads = std::thread::hardware_concurrency();
    DEBUG_ENSURE(max_threads > 0);
  }

  ~RasterTaskScheduler() {
    should_stop_.store(true, std::memory_order_release);
    // check running and/or join
  }

  std::atomic_bool should_stop_ = false;

  // must be joined
  std::vector<std::thread> worker_threads_;

  enum class Result {

  };

  struct Entry {
    RasterTiles *tiles;
    std::promise<Result> result;
  };

  std::mutex sque_mutex_;
  std::vector<RasterCache *> submission_queue_ = {};  // FIFO

  std::mutex cque_mutex_;
  std::vector<RasterCache *> completion_queue_;  // FIFO

  enum class Error {

  };

  // if it has pending submissions from the last frame, remove and replace with
  // this thread-safe, must be able to submit from single thread and read from a
  // single thread use future? std::future<bool>

  bool submit(RasterCache &tile) {
    // consider instead of completion queue consider using std::future<State>
    // alternatively: we can lock and  chack the status of the tile instead
    // there'll be sync issues! we can't have multiple copies do not make it an
    // hard error when the entry already exists on the target queue
    sque_mutex_.lock();

    bool result = false;

    if (auto iter = std::find(submission_queue_.begin(),
                              submission_queue_.end(), &tile);
        iter == submission_queue_.end()) {
      semaphore.push(tile);
      result = true;
    }

    sque_mutex_.unlock();

    return result;
  }

  bool cancel(RasterCache &tile) {
    std::lock_guard guard{sque_mutex_};
    if (auto iter = std::find(submission_queue_.begin(),
                              submission_queue_.end(), &tile);
        iter != submission_queue_.end()) {
      submission_queue_.erase(iter);
      return true;
    }
    return false;
  }

  // queried from the tile itself???????? possible async issues

  // process all events as necessary
  void tick(std::chrono::nanoseconds const &interval) {
    // use for synchronization and ensuring we don't repeat submitted work?
    // is this even necessary? since we definitely won't be doing that.
    // alternatively we can override the tick so we don't repeat, in case
    // there's a pending task from the last tick
    current_ticks_++;
  }

  // const pointer to contextual data here
  // lock for reading, pop submission
  void process_submissions_task__() {
    stx::Option raster_task = semaphore.await_task();

    if (raster_task.has_value()) {
      auto task = raster_task.value();
    }
  }
};

struct StaticContext {};

struct DynamicContext {};

// sizing, representing image dimensions, etc
// how do we handle knowing the sizing ahead of time?
template <typename DataType>
struct DataSource {
  virtual DataType provide() = 0;
  virtual void discard(DataType &&) = 0;

  virtual ~DataSource() = 0;
};

template <typename DataType>
struct AsyncDataSource {
  virtual std::future<DataType> provide_async() = 0;
  virtual void discard(DataType &&) = 0;

  virtual ~DataSource() = 0;
};

struct ImageSpan {
  enum class Format { Rgbx8888, Rgba8888, Rgbx4444, Rgba4444 } format;

  Extent extent{};

  std::span<uint8_t const> data{};
};

using ByteSource = DataSource<std::span<uint8_t const>>;
using ImageSource = DataSource<ImageSpan>;

constexpr float pixel_ratio(uint32_t virtual_extent, uint32_t physical_extent) {
  return static_cast<float>(physical_extent) / virtual_extent;
}

constexpr float to_physical(uint32_t virtual_extent, float pixel_ratio) {
  return static_cast<float>(virtual_extent) / virtual_extent;
}

constexpr float to_virtual(uint32_t virtual_extent, float pixel_ratio) {
  return static_cast<float>(physical_extent) / virtual_extent;
}

}  // namespace ui
}  // namespace vlk

struct Timeline {
  std::vector<float> values;
  // we need a wrapping behaviour in which it wraps the cursor
  size_t start;
  size_t end;
};

constexpr Extent view_fit_self_extent(ViewFit fit,
                                      Extent const &resolved_self_extent,
                                      Extent const &view_extent,
                                      Extent const &allotted_extent) {
  Extent result_self_extent = resolved_self_extent;
  if ((fit & ViewFit::Width) != ViewFit::None &&
      view_extent.width <= allotted_extent.width) {
    result_self_extent.width = view_extent.width;
  }

  if ((fit & ViewFit::Height) != ViewFit::None &&
      view_extent.height <= allotted_extent.height) {
    result_self_extent.height = view_extent.height;
  }

  return result_self_extent;
}

 sk_sp<SkImageFilter> filters[] = {
        nullptr,
        SkImageFilters::DropShadow(7.0f, 0.0f, 0.0f, 3.0f, SK_ColorBLUE,
   nullptr), SkImageFilters::DropShadow(0.0f, 7.0f, 3.0f, 0.0f, SK_ColorBLUE,
   nullptr), SkImageFilters::DropShadow(7.0f, 7.0f, 3.0f, 3.0f, SK_ColorBLUE,
   nullptr), SkImageFilters::DropShadow(7.0f, 7.0f, 3.0f, 3.0f, SK_ColorBLUE,
   std::move(cfif)), SkImageFilters::DropShadow(7.0f, 7.0f, 3.0f, 3.0f,
   SK_ColorBLUE, nullptr, &cropRect),
        SkImageFilters::DropShadow(7.0f, 7.0f, 3.0f, 3.0f, SK_ColorBLUE,
   nullptr, &bogusRect), SkImageFilters::DropShadowOnly(7.0f, 7.0f, 3.0f, 3.0f,
   SK_ColorBLUE, nullptr),
    };
struct BoxShadow {
  // greater than or equal to 0
  float blur_radius;
  // greater than 0
  float blur_sigma;
};
typename BoxShadowAllocator = std::allocator<BoxShadow>
 shadows_{shadows.begin(), shadows.end()}
  std::vector<BoxShadow, BoxShadowAllocator> shadows_;

  stx::Span<BoxShadow const> const &shadows = {}

struct Gradient {
  // linear, radial, sweep
};

   stx::Span<TextShadow const> const& shadows = {}

struct TextShadow {
  Color color = colors::Black;
  IOffset offset = IOffset{0, 0};
  double blur_radius = 0.0;
};

  bool is_underlined() const {
    return (decoration_ & TextDecoration::Overline) != TextDecoration::None;
  }

  bool is_overlined() const {
    return (decoration_ & TextDecoration::Overline) != TextDecoration::None;
  }

  bool has_strikethrough() const {
    return (decoration_ & TextDecoration::StrikeThrough) !=
           TextDecoration::None;
  }

/*
for (auto const& shadow : shadows)
  text_style.addShadow(sktext::TextShadow{
      shadow.color.to_argb(), SkPoint::Make(shadow.offset.x, shadow.offset.y),
      shadow.blur_radius});


Rect Text::compute_area(Extent const& allotted_extent,
                        [[maybe_unused]] stx::Span<Rect> const& children_area) {
  paragraph_->layout(allotted_extent.width);

  // it is laid out on the specified width above, its MaxIntrinsicWidth
  // specifies the max width if the whole text were to be laid out on a straight
  // line.
  uint32_t width = static_cast<uint32_t>(
      std::ceil(std::min(paragraph_->getMaxIntrinsicWidth(),
                         static_cast<float>(allotted_extent.width))));
  uint32_t height = static_cast<uint32_t>(std::ceil(paragraph_->getHeight()));

  return Rect{Offset{0, 0}, Extent{width, height}};
}

void Text::draw(Canvas& canvas,
                [[maybe_unused]] Extent const& requested_extent) {
  SkCanvas* sk_canvas = canvas.as_skia();
  paragraph_->paint(sk_canvas, 0, 0);
}
*/