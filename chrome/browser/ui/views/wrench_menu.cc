// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/wrench_menu.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/search/search.h"
#include "chrome/browser/ui/search/search_model.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_menu_delegate.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/button/text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_config.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/menu_scroll_view_container.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/widget.h"

using content::HostZoomMap;
using content::UserMetricsAction;
using content::WebContents;
using ui::MenuModel;
using views::CustomButton;
using views::ImageButton;
using views::Label;
using views::MenuConfig;
using views::MenuItemView;
using views::TextButton;
using views::View;

namespace {

// Colors used for buttons.
const SkColor kHotBorderColor = SkColorSetARGB(72, 0, 0, 0);
const SkColor kBorderColor = SkColorSetARGB(36, 0, 0, 0);
const SkColor kPushedBorderColor = SkColorSetARGB(72, 0, 0, 0);
const SkColor kHotBackgroundColor = SkColorSetARGB(204, 255, 255, 255);
const SkColor kBackgroundColor = SkColorSetARGB(102, 255, 255, 255);
const SkColor kPushedBackgroundColor = SkColorSetARGB(13, 0, 0, 0);
const SkColor kTouchBackgroundColor = SkColorSetARGB(247, 255, 255, 255);
const SkColor kHotTouchBackgroundColor = SkColorSetARGB(247, 242, 242, 242);
const SkColor kPushedTouchBackgroundColor = SkColorSetARGB(247, 235, 235, 235);

const SkColor kTouchButtonText = 0xff5a5a5a;
const SkColor kTouchImageBrighten = 0x80ffffff;

// Horizontal padding on the edges of the buttons.
const int kHorizontalPadding = 6;
// Horizontal padding for a touch enabled menu.
const int kHorizontalTouchPadding = 15;

// For touch menu items we want to have this height in pixels.
const int kTouchItemHeight = 47;

// Subclass of ImageButton whose preferred size includes the size of the border.
class FullscreenButton : public ImageButton {
 public:
  explicit FullscreenButton(views::ButtonListener* listener)
      : ImageButton(listener) { }

  virtual gfx::Size GetPreferredSize() {
    gfx::Size pref = ImageButton::GetPreferredSize();
    gfx::Insets insets;
    if (border())
      border()->GetInsets(&insets);
    pref.Enlarge(insets.width(), insets.height());
    return pref;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FullscreenButton);
};

// Border for buttons contained in the menu. This is only used for getting the
// insets, the actual painting is done in MenuButtonBackground.
class MenuButtonBorder : public views::Border {
 public:
  MenuButtonBorder()
    : horizontal_padding_(ui::GetDisplayLayout() == ui::LAYOUT_TOUCH ?
                              kHorizontalTouchPadding :
                              kHorizontalPadding) {}

  virtual void Paint(const View& view, gfx::Canvas* canvas) const {
    // Painting of border is done in MenuButtonBackground.
  }

  virtual void GetInsets(gfx::Insets* insets) const {
    insets->Set(MenuConfig::instance().item_top_margin,
                horizontal_padding_,
                MenuConfig::instance().item_bottom_margin,
                horizontal_padding_);
  }

 private:
  // The horizontal padding dependent on the layout.
  const int horizontal_padding_;

  DISALLOW_COPY_AND_ASSIGN(MenuButtonBorder);
};

// Combination border/background for the buttons contained in the menu. The
// painting of the border/background is done here as TextButton does not always
// paint the border.
class MenuButtonBackground : public views::Background {
 public:
  enum ButtonType {
    LEFT_BUTTON,
    CENTER_BUTTON,
    RIGHT_BUTTON,
    SINGLE_BUTTON,
  };

  explicit MenuButtonBackground(ButtonType type)
      : type_(type),
        left_button_(NULL),
        right_button_(NULL) {}

  // Used when the type is CENTER_BUTTON to determine if the left/right edge
  // needs to be rendered selected.
  void SetOtherButtons(CustomButton* left_button, CustomButton* right_button) {
    if (base::i18n::IsRTL()) {
      left_button_ = right_button;
      right_button_ = left_button;
    } else {
      left_button_ = left_button;
      right_button_ = right_button;
    }
  }

