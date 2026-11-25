/* Nintendo Switch presenter for DKC2Recomp: native drako3d backend.
 *
 * SDL's Switch video plus its "opengl" renderer never displayed textured
 * frames (clears presented, textured quads stayed black with no error), so
 * the Switch build drives the GPU directly with drako3d -- the same API as
 * deko3d, only the include differs -- while SDL stays for audio, input,
 * and events only. No SDL window is created; the default NWindow feeds a
 * drako3d swapchain, exactly like the drako_basic example.
 *
 * Zero-copy frame path: the emulator renders the SNES frame straight into
 * pitch-linear texture memory (Dkc3SdlPresenterBeginFrame hands sdl_main.c
 * the CPU pointer and layout pitch before emulation runs), and the GPU
 * samples it directly. The BGRX memory bytes are swizzled to RGB in the
 * image view, so there is no memcpy and no transfer -- FAST. A non-Raw
 * screen filter (never selectable on Switch) falls back to a staging
 * upload. Only compiled on __SWITCH__; desktop builds keep
 * desktop_present_sdl.c.
 */
#ifdef __SWITCH__

#include "desktop_present_sdl.h"
#include "desktop_launcher.h"
#include "dkc3_video.h"

#include <drako3d.h>
#include <switch.h>

#include <stdio.h>
#include <string.h>

enum {
  kSwitchFbCount = 2,
  kSwitchCmdMemSize = 8 * 1024,
  kSwitchCodeMemSize = 64 * 1024,
  kSwitchTexMaxWidth = kDkc3VideoMaximumWidth,
  kSwitchTexMaxHeight = kDkc3VideoHeight,
  kSwitchTexStrideAlign = 512,
};

typedef struct SwitchDrako {
  bool ready;
  bool romfs_mounted;
  DkDevice device;
  DkQueue queue;
  DkCmdBuf cmdbufs[kSwitchFbCount];
  DkMemBlock cmdMems[kSwitchFbCount];
  DkMemBlock codeMem;
  DkShader vertex_shader;
  DkShader fragment_shader;
  DkMemBlock fbMem;
  DkImage framebuffers[kSwitchFbCount];
  DkSwapchain swapchain;
  int fb_width;
  int fb_height;
  DkMemBlock texMem;
  DkImage textures[kSwitchFbCount];
  void *tex_cpu[kSwitchFbCount];
  size_t tex_pitch;
  int tex_width;
  int tex_height;
  DkSampler samplers[2];
  DkMemBlock dataMem;
  DkGpuAddr desc_addr;
  DkGpuAddr vbo_addr;
  DkMemBlock stagingMem;
  void *staging_cpu;
  DkMemBlock readbackMem;
  void *readback_cpu;
  int slot;
  bool has_slot;
  bool direct;
} SwitchDrako;

typedef struct SwitchVertex {
  float x;
  float y;
  float u;
  float v;
} SwitchVertex;

static const DkVtxAttribState s_vtx_attribs[] = {
    {.bufferId = 0,
     .isFixed = 0,
     .offset = 0,
     .size = DkVtxAttribSize_2x32,
     .type = DkVtxAttribType_Float,
     .isBgra = 0},
    {.bufferId = 0,
     .isFixed = 0,
     .offset = 8,
     .size = DkVtxAttribSize_2x32,
     .type = DkVtxAttribType_Float,
     .isBgra = 0},
};

static const DkVtxBufferState s_vtxbuf_state = {.stride =
                                                    sizeof(SwitchVertex),
                                                .divisor = 0};

static const SwitchVertex s_fullscreen_triangle[3] = {
    {-1.0f, -1.0f, 0.0f, 1.0f},
    {3.0f, -1.0f, 2.0f, 1.0f},
    {-1.0f, 3.0f, 0.0f, -1.0f},
};

static SwitchDrako s_drako;

static uint32_t SwitchAlignUp(uint32_t value, uint32_t align) {
  return (value + align - 1) & ~(align - 1);
}

