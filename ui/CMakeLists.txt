add_library(
  vlk_ui STATIC
  src/canvas.cc
  src/widget.cc
  src/window.cc
  src/window_api.cc
  src/vulkan.cc
  src/app.cc
  # subsystems
  src/subsystems/async.cc
  # Widgets
  src/widgets/box.cc
  src/widgets/image.cc
  src/widgets/text.cc)

target_include_directories(
  vlk_ui
  PUBLIC include
  PRIVATE src)

target_link_libraries(
  vlk_ui
  vlk_utils
 # VulkanMemoryAllocator
  vlk_external_skia
  vlk_external_stb
  vlk::asset
  vlk::primitives
  vlk::palettes
  vlk_subsystems_http
  vlk_subsystems_asset_loader
  vlk_subsystems_scheduler
  vlk::subsystem
  stx::stx
  SDL2::SDL2
  fmt::fmt
  ${Vulkan_LIBRARIES})

target_compile_definitions(vlk_ui PUBLIC SK_VULKAN)

add_executable(
  vlk_ui_test
  tests/primitives_test.cc
  tests/snapshot_test.cc
  tests/widget_test.cc
  tests/layout_test.cc
  tests/view_test.cc
  tests/pipeline_test.cc
  tests/raster_tiles_test.cc
  tests/tile_cache_test.cc
  tests/widgets/box_test.cc
  tests/widgets/image_test.cc
  tests/widgets/row_test.cc)

target_link_libraries(vlk_ui_test gtest gtest_main vlk_ui)
target_include_directories(vlk_ui_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/tests)

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang" OR ${CMAKE_CXX_COMPILER_ID}
                                                STREQUAL "GNU")
  target_compile_options(vlk_ui_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
