#include <algorithm>
#include <memory>
#include <vector>

#include "vlk/ui/primitives.h"
#include "vlk/ui/widget.h"
#include "vlk/ui/widgets/layout_widget_base.h"

#include "stx/span.h"

namespace vlk {
namespace ui {

template <bool IsStateful, typename ChildDeleter = DefaultWidgetChildDeleter>
struct BasicMargin : public BoxLayoutWidgetBase<IsStateful, ChildDeleter> {
  using base = BoxLayoutWidgetBase<IsStateful, ChildDeleter>;

  BasicMargin(uint32_t margin, Widget *child)
      : base{child}, margin_{Edge::uniform(margin)} {}
  BasicMargin(uint32_t x, uint32_t y, Widget *child)
      : base{child}, margin_{Edge::xy(x, y)} {}
  BasicMargin(uint32_t top, uint32_t right, uint32_t bottom, uint32_t left,
              Widget *child)
      : base{child}, margin_{Edge::trbl(top, right, bottom, left)} {}
  BasicMargin(Edge const &margin, Widget *child)
      : base{child}, margin_{margin} {}

  virtual Rect compute_area(Extent const &allotted_extent,
                            stx::Span<Rect> const &children_area) override {
    Widget *child = this->get_children()[0];

    std::vector<Rect> child_children_area;
    child_children_area.resize(child->get_children().size());

    Extent allotted_child_extent{};

    allotted_child_extent.width =
        allotted_extent.width -
        std::min(allotted_extent.width, margin_.left + margin_.right);
    allotted_child_extent.height =
        allotted_extent.height -
        std::min(allotted_extent.height, margin_.top + margin_.bottom);

    Rect child_area =
        child->compute_area(allotted_child_extent, child_children_area);

    children_area[0].offset.x = std::min(margin_.left, allotted_extent.width);
    children_area[0].offset.y = std::min(margin_.top, allotted_extent.height);
    children_area[0].extent.width =
        std::min(allotted_child_extent.width, child_area.extent.width);
    children_area[0].extent.height =
        std::min(allotted_child_extent.height, child_area.extent.height);

    Offset offset = {0, 0};

    Extent extent = {0, 0};
    extent.width =
        std::min(margin_.left + child_area.extent.width + margin_.right,
                 allotted_extent.width);
    extent.height =
        std::min(margin_.top + child_area.extent.height + margin_.bottom,
                 allotted_extent.height);

    return Rect{offset, extent};
  }

  virtual bool is_dirty() const noexcept override { return false; }
  virtual void mark_clean() noexcept override {
    // no-op
  }

  virtual std::string_view get_type_hint() const noexcept override {
    return "Margin";
  }

 private:
  Edge margin_;
};

using Margin = BasicMargin<false>;

}  // namespace ui
}  // namespace vlk