  virtual void Paint(gfx::Canvas* canvas, View* view) const {
    CustomButton::ButtonState state =
        (view->GetClassName() == views::Label::kViewClassName) ?
        CustomButton::BS_NORMAL : static_cast<CustomButton*>(view)->state();
    int w = view->width();
    int h = view->height();
    // Windows is drawing its own separators and we cannot use the touch button
    // for that.
#if !defined(OS_WIN)
    if (ui::GetDisplayLayout() == ui::LAYOUT_TOUCH) {
      // Normal buttons get a border drawn on the right side and the rest gets
      // filled in. The left button however does not get a line to combine
      // buttons.
      int border = 0;
      if (type_ != RIGHT_BUTTON) {
        border = 1;
        canvas->FillRect(gfx::Rect(0, 0, border, h),
                         border_color(CustomButton::BS_NORMAL));
      }
      canvas->FillRect(gfx::Rect(border, 0, w - border, h),
                       touch_background_color(state));
      return;
    }
#endif
    switch (TypeAdjustedForRTL()) {
      case LEFT_BUTTON:
        canvas->FillRect(gfx::Rect(1, 1, w, h - 2), background_color(state));
        canvas->FillRect(gfx::Rect(2, 0, w, 1), border_color(state));
        canvas->FillRect(gfx::Rect(1, 1, 1, 1), border_color(state));
        canvas->FillRect(gfx::Rect(0, 2, 1, h - 4), border_color(state));
        canvas->FillRect(gfx::Rect(1, h - 2, 1, 1), border_color(state));
        canvas->FillRect(gfx::Rect(2, h - 1, w, 1), border_color(state));
        break;

      case CENTER_BUTTON: {
        canvas->FillRect(gfx::Rect(1, 1, w - 2, h - 2),
                         background_color(state));
        SkColor left_color = state != CustomButton::BS_NORMAL ?
            border_color(state) : border_color(left_button_->state());
        canvas->FillRect(gfx::Rect(0, 0, 1, h), left_color);
        canvas->FillRect(gfx::Rect(1, 0, w - 2, 1), border_color(state));
        canvas->FillRect(gfx::Rect(1, h - 1, w - 2, 1), border_color(state));
        SkColor right_color = state != CustomButton::BS_NORMAL ?
            border_color(state) : border_color(right_button_->state());
        canvas->FillRect(gfx::Rect(w - 1, 0, 1, h), right_color);
        break;
      }

      case RIGHT_BUTTON:
        canvas->FillRect(gfx::Rect(0, 1, w - 1, h - 2),
                         background_color(state));
        canvas->FillRect(gfx::Rect(0, 0, w - 2, 1), border_color(state));
        canvas->FillRect(gfx::Rect(w - 2, 1, 1, 1), border_color(state));
        canvas->FillRect(gfx::Rect(w - 1, 2, 1, h - 4), border_color(state));
        canvas->FillRect(gfx::Rect(w - 2, h - 2, 1, 1), border_color(state));
        canvas->FillRect(gfx::Rect(0, h - 1, w - 2, 1), border_color(state));
        break;

      case SINGLE_BUTTON:
        canvas->FillRect(gfx::Rect(1, 1, w - 2, h - 2),
                         background_color(state));
        canvas->FillRect(gfx::Rect(2, 0, w - 4, 1), border_color(state));
        canvas->FillRect(gfx::Rect(1, 1, 1, 1), border_color(state));
        canvas->FillRect(gfx::Rect(0, 2, 1, h - 4), border_color(state));
        canvas->FillRect(gfx::Rect(1, h - 2, 1, 1), border_color(state));
        canvas->FillRect(gfx::Rect(2, h - 1, w - 4, 1), border_color(state));
        canvas->FillRect(gfx::Rect(w - 2, 1, 1, 1), border_color(state));
        canvas->FillRect(gfx::Rect(w - 1, 2, 1, h - 4), border_color(state));
        canvas->FillRect(gfx::Rect(w - 2, h - 2, 1, 1), border_color(state));
        break;

      default:
        NOTREACHED();
        break;
    }
  }

 private:
  static SkColor border_color(CustomButton::ButtonState state) {
    switch (state) {
      case CustomButton::BS_HOT:    return kHotBorderColor;
      case CustomButton::BS_PUSHED: return kPushedBorderColor;
      default:                      return kBorderColor;
    }
  }

  static SkColor background_color(CustomButton::ButtonState state) {
    switch (state) {
      case CustomButton::BS_HOT:    return kHotBackgroundColor;
      case CustomButton::BS_PUSHED: return kPushedBackgroundColor;
      default:                      return kBackgroundColor;
    }
  }

  static SkColor touch_background_color(CustomButton::ButtonState state) {
    switch (state) {
      case CustomButton::BS_HOT:    return kHotTouchBackgroundColor;
      case CustomButton::BS_PUSHED: return kPushedTouchBackgroundColor;
      default:                      return kTouchBackgroundColor;
    }
  }

  ButtonType TypeAdjustedForRTL() const {
    if (!base::i18n::IsRTL())
      return type_;

    switch (type_) {
      case LEFT_BUTTON:   return RIGHT_BUTTON;
      case RIGHT_BUTTON:  return LEFT_BUTTON;
      default:            break;
    }
    return type_;
  }

  const ButtonType type_;

  // See description above setter for details.
  CustomButton* left_button_;
  CustomButton* right_button_;

  DISALLOW_COPY_AND_ASSIGN(MenuButtonBackground);
};

// A View subclass that forces SchedulePaint to paint all. Normally when the
// mouse enters/exits a button the buttons invokes SchedulePaint. As part of the
// button border (MenuButtonBackground) is rendered by the button to the
// left/right of it SchedulePaint on the the button may not be enough, so this
// forces a paint all.
class ScheduleAllView : public views::View {
 public:
  ScheduleAllView() {}

