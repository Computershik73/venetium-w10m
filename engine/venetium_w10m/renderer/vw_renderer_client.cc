// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/renderer/vw_renderer_client.h"

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "content/public/common/alternative_error_page_override_info.mojom.h"
#include "media/base/media_types.h"
#include "media/base/video_codecs.h"
#include "media/base/video_codecs_w10m.h"
#include "net/base/net_errors.h"
#include "third_party/blink/public/platform/web_url_error.h"
#include "url/gurl.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// Plain, readable on a small screen, and in the two languages the shell
// speaks; the error's own name says what went wrong to anyone who asks.
constexpr char kErrorPage[] = R"page(<!doctype html>
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
</body></html>)page";

void Replace(std::string* text, std::string_view key, std::string_view value) {
  for (size_t at = text->find(key); at != std::string::npos;
       at = text->find(key, at + value.size())) {
    text->replace(at, key.size(), value);
  }
}

}  // namespace

VwContentRendererClient::VwContentRendererClient() {
  // Made inside ContentMain, after the shell's switches are on the command
  // line: one line in the log says which way this run answers.
  Note(media::W10mEveryVideoCodec()
           ? "codecs: every video codec the build decodes (--vw-codecs=all)"
           : "codecs: pages are told H.264 and VP8; VP9, AV1 and HEVC are "
             "refused (--vw-codecs=all offers them again)");
}
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

// A site that is told VP9 or AV1 plays sends that: YouTube does. Both are
// decoded on the CPU here, four cores of a 32-bit process, and that is more
// than a phone decodes in time; H.264 is the codec these phones are built
// to play. Told that only H.264 and VP8 play, sites send H.264 (YouTube up
// to 1080p). This one
// answer is behind canPlayType and <source type>, MediaSource,
// MediaCapabilities, Encrypted Media, the HLS player's choice of variant,
// WebCodecs and the demuxer of a file played by its address: a WebM whose
// picture is VP9 is not played either, as in Safari before it had VP9. An
// allow-list, the same two codecs calls are held in (blink's
// video_codec_factory.cc), so HEVC stays refused whatever a driver comes to
// report. VP8 stays for old WebM files; audio is not asked about here.
// "--vw-codecs=all" among the shell's switches gives Chromium's answers
// back, for comparing on a device.
bool VwContentRendererClient::IsDecoderSupportedVideoType(
    const media::VideoType& type) {
  // Any thread: canPlayType on the main one, the demuxer on the media
  // thread, WebCodecs in a worker.
  if (!media::W10mEveryVideoCodec() &&
      type.codec != media::VideoCodec::kH264 &&
      type.codec != media::VideoCodec::kVP8) {
    return false;
  }
  // What is allowed still has to be decodable.
  return content::ContentRendererClient::IsDecoderSupportedVideoType(type);
}

}  // namespace venetium_w10m
