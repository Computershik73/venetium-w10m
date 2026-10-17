// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_browser.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "components/url_formatter/url_fixer.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "ui/aura/window.h"
#include "venetium_w10m/browser/vw_browser_context.h"
#include "venetium_w10m/browser/vw_frame_sink.h"
#include "venetium_w10m/browser/vw_platform.h"
#include "venetium_w10m/browser/vw_tab.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

VwBrowser::VwBrowser(VwBrowserContext* context,
                     const EngineConfig& config,
                     std::unique_ptr<VwChromeBridge> chrome)
    : context_(context),
      platform_(std::make_unique<VwPlatform>(
          config.size,
          config.screen,
          static_cast<float>(config.scale),
          /*window_manager=*/chrome != nullptr,
          base::BindRepeating(&VwBrowser::OnWindowsChanged,
                              base::Unretained(this)))),
      chrome_(std::move(chrome)) {}

VwBrowser::~VwBrowser() {
  // Tabs first: their WebContents' windows live in the platform's root.
  while (!tabs_.empty()) {
    tabs_.erase(tabs_.begin());
  }
  chrome_.reset();
  platform_.reset();
}

void VwBrowser::OnWindowsChanged() {
  if (chrome_) {
    chrome_->UpdateBackState();
  }
}

VwTab* VwBrowser::CreateTab(int32_t id,
                            const GURL& url,
                            bool select,
                            int32_t opener) {
  if (chrome_) {
    if (url.is_valid()) {
      chrome_->OpenUrl(url);
    }
    return nullptr;
  }
  content::WebContents::CreateParams params(context_);
  params.initially_hidden = !select;
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(params);
  VwTab* tab = AddTab(
      std::make_unique<VwTab>(this, id, opener, std::move(contents)), select);
  if (url.is_valid()) {
    tab->Navigate(url);
  }
  return tab;
}

VwTab* VwBrowser::AdoptContents(std::unique_ptr<content::WebContents> contents,
                                int32_t opener,
                                bool select) {
  return AddTab(std::make_unique<VwTab>(this, VwEngine::Get()->NextTabId(),
                                        opener, std::move(contents)),
                select);
}

VwTab* VwBrowser::AddTab(std::unique_ptr<VwTab> owned, bool select) {
  VwTab* tab = owned.get();
  const int32_t id = tab->id();
  tabs_[id] = std::move(owned);

  aura::Window* view = tab->view();
  platform_->root()->AddChild(view);
  view->SetBounds(gfx::Rect(platform_->root()->bounds().size()));
  view->Hide();
  NoteF("tab %d: created (opener %d)", id, tab->opener());

  venetium_w10m_events events = VwEngine::Get()->events();
  if (events.tab_opened) {
    events.tab_opened(id, tab->opener(), select ? 1 : 0);
  }
  if (select || !selected_) {
    SelectTab(id);
  }
  tab->ReportState(true);
  return tab;
}

void VwBrowser::CloseTab(int32_t id) {
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return;
  }
  const int32_t opener = it->second->opener();
  if (it->second->fullscreen()) {
    OnFullscreenChanged(it->second.get(), false);
  }
  std::unique_ptr<VwTab> tab = std::move(it->second);
  tabs_.erase(it);
  recent_.erase(std::remove(recent_.begin(), recent_.end(), id),
                recent_.end());
  NoteF("tab %d: closed", id);
  venetium_w10m_events events = VwEngine::Get()->events();
  if (events.tab_closed) {
    events.tab_closed(id);
  }
  if (selected_ == id) {
    selected_ = 0;
    // Back to the tab that opened this one, else the one seen before it.
    int32_t next = 0;
    if (opener && tabs_.count(opener)) {
      next = opener;
    } else if (!recent_.empty()) {
      next = recent_.back();
    } else if (!tabs_.empty()) {
      next = tabs_.begin()->first;
    }
    if (next) {
      SelectTab(next);
    } else {
      PublishSelection();
    }
  } else {
    // The selected tab may have been opened by this one: Back no longer
    // closes it.
    PublishSelection();
  }
  tab.reset();
}

void VwBrowser::CloseTabSoon(int32_t id) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&VwBrowser::CloseTab,
                                weak_factory_.GetWeakPtr(), id));
}

void VwBrowser::SelectTab(int32_t id) {
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return;
  }
  if (selected_ == id) {
    it->second->view()->Show();
    return;
  }
  if (VwTab* old = selected()) {
    if (old->fullscreen()) {
      old->ExitFullscreen();
    }
    old->view()->Hide();
    old->set_last_seen(base::TimeTicks::Now());
  }
  selected_ = id;
  recent_.erase(std::remove(recent_.begin(), recent_.end(), id),
                recent_.end());
  recent_.push_back(id);
  VwTab* tab = it->second.get();
  tab->view()->SetBounds(gfx::Rect(platform_->root()->bounds().size()));
  tab->view()->Show();
  // An unloaded tab loads its page again on being shown (WebContentsImpl
  // makes its NavigationController active); this is for a Show that did not
  // reach it, and does nothing otherwise.
  tab->contents()->GetController().LoadIfNecessary();
  tab->contents()->Focus();
  tab->SetDesktopMode(VwEngine::Get()->desktop_mode.load());
  PublishSelection();
  venetium_w10m_events events = VwEngine::Get()->events();
  if (events.tab_selected) {
    events.tab_selected(id);
  }
}