  virtual void SchedulePaintInRect(const gfx::Rect& r) {
    View::SchedulePaintInRect(gfx::Rect(0, 0, width(), height()));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ScheduleAllView);
};

string16 GetAccessibleNameForWrenchMenuItem(
      MenuModel* model, int item_index, int accessible_string_id) {
  string16 accessible_name = l10n_util::GetStringUTF16(accessible_string_id);
  string16 accelerator_text;

  ui::Accelerator menu_accelerator;
  if (model->GetAcceleratorAt(item_index, &menu_accelerator)) {
    accelerator_text =
        ui::Accelerator(menu_accelerator.key_code(),
                        menu_accelerator.modifiers()).GetShortcutText();
  }

  return MenuItemView::GetAccessibleNameForMenuItem(
      accessible_name, accelerator_text);
}

// WrenchMenuView is a view that can contain text buttons.
class WrenchMenuView : public ScheduleAllView, public views::ButtonListener {
 public:
  WrenchMenuView(WrenchMenu* menu, MenuModel* menu_model)
      : menu_(menu), menu_model_(menu_model) { }

  TextButton* CreateAndConfigureButton(int string_id,
                                       MenuButtonBackground::ButtonType type,
                                       int index,
                                       MenuButtonBackground** background) {
    return CreateButtonWithAccName(
      string_id, type, index, background, string_id);
  }

  TextButton* CreateButtonWithAccName(int string_id,
                                      MenuButtonBackground::ButtonType type,
                                      int index,
                                      MenuButtonBackground** background,
                                      int acc_string_id) {
    TextButton* button = new TextButton(this,
                                        l10n_util::GetStringUTF16(string_id));
    button->SetAccessibleName(
        GetAccessibleNameForWrenchMenuItem(menu_model_, index, acc_string_id));
    button->set_focusable(true);
    button->set_request_focus_on_press(false);
    button->set_tag(index);
    button->SetEnabled(menu_model_->IsEnabledAt(index));
    button->set_prefix_type(TextButton::PREFIX_HIDE);
    MenuButtonBackground* bg = new MenuButtonBackground(type);
    button->set_background(bg);
    button->SetEnabledColor(MenuConfig::instance().text_color);
    if (background)
      *background = bg;
    button->set_border(new MenuButtonBorder());
    button->set_alignment(TextButton::ALIGN_CENTER);
    button->SetFont(views::MenuConfig::instance().font);
    button->ClearMaxTextSize();
    AddChildView(button);
    return button;
  }

 protected:
  // Hosting WrenchMenu.
  WrenchMenu* menu_;

  // The menu model containing the increment/decrement/reset items.
  MenuModel* menu_model_;

 private:
  DISALLOW_COPY_AND_ASSIGN(WrenchMenuView);
};

class ButtonContainerMenuItemView : public MenuItemView {
 public:
  // Constructor for use with button containing menu items which have a
  // different height then .
  explicit ButtonContainerMenuItemView(MenuItemView* parent,
                                       int id,
                                       int height)
      : MenuItemView(parent, id, MenuItemView::NORMAL),
        height_(height) {
  };

  gfx::Size GetChildPreferredSize() OVERRIDE {
    gfx::Size size = MenuItemView::GetChildPreferredSize();
    // When there is a height override given, we need to deduct our spacing
    // above and below to get to the correct height to return here for the
    // child item.
    int height = height_ - GetTopMargin() - GetBottomMargin();
    if (height > size.height())
      size.set_height(height);
    return size;
  }

 private:
  int height_;

  DISALLOW_COPY_AND_ASSIGN(ButtonContainerMenuItemView);
};

gfx::ImageSkia* TintImage(gfx::ImageSkia* image, SkColor tint_value) {
  // In case of touch, the menu needs to be brightened up a bit.
  // Create a new bitmap since we do not want to change the original image.
  SkBitmap bitmap_copy;
  image->bitmap()->copyTo(&bitmap_copy, SkBitmap::kARGB_8888_Config);
  SkCanvas canvas(bitmap_copy);
  SkPaint paint;
  // We leave the old alpha alone and add the new color multiplied
  // with the source alpha to the existing alpha. Thus: We brighten
  // the image up - but only the non transparent pixels.
  paint.setXfermodeMode(SkXfermode::kDstATop_Mode);
  paint.setColor(tint_value);
  canvas.drawPaint(paint);
  return new gfx::ImageSkia(bitmap_copy);
}

}  // namespace

// CutCopyPasteView ------------------------------------------------------------

