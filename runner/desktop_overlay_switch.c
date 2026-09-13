/* Nintendo Switch overlay stub for DKC3Recomp.
 *
 * The desktop overlay is Dear ImGui on an OpenGL 3 context
 * (desktop_overlay.cpp), which has no Switch backend here, and the Switch
 * build runs launcher-free by design ("no intermediary"). Every
 * Dkc3DesktopOverlay* entry point is therefore a NULL-tolerant no-op that
 * keeps the game loop, settings flow, and presenter calls in sdl_main.c
 * compiling unchanged: the overlay is never open, takes no actions, and
 * draws nothing.
 *
 * Only compiled on __SWITCH__; desktop builds keep desktop_overlay.cpp.
 */
#ifdef __SWITCH__

#include "desktop_overlay.h"

#include <stddef.h>
#include <string.h>

/* The host applies overlay settings every frame (ApplyOverlaySettings in
 * sdl_main.c), so GetSettings must return coherent values, not stack
 * garbage: a trashed player_src is exactly how "controls not present"
 * happens. The stub round-trips the live settings through a snapshot. */
static RecompLauncherCSettings s_settings;
static bool s_have_settings;

Dkc3DesktopOverlay *Dkc3DesktopOverlayCreate(
    const RecompLauncherCSettings *settings) {
  if (settings) {
    s_settings = *settings;
    s_have_settings = true;
  }
  /* Non-NULL dummy: sdl_main.c treats NULL as a fatal init failure.
   * The overlay is still never open (IsOpen is always false) and draws
   * nothing. */
  static int s_dummy;
  return (Dkc3DesktopOverlay *)&s_dummy;
}

bool Dkc3DesktopOverlayInitSdl(Dkc3DesktopOverlay *overlay, void *window,
                               void *gl_context) {
  (void)overlay;
  (void)window;
  (void)gl_context;
  /* Must succeed: sdl_main.c treats false as fatal. */
  return true;
}

bool Dkc3DesktopOverlayInitWin32(Dkc3DesktopOverlay *overlay, void *window) {
  (void)overlay;
  (void)window;
  return true;
}

void Dkc3DesktopOverlayDestroy(Dkc3DesktopOverlay *overlay) {
  (void)overlay;
}

bool Dkc3DesktopOverlayProcessSdlEvent(Dkc3DesktopOverlay *overlay,
                                       const void *event) {
  (void)overlay;
  (void)event;
  return false;
}

bool Dkc3DesktopOverlayProcessWin32Message(Dkc3DesktopOverlay *overlay,
                                           void *window, unsigned message,
                                           uintptr_t wparam,
                                           intptr_t lparam) {
  (void)overlay;
  (void)window;
  (void)message;
  (void)wparam;
  (void)lparam;
  return false;
}

void Dkc3DesktopOverlaySetGamepad(Dkc3DesktopOverlay *overlay,
                                   const Dkc3GamepadState *gamepad) {
  (void)overlay;
  (void)gamepad;
}

void Dkc3DesktopOverlaySetHapticsDevice(Dkc3DesktopOverlay *overlay,
                                        const char *name,
                                        bool rumble_supported) {
  (void)overlay;
  (void)name;
  (void)rumble_supported;
}

void Dkc3DesktopOverlayToggle(Dkc3DesktopOverlay *overlay) {
  (void)overlay;
}

bool Dkc3DesktopOverlayIsOpen(const Dkc3DesktopOverlay *overlay) {
  (void)overlay;
  return false;
}

bool Dkc3DesktopOverlayAssistTools(const Dkc3DesktopOverlay *overlay) {
  (void)overlay;
  return false;
}

int Dkc3DesktopOverlaySelectedSlot(const Dkc3DesktopOverlay *overlay) {
  (void)overlay;
  return 0;
}

void Dkc3DesktopOverlayGetSettings(const Dkc3DesktopOverlay *overlay,
                                   RecompLauncherCSettings *settings) {
  (void)overlay;
  if (settings && s_have_settings) *settings = s_settings;
}

void Dkc3DesktopOverlaySetSettings(Dkc3DesktopOverlay *overlay,
                                   const RecompLauncherCSettings *settings) {
  (void)overlay;
  if (settings) {
    s_settings = *settings;
    s_have_settings = true;
  }
}

uint32_t Dkc3DesktopOverlayTakeActions(Dkc3DesktopOverlay *overlay) {
  (void)overlay;
  return 0;
}

void Dkc3DesktopOverlaySetStatus(Dkc3DesktopOverlay *overlay,
                                 const char *status, bool success) {
  (void)overlay;
  (void)status;
  (void)success;
}

void Dkc3DesktopOverlayRenderOpenGl(void *overlay, int width, int height) {
  (void)overlay;
  (void)width;
  (void)height;
}

#endif /* __SWITCH__ */