VwTab* VwBrowser::tab(int32_t id) {
  auto it = tabs_.find(id);
  return it == tabs_.end() ? nullptr : it->second.get();
}

VwTab* VwBrowser::selected() {
  return tab(selected_);
}

// static
GURL VwBrowser::ResolveText(const std::string& text) {
  const std::string trimmed(
      base::TrimWhitespaceASCII(text, base::TRIM_ALL));
  if (trimmed.empty()) {
    return GURL();
  }
  // Something with a scheme of its own: http(s), about:, data:, file:, ...
  const GURL direct(trimmed);
  if (direct.is_valid() && !direct.SchemeIs("javascript") &&
      (direct.SchemeIsHTTPOrHTTPS() || direct.SchemeIs("about") ||
       direct.SchemeIs("data") || direct.SchemeIs("file") ||
       direct.SchemeIs("blob") || trimmed.find("://") != std::string::npos)) {
    return direct;
  }
  // A host: no spaces, and a dot, a port or localhost in it.
  const bool one_word = trimmed.find(' ') == std::string::npos;
  if (one_word && (trimmed.find('.') != std::string::npos ||
                   trimmed.find(':') != std::string::npos ||
                   base::StartsWith(trimmed, "localhost"))) {
    GURL fixed = url_formatter::FixupURL(trimmed, std::string());
    if (fixed.is_valid() && fixed.SchemeIsHTTPOrHTTPS()) {
      return fixed;
    }
  }
  // Anything else is a search.
  std::string url_template = VwEngine::Get()->search_template();
  const std::string query = base::EscapeQueryParamValue(trimmed, true);
  const size_t at = url_template.find("%s");
  if (at == std::string::npos) {
    url_template += query;
  } else {
    url_template.replace(at, 2, query);
  }
  return GURL(url_template);
}

void VwBrowser::NavigateText(int32_t id, const std::string& text) {
  if (chrome_) {
    // Chrome has its own address bar; text from the shell opens a tab.
    CreateTab(0, ResolveText(text), true, 0);
    return;
  }
  VwTab* target = tab(id);
  if (!target) {
    target = selected();
  }
  const GURL url = ResolveText(text);
  if (!url.is_valid()) {
    return;
  }
  if (!target) {
    CreateTab(VwEngine::Get()->NextTabId(), url, true, 0);
    return;
  }
  target->Navigate(url);
}

void VwBrowser::BackButton() {
  if (chrome_) {
    chrome_->Back();
    return;
  }
  VwTab* tab = selected();
  if (!tab) {
    return;
  }
  if (tab->fullscreen()) {
    tab->ExitFullscreen();
  } else if (tab->CanGoBack()) {
    tab->GoBack();
  } else if (tab->opener() && this->tab(tab->opener())) {
    // A tab a page opened, with nowhere left to go back to: closing it
    // returns to the page that opened it.
    CloseTab(tab->id());
  }
}

void VwBrowser::Resize(const gfx::Size& content_pixels) {
  platform_->Resize(content_pixels);
  const gfx::Rect bounds(platform_->root()->bounds().size());
  for (auto& [id, tab] : tabs_) {
    tab->view()->SetBounds(bounds);
  }
}

void VwBrowser::SetDesktopMode(bool desktop) {
  VwEngine::Get()->desktop_mode.store(desktop);
  NoteF("desktop sites %s", desktop ? "on" : "off");
  // The tab on screen now; each of the others when it is next selected, so
  // none is reloaded or re-laid out unseen.
  if (VwTab* tab = selected()) {
    tab->SetDesktopMode(desktop);
  }
}

void VwBrowser::SetKeyboardInset(int32_t pixels) {
  platform_->SetKeyboardInset(pixels);
}

void VwBrowser::AppStateChanged(bool suspended) {
  platform_->SetActive(!suspended);
  if (!suspended) {
    NoteF("app resumed");
    if (chrome_) {
      chrome_->Resumed();
    }
    memory_.OnAppState(false);
    return;
  }
  // A suspended app is ended by its size, and is to give the graphics
  // driver's memory back: the compositor's textures go, and the device is
  // trimmed. As soon as viz's thread gets to it.
  ReleaseGraphicsMemory(/*trim=*/true);
  // The phone usually ends a suspended app without another word: cookies and
  // local storage are written out now.
  NoteF("app suspending: flushing storage");
  if (chrome_) {
    chrome_->Suspending();
  } else {
    content::StoragePartition* partition =
        context_->GetDefaultStoragePartition();
    partition->GetCookieManagerForBrowserProcess()->FlushCookieStore(
        base::DoNothing());
    partition->Flush();
  }
  memory_.OnAppState(true);
}

