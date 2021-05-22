#pragma once

#include <utility>
#include <vector>

#include "vlk/ui/layout_tree.h"
#include "vlk/ui/primitives.h"
#include "vlk/ui/widget.h"

namespace vlk {
namespace ui {

// rendertile
// should have all the widgets on the screen with their clip coordinates, this
// should be updated from the view tree which applies a visibility and
// rectangular clip on it. this will enable the rastertile to only be updated on
// a per-widget basis and not depend on the view itself the view tree would just
// get the tile at the index and mark it as dirty.
//
//
// NOTE: the root widget must not change its view_offset as this will make its
// whole area dirty and it'll make no difference to re-drawing on every frame.
//
// NOTE: keep track of the previous screen offset on scroll, this way, we can
// only clean the parts that have actually changed, and on_view_offset dirty
// won't have to imply on_render_dirty and therefore invalidating the whole area
// the view widget covers.
// what if the widget is not visible in b
//

// translates widget from its normal position on its parent view.
template <typename T>
constexpr void view_translate_helper_(T &entry, IOffset const &translation) {
  entry.effective_parent_view_offset =
      IOffset(entry.layout_node->parent_view_offset) + translation;
}

// cache invalidation sources:
// - view offset change
// - layout change
// - viewport resize
//
// not affected by viewport scrolling
//
// invalidates:
// - tile cache
//
struct ViewTree {
  // presently we update the effective parent view offset and screen offsets
  // on-demand for each subview and child widgets primarily because we want to
  // avoid re-calculating that for each widget at the final render stage. the
  // view clip effects are dependent on the views so we don't have to touch to
  // many places in memory to get that result in the final render stage.

  struct View {
    // all entries are positioned relative to the view
    // we update the screen offsets
    // for all the child widgets on scroll

    struct Entry {
      LayoutTree::Node const *layout_node;

      IOffset screen_offset;

      // offset on the parent view after translation (i.e. by scrolling)
      IOffset effective_parent_view_offset;

      // can be null
      View const *parent;

      ZIndex z_index;

      IRect clip_rect;

      void build(LayoutTree::Node const &init_layout_node, View &view_parent,
                 ZIndex init_z_index) {
        // build child widgets, add child views to the tree.
        // we need to avoid performing multiple counting on every
        // depth increment. we can add them to the tree and fix the reference
        // linking later on.

        layout_node = &init_layout_node;

        // not yet updated
        effective_parent_view_offset = {};
        screen_offset = {};

        parent = nullptr;

        z_index = init_layout_node.widget->get_z_index().unwrap_or(
            std::move(init_z_index));

        clip_rect = {};

        for (LayoutTree::Node const &child : init_layout_node.children) {
          if (child.type == Widget::Type::View) {
            view_parent.subviews.push_back(View{});
            view_parent.subviews.back().build(child, init_z_index + 1);
          } else {
            view_parent.entries.push_back(View::Entry{});
            view_parent.entries.back().build(child, view_parent,
                                             init_z_index + 1);
          }
        }
      }
    };

    bool is_dirty = true;

    // non-null
    LayoutTree::Node const *layout_node;

    // pre-calculated for the tile cache
    ZIndex z_index;

    // position on the root widget. by screen here, we mean the resulting
    // surface of the root widget.
    IOffset screen_offset;

    // offset on the parent view after translation (i.e. by scrolling)
    IOffset effective_parent_view_offset;

    // will make processing view clips easier for the tile cache
    View const *parent;

    // non-view widgets. not sorted in any particular order
    std::vector<Entry> entries;

    // view widgets. not sorted in any particular order
    std::vector<View> subviews;

    void build(LayoutTree::Node const &init_layout_node, ZIndex init_z_index) {
      is_dirty = true;
      layout_node = &init_layout_node;
      z_index = init_layout_node.widget->get_z_index().unwrap_or(
          std::move(init_z_index));

      // needs to be updated after building the tree, by recursively triggering
      // a on_view_offset dirty starting from the root view.
      screen_offset = {};
      effective_parent_view_offset = {};

      // parents and children are not linked until the building of the whole
      // tree is done as the addresses will not be stable until then
      parent = nullptr;

      size_t const num_children = init_layout_node.children.size();

      for (size_t i = 0; i < num_children; i++) {
        LayoutTree::Node const &child = init_layout_node.children[i];
        if (child.type == Widget::Type::View) {
          subviews.push_back(View{});
          subviews.back().build(child, z_index + 1);
        } else {
          entries.push_back(View::Entry{});
          entries.back().build(child, *this, z_index + 1);
        }
      }
    }

    static void update_screen_offset_helper_(View &subview, View &parent) {
      subview.screen_offset =
          parent.screen_offset + subview.effective_parent_view_offset;
      // remember: views do not have render data so they will not invalidate any
      // area of the cache
    }

