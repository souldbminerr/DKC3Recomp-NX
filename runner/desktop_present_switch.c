/* Nintendo Switch presenter for DKC3Recomp: same Dkc3SdlPresenter API as
 * desktop_present_sdl.c, backed by SDL_Renderer instead of a raw desktop
 * OpenGL context.
 *
 * On Switch portlibs SDL2, SDL_RENDERER_ACCELERATED is a GLES2 renderer,
 * so this IS the OpenGL path here (SDL_WINDOW_OPENGL + raw GL contexts
 * are not exposed by the Switch SDL video driver, while the renderer
 * path matches the devkitPro sdl2-simple/sdl2-demo examples exactly).
 * Only compiled on __SWITCH__; desktop builds keep desktop_present_sdl.c.
 *
 * Docked 1920x1080 / handheld 1280x720 window (decided at boot, rechecked
 * while presenting); the SNES frame is letterboxed through the
 * logical-size scaler. Reconstruct upscaling is unavailable (no GLSL
 * compiler hooks here) and falls back to the sampler implied by
 * linear_filter, reported through Backend()/shader_error like desktop.
 */
#ifdef __SWITCH__

#include "desktop_present_sdl.h"
#include "desktop_launcher.h"

#include <SDL.h>

#include <stdio.h>
#include <string.h>

#include <switch.h>

enum {
  kSwitchWindowWidth = 1280,
  kSwitchWindowHeight = 720,
};

/* The shared presenter struct carries a GLuint `texture` for desktop GL;
 * a pointer does not fit an AArch64 address into 32 bits, so the Switch
 * texture lives here instead. There is exactly one presenter (SdlHost). */
static SDL_Texture *s_texture;
static int s_texture_width;
static int s_texture_height;

static SDL_Window *SwitchWindow(Dkc3SdlPresenter *presenter) {
  return (SDL_Window *)presenter->window;
}

static SDL_Renderer *SwitchRenderer(Dkc3SdlPresenter *presenter) {
  return (SDL_Renderer *)presenter->gl_context;
}

static SDL_Texture *SwitchTexture(Dkc3SdlPresenter *presenter) {
  (void)presenter;
  return s_texture;
}

static void SwitchDropTexture(Dkc3SdlPresenter *presenter) {
  (void)presenter;
  if (s_texture) SDL_DestroyTexture(s_texture);
  s_texture = NULL;
  s_texture_width = 0;
  s_texture_height = 0;
}

static bool SwitchEnsureTexture(Dkc3SdlPresenter *presenter, int width,
                                int height) {
  if (width <= 0 || height <= 0) return false;
  if (s_texture && s_texture_width == width && s_texture_height == height)
    return true;
  SwitchDropTexture(presenter);
  SDL_Texture *texture = SDL_CreateTexture(
      SwitchRenderer(presenter), SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!texture) return false;
  /* SNES frames are opaque; never blend them with the clear color. */
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
  s_texture = texture;
  s_texture_width = width;
  s_texture_height = height;
  return true;
}

bool Dkc3SdlPresenterInit(Dkc3SdlPresenter *presenter, int window_scale,
                          int fullscreen, bool hidden, bool linear_filter,
                          int source_width, int source_height,
                          char *error, size_t error_capacity) {
  (void)window_scale;
  (void)fullscreen;
  (void)hidden;
  if (!presenter) return false;
  memset(presenter, 0, sizeof *presenter);
  presenter->linear_filter = linear_filter;
  presenter->upscaler =
      linear_filter ? kDkc3UpscalerBilinear : kDkc3UpscalerNearest;

  SDL_Window *window;
  /* Docked = 1080p, handheld = 720p: the only two modes Switch SDL2
   * exposes (SDL_SetWindowSize switches between them). */
  int switch_w = 1280, switch_h = 720;
  if (appletGetOperationMode() == AppletOperationMode_Console) {
    switch_w = 1920;
    switch_h = 1080;
  }
  window = SDL_CreateWindow(DKC3_PRODUCT_TITLE, 0, 0,
                            switch_w, switch_h, 0);
  if (!window) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "SDL_CreateWindow: %s",
               SDL_GetError());
    return false;
  }
  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    /* Unaccelerated fallback still runs; pacing degrades, playability
     * does not. */
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!renderer) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "SDL_CreateRenderer: %s",
               SDL_GetError());
    SDL_DestroyWindow(window);
    return false;
  }
  presenter->window = window;
  presenter->gl_context = renderer;
  presenter->software_paced = false;
  presenter->vsync_status = kDkc3DesktopVsyncEnabled;
  snprintf(presenter->backend, sizeof presenter->backend, "sdl2-gles2");
  if (!SwitchEnsureTexture(presenter, source_width, source_height)) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "SDL_CreateTexture: %s",
               SDL_GetError());
    Dkc3SdlPresenterDestroy(presenter);
    return false;
  }
  /* Nearest vs linear maps onto the renderer's global scale-quality
   * hint on SDL2 (per-texture scale modes are SDL3-only). */
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
              linear_filter ? "1" : "0");
  return true;
}

