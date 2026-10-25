// FileExport.h -- downloads the user can find, as gecko-w10m's FileBridge.
//
// With interop unlock alone the engine (Win32, by path) may not write the
// phone's Downloads: Chrome downloads into LocalState\Downloads, the staging
// folder (VENETIUM_DOWNLOAD_STAGING, set by the engine), and each finished
// download is copied out here through the storage API -- into the file picked
// in a Save As, or into Downloads\Venetium (DownloadsFolder). Opening a
// download, or showing its folder, then goes to the copy. Staged files are
// cleared two days after they were copied out. Where the engine may write
// Downloads itself (root access) there is no staging and nothing to copy.
#pragma once

#include <functional>
#include <string>

namespace venetium_w10m::client {

// The staging folder, or empty when the engine downloads straight into the
// phone's Downloads.
std::wstring DownloadStaging();

// A download finished at |path|. Copies it out when it was staged, on a
// worker thread, and then calls |done| there with where it went (|path|
// itself when nothing had to be copied), or with an error.
void ExportDownload(std::wstring path,
                    std::function<void(std::wstring where,
                                       std::wstring error)> done);

// Opens the copy of the download staged at |path| -- or, with |reveal|, the
// folder it is in -- from the UI thread. False when |path| has no copy.
bool OpenDownloadCopy(std::wstring const& path, bool reveal);

// Save As: the path the engine is to write for |picked| (a StorageFile):
// the picked file's own when the engine may write it, else a staged file
// copied into the picked one when the download finishes.
std::wstring PathToSaveInto(winrt::Windows::Storage::StorageFile const& picked);

// Staged downloads copied out two days ago and more, and the folders Save As
// left behind. On a worker thread.
void CleanUpStagedDownloads();

}  // namespace venetium_w10m::client
