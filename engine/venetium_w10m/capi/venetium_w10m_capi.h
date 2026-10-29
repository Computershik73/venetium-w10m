// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// venetium_w10m_capi.h -- the flat C boundary between the Chromium engine
// (venetium_engine.dll, built by clang-cl inside the Chromium tree) and the
// UWP shell (VenetiumW10m.exe, built by cl.exe with C++/WinRT).
//
// Why a C boundary: the two halves are built by different compilers with
// different exception models (clang-cl, no C++ EH on ARM32 Windows; cl.exe,
// /EHsc because C++/WinRT needs it), and the shell has to keep working when
// the engine is missing or fails to load. The shell resolves every entry
// point with GetProcAddress after LoadPackagedLibrary, exactly as the
// gecko-w10m shell does with xul.dll.
//
// Threading: venetium_w10m_run() owns the thread it is called on (it becomes
// Chromium's browser UI thread) and returns only at shutdown. Every other
// function may be called from any thread -- normally the XAML UI thread --
// and posts its work to the engine; none of them blocks on the engine.
// Callbacks in venetium_w10m_events arrive on engine threads, never on the
// XAML thread: the shell dispatches them itself.
//
// Coordinates are device (physical) pixels of the content area, origin at
// its top-left corner. Strings are UTF-8 unless a parameter says UTF-16.

#ifndef VENETIUM_W10M_CAPI_H_
#define VENETIUM_W10M_CAPI_H_

#include <stdint.h>
#include <wchar.h>

#if defined(VENETIUM_W10M_ENGINE_IMPLEMENTATION)
#define VW_EXPORT __declspec(dllexport)
#else
#define VW_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---- Logging ---------------------------------------------------------------
// Where the engine's diagnostic lines go (plain ASCII/UTF-8, no newline).
// Set before venetium_w10m_run; lines before that are dropped.
typedef void (*venetium_w10m_log_fn)(const char* line);
VW_EXPORT void venetium_w10m_set_logger(venetium_w10m_log_fn fn);

// ---- Lifecycle -------------------------------------------------------------
typedef struct {
  const wchar_t* install_dir;  // package install folder: paks, icudtl.dat
  const wchar_t* profile_dir;  // writable: user data (LocalState\profile)
  const wchar_t* cache_dir;    // writable: disk cache (LocalCache)
  const wchar_t* temp_dir;     // writable: temp files (TempState)
  int32_t width;               // content area, device pixels
  int32_t height;
  double scale;                // device pixels per DIP (e.g. 2.0, 2.4)
  int32_t jit;                 // 1: V8 JIT through the *FromApp allocators
                               // (needs rescap:codeGeneration); 0: jitless
  const char* switches;        // extra Chromium switches, space separated,
                               // e.g. "--enable-logging --v=1"; may be NULL
  const char* initial_url;     // first tab; NULL or "" for about:blank
} venetium_w10m_config;

// Runs the engine on the calling thread until it shuts down. Returns the
// content::ContentMain exit code, or a negative value if it never got there.
VW_EXPORT int32_t venetium_w10m_run(const venetium_w10m_config* config);

// Asks the engine to shut down; venetium_w10m_run returns afterwards.
VW_EXPORT void venetium_w10m_quit(void);

// The app is being suspended (1) or resumed (0). Suspension flushes cookies,
// the HTTP cache index and session state, because a phone usually ends a
// suspended app without telling it.
VW_EXPORT void venetium_w10m_app_state(int32_t suspended);

// The phone says memory is low (1: moderate, 2: critical): what
// Windows.System.MemoryManager reports of the app's use against its limit,
// passed on at once. The engine weighs it with what memory_status last told
// it and decides itself what to let go of, and how often.
VW_EXPORT void venetium_w10m_memory_pressure(int32_t level);

// What the shell measured, every couple of seconds, in megabytes: the
// largest free piece of the process's address space and all of it that is
// free, the app's memory use as the phone counts it, and the limit the phone
// gives the app (0: not known). A 32-bit process runs out of address space
// long before it reaches the phone's limit, so it is by these numbers that
// the engine trims its caches and unloads the pages of background tabs
// (browser/vw_memory.h). Any thread.
VW_EXPORT void venetium_w10m_memory_status(int32_t largest_free_mb,
                                           int32_t free_mb, int32_t app_mb,
                                           int32_t limit_mb);