bool Dkc3SdlPresenterPresent(Dkc3SdlPresenter *presenter,
                             const uint8_t *pixels, int source_width,
                             int source_height,
                             Dkc3SdlOverlayDraw overlay_draw,
                             void *overlay_user) {
  if (!presenter || !pixels) return false;
  SDL_Renderer *renderer = SwitchRenderer(presenter);
  if (!renderer) return false;
  /* Dock/handheld transitions mid-session: resize to the matching
   * mode (checked ~5x/sec; SDL_SetWindowSize switches the output). */
  {
    static unsigned s_tick;
    if (++s_tick >= 12) {
      int want_w = 1280, want_h = 720;
      int cur_w = 0, cur_h = 0;
      s_tick = 0;
      if (appletGetOperationMode() == AppletOperationMode_Console) {
        want_w = 1920;
        want_h = 1080;
      }
      SDL_GetWindowSize(SwitchWindow(presenter), &cur_w, &cur_h);
      if (cur_w != want_w || cur_h != want_h)
        SDL_SetWindowSize(SwitchWindow(presenter), want_w, want_h);
    }
  }
  if (!SwitchEnsureTexture(presenter, source_width, source_height))
    return false;
  SDL_Texture *texture = SwitchTexture(presenter);
  if (SDL_UpdateTexture(texture, NULL, pixels, source_width * 4) != 0)
    return false;
  /* Stretch to fill the whole drawable: no logical-size letterbox, no
   * pillarbox. SNES pixels are 7:6 non-square, so a 256-column frame
   * stretches ~33% and a 342-column 16:9 frame ~16% -- the handheld
   * tradeoff for edge-to-edge picture. */
  SDL_RenderSetLogicalSize(renderer, 0, 0);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  if (SDL_RenderCopy(renderer, texture, NULL, NULL) != 0) return false;
  if (overlay_draw && overlay_user)
    overlay_draw(overlay_user, source_width, source_height);
  SDL_RenderPresent(renderer);
  if (presenter->capture_rgb && presenter->capture_width > 0 &&
      presenter->capture_height > 0) {
    /* Best-effort one-shot readback for DKC3_DESKTOP_SCREENSHOT runs. */
    int out_w = 0, out_h = 0;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    if (out_w == presenter->capture_width &&
        out_h == presenter->capture_height &&
        SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB24,
                             presenter->capture_rgb,
                             presenter->capture_width * 3) == 0)
      presenter->capture_done = true;
    presenter->capture_rgb = NULL;
  }
  return true;
}

void Dkc3SdlPresenterSetTitle(Dkc3SdlPresenter *presenter,
                              const char *title) {
  if (presenter && presenter->window && title)
    SDL_SetWindowTitle(SwitchWindow(presenter), title);
}