static bool SwitchInitSwapchain(SwitchDrako *drako, int width, int height,
                                char *error, size_t error_capacity) {
  DkImageLayoutMaker layout_maker;
  dkImageLayoutMakerDefaults(&layout_maker, drako->device);
  layout_maker.flags =
      DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
      DkImageFlags_HwCompression;
  layout_maker.format = DkImageFormat_RGBA8_Unorm;
  layout_maker.dimensions[0] = (uint32_t)width;
  layout_maker.dimensions[1] = (uint32_t)height;
  DkImageLayout layout;
  dkImageLayoutInitialize(&layout, &layout_maker);
  uint32_t image_size = (uint32_t)dkImageLayoutGetSize(&layout);
  uint32_t image_align = dkImageLayoutGetAlignment(&layout);
  image_size = SwitchAlignUp(image_size, image_align);

  const DkImage *images[kSwitchFbCount];
  for (int i = 0; i < kSwitchFbCount; i++) {
    dkImageInitialize(&drako->framebuffers[i], &layout, drako->fbMem,
                      (uint32_t)i * image_size);
    images[i] = &drako->framebuffers[i];
  }
  DkSwapchainMaker maker;
  dkSwapchainMakerDefaults(&maker, drako->device, nwindowGetDefault(), images,
                           kSwitchFbCount);
  drako->swapchain = dkSwapchainCreate(&maker);
  if (!drako->swapchain) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "dkSwapchainCreate failed");
    return false;
  }
  drako->fb_width = width;
  drako->fb_height = height;
  return true;
}

static bool SwitchInitTexture(SwitchDrako *drako, int width, int height,
                               char *error, size_t error_capacity) {
  /* Pitch-linear so the emulator renders straight into mapped memory.
   * Explicit stride: pitchStride 0 would pack tightly, but then the
   * stride is only known after layout; fixing it up front keeps the
   * draw pitch constant and known. */
  DkImageLayoutMaker layout_maker;
  dkImageLayoutMakerDefaults(&layout_maker, drako->device);
  layout_maker.flags = DkImageFlags_PitchLinear;
  layout_maker.format = DkImageFormat_RGBA8_Unorm;
  layout_maker.dimensions[0] = (uint32_t)width;
  layout_maker.dimensions[1] = (uint32_t)height;
  layout_maker.pitchStride = SwitchAlignUp((uint32_t)width * 4,
                                           kSwitchTexStrideAlign);
  DkImageLayout layout;
  dkImageLayoutInitialize(&layout, &layout_maker);
  uint32_t image_size = (uint32_t)dkImageLayoutGetSize(&layout);
  uint32_t image_align = dkImageLayoutGetAlignment(&layout);
  image_size = SwitchAlignUp(image_size, image_align);

  uint64_t block_size = (uint64_t)image_size * kSwitchFbCount;
  if (block_size > dkMemBlockGetSize(drako->texMem)) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "texture %dx%d exceeds block", width,
               height);
    return false;
  }
  uint8_t *base = (uint8_t *)dkMemBlockGetCpuAddr(drako->texMem);
  if (!base) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "texture block not CPU-mapped");
    return false;
  }
  for (int i = 0; i < kSwitchFbCount; i++) {
    dkImageInitialize(&drako->textures[i], &layout, drako->texMem,
                      (uint32_t)i * image_size);
    drako->tex_cpu[i] = base + (size_t)i * image_size;
  }
  drako->tex_pitch = layout_maker.pitchStride;
  drako->tex_width = width;
  drako->tex_height = height;

  /* Descriptors are pushed per frame (dkCmdBufPushData), so the data
   * block only holds the static vertex buffer. Sampler configs live in
   * state and are pushed alongside the per-slot image view. */
  drako->desc_addr = dkMemBlockGetGpuAddr(drako->dataMem);
  drako->vbo_addr = drako->desc_addr + 256;
  memcpy((uint8_t *)dkMemBlockGetCpuAddr(drako->dataMem) + 256,
         s_fullscreen_triangle, sizeof s_fullscreen_triangle);
  dkSamplerDefaults(&drako->samplers[0]);
  drako->samplers[0].minFilter = DkFilter_Nearest;
  drako->samplers[0].magFilter = DkFilter_Nearest;
  drako->samplers[0].wrapMode[0] = DkWrapMode_ClampToEdge;
  drako->samplers[0].wrapMode[1] = DkWrapMode_ClampToEdge;
  drako->samplers[0].wrapMode[2] = DkWrapMode_ClampToEdge;
  drako->samplers[1] = drako->samplers[0];
  drako->samplers[1].minFilter = DkFilter_Linear;
  drako->samplers[1].magFilter = DkFilter_Linear;
  return true;
}