// ---- Frames (software path) -------------------------------------------------
// The newest composited frame of the visible tab, BGRA premultiplied, rows
// top-down, stride width*4 -- the layout of a XAML WriteableBitmap.
//
// In:  *width, *height  the shape of the shell's bitmap
//      *serial          the serial of the frame the shell last showed
// Out: returns 1 and copies when the frame is newer than *serial and has the
//      shell's shape (*serial is updated); returns 0 otherwise, with *width
//      and *height set to the engine's frame size. dest == NULL only asks for
//      the size.
VW_EXPORT int32_t venetium_w10m_frame_copy(void* dest, int32_t capacity,
                                           int32_t* width, int32_t* height,
                                           uint64_t* serial);

// ---- Geometry ---------------------------------------------------------------
// The content area changed size (device pixels).
VW_EXPORT void venetium_w10m_resize(int32_t width, int32_t height);
// The whole display (device pixels); what screen.width/height report.
VW_EXPORT void venetium_w10m_set_screen(int32_t width, int32_t height);
// Raw display density, for touch slop and the like.
VW_EXPORT void venetium_w10m_set_dpi(int32_t dpi);
// Device pixels per DIP changed (rotation to a display with another scale).
VW_EXPORT void venetium_w10m_set_scale(double scale);
// The shell's XAML SwapChainPanel (an IInspectable*), from the UI thread
// before venetium_w10m_run: the engine presents into it when it can. Kept
// for the whole run.
VW_EXPORT void venetium_w10m_set_panel(void* swap_chain_panel);
// How frames reach the screen: 0 through frame_copy (the shell shows them),
// 1 software frames presented by Direct3D 11 into the panel, 2 ANGLE, 3 the
// Direct3D 11 compositor. Any thread; may change during the run.
VW_EXPORT int32_t venetium_w10m_presenting(void);
// Frames presented into the panel so far.
VW_EXPORT uint64_t venetium_w10m_presented_frames(void);

// Bottom inset the on-screen keyboard covers, in device pixels (0: hidden).
// The shell keeps the content area its full size while the keyboard is up;
// the page is inset by this much and scrolls the focused field into view.
VW_EXPORT void venetium_w10m_set_keyboard_inset(int32_t pixels);

// ---- Input ------------------------------------------------------------------
// One finger: state 0 down, 1 move, 2 up, 3 cancel. time_qpc is
// QueryPerformanceCounter when the shell got the event (0: now), so moves
// that queue up keep their spacing.
VW_EXPORT void venetium_w10m_input_touch(int32_t pointer_id, int32_t state,
                                         int32_t x, int32_t y,
                                         int64_t time_qpc);
// Mouse, as WM_* numbers: 0x200 move, 0x201 left down, 0x202 left up,
// 0x204/0x205 right, 0x207/0x208 middle.
VW_EXPORT void venetium_w10m_input_mouse(int32_t message, int32_t x, int32_t y);
// Wheel or precision pan, in device pixels, signed as the wheel messages are:
// dy > 0 scrolls up (the wheel turned away from the user), dx > 0 right.
VW_EXPORT void venetium_w10m_input_wheel(int32_t x, int32_t y, double dx,
                                         double dy);
// A key with no text: Windows virtual-key code, down (1) or up (0), and the
// modifier bits 1 shift, 2 control, 4 alt.
VW_EXPORT void venetium_w10m_input_key(int32_t vkey, int32_t down,
                                       uint32_t modifiers);
// Text typed or committed by the on-screen keyboard (UTF-16).
VW_EXPORT void venetium_w10m_input_text(const uint16_t* text, int32_t length);
// IME composition in progress (UTF-16); length 0 clears it.
VW_EXPORT void venetium_w10m_input_composition(const uint16_t* text,
                                               int32_t length);

