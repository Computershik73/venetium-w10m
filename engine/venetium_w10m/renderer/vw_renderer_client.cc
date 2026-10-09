// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/renderer/vw_renderer_client.h"

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "content/public/common/alternative_error_page_override_info.mojom.h"
#include "net/base/net_errors.h"
#include "third_party/blink/public/platform/web_url_error.h"
#include "url/gurl.h"

namespace venetium_w10m {

namespace {

// Plain, readable on a small screen, and in the two languages the shell
// speaks; the error's own name says what went wrong to anyone who asks.
constexpr char kErrorPage[] = R"(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%TITLE%</title>
<style>
body { font-family: "Segoe UI", sans-serif; margin: 24px; color: #222;
       background: #fafafa; }
h1 { font-size: 20px; font-weight: 600; margin: 32px 0 12px; }
p { font-size: 15px; line-height: 1.4; color: #555; word-break: break-all; }
code { font-size: 13px; color: #888; }
button { margin-top: 20px; font-size: 16px; padding: 10px 24px;
         border: 0; border-radius: 4px; background: #2b6ed2; color: #fff; }
@media (prefers-color-scheme: dark) {
  body { background: #1c1b22; color: #eee; } p { color: #bbb; } }
</style></head><body>
<h1>Страница недоступна · This page can't be reached</h1>
<p>%URL%</p>
<p><code>%ERROR%</code></p>
<button onclick="location.reload()">Обновить · Reload</button>
</body></html>)";

void Replace(std::string* text, std::string_view key, std::string_view value) {
  for (size_t at = text->find(key); at != std::string::npos;
       at = text->find(key, at + value.size())) {
    text->replace(at, key.size(), value);
  }
}

}  // namespace

VwContentRendererClient::VwContentRendererClient() = default;
VwContentRendererClient::~VwContentRendererClient() = default;

void VwContentRendererClient::PrepareErrorPage(
    content::RenderFrame* render_frame,
    const blink::WebURLError& error,
    const std::string& http_method,
    content::mojom::AlternativeErrorPageOverrideInfoPtr
        alternative_error_page_info,
    std::string* error_html) {
  if (!error_html) {
    return;
  }
  const GURL url = error.url();
  const std::string escaped_url =
      base::EscapeForHTML(url.possibly_invalid_spec());
  std::string page = kErrorPage;
  Replace(&page, "%TITLE%", base::EscapeForHTML(url.host()));
  Replace(&page, "%URL%", escaped_url);
  Replace(&page, "%ERROR%", net::ErrorToShortString(error.reason()));
  *error_html = std::move(page);
}

}  // namespace venetium_w10m