static void SwitchDropSwapchain(SwitchDrako *drako) {
  if (drako->swapchain) {
    dkQueueWaitIdle(drako->queue);
    dkSwapchainDestroy(drako->swapchain);
    drako->swapchain = 0;
  }
  drako->has_slot = false;
}

static bool SwitchEnsureSize(SwitchDrako *drako, int width, int height,
                             char *error, size_t error_capacity) {
  if (drako->fb_width == width && drako->fb_height == height) return true;
  SwitchDropSwapchain(drako);
  return SwitchInitSwapchain(drako, width, height, error, error_capacity);
}

static bool SwitchEnsureTexture(SwitchDrako *drako, int width, int height,
                                char *error, size_t error_capacity) {
  if (drako->tex_width == width && drako->tex_height == height) return true;
  dkQueueWaitIdle(drako->queue);
  return SwitchInitTexture(drako, width, height, error, error_capacity);
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
  memset(&s_drako, 0, sizeof s_drako);
  presenter->linear_filter = linear_filter;
  presenter->upscaler =
      linear_filter ? kDkc3UpscalerBilinear : kDkc3UpscalerNearest;

  if (!s_drako.romfs_mounted) {
    /* romfsInit returns a Result: 0 is success, not failure. */
    Result romfs_result = romfsInit();
    if (R_FAILED(romfs_result)) {
      if (error && error_capacity)
        snprintf(error, error_capacity, "romfsInit failed (0x%x)",
                 (unsigned)romfs_result);
      return false;
    }
    s_drako.romfs_mounted = true;
  }

  DkDeviceMaker device_maker;
  dkDeviceMakerDefaults(&device_maker);
  s_drako.device = dkDeviceCreate(&device_maker);
  if (!s_drako.device) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "dkDeviceCreate failed");
    return false;
  }

  /* Shader code block first so offsets stay aligned. */
  DkMemBlockMaker block_maker;
  dkMemBlockMakerDefaults(&block_maker, s_drako.device, kSwitchCodeMemSize);
  block_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached |
                      DkMemBlockFlags_Code;
  s_drako.codeMem = dkMemBlockCreate(&block_maker);
  if (!s_drako.codeMem) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "code block failed");
    Dkc3SdlPresenterDestroy(presenter);
    return false;
  }
  uint32_t code_offset = 0;
  const char *shader_paths[2] = {
      "romfs:/shaders/switch_present_vsh.dksh",
      "romfs:/shaders/switch_present_fsh.dksh",
  };
  DkShader *shaders[2] = {&s_drako.vertex_shader, &s_drako.fragment_shader};
  for (int i = 0; i < 2; i++) {
    FILE *stream = fopen(shader_paths[i], "rb");
    if (!stream) {
      if (error && error_capacity)
        snprintf(error, error_capacity, "missing %s", shader_paths[i]);
      Dkc3SdlPresenterDestroy(presenter);
      return false;
    }
    fseek(stream, 0, SEEK_END);
    long size = ftell(stream);
    rewind(stream);
    if (size <= 0 || (uint32_t)size > kSwitchCodeMemSize / 2) {
      if (error && error_capacity)
        snprintf(error, error_capacity, "bad %s", shader_paths[i]);
      fclose(stream);
      Dkc3SdlPresenterDestroy(presenter);
      return false;
    }
    code_offset =
        SwitchAlignUp(code_offset, DK_SHADER_CODE_ALIGNMENT);
    if (fread((uint8_t *)dkMemBlockGetCpuAddr(s_drako.codeMem) + code_offset,
              1, (size_t)size, stream) != (size_t)size) {
      if (error && error_capacity)
        snprintf(error, error_capacity, "read %s", shader_paths[i]);
      fclose(stream);
      Dkc3SdlPresenterDestroy(presenter);
      return false;
    }
    fclose(stream);
    DkShaderMaker shader_maker;
    dkShaderMakerDefaults(&shader_maker, s_drako.codeMem, code_offset);
    dkShaderInitialize(shaders[i], &shader_maker);
    code_offset += SwitchAlignUp((uint32_t)size, DK_SHADER_CODE_ALIGNMENT);
  }

  /* Framebuffer block sized for 1080p; smaller modes reuse it. */
  {
    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, s_drako.device);
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
                         DkImageFlags_HwCompression;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
    layout_maker.dimensions[0] = 1920;
    layout_maker.dimensions[1] = 1080;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layout_maker);
    uint32_t image_size = (uint32_t)dkImageLayoutGetSize(&layout);
    image_size = SwitchAlignUp(
        image_size, dkImageLayoutGetAlignment(&layout));
    dkMemBlockMakerDefaults(&block_maker, s_drako.device,
                            image_size * kSwitchFbCount);
    block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    s_drako.fbMem = dkMemBlockCreate(&block_maker);
    if (!s_drako.fbMem) {
      if (error && error_capacity)
        snprintf(error, error_capacity, "framebuffer block failed");
      Dkc3SdlPresenterDestroy(presenter);
      return false;
    }
  }

  /* Texture block for the widest frame; live size set below. */
  {
    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, s_drako.device);
    layout_maker.flags = DkImageFlags_PitchLinear;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
    layout_maker.dimensions[0] = kSwitchTexMaxWidth;
    layout_maker.dimensions[1] = kSwitchTexMaxHeight;
    layout_maker.pitchStride = SwitchAlignUp(kSwitchTexMaxWidth * 4,
                                             kSwitchTexStrideAlign);
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layout_maker);
    uint32_t image_size = (uint32_t)dkImageLayoutGetSize(&layout);
    image_size = SwitchAlignUp(
        image_size, dkImageLayoutGetAlignment(&layout));
    dkMemBlockMakerDefaults(&block_maker, s_drako.device,
                            image_size * kSwitchFbCount);
    block_maker.flags = DkMemBlockFlags_CpuUncached |
                        DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    s_drako.texMem = dkMemBlockCreate(&block_maker);
    if (!s_drako.texMem) {
      if (error && error_capacity)
        snprintf(error, error_capacity, "texture block failed");
      Dkc3SdlPresenterDestroy(presenter);
      return false;
    }
  }

  /* Descriptor, staging, command, and readback blocks. */
  dkMemBlockMakerDefaults(&block_maker, s_drako.device, 4096);
  block_maker.flags =
      DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
  s_drako.dataMem = dkMemBlockCreate(&block_maker);
  dkMemBlockMakerDefaults(
      &block_maker, s_drako.device,
      (uint32_t)kSwitchTexMaxWidth * kSwitchTexMaxHeight * 4);
  s_drako.stagingMem = dkMemBlockCreate(&block_maker);
  dkMemBlockMakerDefaults(&block_maker, s_drako.device, 1920 * 1080 * 4);
  s_drako.readbackMem = dkMemBlockCreate(&block_maker);
  for (int i = 0; i < kSwitchFbCount; i++) {
    dkMemBlockMakerDefaults(&block_maker, s_drako.device,
                            kSwitchCmdMemSize);
    s_drako.cmdMems[i] = dkMemBlockCreate(&block_maker);
  }
  if (!s_drako.dataMem || !s_drako.stagingMem || !s_drako.readbackMem ||
      !s_drako.cmdMems[0] || !s_drako.cmdMems[1]) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "aux blocks failed");
    Dkc3SdlPresenterDestroy(presenter);
    return false;
  }
  s_drako.staging_cpu = dkMemBlockGetCpuAddr(s_drako.stagingMem);
  s_drako.readback_cpu = dkMemBlockGetCpuAddr(s_drako.readbackMem);

  DkCmdBufMaker cmdbuf_maker;
  dkCmdBufMakerDefaults(&cmdbuf_maker, s_drako.device);
  for (int i = 0; i < kSwitchFbCount; i++) {
    s_drako.cmdbufs[i] = dkCmdBufCreate(&cmdbuf_maker);
    if (!s_drako.cmdbufs[i]) {
      if (error && error_capacity)
        snprintf(error, error_capacity, "cmdbuf %d failed", i);
      Dkc3SdlPresenterDestroy(presenter);
      return false;
    }
    dkCmdBufAddMemory(s_drako.cmdbufs[i], s_drako.cmdMems[i], 0,
                      kSwitchCmdMemSize);
  }

  DkQueueMaker queue_maker;
  dkQueueMakerDefaults(&queue_maker, s_drako.device);
  queue_maker.flags = DkQueueFlags_Graphics;
  s_drako.queue = dkQueueCreate(&queue_maker);
  if (!s_drako.queue) {
    if (error && error_capacity)
      snprintf(error, error_capacity, "queue failed");
    Dkc3SdlPresenterDestroy(presenter);
    return false;
  }

  /* Docked = 1080p, handheld = 720p. */
  int switch_w = 1280, switch_h = 720;
  if (appletGetOperationMode() == AppletOperationMode_Console) {
    switch_w = 1920;
    switch_h = 1080;
  }
  if (!SwitchInitSwapchain(&s_drako, switch_w, switch_h, error,
                           error_capacity)) {
    Dkc3SdlPresenterDestroy(presenter);
    return false;
  }
  if (!SwitchInitTexture(&s_drako, source_width > 0 ? source_width : 256,
                         source_height > 0 ? source_height : 224, error,
                         error_capacity)) {
    Dkc3SdlPresenterDestroy(presenter);
    return false;
  }

  presenter->software_paced = false;
  presenter->vsync_status = kDkc3DesktopVsyncEnabled;
  snprintf(presenter->backend, sizeof presenter->backend, "drako3d");
  s_drako.ready = true;
  return true;
}

