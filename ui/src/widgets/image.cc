#include "vlk/ui/widgets/image.h"

#include "vlk/ui/image_asset.h"

namespace vlk {

namespace ui {

namespace impl {

//! 60Hz * (60 seconds * 2) = 60Hz * 120 seconds = 2 Minutes @60Hz = 1 Minute
//! @120Hz
constexpr Ticks default_texture_asset_timeout = Ticks{60 * 60 * 2};

constexpr WidgetDirtiness map_diff(ImageDiff diff) {
  WidgetDirtiness dirtiness = WidgetDirtiness::None;

  if ((diff & ImageDiff::Source) != ImageDiff::None) {
    dirtiness |= WidgetDirtiness::Layout | WidgetDirtiness::Render;
  }

  if ((diff & ImageDiff::Extent) != ImageDiff::None) {
    dirtiness |= WidgetDirtiness::Layout;
  }

  if ((diff & ImageDiff::BorderRadius) != ImageDiff::None) {
    dirtiness |= WidgetDirtiness::Render;
  }

  if ((diff & ImageDiff::AspectRatio) != ImageDiff::None) {
    dirtiness |= WidgetDirtiness::Layout;
  }

  return dirtiness;
}

inline ImageDiff image_props_diff(ImageProps const &props,
                                  ImageProps const &new_props) {
  ImageDiff diff = ImageDiff::None;

  if (props.source_ref() != new_props.source_ref()) {
    diff |= ImageDiff::Source;
  }

  if (props.extent() != new_props.extent()) {
    diff |= ImageDiff::Extent;
  }

  if (props.border_radius() != new_props.border_radius()) {
    diff |= ImageDiff::BorderRadius;
  }

  if (props.aspect_ratio() != new_props.aspect_ratio()) {
    diff |= ImageDiff::AspectRatio;
  }

  return diff;
}

inline auto get_image_asset(AssetManager &asset_manager,
                            ImageSource const &source)
    -> stx::Result<std::shared_ptr<ImageAsset const>, AssetError> {
  if (std::holds_alternative<MemoryImageSource>(source)) {
    return get_asset(asset_manager, std::get<MemoryImageSource>(source));
  } else if (std::holds_alternative<FileImageSource>(source)) {
    return get_asset(asset_manager, std::get<FileImageSource>(source));
  } else {
    VLK_PANIC();
  }
}

inline auto add_image_asset(AssetManager &asset_manager,
                            ImageSource const &source)
    -> stx::Result<stx::NoneType, AssetError> {
  if (std::holds_alternative<MemoryImageSource>(source)) {
    return add_asset(asset_manager, std::get<MemoryImageSource>(source));
  } else if (std::holds_alternative<FileImageSource>(source)) {
    return add_asset(asset_manager, std::get<FileImageSource>(source));
  } else {
    VLK_PANIC();
  }
}

// TODO(lamarrr): this should probably be moved to an impl conversions file
constexpr std::array<SkVector, 4> to_skia(BorderRadius const &border_radius) {
  return {
      SkVector::Make(border_radius.top_left, border_radius.top_left),
      SkVector::Make(border_radius.top_right, border_radius.top_right),
      SkVector::Make(border_radius.bottom_left, border_radius.bottom_left),
      SkVector::Make(border_radius.bottom_right, border_radius.bottom_right),
  };
}

}  // namespace impl

void Image::update_props(ImageProps props) {
  // once a file image is loaded and no extent is provided we'd need to
  // perform a re-layout
  //
  //
  // once the image arrives, we update the prop to use the new extent of the
  // new image
  //
  //

  diff_ |= impl::image_props_diff(storage_.props, props);

  bool const previously_drawn = storage_.drawn_in_last_tick;

  storage_ =
      impl::ImageStorage{std::move(props), ImageState::Stale, previously_drawn};
}
// TODO(lamarrr): once the asset is discarded, the tick calls marK_render_dirty
// which then triggers another draw call, we should wait for another draw call
// before marking it as dirty? partial invalidation?
//
//
// we are thus discarding the texture whilst it is still in view
//
//
// we can somehow notify the widget via the tile_cache we only need to set a
// value in the widget
//
//
// we only need to record for the in_focus tiles and discard recordings for
// non-infocus tiles and thus ignore their dirtiness an almost infinite-extent
// widget for example will not be drawable if complex enough since we'd need to
// have recordings for all of the tiles, both active and inactive ones
//
// draw methods could also perform offscreen rendering which could be costly if
// the widget is not in view
//
// when the texture hasn't been used for long, it is marked as discarded and
// then discarded and immediately added back, and marked dirty unnecassarily?
//

void Image::draw(Canvas &canvas) {
  storage_.drawn_in_last_tick = true;

  SkCanvas &sk_canvas = canvas.to_skia();

  // extent has already been taken care of
  Extent const widget_extent = canvas.extent();

  switch (storage_.state) {
    case ImageState::Loading:
      draw_loading_image(canvas);
      break;

    case ImageState::LoadFailed:
      draw_error_image(canvas);
      break;

    case ImageState::Loaded: {
      // load_result_ref, handle error?
      sk_sp<SkImage> const &texture = storage_.asset.value()->get_ref().value();

      int const texture_width = texture->width();
      int const texture_height = texture->height();
      Extent const texture_extent =
          Extent{static_cast<uint32_t>(texture_width),
                 static_cast<uint32_t>(texture_height)};

      sk_canvas.save();

      if (storage_.props.border_radius() != BorderRadius::all(0)) {
        SkRRect round_rect;

        auto const border_radii = impl::to_skia(storage_.props.border_radius());

        round_rect.setRectRadii(
            SkRect::MakeWH(widget_extent.width, widget_extent.height),
            border_radii.data());

        sk_canvas.clipRRect(round_rect, true);
      }

      // due to aspect ratio cropping we need to place image at the
      // center.
      Extent const roi =
          storage_.props.aspect_ratio().is_some()
              ? aspect_ratio_trim(storage_.props.aspect_ratio().unwrap(),
                                  texture_extent)
              : texture_extent;

      int const start_x = (texture_width - roi.width) / 2;
      int const start_y = (texture_height - roi.height) / 2;

      sk_canvas.drawImageRect(
          texture, SkRect::MakeXYWH(start_x, start_y, roi.width, roi.height),
          SkRect::MakeXYWH(0, 0, widget_extent.width, widget_extent.height),
          nullptr);

      sk_canvas.restore();

    } break;

    case ImageState::Stale:
      break;

    default:
      VLK_PANIC("Unexpected State");
  }
}

void Image::tick(std::chrono::nanoseconds, AssetManager &asset_manager) {
  if (storage_.state == ImageState::Stale && storage_.drawn_in_last_tick) {
    impl::add_image_asset(asset_manager, storage_.props.source_ref())
        .match([&](stx::NoneType) { storage_.state = ImageState::Loading; },
               [&](AssetError error) {
                 switch (error) {
                   case AssetError::TagExists:
                     storage_.state = ImageState::Loading;
                     break;
                   default:
                     VLK_PANIC("Unexpected State");
                 }
               });

    // mark the widget as dirty so a loading image is displayed
    Widget::mark_render_dirty();
  }

  // we've submitted the image to the asset manager or it has previously
  // been submitted by another widget and we are awaiting the status of
  // the image
  if (storage_.state == ImageState::Loading) {
    storage_.state =
        impl::get_image_asset(asset_manager, storage_.props.source_ref())
            .match(
                [&](auto &&asset) -> ImageState {
                  return asset->get_ref().match(
                      [&](auto &) {
                        storage_.asset = stx::Some(std::move(asset));
                        return ImageState::Loaded;
                      },
                      [&](ImageLoadError error) {
                        VLK_WARN("Failed to load image for {}, error: {}",
                                 format(*this), format(error));
                        return ImageState::LoadFailed;
                      });
                },
                [&](AssetError error) -> ImageState {
                  switch (error) {
                      // image is still loading
                    case AssetError::IsLoading:
                      return ImageState::Loading;
                    default:
                      VLK_PANIC("Unexpected State");
                  }
                });

    // if state changed from image loading (to success or failure), mark as
    // dirty so the failure or sucess image can be displayed
    if (storage_.state != ImageState::Loading) {
      Widget::mark_render_dirty();
    }

    // if the image loaded correctly and the user did not already provide an
    // extent, we need to request for a relayout to the loaded image asset's
    // extent. if a reflow is needed, immediately return so the relayout can
    // be processed by the system before rendering
    if (storage_.state == ImageState::Loaded &&
        storage_.props.extent().is_none()) {
      sk_sp texture = storage_.asset.value()->get_ref().value();
      Widget::update_self_extent(
          SelfExtent::absolute(texture->width(), texture->height()));
      return;
    }
  }

  // the image has been successfully loaded and the required layout for the
  // image has been established
  if (storage_.state == ImageState::Loaded) {
    // image asset usage tracking
    if (storage_.drawn_in_last_tick) {
      storage_.asset_stale_ticks.reset();
    } else {
      storage_.asset_stale_ticks++;
      // mark widget as dirty since the asset has been discarded after not
      // being used for long
      if (storage_.asset_stale_ticks >= impl::default_texture_asset_timeout) {
        storage_.asset = stx::None;
        Widget::mark_render_dirty();
        storage_.state = ImageState::Stale;
      }
    }
  }

  // we failed to load the image, we proceed to render error image.
  // the error image uses whatever extent the widget has available.
  if (storage_.state == ImageState::LoadFailed) {
    // no-op
  }

  // reset
  storage_.drawn_in_last_tick = false;

  if (diff_ != impl::ImageDiff::None) {
    WidgetDirtiness dirtiness = impl::map_diff(diff_);

    storage_.props.extent().match(
        [&](SelfExtent extent) { Widget::update_self_extent(extent); },
        [&]() { Widget::update_self_extent(SelfExtent::absolute(100, 100)); });

    storage_.props.aspect_ratio().match(
        [&](Extent) { Widget::update_needs_trimming(true); },
        [&]() { Widget::update_needs_trimming(false); });

    Widget::add_dirtiness(dirtiness);
    diff_ = impl::ImageDiff::None;
  }
}

}  // namespace ui

}  // namespace vlk