    static void update_screen_offset_helper_(View::Entry &entry, View &parent) {
      // mark that the intersecting tiles are now dirty
      IOffset const new_screen_offset =
          parent.screen_offset + entry.effective_parent_view_offset;

      IRect const previous_clip_rect = entry.clip_rect;

      ViewTree::View const *ancestor = entry.parent;

      IRect new_clip_rect =
          IRect{new_screen_offset, entry.layout_node->self_extent};

      while (ancestor != nullptr && new_clip_rect.visible()) {
        IRect ancestor_view_screen_rect{ancestor->screen_offset,
                                        ancestor->layout_node->self_extent};

        if (!ancestor_view_screen_rect.overlaps(new_clip_rect)) {
          new_clip_rect = new_clip_rect.with_extent(Extent{0, 0});
        } else {
          new_clip_rect = ancestor_view_screen_rect.intersect(new_clip_rect);
        }

        ancestor = ancestor->parent;
      }

      entry.clip_rect = new_clip_rect;

      // only mark intersecting tiles as dirty if its clip rect is visible

      if (entry.screen_offset != new_screen_offset) {
        // call the callback so the tile cache is aware that its entries are now
        // dirty
        if (previous_clip_rect.visible()) {
          entry.layout_node->widget->mark_render_dirty();
        }

        entry.screen_offset = new_screen_offset;

        if (new_clip_rect.visible()) {
          entry.layout_node->widget->mark_render_dirty();
        }
      }
    }

    static void update_screen_offset(View &child, View &parent) {
      update_screen_offset_helper_(child, parent);

      for (View::Entry &entry : child.entries) {
        update_screen_offset_helper_(entry, child);
      }

      for (View &subview : child.subviews) {
        update_screen_offset(subview, child);
      }
    }

    void translate(IOffset const &translation) {
      // adjust the view offset of the parent view.
      // and shifts (translates) the children accordingly. we have to
      // recursively perform passes to update the sceen offsets in the children.

      for (View::Entry &child : entries) {
        // translate the children's effective parent view offset by
        // `translation`.
        view_translate_helper_(child, translation);
        // update the resulting screen offset to match the new view position
        // using the parent view's
        update_screen_offset_helper_(child, *this);
      }

      // NOTE: that this only requires that we update this view's child views
      // offset and no need for translating them relative to their parent view
      for (View &subview : subviews) {
        view_translate_helper_(subview, translation);
        update_screen_offset(subview, *this);
      }
    }

    void clean_offsets() {
      // its offset could be correct and its children offset be incorrect so we
      // still need to recurse to the children irregardless
      if (is_dirty) {
        IOffset const translation =
            layout_node->widget->get_view_offset().resolve(
                layout_node->view_extent);
        translate(translation);
        is_dirty = false;
      }

      for (View &subview : subviews) {
        subview.clean_offsets();
      }
    }

    void force_clean_offsets() {
      IOffset const translation =
          layout_node->widget->get_view_offset().resolve(
              layout_node->view_extent);
      translate(translation);
      is_dirty = false;

      for (View &subview : subviews) {
        subview.force_clean_offsets();
      }
    }

    void build_links(bool &any_view_dirty) {
      // it is safe for the widget to access this multiple times between ticks
      // even though the user could pay a perf penalty. this saves us the stress
      // of accumulating scroll offsets into a vector.
      // the tiles the children widgets intersect will be marked as dirty.
      // performing multiple scrolls in between a tick wil unnecessarily mark
      // more tiles than needed.
      WidgetStateProxyAccessor::access(*layout_node->widget).on_render_dirty =
          [] {};
      WidgetStateProxyAccessor::access(*layout_node->widget)
          .on_view_offset_dirty = ([this, &any_view_dirty] {
        this->is_dirty = true;
        any_view_dirty = true;
      });

      for (View::Entry &entry : entries) {
        entry.parent = this;
      }

      for (View &subview : subviews) {
        subview.parent = this;
        subview.build_links(any_view_dirty);
      }
    }
  };

  ViewTree() = default;

  ViewTree(ViewTree const &) = delete;
  ViewTree(ViewTree &&) = delete;

  ViewTree &operator=(ViewTree const &) = delete;
  ViewTree &operator=(ViewTree &&) = delete;

  ~ViewTree() = default;

  View root_view{};
  bool any_view_dirty = true;

  void clean_offsets() {
    if (any_view_dirty) {
      root_view.clean_offsets();
      any_view_dirty = false;
    }
  }

  void force_clean_offsets() {
    root_view.force_clean_offsets();
    any_view_dirty = false;
  }

  void build_links() { root_view.build_links(any_view_dirty); }

  void build(LayoutTree const &layout_tree) {
    VLK_ENSURE(root_view.layout_node == nullptr);
    VLK_ENSURE(root_view.subviews.empty());
    VLK_ENSURE(root_view.entries.empty());

    VLK_ENSURE(layout_tree.root_node.type == Widget::Type::View);
    VLK_ENSURE(layout_tree.root_node.widget != nullptr);

    any_view_dirty = true;
    root_view.build(layout_tree.root_node, 0);
    // this should be cheap since we are unlikely to have many views and
    // subviews (>100 for example)
    build_links();
    tick(std::chrono::nanoseconds(0));
  }

  void tick(std::chrono::nanoseconds) { clean_offsets(); }
};

}  // namespace ui
}  // namespace vlk