bool Dkc3SdlPresenterBeginFrame(Dkc3SdlPresenter *presenter, bool direct,
                               uint8_t **pixels, size_t *pitch) {
  (void)presenter;
  SwitchDrako *drako = &s_drako;
  if (!drako->ready) return false;
  /* Serialize against the previous frame first: the command buffers and
   * textures are ping-ponged per swapchain slot, and re-recording while
   * the GPU still reads them faults the channel (death lands in the next
   * acquire). Matches the proven MesenCE pattern of WaitIdle per frame. */
  dkQueueWaitIdle(drako->queue);
  int slot = dkQueueAcquireImage(drako->queue, drako->swapchain);
  if (slot < 0 || slot >= kSwitchFbCount) return false;
  drako->slot = slot;
  drako->has_slot = true;
  drako->direct = direct && drako->tex_cpu[slot] != NULL;
  if (pixels) *pixels = NULL;
  if (pitch) *pitch = 0;
  if (drako->direct) {
    /* Clear the full texture (gutter included) so a narrower frame never
     * shows stale columns from a wider one. */
    memset(drako->tex_cpu[slot], 0,
           drako->tex_pitch * (size_t)drako->tex_height);
    if (pixels) *pixels = (uint8_t *)drako->tex_cpu[slot];
    if (pitch) *pitch = drako->tex_pitch;
  }
  return true;
}

