// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_tab_grid.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace venetium_w10m {
namespace {

// Chrome for Android's tab switcher, in DIPs.
constexpr int kHeaderHeight = 56;
constexpr int kHeaderButton = 48;
constexpr int kSearchHeight = 44;
constexpr int kGap = 12;
constexpr int kCardTitleHeight = 40;
constexpr int kCardInset = 4;
constexpr int kCardRadius = 16;
constexpr int kPictureRadius = 12;
constexpr int kActiveBorder = 3;

enum MenuCommand { kCloseAllTabs = 1, kSettings };

// A page's picture as Chrome keeps it for its tab hover cards: a JPEG,
// taken as the tab went to the background.
SkBitmap DecodePicture(ThumbnailImage::CompressedThumbnailData data) {
  if (!data) {
    return SkBitmap();
  }
  return gfx::JPEGCodec::Decode(data->data);
}

bool Matches(content::WebContents* contents, const std::u16string& filter) {
  return base::i18n::ToLower(contents->GetTitle()).find(filter) !=
             std::u16string::npos ||
         base::i18n::ToLower(base::UTF8ToUTF16(
                                 contents->GetVisibleURL().spec()))
                 .find(filter) != std::u16string::npos;
}

// The number of tabs in a rounded square, as on the toolbar's button.
class TabCountView : public views::View {
  METADATA_HEADER(TabCountView, views::View)

 public:
  TabCountView() = default;
  TabCountView(const TabCountView&) = delete;
  TabCountView& operator=(const TabCountView&) = delete;
  ~TabCountView() override = default;

  void SetCount(int count) {
    // Chrome for Android's face for a hundred tabs and more.
    text_ = count < 100 ? base::NumberToString16(count) : u":D";
    SchedulePaint();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const SkColor color = colors->GetColor(ui::kColorSysOnSurface);
    const float side = std::min(width(), height()) * 0.45f;
    const gfx::RectF box((width() - side) / 2, (height() - side) / 2, side,
                         side);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(2);
    flags.setColor(color);
    canvas->DrawRoundRect(box, side / 5, flags);
    const gfx::FontList font =
        gfx::FontList()
            .DeriveWithSizeDelta(static_cast<int>(side * 0.55f) -
                                 gfx::FontList().GetFontSize())
            .DeriveWithWeight(gfx::Font::Weight::BOLD);
    canvas->DrawStringRectWithFlags(text_, font, color,
                                    gfx::ToEnclosingRect(box),
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  std::u16string text_;
};

BEGIN_METADATA(TabCountView)
END_METADATA

// One tab: its favicon, title and close button over its page's picture, in
// a rounded card, outlined when it is the tab in front.
class TabCard : public views::Button {
  METADATA_HEADER(TabCard, views::Button)

 public:
  TabCard(content::WebContents* contents,
          bool active,
          PressedCallback open,
          PressedCallback close)
      : views::Button(std::move(open)), contents_(contents), active_(active) {
    favicon_ = AddChildView(std::make_unique<views::ImageView>());
    favicon_->SetImageSize(gfx::Size(16, 16));
    favicon_->SetCanProcessEventsWithinSubtree(false);
    title_ = AddChildView(std::make_unique<views::Label>());
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_->SetAutoColorReadabilityEnabled(false);
    title_->SetEnabledColor(ui::kColorSysOnSurface);
    title_->SetCanProcessEventsWithinSubtree(false);
    close_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
        std::move(close), vector_icons::kCloseIcon, 18,
        ui::kColorSysOnSurface));
    close_->SetTooltipText(l10n_util::GetStringUTF16(IDS_APP_CLOSE));
    close_->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_APP_CLOSE));
    Update();
  }
  TabCard(const TabCard&) = delete;
  TabCard& operator=(const TabCard&) = delete;
  ~TabCard() override = default;

  content::WebContents* contents() const { return contents_; }
  bool active() const { return active_; }

  // The tab's title and favicon, as they are now.
  void Update() {
    std::u16string title = contents_->GetTitle();
    if (title.empty()) {
      title = base::UTF8ToUTF16(contents_->GetVisibleURL().spec());
    }
    title_->SetText(title);
    GetViewAccessibility().SetName(title);
    const gfx::Image favicon = favicon::TabFaviconFromWebContents(contents_);
    favicon_->SetImage(favicon.IsEmpty()
                           ? ui::ImageModel()
                           : ui::ImageModel::FromImage(favicon));
  }

  void SetPicture(const gfx::ImageSkia& picture) {
    picture_ = picture;
    SchedulePaint();
  }

  // views::View:
  void Layout(PassKey) override {
    favicon_->SetBounds(12, (kCardTitleHeight - 16) / 2, 16, 16);
    close_->SetBounds(width() - kCardTitleHeight, 0, kCardTitleHeight,
                      kCardTitleHeight);
    title_->SetBounds(36, 0, std::max(0, width() - 36 - kCardTitleHeight),
                      kCardTitleHeight);
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const gfx::RectF card(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(ui::kColorSysBase));
    canvas->DrawRoundRect(card, kCardRadius, flags);

    // The page's picture: its top, as wide as the card.
    const gfx::Rect frame(kCardInset, kCardTitleHeight,
                          std::max(0, width() - 2 * kCardInset),
                          std::max(0, height() - kCardTitleHeight - kCardInset));
    if (!frame.IsEmpty()) {
      canvas->Save();
      canvas->ClipPath(SkPath::RRect(SkRRect::MakeRectXY(
                           gfx::RectToSkRect(frame), kPictureRadius,
                           kPictureRadius)),
                       /*do_anti_alias=*/true);
      if (picture_.isNull() || picture_.width() <= 0 ||
          picture_.height() <= 0) {
        canvas->FillRect(frame,
                         colors->GetColor(ui::kColorSysBaseContainer));
      } else {
        const float scale =
            std::max(static_cast<float>(frame.width()) / picture_.width(),
                     static_cast<float>(frame.height()) / picture_.height());
        const int source_width = std::min(
            picture_.width(),
            static_cast<int>(std::lround(frame.width() / scale)));
        const int source_height = std::min(
            picture_.height(),
            static_cast<int>(std::lround(frame.height() / scale)));
        canvas->DrawImageInt(picture_, (picture_.width() - source_width) / 2,
                             0, source_width, source_height, frame.x(),
                             frame.y(), frame.width(), frame.height(),
                             /*filter=*/true);
      }
      canvas->Restore();
    }

    if (active_) {
      gfx::RectF outline = card;
      outline.Inset(kActiveBorder / 2.f);
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(kActiveBorder);
      flags.setColor(colors->GetColor(ui::kColorSysPrimary));
      canvas->DrawRoundRect(outline, kCardRadius - kActiveBorder / 2.f, flags);
    }
  }

 private:
  const raw_ptr<content::WebContents> contents_;
  const bool active_;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageButton> close_ = nullptr;
  gfx::ImageSkia picture_;
};