// CutCopyPasteView is the view containing the cut/copy/paste buttons.
class WrenchMenu::CutCopyPasteView : public WrenchMenuView {
 public:
  CutCopyPasteView(WrenchMenu* menu,
                   MenuModel* menu_model,
                   int cut_index,
                   int copy_index,
                   int paste_index)
      : WrenchMenuView(menu, menu_model) {
    TextButton* cut = CreateAndConfigureButton(
        IDS_CUT, MenuButtonBackground::LEFT_BUTTON, cut_index, NULL);

    MenuButtonBackground* copy_background = NULL;
    TextButton* copy = CreateAndConfigureButton(
        IDS_COPY, MenuButtonBackground::CENTER_BUTTON, copy_index,
        &copy_background);

    bool is_touch = ui::GetDisplayLayout() == ui::LAYOUT_TOUCH;

    TextButton* paste = CreateAndConfigureButton(
        IDS_PASTE,
#if !defined(OS_WIN)
        is_touch ? MenuButtonBackground::CENTER_BUTTON :
#endif
            MenuButtonBackground::RIGHT_BUTTON,
        paste_index,
        NULL);
    if (is_touch) {
      cut->SetEnabledColor(kTouchButtonText);
      copy->SetEnabledColor(kTouchButtonText);
      paste->SetEnabledColor(kTouchButtonText);
    }
    copy_background->SetOtherButtons(cut, paste);
  }

  gfx::Size GetPreferredSize() {
    // Returned height doesn't matter as MenuItemView forces everything to the
    // height of the menuitemview.
    return gfx::Size(GetMaxChildViewPreferredWidth() * child_count(), 0);
  }

  void Layout() {
    // All buttons are given the same width.
    int width = GetMaxChildViewPreferredWidth();
    for (int i = 0; i < child_count(); ++i)
      child_at(i)->SetBounds(i * width, 0, width, height());
  }

  // ButtonListener
  virtual void ButtonPressed(views::Button* sender, const views::Event& event) {
    menu_->CancelAndEvaluate(menu_model_, sender->tag());
  }

 private:
  // Returns the max preferred width of all the children.
  int GetMaxChildViewPreferredWidth() {
    int width = 0;
    for (int i = 0; i < child_count(); ++i)
      width = std::max(width, child_at(i)->GetPreferredSize().width());
    return width;
  }

  DISALLOW_COPY_AND_ASSIGN(CutCopyPasteView);
};

// ZoomView --------------------------------------------------------------------

// Padding between the increment buttons and the reset button.
static const int kZoomPadding = 6;
static const int kTouchZoomPadding = 14;