bool Dkc3SdlPresenterPresent(Dkc3SdlPresenter *presenter,
                             const uint8_t *pixels, int source_width,
                             int source_height,
                             Dkc3SdlOverlayDraw overlay_draw,
                             void *overlay_user) {
  (void)overlay_draw;
  (void)overlay_user;
  if (!presenter || !pixels) return false;
  SwitchDrako *drako = &s_drako;
  if (!drako->ready) return false;

  /* Dock/handheld transitions resize the swapchain. */
  {
    static unsigned s_tick;
    if (++s_tick >= 12) {
      int want_w = 1280, want_h = 720;
      s_tick = 0;
      if (appletGetOperationMode() == AppletOperationMode_Console) {
        want_w = 1920;
        want_h = 1080;
      }
      if (want_w != drako->fb_width || want_h != drako->fb_height) {
        char error[160] = {0};
        if (!SwitchEnsureSize(drako, want_w, want_h, error, sizeof error)) {
          fprintf(stderr, "[SwitchVideo] resize failed: %s\n", error);
          return false;
        }
      }
    }
  }

  if (!drako->has_slot) {
    int slot = dkQueueAcquireImage(drako->queue, drako->swapchain);
    if (slot < 0 || slot >= kSwitchFbCount) return false;
    drako->slot = slot;
    drako->has_slot = true;
  }
  int slot = drako->slot;
  drako->has_slot = false;

  if (!SwitchEnsureTexture(drako, source_width, source_height, NULL, 0))
    return false;

  DkCmdBuf cmdbuf = drako->cmdbufs[slot];
  dkCmdBufClear(cmdbuf);

  if (!drako->direct) {
    /* Filtered fallback: packed filter output through staging. */
    size_t bytes = (size_t)source_width * (size_t)source_height * 4;
    memcpy(drako->staging_cpu, pixels, bytes);
    dkMemBlockFlushCpuCache(drako->stagingMem, 0, (uint32_t)bytes);
    DkCopyBuf src = {dkMemBlockGetGpuAddr(drako->stagingMem),
                     (uint32_t)source_width, (uint32_t)source_height};
    DkImageView tex_view;
    dkImageViewDefaults(&tex_view, &drako->textures[slot]);
    tex_view.swizzle[0] = DkImageSwizzle_Blue;
    tex_view.swizzle[1] = DkImageSwizzle_Green;
    tex_view.swizzle[2] = DkImageSwizzle_Red;
    tex_view.swizzle[3] = DkImageSwizzle_One;
    DkImageRect dst_rect = {0, 0, 0, (uint32_t)source_width,
                            (uint32_t)source_height, 1};
    dkCmdBufCopyBufferToImage(cmdbuf, &src, &tex_view, &dst_rect, 0);
    dkCmdBufBarrier(cmdbuf, DkBarrier_Full,
                     DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
  } else {
    /* Zero-copy: the emulator rendered straight into texture memory.
     * Only the texture cache needs invalidating. */
    dkCmdBufBarrier(cmdbuf, DkBarrier_None, DkInvalidateFlags_Image);
  }

  DkImageView target_view;
  dkImageViewDefaults(&target_view, &drako->framebuffers[slot]);
  dkCmdBufBindRenderTarget(cmdbuf, &target_view, NULL);

  DkViewport viewport = {0.0f, 0.0f, (float)drako->fb_width,
                         (float)drako->fb_height, 0.0f, 1.0f};
  DkScissor scissor = {0, 0, drako->fb_width, drako->fb_height};
  dkCmdBufSetViewports(cmdbuf, 0, &viewport, 1);
  dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);

  DkShader const *shaders[] = {&drako->vertex_shader,
                               &drako->fragment_shader};
  dkCmdBufBindShaders(cmdbuf, DkStageFlag_GraphicsMask, shaders, 2);
  DkRasterizerState rasterizer;
  dkRasterizerStateDefaults(&rasterizer);
  DkColorState color_state;
  dkColorStateDefaults(&color_state);
  DkColorWriteState color_write_state;
  dkColorWriteStateDefaults(&color_write_state);
  dkCmdBufBindRasterizerState(cmdbuf, &rasterizer);
  dkCmdBufBindColorState(cmdbuf, &color_state);
  dkCmdBufBindColorWriteState(cmdbuf, &color_write_state);

  /* Real hardware descriptors, pushed per frame: a raw DkImageView /
   * DkSampler struct is NOT a valid descriptor. The pushed image view
   * points at this slot's texture with the BGRX->RGB view swizzle, so
   * the GPU does the channel remap with no CPU swizzle and no copy. */
  struct {
    DkImageDescriptor image;
    DkSamplerDescriptor sampler;
  } descriptors;
  DkImageView tex_view;
  dkImageViewDefaults(&tex_view, &drako->textures[slot]);
  tex_view.swizzle[0] = DkImageSwizzle_Blue;
  tex_view.swizzle[1] = DkImageSwizzle_Green;
  tex_view.swizzle[2] = DkImageSwizzle_Red;
  tex_view.swizzle[3] = DkImageSwizzle_One;
  dkImageDescriptorInitialize(&descriptors.image, &tex_view, false, false);
  dkSamplerDescriptorInitialize(
      &descriptors.sampler,
      &drako->samplers[presenter->upscaler == kDkc3UpscalerBilinear ? 1
                                                                   : 0]);
  dkCmdBufPushData(cmdbuf, drako->desc_addr, &descriptors,
                   sizeof descriptors);
  dkCmdBufBindImageDescriptorSet(cmdbuf, drako->desc_addr, 1);
  dkCmdBufBindSamplerDescriptorSet(
      cmdbuf, drako->desc_addr + (DkGpuAddr)sizeof(DkImageDescriptor), 1);
  dkCmdBufBindTexture(cmdbuf, DkStage_Fragment, 0,
                      dkMakeTextureHandle(0, 0));
  DkBufExtents vtx_extents = {drako->vbo_addr,
                              (uint32_t)sizeof s_fullscreen_triangle};
  dkCmdBufBindVtxAttribState(cmdbuf, s_vtx_attribs,
                             sizeof s_vtx_attribs / sizeof s_vtx_attribs[0]);
  dkCmdBufBindVtxBufferState(cmdbuf, &s_vtxbuf_state, 1);
  dkCmdBufBindVtxBuffers(cmdbuf, 0, &vtx_extents, 1);
  dkCmdBufDraw(cmdbuf, DkPrimitive_Triangles, 3, 1, 0, 0);

  bool capture = presenter->capture_rgb && presenter->capture_width > 0 &&
                 presenter->capture_height > 0;
  if (capture) {
    int out_w = presenter->capture_width;
    int out_h = presenter->capture_height;
    DkCopyBuf dst = {dkMemBlockGetGpuAddr(drako->readbackMem),
                     (uint32_t)out_w, (uint32_t)out_h};
    DkImageRect src_rect = {0, 0, 0, (uint32_t)out_w, (uint32_t)out_h, 1};
    dkCmdBufCopyImageToBuffer(cmdbuf, &target_view, &src_rect, &dst, 0);
  }

  dkQueueSubmitCommands(drako->queue, dkCmdBufFinishList(cmdbuf));
  dkQueuePresentImage(drako->queue, drako->swapchain, slot);

  if (capture) {
    /* Best-effort one-shot readback: stall, convert RGBA to RGB24. */
    dkQueueWaitIdle(drako->queue);
    int out_w = presenter->capture_width;
    int out_h = presenter->capture_height;
    const uint8_t *src = (const uint8_t *)drako->readback_cpu;
    uint8_t *dst = presenter->capture_rgb;
    for (int y = 0; y < out_h; y++) {
      for (int x = 0; x < out_w; x++) {
        dst[(size_t)(y * out_w + x) * 3 + 0] = src[(size_t)(y * out_w + x) * 4 + 0];
        dst[(size_t)(y * out_w + x) * 3 + 1] = src[(size_t)(y * out_w + x) * 4 + 1];
        dst[(size_t)(y * out_w + x) * 3 + 2] = src[(size_t)(y * out_w + x) * 4 + 2];
      }
    }
    presenter->capture_done = true;
    presenter->capture_rgb = NULL;
  }
  return true;
}