BEGIN_METADATA(TabCard)
END_METADATA

// The cards, in as many columns as fit: two on a phone held upright.
class CardsView : public views::View {
  METADATA_HEADER(CardsView, views::View)

 public:
  CardsView() = default;
  CardsView(const CardsView&) = delete;
  CardsView& operator=(const CardsView&) = delete;
  ~CardsView() override = default;

  // The pictures' height to width, the page's own (within reason).
  void set_aspect(float aspect) { aspect_ = std::clamp(aspect, 0.6f, 1.25f); }

  int HeightFor(int width) const {
    const int count = static_cast<int>(children().size());
    const int columns = Columns(width);
    const int rows = (count + columns - 1) / columns;
    return kGap + rows * (CardSize(width).height() + kGap);
  }

  // views::View:
  void Layout(PassKey) override {
    const int columns = Columns(width());
    const gfx::Size card = CardSize(width());
    int i = 0;
    for (views::View* child : children()) {
      const int row = i / columns;
      const int column = i % columns;
      child->SetBounds(kGap + column * (card.width() + kGap),
                       kGap + row * (card.height() + kGap), card.width(),
                       card.height());
      ++i;
    }
  }

 private:
  static int Columns(int width) { return width >= 560 ? 3 : 2; }

  gfx::Size CardSize(int width) const {
    const int columns = Columns(width);
    const int card_width =
        std::max(0, (width - kGap * (columns + 1)) / columns);
    const int picture_height = static_cast<int>(
        std::lround(std::max(0, card_width - 2 * kCardInset) * aspect_));
    return gfx::Size(card_width,
                     kCardTitleHeight + picture_height + kCardInset);
  }