// ZoomView contains the various zoom controls: two buttons to increase/decrease
// the zoom, a label showing the current zoom percent, and a button to go
// full-screen.
class WrenchMenu::ZoomView : public WrenchMenuView,
                             public content::NotificationObserver {
 public:
  ZoomView(WrenchMenu* menu,
           MenuModel* menu_model,
           int decrement_index,
           int increment_index,
           int fullscreen_index)
      : WrenchMenuView(menu, menu_model),
        fullscreen_index_(fullscreen_index),
        increment_button_(NULL),
        zoom_label_(NULL),
        decrement_button_(NULL),
        fullscreen_button_(NULL),
        zoom_label_width_(0) {
    decrement_button_ = CreateButtonWithAccName(
        IDS_ZOOM_MINUS2, MenuButtonBackground::LEFT_BUTTON, decrement_index,
        NULL, IDS_ACCNAME_ZOOM_MINUS2);

    zoom_label_ = new Label(
        l10n_util::GetStringFUTF16Int(IDS_ZOOM_PERCENT, 100));
    zoom_label_->SetAutoColorReadabilityEnabled(false);
    zoom_label_->SetHorizontalAlignment(Label::ALIGN_RIGHT);

    bool is_touch = ui::GetDisplayLayout() == ui::LAYOUT_TOUCH;

    MenuButtonBackground* center_bg = new MenuButtonBackground(
#if !defined(OS_WIN)
        is_touch ? MenuButtonBackground::RIGHT_BUTTON :
#endif
            MenuButtonBackground::CENTER_BUTTON);
    zoom_label_->set_background(center_bg);
    zoom_label_->set_border(new MenuButtonBorder());
    zoom_label_->SetFont(MenuConfig::instance().font);

    AddChildView(zoom_label_);
    zoom_label_width_ = MaxWidthForZoomLabel();

    increment_button_ = CreateButtonWithAccName(
        IDS_ZOOM_PLUS2, MenuButtonBackground::RIGHT_BUTTON, increment_index,
        NULL, IDS_ACCNAME_ZOOM_PLUS2);

    center_bg->SetOtherButtons(decrement_button_, increment_button_);

    fullscreen_button_ = new FullscreenButton(this);
    gfx::ImageSkia* full_screen_image =
        ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
            IDR_FULLSCREEN_MENU_BUTTON);
    if (is_touch) {
      tinted_fullscreen_image_.reset(TintImage(full_screen_image,
                                               kTouchImageBrighten));
      fullscreen_button_->SetImage(ImageButton::BS_NORMAL,
          tinted_fullscreen_image_.get());
    } else {
      fullscreen_button_->SetImage(ImageButton::BS_NORMAL, full_screen_image);
    }
    if (is_touch) {
      zoom_label_->SetEnabledColor(kTouchButtonText);
      decrement_button_->SetEnabledColor(kTouchButtonText);
      increment_button_->SetEnabledColor(kTouchButtonText);
    } else {
      zoom_label_->SetEnabledColor(MenuConfig::instance().text_color);
    }

    fullscreen_button_->set_focusable(true);
    fullscreen_button_->set_request_focus_on_press(false);
    fullscreen_button_->set_tag(fullscreen_index);
    fullscreen_button_->SetImageAlignment(
        ImageButton::ALIGN_CENTER, ImageButton::ALIGN_MIDDLE);
    int horizontal_padding = ui::GetDisplayLayout() == ui::LAYOUT_TOUCH ?
                                 kHorizontalTouchPadding : kHorizontalPadding;
    fullscreen_button_->set_border(views::Border::CreateEmptyBorder(
        0, horizontal_padding, 0, horizontal_padding));
    fullscreen_button_->set_background(
        new MenuButtonBackground(MenuButtonBackground::SINGLE_BUTTON));
    fullscreen_button_->SetAccessibleName(
        GetAccessibleNameForWrenchMenuItem(
            menu_model, fullscreen_index, IDS_ACCNAME_FULLSCREEN));
    AddChildView(fullscreen_button_);

    UpdateZoomControls();

    registrar_.Add(
        this, content::NOTIFICATION_ZOOM_LEVEL_CHANGED,
        content::Source<HostZoomMap>(
            HostZoomMap::GetForBrowserContext(menu->browser_->profile())));
  }

  gfx::Size GetPreferredSize() {
    // The increment/decrement button are forced to the same width.
    int button_width = std::max(increment_button_->GetPreferredSize().width(),
                                decrement_button_->GetPreferredSize().width());
    int zoom_padding = ui::GetDisplayLayout() == ui::LAYOUT_TOUCH ?
                           kTouchZoomPadding : kZoomPadding;
    int fullscreen_width = fullscreen_button_->GetPreferredSize().width() +
                           zoom_padding;
    // Returned height doesn't matter as MenuItemView forces everything to the
    // height of the menuitemview. Note that we have overridden the height when
    // constructing the menu.
    return gfx::Size(button_width + zoom_label_width_ + button_width +
                     fullscreen_width, 0);
  }

  void Layout() {
    int x = 0;
    int button_width = std::max(increment_button_->GetPreferredSize().width(),
                                decrement_button_->GetPreferredSize().width());
    gfx::Rect bounds(0, 0, button_width, height());

    decrement_button_->SetBoundsRect(bounds);

    x += bounds.width();
    bounds.set_x(x);
    bounds.set_width(zoom_label_width_);
    zoom_label_->SetBoundsRect(bounds);

    x += bounds.width();
    bounds.set_x(x);
    bounds.set_width(button_width);
    increment_button_->SetBoundsRect(bounds);

    bool is_touch = ui::GetDisplayLayout() == ui::LAYOUT_TOUCH;
    x += bounds.width() + (is_touch ? 0 : kZoomPadding);
    bounds.set_x(x);
    bounds.set_width(fullscreen_button_->GetPreferredSize().width() +
                     (is_touch ? kTouchZoomPadding : 0));
    fullscreen_button_->SetBoundsRect(bounds);
  }

  // ButtonListener:
  virtual void ButtonPressed(views::Button* sender, const views::Event& event) {
    if (sender->tag() == fullscreen_index_) {
      menu_->CancelAndEvaluate(menu_model_, sender->tag());
    } else {
      // Zoom buttons don't close the menu.
      menu_model_->ActivatedAt(sender->tag());
    }
  }

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) {
    DCHECK_EQ(content::NOTIFICATION_ZOOM_LEVEL_CHANGED, type);
    UpdateZoomControls();
  }

 private:
  void UpdateZoomControls() {
    int zoom = 100;
    // Don't override initial states of increment and decrement buttons when
    // instant extended API is enabled and mode is NTP; they are properly
    // updated in ToolbarView::ModeChanged() via CommandUpdater, and queried
    // via WrenchMenuModel::IsCommandIdEnabled() when the buttons were created
    // in CreateButtonWithAccName().
    if (!(chrome::search::IsInstantExtendedAPIEnabled(
              menu_->browser_->profile()) &&
          menu_->browser_->search_model()->mode().is_ntp())) {
      bool enable_increment = false;
      bool enable_decrement = false;
      WebContents* selected_tab = chrome::GetActiveWebContents(menu_->browser_);
      if (selected_tab) {
        zoom = selected_tab->GetZoomPercent(&enable_increment,
                                            &enable_decrement);
      }
      increment_button_->SetEnabled(enable_increment);
      decrement_button_->SetEnabled(enable_decrement);
    }
    zoom_label_->SetText(
        l10n_util::GetStringFUTF16Int(IDS_ZOOM_PERCENT, zoom));

    zoom_label_width_ = MaxWidthForZoomLabel();
  }

  // Calculates the max width the zoom string can be.
  int MaxWidthForZoomLabel() {
    gfx::Font font = zoom_label_->font();
    gfx::Insets insets;
    if (zoom_label_->border())
      zoom_label_->border()->GetInsets(&insets);

    int max_w = 0;

    WebContents* selected_tab = chrome::GetActiveWebContents(menu_->browser_);
    if (selected_tab) {
      int min_percent = selected_tab->GetMinimumZoomPercent();
      int max_percent = selected_tab->GetMaximumZoomPercent();

      int step = (max_percent - min_percent) / 10;
      for (int i = min_percent; i <= max_percent; i += step) {
        int w = font.GetStringWidth(
            l10n_util::GetStringFUTF16Int(IDS_ZOOM_PERCENT, i));
        max_w = std::max(w, max_w);
      }
    } else {
      max_w = font.GetStringWidth(
          l10n_util::GetStringFUTF16Int(IDS_ZOOM_PERCENT, 100));
    }

    return max_w + insets.width();
  }

  // Index of the fullscreen menu item in the model.
  const int fullscreen_index_;

  content::NotificationRegistrar registrar_;

  // Button for incrementing the zoom.
  TextButton* increment_button_;

  // Label showing zoom as a percent.
  Label* zoom_label_;

  // Button for decrementing the zoom.
  TextButton* decrement_button_;

  ImageButton* fullscreen_button_;

  // The tinted bitmap of the fullscreen button.
  scoped_ptr<gfx::ImageSkia> tinted_fullscreen_image_;

  // Width given to |zoom_label_|. This is the width at 100%.
  int zoom_label_width_;

  DISALLOW_COPY_AND_ASSIGN(ZoomView);
};