int Dkc3SdlPresenterSetUpscaler(Dkc3SdlPresenter *presenter, int upscaler,
                                int mode, float strength, float softness,
                                float shading) {
  (void)mode;
  (void)strength;
  (void)softness;
  (void)shading;
  if (!presenter) return kDkc3UpscalerNearest;
  if (upscaler == kDkc3UpscalerReconstruct) {
    /* No GLSL hooks on the renderer path: fall back to the sampler and
     * say so, exactly like the desktop presenter does when its shader
     * fails to build. */
    snprintf(presenter->shader_error, sizeof presenter->shader_error,
             "reconstruct upscaler unavailable on Switch; using %s",
             presenter->linear_filter ? "bilinear" : "nearest");
    upscaler = presenter->linear_filter ? kDkc3UpscalerBilinear
                                        : kDkc3UpscalerNearest;
  } else {
    presenter->shader_error[0] = '\0';
  }
  presenter->upscaler = upscaler;
  presenter->reconstruct_mode = mode;
  presenter->reconstruct_strength = strength;
  presenter->reconstruct_softness = softness;
  presenter->reconstruct_shading = shading;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
              upscaler == kDkc3UpscalerBilinear ? "1" : "0");
  return upscaler;
}

const char *Dkc3SdlPresenterUpscalerName(int upscaler) {
  switch (upscaler) {
    case kDkc3UpscalerNearest: return "nearest";
    case kDkc3UpscalerBilinear: return "bilinear";
    case kDkc3UpscalerReconstruct: return "reconstruct";
    default: return "unknown";
  }
}

bool Dkc3SdlPresenterUpscalerFromName(const char *name, int *upscaler) {
  if (!name || !upscaler) return false;
  if (strcmp(name, "nearest") == 0) *upscaler = kDkc3UpscalerNearest;
  else if (strcmp(name, "bilinear") == 0) *upscaler = kDkc3UpscalerBilinear;
  else if (strcmp(name, "reconstruct") == 0)
    *upscaler = kDkc3UpscalerReconstruct;
  else return false;
  return true;
}

void Dkc3SdlPresenterDrawableSize(Dkc3SdlPresenter *presenter, int *width,
                                  int *height) {
  int w = 0, h = 0;
  if (presenter && SwitchRenderer(presenter))
    SDL_GetRendererOutputSize(SwitchRenderer(presenter), &w, &h);
  if (width) *width = w;
  if (height) *height = h;
}

void Dkc3SdlPresenterArmCapture(Dkc3SdlPresenter *presenter, uint8_t *rgb,
                                int width, int height) {
  if (!presenter) return;
  presenter->capture_rgb = rgb;
  presenter->capture_width = width;
  presenter->capture_height = height;
  presenter->capture_done = false;
}

bool Dkc3SdlPresenterSetFullscreen(Dkc3SdlPresenter *presenter,
                                   bool fullscreen) {
  (void)presenter;
  (void)fullscreen;
  /* The Switch window is always an unbordered full-drawable surface;
   * there is nothing to toggle. */
  return true;
}

bool Dkc3SdlPresenterIsFullscreen(const Dkc3SdlPresenter *presenter) {
  (void)presenter;
  return true;
}

const char *Dkc3SdlPresenterBackend(const Dkc3SdlPresenter *presenter) {
  return (presenter && presenter->backend[0]) ? presenter->backend
                                              : "sdl2-gles2";
}

Dkc3DesktopVsyncStatus Dkc3SdlPresenterVsyncStatus(
    const Dkc3SdlPresenter *presenter) {
  if (!presenter) return kDkc3DesktopVsyncUnsupported;
  return presenter->vsync_status;
}

void *Dkc3SdlPresenterNativeWindow(const Dkc3SdlPresenter *presenter) {
  (void)presenter;
  return NULL;
}

bool Dkc3SdlPresenterUsesSoftwarePacing(const Dkc3SdlPresenter *presenter) {
  (void)presenter;
  /* The renderer runs PRESENTVSYNC: presentation itself paces to the
   * 60 Hz display, so the host clock owns frame deadlines. */
  return false;
}

void Dkc3SdlPresenterDestroy(Dkc3SdlPresenter *presenter) {
  if (!presenter) return;
  SwitchDropTexture(presenter);
  if (presenter->gl_context)
    SDL_DestroyRenderer(SwitchRenderer(presenter));
  if (presenter->window) SDL_DestroyWindow(SwitchWindow(presenter));
  presenter->gl_context = NULL;
  presenter->window = NULL;
}

#endif /* __SWITCH__ */