// Whether the focused element takes text (1) or not (0).
VW_EXPORT int32_t venetium_w10m_text_input_wanted(void);
// The focused field: bits 0-6 its kind (0 none, 1 text, 2 password, 3 search,
// 4 email, 5 number, 6 telephone, 7 url, 8 textarea/contenteditable); bit 7
// set while the page wants the on-screen keyboard (the user tapped a field;
// a page focusing one by itself does not ask); the upper 24 bits a serial
// that changes on every such request, also for a field already focused.
VW_EXPORT uint32_t venetium_w10m_text_input_state(void);

// ---- Tabs and navigation ----------------------------------------------------
// Tabs are numbered by the engine (ids > 0, never reused within a run).
// Exactly one tab is visible; its frames are what frame_copy returns.
VW_EXPORT int32_t venetium_w10m_tab_create(const char* url, int32_t select);
VW_EXPORT void venetium_w10m_tab_close(int32_t tab);
VW_EXPORT void venetium_w10m_tab_select(int32_t tab);
// Loads text typed in an address bar: a URL, or anything else goes to the
// search engine template set by venetium_w10m_set_search_url.
VW_EXPORT void venetium_w10m_tab_navigate(int32_t tab, const char* text);
VW_EXPORT void venetium_w10m_tab_back(int32_t tab);
VW_EXPORT void venetium_w10m_tab_forward(int32_t tab);
VW_EXPORT void venetium_w10m_tab_reload(int32_t tab);
VW_EXPORT void venetium_w10m_tab_stop(int32_t tab);
// The phone's back button: 1 if the engine used it (closed a popup, left
// fullscreen, went back), 0 if the shell should handle it (leave the app).
VW_EXPORT int32_t venetium_w10m_back_button(void);
// Opens a URL from outside (protocol activation, "open with"): a new tab,
// selected. Returns 1 once the engine has taken it, 0 while it is starting.
VW_EXPORT int32_t venetium_w10m_open_url(const char* url);
// "%s" in the template is replaced by the escaped query.
VW_EXPORT void venetium_w10m_set_search_url(const char* url_template);
// Desktop (1) or mobile (0) user agent for new navigations of every tab.
VW_EXPORT void venetium_w10m_set_desktop_mode(int32_t desktop);

// ---- Interface ---------------------------------------------------------------
// Who draws the browser's interface in a run with these engine switches and
// this profile directory: 1 Chrome's own, which the engine draws into the
// panel with everything else -- tabs, address bar, menus, dialogs -- while
// the shell is its host (the panel, touch, the keyboard, the back button,
// the status bar); 0 the shell's address bar, tabs and menus over the
// engine's pages. Any thread, before venetium_w10m_run, whose run answers
// the same for the same switches and profile.
VW_EXPORT int32_t venetium_w10m_interface(const char* switches,
                                          const wchar_t* profile_dir);