// WrenchMenu ------------------------------------------------------------------

WrenchMenu::WrenchMenu(Browser* browser)
    : root_(NULL),
      browser_(browser),
      selected_menu_model_(NULL),
      selected_index_(0),
      bookmark_menu_(NULL),
      feedback_menu_item_(NULL),
      first_bookmark_command_id_(0) {
  registrar_.Add(this, chrome::NOTIFICATION_GLOBAL_ERRORS_CHANGED,
                 content::Source<Profile>(browser_->profile()));
}

WrenchMenu::~WrenchMenu() {
  if (bookmark_menu_delegate_.get()) {
    BookmarkModel* model = browser_->profile()->GetBookmarkModel();
    if (model)
      model->RemoveObserver(this);
  }
}

void WrenchMenu::Init(ui::MenuModel* model) {
  DCHECK(!root_);
  root_ = new MenuItemView(this);
  root_->set_has_icons(true);  // We have checks, radios and icons, set this
                               // so we get the taller menu style.
  int next_id = 1;
  PopulateMenu(root_, model, &next_id);
  first_bookmark_command_id_ = next_id + 1;
  menu_runner_.reset(new views::MenuRunner(root_));
}

void WrenchMenu::RunMenu(views::MenuButton* host) {
  gfx::Point screen_loc;
  views::View::ConvertPointToScreen(host, &screen_loc);
  gfx::Rect bounds(screen_loc, host->size());
  content::RecordAction(UserMetricsAction("ShowAppMenu"));
  if (menu_runner_->RunMenuAt(host->GetWidget(), host, bounds,
          MenuItemView::TOPRIGHT, views::MenuRunner::HAS_MNEMONICS) ==
      views::MenuRunner::MENU_DELETED)
    return;
  if (bookmark_menu_delegate_.get()) {
    BookmarkModel* model = browser_->profile()->GetBookmarkModel();
    if (model)
      model->RemoveObserver(this);
  }
  if (selected_menu_model_)
    selected_menu_model_->ActivatedAt(selected_index_);
}

string16 WrenchMenu::GetTooltipText(int id,
                                    const gfx::Point& p) const {
  return is_bookmark_command(id) ?
      bookmark_menu_delegate_->GetTooltipText(id, p) : string16();
}

bool WrenchMenu::IsTriggerableEvent(views::MenuItemView* menu,
                                    const views::Event& e) {
  return is_bookmark_command(menu->GetCommand()) ?
      bookmark_menu_delegate_->IsTriggerableEvent(menu, e) :
      MenuDelegate::IsTriggerableEvent(menu, e);
}

bool WrenchMenu::GetDropFormats(
      MenuItemView* menu,
      int* formats,
      std::set<ui::OSExchangeData::CustomFormat>* custom_formats) {
  CreateBookmarkMenu();
  return bookmark_menu_delegate_.get() &&
      bookmark_menu_delegate_->GetDropFormats(menu, formats, custom_formats);
}

bool WrenchMenu::AreDropTypesRequired(MenuItemView* menu) {
  CreateBookmarkMenu();
  return bookmark_menu_delegate_.get() &&
      bookmark_menu_delegate_->AreDropTypesRequired(menu);
}

bool WrenchMenu::CanDrop(MenuItemView* menu,
                         const ui::OSExchangeData& data) {
  CreateBookmarkMenu();
  return bookmark_menu_delegate_.get() &&
      bookmark_menu_delegate_->CanDrop(menu, data);
}

int WrenchMenu::GetDropOperation(
    MenuItemView* item,
    const views::DropTargetEvent& event,
    DropPosition* position) {
  return is_bookmark_command(item->GetCommand()) ?
      bookmark_menu_delegate_->GetDropOperation(item, event, position) :
      ui::DragDropTypes::DRAG_NONE;
}

