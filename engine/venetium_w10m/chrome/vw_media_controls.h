// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The phone's media controls -- on the lock screen and in the volume
// flyout -- for what a page plays: Chrome's SystemMediaControls, whose
// notifier follows the active media session (navigator.mediaSession's
// title, artist, album and artwork; the tab's title and the site when a
// page gives none) and asks for play, pause, next and previous. The shell
// owns the view's SystemMediaTransportControls: it hears the state
// (events.media_state) and sends the buttons back
// (venetium_w10m_media_button). With the backgroundMediaPlayback
// capability the app keeps running in the background while it plays.

#ifndef VENETIUM_W10M_CHROME_VW_MEDIA_CONTROLS_H_
#define VENETIUM_W10M_CHROME_VW_MEDIA_CONTROLS_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "components/system_media_controls/system_media_controls.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace venetium_w10m {

class VwMediaControls : public system_media_controls::SystemMediaControls {
 public:
  // Gives Chrome these controls (before the first media session).
  static void Install();
  // A button of the phone's controls (venetium_w10m_media_button), on the UI
  // thread: 1 play, 2 pause, 3 next, 4 previous, 5 stop, 6 play/pause.
  static void Press(int32_t button);

  VwMediaControls();
  VwMediaControls(const VwMediaControls&) = delete;
  VwMediaControls& operator=(const VwMediaControls&) = delete;
  ~VwMediaControls() override;

  // system_media_controls::SystemMediaControls:
  void AddObserver(
      system_media_controls::SystemMediaControlsObserver* observer) override;
  void RemoveObserver(
      system_media_controls::SystemMediaControlsObserver* observer) override;
  void SetEnabled(bool enabled) override;
  void SetIsNextEnabled(bool value) override;
  void SetIsPreviousEnabled(bool value) override;
  void SetIsPlayPauseEnabled(bool value) override;
  void SetIsStopEnabled(bool value) override;
  void SetPlaybackStatus(PlaybackStatus value) override;
  void SetTitle(const std::u16string& value) override;
  void SetArtist(const std::u16string& value) override;
  void SetAlbum(const std::u16string& value) override;
  void SetThumbnail(const SkBitmap& bitmap) override;
  void ClearThumbnail() override;
  void ClearMetadata() override;
  void UpdateDisplay() override;
  bool GetVisibilityForTesting() const override;

 private:
  void SetButton(int bit, bool value);
  void ForgetArtwork();
  void ArtworkWritten(int serial, base::FilePath path);
  // Sends the whole state to the shell.
  void Tell();

  base::ObserverList<system_media_controls::SystemMediaControlsObserver>
      observers_;
  bool enabled_ = false;
  // Bits: 1 play/pause, 2 next, 4 previous, 8 stop.
  int buttons_ = 0;
  PlaybackStatus status_ = PlaybackStatus::kStopped;
  std::u16string title_;
  std::u16string artist_;
  std::u16string album_;
  // The artwork, written as a PNG file for the shell.
  base::FilePath artwork_;
  int artwork_serial_ = 0;
  // What the log last said.
  int told_state_ = -1;
  std::string told_title_;
  base::WeakPtrFactory<VwMediaControls> weak_factory_{this};
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_MEDIA_CONTROLS_H_