  float aspect_ = 1.25f;
};

BEGIN_METADATA(CardsView)
END_METADATA

// The tab switcher's whole window.
class GridView : public views::View,
                 public TabStripModelObserver,
                 public views::TextfieldController,
                 public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(GridView, views::View)

 public:
  explicit GridView(Browser* browser)
      : browser_(browser), model_(browser->tab_strip_model()) {
    SetBackground(views::CreateSolidBackground(ui::kColorSysBaseContainer));

    new_tab_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
        base::BindRepeating(&GridView::NewTab, base::Unretained(this)),
        kAddIcon, 24, ui::kColorSysOnSurface));
    new_tab_->SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_NEW_TAB));
    new_tab_->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_TOOLTIP_NEW_TAB));
    count_ = AddChildView(std::make_unique<TabCountView>());
    menu_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
        base::BindRepeating(&GridView::ShowMenu, base::Unretained(this)),
        kMoreVertIcon, 24, ui::kColorSysOnSurface));
    menu_->SetTooltipText(l10n_util::GetStringUTF16(IDS_APPMENU_TOOLTIP));
    menu_->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_APPMENU_TOOLTIP));

    search_box_ = AddChildView(std::make_unique<views::View>());
    search_box_->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysBase, kSearchHeight / 2.f));
    search_icon_ = search_box_->AddChildView(std::make_unique<views::ImageView>(
        ui::ImageModel::FromVectorIcon(vector_icons::kSearchIcon,
                                       ui::kColorSysOnSurface, 20)));
    search_ = search_box_->AddChildView(std::make_unique<views::Textfield>());
    search_->set_controller(this);
    search_->SetPlaceholderText(
        l10n_util::GetStringUTF16(IDS_TAB_SEARCH_SEARCH_TABS));
    search_->SetBorder(views::NullBorder());
    search_->SetBackgroundEnabled(false);
    search_->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_TAB_SEARCH_SEARCH_TABS));

    scroll_ = AddChildView(std::make_unique<views::ScrollView>());
    scroll_->SetBackgroundColor(std::nullopt);
    scroll_->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll_->SetVerticalScrollBarMode(
        views::ScrollView::ScrollBarMode::kHiddenButEnabled);
    scroll_->SetDrawOverflowIndicator(false);
    cards_ = scroll_->SetContents(std::make_unique<CardsView>());

    model_->AddObserver(this);
    Rebuild();
    LoadPictures();
  }
  GridView(const GridView&) = delete;
  GridView& operator=(const GridView&) = delete;
  ~GridView() override = default;

  // views::View:
  void Layout(PassKey) override {
    const int w = width();
    const int button_top = (kHeaderHeight - kHeaderButton) / 2;
    new_tab_->SetBounds(4, button_top, kHeaderButton, kHeaderButton);
    count_->SetBounds((w - kHeaderButton) / 2, button_top, kHeaderButton,
                      kHeaderButton);
    menu_->SetBounds(w - 4 - kHeaderButton, button_top, kHeaderButton,
                     kHeaderButton);

    search_box_->SetBounds(16, kHeaderHeight, std::max(0, w - 32),
                           kSearchHeight);
    search_icon_->SetBounds(12, (kSearchHeight - 20) / 2, 20, 20);
    search_->SetBounds(44, 0, std::max(0, search_box_->width() - 56),
                       kSearchHeight);

    const int top = kHeaderHeight + kSearchHeight + kGap / 2;
    scroll_->SetBounds(0, top, w, std::max(0, height() - top));
    if (BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser_)) {
      const gfx::Size page = view->contents_web_view()->size();
      if (!page.IsEmpty()) {
        cards_->set_aspect(static_cast<float>(page.height()) / page.width());
      }
    }
    cards_->SetSize(gfx::Size(w, cards_->HeightFor(w)));

    if (!scrolled_to_active_ && height() > 0) {
      scrolled_to_active_ = true;
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&GridView::ScrollToActive,
                                    weak_factory_.GetWeakPtr()));
    }
  }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    // Not now: this may be a card's own close button, still pressed.
    if (rebuild_pending_) {
      return;
    }
    rebuild_pending_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&GridView::Rebuild, weak_factory_.GetWeakPtr()));
  }
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override {
    if (TabCard* card = CardFor(tab->GetContents())) {
      card->Update();
    }
  }
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override {
    model_ = nullptr;
    Close();
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    filter_ = base::i18n::ToLower(new_contents);
    Rebuild();
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    if (!model_) {
      return;
    }
    switch (command_id) {
      case kCloseAllTabs:
        // The window closes with its last tab, and this with it; a new
        // window with a new tab follows (VwChromeHost::ReopenIfEmpty).
        model_->CloseAllTabs();
        break;
      case kSettings:
        chrome::ShowSettings(browser_);
        Close();
        break;
    }
  }

 private:
  void Close() {
    if (views::Widget* widget = GetWidget()) {
      widget->Close();
    }
  }

  TabCard* CardFor(content::WebContents* contents) {
    for (views::View* child : cards_->children()) {
      auto* card = static_cast<TabCard*>(child);
      if (card->contents() == contents) {
        return card;
      }
    }
    return nullptr;
  }

  bool HasTab(content::WebContents* contents) const {
    return model_ &&
           model_->GetIndexOfWebContents(contents) != TabStripModel::kNoTab;
  }

  void Rebuild() {
    rebuild_pending_ = false;
    if (!model_) {
      return;
    }
    // The pictures of tabs that are gone go.
    std::set<content::WebContents*> tabs;
    for (int i = 0; i < model_->count(); ++i) {
      tabs.insert(model_->GetWebContentsAt(i));
    }
    std::erase_if(pictures_, [&tabs](const auto& entry) {
      return !tabs.contains(entry.first);
    });

    cards_->RemoveAllChildViews();
    content::WebContents* const active = model_->GetActiveWebContents();
    for (int i = 0; i < model_->count(); ++i) {
      content::WebContents* contents = model_->GetWebContentsAt(i);
      if (!filter_.empty() && !Matches(contents, filter_)) {
        continue;
      }
      auto* card = cards_->AddChildView(std::make_unique<TabCard>(
          contents, contents == active,
          base::BindRepeating(&GridView::OpenTab, weak_factory_.GetWeakPtr(),
                              base::Unretained(contents)),
          base::BindRepeating(&GridView::CloseTab, weak_factory_.GetWeakPtr(),
                              base::Unretained(contents))));
      const auto picture = pictures_.find(contents);
      if (picture != pictures_.end()) {
        card->SetPicture(picture->second);
      }
    }
    count_->SetCount(model_->count());
    InvalidateLayout();
    SchedulePaint();
  }

  void ScrollToActive() {
    for (views::View* child : cards_->children()) {
      if (static_cast<TabCard*>(child)->active()) {
        child->ScrollViewToVisible();
        return;
      }
    }
  }

  void OpenTab(content::WebContents* contents) {
    if (!model_) {
      return;
    }
    const int index = model_->GetIndexOfWebContents(contents);
    if (index != TabStripModel::kNoTab) {
      model_->ActivateTabAt(index,
                            TabStripUserGestureDetails(
                                TabStripUserGestureDetails::GestureType::kOther));
    }
    Close();
  }

  void CloseTab(content::WebContents* contents) {
    if (!model_) {
      return;
    }
    const int index = model_->GetIndexOfWebContents(contents);
    if (index != TabStripModel::kNoTab) {
      model_->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
    }
  }

  void NewTab() {
    chrome::NewTab(browser_);
    Close();
  }

  void ShowMenu() {
    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_model_->AddItem(kCloseAllTabs, l10n_util::GetStringUTF16(
                                            IDS_TAB_CXMENU_CLOSEALLTABS));
    menu_model_->AddItem(kSettings, l10n_util::GetStringUTF16(IDS_SETTINGS));
    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
    menu_runner_->RunMenuAt(GetWidget(), nullptr, menu_->GetBoundsInScreen(),
                            views::MenuAnchorPosition::kTopRight,
                            ui::mojom::MenuSourceType::kTouch);
  }

  // The tab in front, as it is now; the others as Chrome took them when
  // they went to the background.
  void LoadPictures() {
    content::WebContents* const active = model_->GetActiveWebContents();
    for (int i = 0; i < model_->count(); ++i) {
      content::WebContents* contents = model_->GetWebContentsAt(i);
      if (contents == active && CaptureFront(contents)) {
        continue;
      }
      ThumbnailTabHelper* helper = ThumbnailTabHelper::FromWebContents(contents);
      ThumbnailImage::CompressedThumbnailData data =
          helper ? helper->thumbnail()->data() : nullptr;
      if (!data) {
        continue;
      }
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE,
          {base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
          base::BindOnce(&DecodePicture, std::move(data)),
          base::BindOnce(&GridView::SetPicture, weak_factory_.GetWeakPtr(),
                         base::Unretained(contents), /*keep=*/false));
    }
  }

  bool CaptureFront(content::WebContents* contents) {
    content::RenderWidgetHostView* view = contents->GetRenderWidgetHostView();
    if (!view || !view->IsSurfaceAvailableForCopy()) {
      return false;
    }
    const gfx::Size size = view->GetViewBounds().size();
    if (size.IsEmpty()) {
      return false;
    }
    // Half the screen's pixels: a card is under half the screen wide.
    const gfx::Size target = gfx::ScaleToCeiledSize(
        size, std::max(view->GetDeviceScaleFactor(), 1.f) * 0.5f);
    view->CopyFromSurface(
        gfx::Rect(), target, base::TimeDelta(),
        base::BindOnce(
            [](base::WeakPtr<GridView> grid, content::WebContents* contents,
               const content::CopyFromSurfaceResult& result) {
              if (grid && result.has_value()) {
                grid->SetPicture(contents, /*keep=*/true, result->bitmap);
              }
            },
            weak_factory_.GetWeakPtr(), base::Unretained(contents)));
    return true;
  }

  // With |keep|, Chrome keeps the picture too, for its next showing.
  void SetPicture(content::WebContents* contents, bool keep, SkBitmap bitmap) {
    if (bitmap.drawsNothing() || !HasTab(contents)) {
      return;
    }
    if (keep) {
      if (ThumbnailTabHelper* helper =
              ThumbnailTabHelper::FromWebContents(contents)) {
        helper->thumbnail()->AssignSkBitmap(bitmap);
      }
    }
    const gfx::ImageSkia picture = gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
    pictures_[contents] = picture;
    if (TabCard* card = CardFor(contents)) {
      card->SetPicture(picture);
    }
  }

  const raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> model_;
  raw_ptr<views::ImageButton> new_tab_ = nullptr;
  raw_ptr<TabCountView> count_ = nullptr;
  raw_ptr<views::ImageButton> menu_ = nullptr;
  raw_ptr<views::View> search_box_ = nullptr;
  raw_ptr<views::ImageView> search_icon_ = nullptr;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<CardsView> cards_ = nullptr;
  std::map<content::WebContents*, gfx::ImageSkia> pictures_;
  std::u16string filter_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  bool scrolled_to_active_ = false;
  bool rebuild_pending_ = false;
  base::WeakPtrFactory<GridView> weak_factory_{this};
};