int WrenchMenu::OnPerformDrop(MenuItemView* menu,
                              DropPosition position,
                              const views::DropTargetEvent& event) {
  if (!is_bookmark_command(menu->GetCommand()))
    return ui::DragDropTypes::DRAG_NONE;

  int result = bookmark_menu_delegate_->OnPerformDrop(menu, position, event);
  return result;
}

bool WrenchMenu::ShowContextMenu(MenuItemView* source,
                                 int id,
                                 const gfx::Point& p,
                                 bool is_mouse_gesture) {
  return is_bookmark_command(id) ?
      bookmark_menu_delegate_->ShowContextMenu(source, id, p,
                                               is_mouse_gesture) :
      false;
}

bool WrenchMenu::CanDrag(MenuItemView* menu) {
  return is_bookmark_command(menu->GetCommand()) ?
      bookmark_menu_delegate_->CanDrag(menu) : false;
}

void WrenchMenu::WriteDragData(MenuItemView* sender,
                               ui::OSExchangeData* data) {
  DCHECK(is_bookmark_command(sender->GetCommand()));
  return bookmark_menu_delegate_->WriteDragData(sender, data);
}

int WrenchMenu::GetDragOperations(MenuItemView* sender) {
  return is_bookmark_command(sender->GetCommand()) ?
      bookmark_menu_delegate_->GetDragOperations(sender) :
      MenuDelegate::GetDragOperations(sender);
}

int WrenchMenu::GetMaxWidthForMenu(MenuItemView* menu) {
  return is_bookmark_command(menu->GetCommand()) ?
      bookmark_menu_delegate_->GetMaxWidthForMenu(menu) :
      MenuDelegate::GetMaxWidthForMenu(menu);
}

bool WrenchMenu::IsItemChecked(int id) const {
  if (is_bookmark_command(id))
    return false;

  const Entry& entry = id_to_entry_.find(id)->second;
  return entry.first->IsItemCheckedAt(entry.second);
}

bool WrenchMenu::IsCommandEnabled(int id) const {
  if (is_bookmark_command(id))
    return true;

  if (id == 0)
    return false;  // The root item.

  const Entry& entry = id_to_entry_.find(id)->second;
  int command_id = entry.first->GetCommandIdAt(entry.second);
  // The items representing the cut menu (cut/copy/paste) are always enabled.
  // The child views of these items enabled state updates appropriately.
  return command_id == IDC_CUT || entry.first->IsEnabledAt(entry.second);
}

void WrenchMenu::ExecuteCommand(int id, int mouse_event_flags) {
  if (is_bookmark_command(id)) {
    bookmark_menu_delegate_->ExecuteCommand(id, mouse_event_flags);
    return;
  }

  // Not a bookmark
  const Entry& entry = id_to_entry_.find(id)->second;
  int command_id = entry.first->GetCommandIdAt(entry.second);

  if (command_id == IDC_CUT || command_id == IDC_ZOOM_MINUS) {
    // These items are represented by child views. If ExecuteCommand is invoked
    // it means the user clicked on the area around the buttons and we should
    // not do anyting.
    return;
  }

  return entry.first->ActivatedAt(entry.second);
}

bool WrenchMenu::GetAccelerator(int id, ui::Accelerator* accelerator) {
  if (is_bookmark_command(id))
    return false;
  IDToEntry::iterator ix = id_to_entry_.find(id);
  if (ix == id_to_entry_.end()) {
    // There is no entry for this id.
    return false;
  }

  const Entry& entry = ix->second;
  int command_id = entry.first->GetCommandIdAt(entry.second);
  if (command_id == IDC_CUT || command_id == IDC_ZOOM_MINUS) {
    // These have special child views; don't show the accelerator for them.
    return false;
  }

  ui::Accelerator menu_accelerator;
  if (!entry.first->GetAcceleratorAt(entry.second, &menu_accelerator))
    return false;

  *accelerator = ui::Accelerator(menu_accelerator.key_code(),
                                 menu_accelerator.modifiers());
  return true;
}

void WrenchMenu::WillShowMenu(MenuItemView* menu) {
  if (menu == bookmark_menu_)
    CreateBookmarkMenu();
}

void WrenchMenu::WillHideMenu(MenuItemView* menu) {
  // Turns off the fade out animation of the wrench menus if
  // |feedback_menu_item_| is selected.  This excludes the wrench menu itself
  // from the snapshot in the feedback UI.
  if (menu->HasSubmenu() && feedback_menu_item_ &&
      feedback_menu_item_->IsSelected()) {
    // It's okay to just turn off the animation and no to take care the
    // animation back because the menu widget will be recreated next time
    // it's opened. See ToolbarView::RunMenu() and Init() of this class.
    menu->GetSubmenu()->GetWidget()->
        SetVisibilityChangedAnimationsEnabled(false);
  }
}

void WrenchMenu::BookmarkModelChanged() {
  DCHECK(bookmark_menu_delegate_.get());
  if (!bookmark_menu_delegate_->is_mutating_model())
    root_->Cancel();
}