void Dkc3SdlPresenterSetTitle(Dkc3SdlPresenter *presenter,
                              const char *title) {
  (void)presenter;
  (void)title;
  /* No SDL window on this backend; nothing to retitle. */
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
    /* No shader hooks on this path either: fall back to the sampler and
     * say so, like the desktop presenter does when its shader fails. */
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
  if (presenter && s_drako.ready) {
    w = s_drako.fb_width;
    h = s_drako.fb_height;
  }
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
  /* The drako3d swapchain always owns the full drawable surface; there
   * is nothing to toggle. */
  return true;
}

bool Dkc3SdlPresenterIsFullscreen(const Dkc3SdlPresenter *presenter) {
  (void)presenter;
  return true;
}

const char *Dkc3SdlPresenterBackend(const Dkc3SdlPresenter *presenter) {
  return (presenter && presenter->backend[0]) ? presenter->backend
                                              : "drako3d";
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

bool Dkc3SdlPresenterUsesSoftwarePacing(
    const Dkc3SdlPresenter *presenter) {
  (void)presenter;
  /* Presentation is display-paced (fifo swapchain), so the host clock
   * owns frame deadlines. */
  return false;
}

void Dkc3SdlPresenterDestroy(Dkc3SdlPresenter *presenter) {
  SwitchDrako *drako = &s_drako;
  if (drako->queue) dkQueueWaitIdle(drako->queue);
  if (drako->swapchain) {
    dkSwapchainDestroy(drako->swapchain);
    drako->swapchain = 0;
  }
  if (drako->queue) {
    dkQueueDestroy(drako->queue);
    drako->queue = 0;
  }
  for (int i = 0; i < kSwitchFbCount; i++) {
    if (drako->cmdbufs[i]) {
      dkCmdBufDestroy(drako->cmdbufs[i]);
      drako->cmdbufs[i] = 0;
    }
    if (drako->cmdMems[i]) {
      dkMemBlockDestroy(drako->cmdMems[i]);
      drako->cmdMems[i] = 0;
    }
  }
  if (drako->codeMem) {
    dkMemBlockDestroy(drako->codeMem);
    drako->codeMem = 0;
  }
  if (drako->fbMem) {
    dkMemBlockDestroy(drako->fbMem);
    drako->fbMem = 0;
  }
  if (drako->texMem) {
    dkMemBlockDestroy(drako->texMem);
    drako->texMem = 0;
  }
  if (drako->dataMem) {
    dkMemBlockDestroy(drako->dataMem);
    drako->dataMem = 0;
  }
  if (drako->stagingMem) {
    dkMemBlockDestroy(drako->stagingMem);
    drako->stagingMem = 0;
  }
  if (drako->readbackMem) {
    dkMemBlockDestroy(drako->readbackMem);
    drako->readbackMem = 0;
  }
  if (drako->device) {
    dkDeviceDestroy(drako->device);
    drako->device = 0;
  }
  if (drako->romfs_mounted) {
    romfsExit();
    drako->romfs_mounted = false;
  }
  drako->ready = false;
  drako->has_slot = false;
  if (presenter) {
    presenter->window = NULL;
    presenter->gl_context = NULL;
  }
}

#endif /* __SWITCH__ */
