

#include "vlk/ui/widgets/row.h"

#include "gtest/gtest.h"
#include "mock_widgets.h"
#include "vlk/ui/app.h"
#include "vlk/ui/palettes/ios.h"
#include "vlk/ui/palettes/material.h"
#include "vlk/ui/pipeline.h"
#include "vlk/ui/render_context.h"
#include "vlk/ui/tile_cache.h"
#include "vlk/ui/vulkan.h"
#include "vlk/ui/widgets/box.h"
#include "vlk/ui/widgets/image.h"
#include "vlk/ui/widgets/text.h"
#include "vlk/ui/window.h"

using namespace vlk::ui;
using namespace vlk;
// RowProps constrain

// what to work on next?
// on image loading the user needs to use a default fallback image or a provided
// one. transition?
//
//
// Add imgui and glfw for testing
//

struct WhiteBgFlex : public MockFlex {
  WhiteBgFlex(std::initializer_list<Widget*> children) : MockFlex{children} {}

  ~WhiteBgFlex() {}

  virtual void draw(Canvas& canvas) override {
    // TODO(lamarrr): this won't give us a correct behaviour?
    // as it will also clear the outlying parts
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc);
    paint.setColor(SK_ColorWHITE);
    paint.setStyle(SkPaint::Style::kFill_Style);

    canvas.to_skia().drawRect(to_sk_rect(Rect{{}, canvas.extent()}), paint);
  }

  virtual void tick(std::chrono::nanoseconds, AssetManager&) override {
    // mark_render_dirty();
  }
};

TEST(RowTest, BasicTest) {
  RenderContext context;

  constexpr Color color_list[] = {ios::DarkPurple, ios::DarkRed,
                                  ios::DarkIndigo, ios::DarkMint,
                                  ios::DarkTeal};

  // TODO(lamarrr): why isn't this wrapping to the next line? it actually is
  // wrapping but the height allotted is wrong? or do we need to scroll the root
  // view on viewport scroll?

  // TODO(lamarrr): we need a flexbox, not row or column
  MockView view{new Row{
      [&](size_t i) -> Widget* {
        if (i >= 8) return nullptr;

        if (i == 0)
          return new WhiteBgFlex{new Text{
              {InlineText{"Apparently we had reached a great height in the "
                          "atmosphere, for "
                          "the sky was a dead black, and the stars had ceased "
                          "to twinkle. "
                          "By the same illusion which lifts the horizon of the "
                          "sea to the "
                          "level of the spectator on a hillside, the sable "
                          "cloud beneath "
                          "was dished out, and the car seemed to float in the "
                          "middle of an "
                          "immense dark sphere, whose upper half was strewn "
                          "with silver. "},
               InlineText{"Looking down into the dark gulf below, I could "
                          "see a ruddy "
                          "light streaming through a rift in the clouds.",
                          TextProps{}.color(ios::LightRed)},
               InlineText{"explicit",
                          TextProps{}
                              .font_size(20.0f)
                              .color(ios::LightPurple)
                              .font(FileTypefaceSource{
                                  "/home/lamar/Desktop/"
                                  "MaterialIcons-Regular-4.0.0.ttf"})}

              },
              ParagraphProps{}
                  .font_size(20.0f)
                  .color(ios::DarkGray6)
                  .font(SystemFont{"SF Pro"})}};

        if (i == 1) {
          return new Image{ImageProps{
              FileImageSource{"/home/lamar/Pictures/E0U2xTYVcAE1-gl.jpeg"}}
                               .extent(700, 700)
                               .aspect_ratio(3, 1)
                               .border_radius(BorderRadius::all(50))};
        }

        if (i == 2) {
          return new Image{
              ImageProps{FileImageSource{"/home/lamar/Pictures/crow.PNG"}}
                  .extent(500, 500)
                  .aspect_ratio(3, 2)
                  .border_radius(BorderRadius::all(50))};
        }

        if (i == 3) {
          return new Image{
              ImageProps{FileImageSource{"/home/lamar/Pictures/IMG_0079.JPG"}}
                  .extent(500, 500)
                  .aspect_ratio(2, 1)
                  .border_radius(BorderRadius::spec(20, 10, 5, 40))};
        }

        if (i == 4) {
          return new Image{ImageProps{
              MemoryImageSource{ImageInfo{Extent{2, 2}, ImageFormat::RGB},
                                {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 0}}}
                               .extent(500, 500)
                               .aspect_ratio(2, 1)
                               .border_radius(BorderRadius::all(20))};
        }

        return new Box(
            new Box(new Text{"Aa Type of A Box (" + std::to_string(i) + ")",
                             TextProps{}
                                 .font_size(10.0f)
                                 .color(colors::White)
                                 .font(SystemFont{"SF Pro"})},
                    BoxProps{}
                        .padding(Padding::all(15))
                        .border_radius(BorderRadius::all(20))
                        .color(color_list[i % std::size(color_list)])),
            BoxProps{}
                .image(FileImageSource{
                    "/home/lamar/Pictures/E0U20cZUYAEaJqL.jpeg"})
                .padding(Padding::all(50))
                .border(Border::all(ios::DarkPurple, 20))
                .border_radius(BorderRadius::all(50)));
      },
      RowProps{}
          .main_align(MainAlign::SpaceAround)
          .cross_align(CrossAlign::Start)}};

  App app{&view, AppCfg{}};

  while (true) {
    app.tick();
  }

  Extent screen_extent{2000, 1000};

  Pipeline pipeline{view, context};

  pipeline.viewport.resize(
      screen_extent, pipeline.viewport.get_unresolved_widgets_allocation());

  for (size_t i = 0; i < 1'00; i++) {
    constexpr float mul = 1 / 50.0f;
    pipeline.tick(std::chrono::nanoseconds(0));
    pipeline.tile_cache.scroll_backing_store_logical(IOffset{0, mul * i * 0});
    pipeline.tile_cache.backing_store_cache.save_pixels_to_file(
        "./ui_output_row_" + std::to_string(i));
    VLK_LOG("written tick: {}", i);
  }
}