void WrenchMenu::Observe(int type,
                         const content::NotificationSource& source,
                         const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_GLOBAL_ERRORS_CHANGED:
      // A change in the global errors list can add or remove items from the
      // menu. Close the menu to avoid have a stale menu on-screen.
      root_->Cancel();
      break;
    default:
      NOTREACHED();
  }
}

void WrenchMenu::PopulateMenu(MenuItemView* parent,
                              MenuModel* model,
                              int* next_id) {
  bool is_touch = ui::GetDisplayLayout() == ui::LAYOUT_TOUCH;

  int index_offset = model->GetFirstItemIndex(NULL);
  for (int i = 0, max = model->GetItemCount(); i < max; ++i) {
    int index = i + index_offset;

    // The button container menu items have a special height which we have to
    // use instead of the normal height.
    int height = 0;
    if (is_touch &&
        (model->GetCommandIdAt(index) == IDC_CUT ||
         model->GetCommandIdAt(index) == IDC_ZOOM_MINUS))
      height = kTouchItemHeight;

    MenuItemView* item = AppendMenuItem(
        parent, model, index, model->GetTypeAt(index), next_id, height);

    if (model->GetTypeAt(index) == MenuModel::TYPE_SUBMENU)
      PopulateMenu(item, model->GetSubmenuModelAt(index), next_id);

    switch (model->GetCommandIdAt(index)) {
      case IDC_CUT:
        DCHECK_EQ(MenuModel::TYPE_COMMAND, model->GetTypeAt(index));
        DCHECK_LT(i + 2, max);
        DCHECK_EQ(IDC_COPY, model->GetCommandIdAt(index + 1));
        DCHECK_EQ(IDC_PASTE, model->GetCommandIdAt(index + 2));
        item->SetTitle(l10n_util::GetStringUTF16(IDS_EDIT2));
        item->AddChildView(
            new CutCopyPasteView(
                this, model, index, index + 1, index + 2));
        i += 2;
        break;

      case IDC_ZOOM_MINUS:
        DCHECK_EQ(MenuModel::TYPE_COMMAND, model->GetTypeAt(index));
        DCHECK_EQ(IDC_ZOOM_PLUS, model->GetCommandIdAt(index + 1));
        DCHECK_EQ(IDC_FULLSCREEN, model->GetCommandIdAt(index + 2));
        item->SetTitle(l10n_util::GetStringUTF16(IDS_ZOOM_MENU2));
        item->AddChildView(
            new ZoomView(this, model, index, index + 1, index + 2));
        i += 2;
        break;

      case IDC_BOOKMARKS_MENU:
        DCHECK(!bookmark_menu_);
        bookmark_menu_ = item;
        break;

      case IDC_FEEDBACK:
        DCHECK(!feedback_menu_item_);
        feedback_menu_item_ = item;
        break;

      default:
        break;
    }
  }
}

MenuItemView* WrenchMenu::AppendMenuItem(MenuItemView* parent,
                                         MenuModel* model,
                                         int index,
                                         MenuModel::ItemType menu_type,
                                         int* next_id,
                                         int height) {
  int id = (*next_id)++;

  id_to_entry_[id].first = model;
  id_to_entry_[id].second = index;

  MenuItemView* menu_item = NULL;
  if (height > 0) {
    // For menu items with a special menu height we use our special class to be
    // able to modify the item height.
    menu_item = new ButtonContainerMenuItemView(parent, id, height);
    parent->GetSubmenu()->AddChildView(menu_item);
  } else {
    // For all other cases we use the more generic way to add menu items.
    menu_item = parent->AppendMenuItemFromModel(model, index, id);
  }

  if (menu_item) {
    // Flush all buttons to the right side of the menu for touch menus.
    menu_item->set_use_right_margin(
        ui::GetDisplayLayout() != ui::LAYOUT_TOUCH);
    menu_item->SetVisible(model->IsVisibleAt(index));

    if (menu_type == MenuModel::TYPE_COMMAND && model->HasIcons()) {
      gfx::ImageSkia icon;
      if (model->GetIconAt(index, &icon))
        menu_item->SetIcon(icon);
    }
  }

  return menu_item;
}

void WrenchMenu::CancelAndEvaluate(MenuModel* model, int index) {
  selected_menu_model_ = model;
  selected_index_ = index;
  root_->Cancel();
}

void WrenchMenu::CreateBookmarkMenu() {
  if (bookmark_menu_delegate_.get())
    return;  // Already created the menu.

  BookmarkModel* model = browser_->profile()->GetBookmarkModel();
  if (!model->IsLoaded())
    return;

  model->AddObserver(this);

  // TODO(oshima): Replace with views only API.
  views::Widget* parent = views::Widget::GetWidgetForNativeWindow(
      browser_->window()->GetNativeWindow());
  bookmark_menu_delegate_.reset(
      new BookmarkMenuDelegate(browser_,
                               browser_,
                               parent,
                               first_bookmark_command_id_));
  bookmark_menu_delegate_->Init(
      this, bookmark_menu_, model->bookmark_bar_node(), 0,
      BookmarkMenuDelegate::SHOW_PERMANENT_FOLDERS,
      bookmark_utils::LAUNCH_WRENCH_MENU);
}