void VwBrowser::MemoryPressure(int32_t level) {
  if (level >= 2) {
    // The phone's own word that the app is at its limit: the compositor's
    // textures go at once, whatever the arbiter makes of the rest.
    ReleaseGraphicsMemory(/*trim=*/false);
  }
  memory_.OnPhoneLevel(level);
}

bool VwBrowser::TiedToSelected(VwTab* tab) {
  VwTab* front = selected();
  if (!front || front == tab) {
    return front == tab;
  }
  const auto opened_by = [](VwTab* child, VwTab* parent) {
    content::RenderFrameHost* opener = child->contents()->GetOpener();
    return opener && content::WebContents::FromRenderFrameHost(opener) ==
                         parent->contents();
  };
  return opened_by(front, tab) || opened_by(tab, front);
}

const char* VwBrowser::WhyKept(VwTab* tab, base::TimeDelta spare) {
  const char* why = nullptr;
  if (!tab->CanDiscard(&why)) {
    return why;
  }
  if (TiedToSelected(tab)) {
    // A sign-in window and the page waiting for its answer, say: unloading
    // either ends what the person is in the middle of.
    return "bound to the tab on screen as opener and opened";
  }
  if (base::TimeTicks::Now() - tab->last_seen() < spare) {
    return "only just left the screen";
  }
  return nullptr;
}

int VwBrowser::UnloadableTabs(base::TimeDelta spare) {
  if (chrome_) {
    return chrome_->DiscardableTabs(spare);
  }
  int count = 0;
  for (auto& [id, tab] : tabs_) {
    if (id != selected_ && !WhyKept(tab.get(), spare)) {
      ++count;
    }
  }
  return count;
}

int VwBrowser::UnloadBackgroundTabs(int limit,
                                    base::TimeDelta spare,
                                    const char* cause) {
  if (chrome_) {
    return chrome_->DiscardBackgroundTabs(limit, spare, cause);
  }
  std::vector<VwTab*> candidates;
  for (auto& [id, tab] : tabs_) {
    if (id != selected_ && !WhyKept(tab.get(), spare)) {
      candidates.push_back(tab.get());
    }
  }
  std::sort(candidates.begin(), candidates.end(), [](VwTab* a, VwTab* b) {
    return a->last_seen() < b->last_seen();
  });
  const base::TimeTicks now = base::TimeTicks::Now();
  int unloaded = 0;
  for (VwTab* tab : candidates) {
    if (unloaded >= limit) {
      break;
    }
    const int seconds = static_cast<int>((now - tab->last_seen()).InSeconds());
    tab->Discard();
    // As in AddTab: the tab's view is a new window.
    aura::Window* view = tab->view();
    platform_->root()->AddChild(view);
    view->SetBounds(gfx::Rect(platform_->root()->bounds().size()));
    view->Hide();
    NoteF("tab %d: unloaded (least recently used, %d s off screen; %s)",
          tab->id(), seconds, cause);
    tab->ReportState(true);
    ++unloaded;
  }
  return unloaded;
}

void VwBrowser::NoteKeptTabs(base::TimeDelta spare) {
  if (chrome_) {
    Note("memory: no background tab to discard");
    return;
  }
  std::string kept;
  int unloaded = 0;
  for (auto& [id, tab] : tabs_) {
    if (id == selected_) {
      continue;
    }
    if (tab->discarded()) {
      ++unloaded;
      continue;
    }
    if (const char* why = WhyKept(tab.get(), spare)) {
      kept += absl::StrFormat("; tab %d: %s", id, why);
    }
  }
  NoteF("memory: no background tab to unload (%d unloaded already)%s",
        unloaded, kept);
}

void VwBrowser::OnTabStateChanged(VwTab* tab) {
  if (tab->id() == selected_) {
    PublishSelection();
  }
}

void VwBrowser::OnFullscreenChanged(VwTab* tab, bool on) {
  if (tab->id() == selected_) {
    // Back leaves fullscreen first.
    PublishSelection();
  }
  venetium_w10m_events events = VwEngine::Get()->events();
  if (events.fullscreen) {
    events.fullscreen(on ? 1 : 0);
  }
}

void VwBrowser::PublishSelection() {
  VwEngine* engine = VwEngine::Get();
  VwTab* tab = selected();
  engine->selected_tab.store(tab ? tab->id() : 0);
  engine->selected_can_go_back.store(
      tab && (tab->fullscreen() || tab->CanGoBack() ||
              (tab->opener() && this->tab(tab->opener()))));
  engine->fullscreen.store(tab && tab->fullscreen());
}

}  // namespace venetium_w10m