BEGIN_METADATA(GridView)
END_METADATA

// The switcher's window, over the browser window and as large as the screen
// (the window manager keeps maximized windows so); it goes when it closes.
class GridWindow : public views::WidgetObserver {
 public:
  explicit GridWindow(BrowserView* browser_view, Browser* browser) {
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    params.parent = browser_view->GetWidget()->GetNativeView();
    params.bounds = browser_view->GetWidget()->GetWindowBoundsInScreen();
    params.show_state = ui::mojom::WindowShowState::kMaximized;
    params.name = "VwTabGrid";
    widget_ = std::make_unique<views::Widget>();
    widget_->Init(std::move(params));
    widget_->SetContentsView(std::make_unique<GridView>(browser));
    observation_.Observe(widget_.get());
    widget_->Show();
  }
  GridWindow(const GridWindow&) = delete;
  GridWindow& operator=(const GridWindow&) = delete;
  ~GridWindow() override = default;

  static GridWindow* current;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override {
    observation_.Reset();
    if (current == this) {
      current = nullptr;
    }
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                               this);
  }

 private:
  std::unique_ptr<views::Widget> widget_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

GridWindow* GridWindow::current = nullptr;

}  // namespace

// static
void VwTabGrid::Show(Browser* browser) {
  if (GridWindow::current || !browser) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->GetWidget()) {
    return;
  }
  GridWindow::current = new GridWindow(browser_view, browser);
}

}  // namespace venetium_w10m
