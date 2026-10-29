// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_media_controls.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "components/system_media_controls/system_media_controls_observer.h"
#include "ui/gfx/codec/png_codec.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// The one Chrome made (it makes one, for the browser's active session), for
// the shell's buttons.
VwMediaControls* g_controls = nullptr;

std::unique_ptr<system_media_controls::SystemMediaControls> Make() {
  return std::make_unique<VwMediaControls>();
}

// The artwork as a PNG file in the app's temporary folder; on a thread that
// may block. Two names in turn: the picture shown is never the file being
// written. An empty path if it could not be written.
base::FilePath WriteArtwork(SkBitmap bitmap, int serial) {
  base::FilePath dir;
  if (!base::GetTempDir(&dir)) {
    return base::FilePath();
  }
  const base::FilePath path = dir.AppendASCII(
      "media-artwork-" + base::NumberToString(serial % 2) + ".png");
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                        /*discard_transparency=*/false);
  if (!png || !base::WriteFile(path, *png)) {
    return base::FilePath();
  }
  return path;
}

}  // namespace

// static
void VwMediaControls::Install() {
  system_media_controls::SystemMediaControls::SetW10mFactory(&Make);
}

// static
void VwMediaControls::Press(int32_t button) {
  if (!g_controls) {
    return;
  }
  NoteF("media: the phone's button %d", button);
  for (auto& observer : g_controls->observers_) {
    switch (button) {
      case 1:
        observer.OnPlay(g_controls);
        break;
      case 2:
        observer.OnPause(g_controls);
        break;
      case 3:
        observer.OnNext(g_controls);
        break;
      case 4:
        observer.OnPrevious(g_controls);
        break;
      case 5:
        observer.OnStop(g_controls);
        break;
      case 6:
        observer.OnPlayPause(g_controls);
        break;
      default:
        break;
    }
  }
}

VwMediaControls::VwMediaControls() {
  g_controls = this;
  Note("media: the phone's media controls are Chrome's");
}

VwMediaControls::~VwMediaControls() {
  if (g_controls == this) {
    g_controls = nullptr;
  }
}

void VwMediaControls::AddObserver(
    system_media_controls::SystemMediaControlsObserver* observer) {
  observers_.AddObserver(observer);
  observer->OnServiceReady();
}

void VwMediaControls::RemoveObserver(
    system_media_controls::SystemMediaControlsObserver* observer) {
  observers_.RemoveObserver(observer);
}

void VwMediaControls::SetEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }
  enabled_ = enabled;
  Tell();
}

void VwMediaControls::SetIsNextEnabled(bool value) {
  SetButton(2, value);
}

void VwMediaControls::SetIsPreviousEnabled(bool value) {
  SetButton(4, value);
}

void VwMediaControls::SetIsPlayPauseEnabled(bool value) {
  SetButton(1, value);
}

void VwMediaControls::SetIsStopEnabled(bool value) {
  SetButton(8, value);
}

void VwMediaControls::SetButton(int bit, bool value) {
  const int buttons = value ? (buttons_ | bit) : (buttons_ & ~bit);
  if (buttons == buttons_) {
    return;
  }
  buttons_ = buttons;
  // Said at once, as Windows' own controls are: the actions of a session
  // change without a display update.
  Tell();
}

void VwMediaControls::SetPlaybackStatus(PlaybackStatus value) {
  // Said at once too: the notifier sends no UpdateDisplay for a state alone.
  if (status_ == value) {
    return;
  }
  status_ = value;
  Tell();
}

void VwMediaControls::SetTitle(const std::u16string& value) {
  title_ = value;
}

void VwMediaControls::SetArtist(const std::u16string& value) {
  artist_ = value;
}

void VwMediaControls::SetAlbum(const std::u16string& value) {
  album_ = value;
}

void VwMediaControls::SetThumbnail(const SkBitmap& bitmap) {
  if (bitmap.drawsNothing()) {
    ClearThumbnail();
    return;
  }
  const int serial = ++artwork_serial_;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&WriteArtwork, bitmap, serial),
      base::BindOnce(&VwMediaControls::ArtworkWritten,
                     weak_factory_.GetWeakPtr(), serial));
}

void VwMediaControls::ArtworkWritten(int serial, base::FilePath path) {
  if (serial != artwork_serial_) {
    // A newer picture is on its way, or the picture was cleared.
    return;
  }
  artwork_ = path;
  Tell();
}

void VwMediaControls::ClearThumbnail() {
  ForgetArtwork();
  Tell();
}

void VwMediaControls::ForgetArtwork() {
  ++artwork_serial_;
  artwork_.clear();
}

void VwMediaControls::ClearMetadata() {
  title_.clear();
  artist_.clear();
  album_.clear();
  ForgetArtwork();
  // As Windows' own controls: nothing to show, so they are hidden (the
  // next display update shows them).
  enabled_ = false;
  Tell();
}

void VwMediaControls::UpdateDisplay() {
  enabled_ = true;
  Tell();
}

bool VwMediaControls::GetVisibilityForTesting() const {
  return enabled_;
}

void VwMediaControls::Tell() {
  int32_t state = 0;
  if (enabled_) {
    switch (status_) {
      case PlaybackStatus::kPlaying:
        state = 1;
        break;
      case PlaybackStatus::kPaused:
        state = 2;
        break;
      case PlaybackStatus::kStopped:
        state = 3;
        break;
    }
  }
  const std::string title = base::UTF16ToUTF8(title_);
  const std::string artist = base::UTF16ToUTF8(artist_);
  const std::string album = base::UTF16ToUTF8(album_);
  const std::string artwork = artwork_.AsUTF8Unsafe();
  if (state != told_state_ || title != told_title_) {
    told_state_ = state;
    told_title_ = title;
    NoteF("media: %s%s%s", state == 0   ? "hidden"
                           : state == 1 ? "playing"
                           : state == 2 ? "paused"
                                        : "stopped",
          title.empty() ? "" : ", ", title);
  }
  const venetium_w10m_events events = VwEngine::Get()->events();
  if (events.media_state) {
    events.media_state(state, title.c_str(), artist.c_str(), album.c_str(),
                       artwork.c_str(), buttons_);
  }
}

}  // namespace venetium_w10m