// ---- Events (engine -> shell) ----------------------------------------------
typedef struct {
  // A tab's state changed. Every field is current; the shell compares.
  // progress is 0..1, or -1 for a background tab whose page was unloaded to
  // give its memory back: the tab keeps its URL, its title and its history,
  // and loads the page again when it is selected.
  void (*tab_state)(int32_t tab, const char* url, const char* title,
                    float progress, int32_t loading, int32_t can_back,
                    int32_t can_forward, int32_t secure);
  // The engine opened a tab (window.open, target=_blank). opener may be 0.
  void (*tab_opened)(int32_t tab, int32_t opener, int32_t selected);
  // A tab is gone (window.close, or a renderer that died for good).
  void (*tab_closed)(int32_t tab);
  // The visible tab changed (the engine selected one itself).
  void (*tab_selected)(int32_t tab);
  // A page entered (1) or left (0) fullscreen.
  void (*fullscreen)(int32_t on);
  // A URI the engine cannot load itself: mailto:, tel:, ms-settings: ...
  void (*launch_uri)(const char* uri);
  // A download finished: full path of the file, its MIME type.
  void (*download_done)(const char* path, const char* mime);
  // A JavaScript dialog: kind 0 alert, 1 confirm, 2 prompt, 3 beforeunload.
  // The shell answers with venetium_w10m_dialog_reply(id, ...).
  void (*dialog)(int32_t id, int32_t kind, const char* message,
                 const char* default_text);
  // A page asks for a permission: kind 1 camera, 2 microphone, 3 location,
  // 4 notifications. Answer with venetium_w10m_permission_reply.
  void (*permission)(int32_t id, int32_t kind, const char* origin);
  // <input type=file>: accept is the accept attribute, multiple 0/1, mode 0
  // open, 1 save (Chrome's Save As: accept is then the file name offered),
  // 2 a folder (Chrome's download location: the shell remembers it as where
  // downloads go and answers with its path). Answer with
  // venetium_w10m_file_reply.
  void (*file_chooser)(int32_t id, const char* accept, int32_t multiple,
                       int32_t mode);
  // Something was copied to the clipboard by a page (UTF-8 text).
  void (*clipboard_set)(const char* text);
  // Chrome's menu asked for something the shell does: 1 About, 2 the log,
  // 3 the engine switches, 4 copy the logs to Pictures; 5/6 the JIT on/off
  // for the next start; 7/8 the black theme on/off (the shell's own colours
  // and --vw-amoled from then on); 9/10 the toolbar slid away/back as a
  // page scrolled (the status bar with it).
  void (*shell_command)(int32_t command);
  // Chrome asks for a file to be opened (folder 0: a download, with the
  // app that takes it), a folder to be shown (folder 1), or the folder a file
  // is in (folder 2: "show in folder", the file's path). UTF-8 path.
  void (*open_path)(const char* path, int32_t folder);
  // A download started: the name of its file (UTF-8).
  void (*download_started)(const char* name);
  // An installed app's menu asks for a tile on Start: its id (for
  // venetium_w10m_launch_app), its name and its icon as a PNG file (empty
  // when it has none). UTF-8.
  void (*pin_app)(const char* app_id, const char* name, const char* icon);
  // How many downloads are going (a file's or an extension's), each time
  // the number changes: while there are some, the shell asks Windows to
  // keep the app running when it is put in the background, which would
  // otherwise suspend it and stop them.
  void (*downloads_active)(int32_t count);
  // What a page plays, for the phone's media controls (the lock screen's,
  // the volume flyout's): state 0 nothing (the controls hidden), 1 playing,
  // 2 paused, 3 stopped; the title (the tab's title when the page gives
  // none), the artist (the site when it gives none), the album, the artwork
  // as a PNG file (empty when there is none) and the buttons that work: bits
  // 1 play/pause, 2 next, 4 previous, 8 stop. UTF-8. Each time any of it
  // changes. The buttons come back with venetium_w10m_media_button.
  void (*media_state)(int32_t state, const char* title, const char* artist,
                      const char* album, const char* artwork,
                      int32_t buttons);
  // A site may use the camera (kind 1), the microphone (2) or the location
  // (3), and the phone's own consent for the app (Settings > Privacy) is
  // still to be given: the shell asks for it -- or, when it was turned off,
  // opens the setting -- and answers with venetium_w10m_device_access_reply.
  void (*device_access)(int32_t kind);
} venetium_w10m_events;

VW_EXPORT void venetium_w10m_set_event_sink(const venetium_w10m_events* sink);

// Opens an installed app (pin_app's id) in its window: its tile on Start was
// tapped. May come before the browser has started.
VW_EXPORT void venetium_w10m_launch_app(const char* app_id);

VW_EXPORT void venetium_w10m_dialog_reply(int32_t id, int32_t accepted,
                                          const char* text);
VW_EXPORT void venetium_w10m_permission_reply(int32_t id, int32_t granted);
// paths: UTF-8, separated by '\n'; NULL or "" cancels.
VW_EXPORT void venetium_w10m_file_reply(int32_t id, const char* paths);
// The phone's clipboard, for a paste (UTF-8).
VW_EXPORT void venetium_w10m_clipboard_phone_text(const char* text);
// A button of the phone's media controls was pressed: 1 play, 2 pause,
// 3 next, 4 previous, 5 stop, 6 play/pause.
VW_EXPORT void venetium_w10m_media_button(int32_t button);
// The phone's consent for a device_access kind: allowed 1 or 0.
VW_EXPORT void venetium_w10m_device_access_reply(int32_t kind,
                                                 int32_t allowed);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VENETIUM_W10M_CAPI_H_
