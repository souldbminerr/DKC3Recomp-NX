# Makefile.switch — Nintendo Switch (libnx) build for DKC3Recomp.
#
# Copy-free: this file lives in the game repo and includes the framework
# fragment at snesrecomp/runner/switch.mk. Requires devkitA64 + switch
# portlibs SDL2. No CMake, no recomp-ui, no launcher: the NRO boots
# straight from sdmc:/switch/dkc3/rom.smc.
#
#   1. Generate private sources once (needs the En/Fr v1.0 ROM):
#        python scripts/generate_snesrecomp.py --rom <path>/dkc3.smc
#   2. make                          # -> dkc3.nro + dkc3.nacp (static, SDL2)
#      make DKC3_SWITCH_DYNAMIC=1  # -> dynamically linked (libounce.so, shared libs)
#      make DKC3_SWITCH_DYNAMIC=1 DKC3_SWITCH_SDL=3  # -> + shared SDL3
#      make DEBUG=1 ...            # -> debug drako3d (-ldrako3dd) + L3/R3 extras
#   3. Copy dkc3.nro to sdmc:/switch/dkc3/ and place rom.smc beside it.
#      For DKC3_SWITCH_DYNAMIC=1 also copy the needed .so NROs
#      ($(LIBNX)/lib/shared/nro, $(PORTLIBS_SWITCH2)/lib/shared/nro) next to
#      it by hand; this Makefile bundles nothing. Needs a dynlink loader.
#
# Video is native drako3d (textured quad into a swapchain on the default
# NWindow; GLSL in runner/ compiles to romfs blobs at build time via uam2).
# Audio/input/events stay on SDL (2 or 3); input is raw SDL_Joystick
# (see runner/switch_gamepad.c for the Switch button map). Desktop-only
# units (desktop_present_sdl.c, desktop_overlay.cpp, desktop_main.c,
# macos_*) are excluded here.

.SUFFIXES:

# Keep `all` the default goal: helper file rules below must not hijack it.
.DEFAULT_GOAL := all

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
export TOPDIR
include $(DEVKITPRO)/libounce/switch2_rules

TARGET		:=	dkc3
BUILD		:=	build-switch
APP_TITLE	:=	Donkey Kong Country 3
APP_AUTHOR	:=	elliotttate and Souldbminer
APP_VERSION	:=	0.0.5

# SD layout: sdmc:/switch/<SWITCH_APP_DIR>/rom.smc (+config, saves).
SWITCH_APP_DIR	?=	dkc3

# Incremental rebuilds sometimes invoke the build dir directly
# (make -C build-switch ...). TOPDIR then points at the build dir itself;
# step back up to the repo so every flow shares one layout.
ifeq ($(notdir $(TOPDIR)),$(BUILD))
TOPDIR := $(patsubst %/,%,$(dir $(TOPDIR)))
endif

# SDL backend for the Switch host: 2 (default, static switch portlibs SDL)
# or 3 (shared portlibs SDL 3.4.0 from the Switch 2 prefix). 3 appends
# -DSNESRECOMP_SDL3=1 after the framework's -DSNESRECOMP_SDL3=0 so the
# later define wins; game-owned SDL call sites carry explicit
# #if SNESRECOMP_SDL3 branches. Shared SDL3 only loads in a dynamically
# linked program, so 3 requires DKC3_SWITCH_DYNAMIC=1.
DKC3_SWITCH_SDL	?=	2
DKC3_SWITCH_DYNAMIC	?=	0

ifeq ($(DKC3_SWITCH_SDL),3)
ifeq ($(DKC3_SWITCH_DYNAMIC),1)
# ok: shared SDL3 in a dynamic program
else
$(error "DKC3_SWITCH_SDL=3 needs DKC3_SWITCH_DYNAMIC=1 (SDL3 is shared-only)")
endif
endif

SNESRECOMP_ROOT	?=	$(TOPDIR)/snesrecomp
export SNESRECOMP_ROOT
include $(SNESRECOMP_ROOT)/runner/switch.mk

# This pinned framework revision predates five newer runner units that
# nothing in this game references; drop them instead of dirtying the
# submodule. (Upstream switch.mk keeps them for current checkouts.)
SNESRECOMP_SWITCH_CFILES := $(filter-out \
	$(SNESRECOMP_SWITCH_ROOT)src/mod_audio.c \
	$(SNESRECOMP_SWITCH_ROOT)src/host_mesh.c \
	$(SNESRECOMP_SWITCH_ROOT)src/host_mesh_builder.c \
	$(SNESRECOMP_SWITCH_ROOT)src/guarded_patch.c \
	$(SNESRECOMP_SWITCH_ROOT)src/snes/sdd1.c \
	$(SNESRECOMP_SWITCH_ROOT)src/desktop/mmx_config.c \
	, $(SNESRECOMP_SWITCH_CFILES))

# Upstream runtime adaptations (cmake/runtime-patches/*.hunks, incl. the
# 31-37% interpreter hunk cut + NEON PPU blends): fail-closed literal
# replacements applied to build-tree copies of the pinned runner, which the
# Switch build compiles instead of the pristine TUs. Regenerates when the
# hunks, either applier script, or the pinned sources change; the guard
# script re-applies the Switch tier2 cuts on top (idempotent: already-guarded
# copies succeed unchanged, so re-runs after hand edits are safe).
DKC3_PATCHED_DIR := $(TOPDIR)/patched-runtime
DKC3_PATCHED_NAMES := apu.c audio_trace.c common_cpu_infra.c cpu_state.c \
	dsp.c dsp_shadow.c interp_bridge.c ppu.c tier2_capture.c
DKC3_PATCHED_SRCS := $(addprefix $(DKC3_PATCHED_DIR)/,$(DKC3_PATCHED_NAMES))
DKC3_PATCHED_ORIGS := \
	$(addprefix $(SNESRECOMP_SWITCH_ROOT)src/,common_cpu_infra.c cpu_state.c audio_trace.c) \
	$(addprefix $(SNESRECOMP_SWITCH_ROOT)src/snes/,apu.c dsp.c dsp_shadow.c ppu.c tier2_capture.c interp_bridge.c)

$(DKC3_PATCHED_DIR)/manifest.txt: $(wildcard $(TOPDIR)/cmake/runtime-patches/*.hunks) \
		$(TOPDIR)/scripts/apply_dkc3_runtime_patches.py \
		$(TOPDIR)/scripts/apply_switch_runtime_guards.py \
		$(DKC3_PATCHED_ORIGS)
	python $(TOPDIR)/scripts/apply_dkc3_runtime_patches.py --patches $(TOPDIR)/cmake/runtime-patches --snesrecomp $(SNESRECOMP_ROOT) --out $(DKC3_PATCHED_DIR)
	python $(TOPDIR)/scripts/apply_switch_runtime_guards.py --patched $(DKC3_PATCHED_DIR)

$(DKC3_PATCHED_SRCS): $(DKC3_PATCHED_DIR)/manifest.txt

SNESRECOMP_SWITCH_CFILES := $(filter-out $(DKC3_PATCHED_ORIGS),$(SNESRECOMP_SWITCH_CFILES)) $(DKC3_PATCHED_SRCS)

# Private generated recomp units (scripts/generate_snesrecomp.py --rom ...).
DKC3_GEN_DIR		:=	$(TOPDIR)/generated/snesrecomp
DKC3_GEN_SOURCES	:=	$(wildcard $(DKC3_GEN_DIR)/*.c)

GAME_CFILES := \
	$(DKC3_GEN_SOURCES) \
	$(TOPDIR)/runner/dkc3_game.c \
	$(TOPDIR)/runner/dkc3_video.c \
	$(TOPDIR)/runner/dkc3_hdma.c \
	$(TOPDIR)/runner/dkc3_haptics.c \
	$(TOPDIR)/runner/dkc3_spc_music.c \
	$(TOPDIR)/runner/diagnostics_switch.c \
	$(TOPDIR)/runner/headless_host.c \
	$(TOPDIR)/runner/verified_rom.c \
	$(TOPDIR)/runner/desktop_filter.c \
	$(TOPDIR)/runner/desktop_fps.c \
	$(TOPDIR)/runner/desktop_input.c \
	$(TOPDIR)/runner/input_recording.c \
	$(TOPDIR)/runner/desktop_launcher.c \
	$(TOPDIR)/runner/desktop_overlay_model.c \
	$(TOPDIR)/runner/desktop_vsync.c \
	$(TOPDIR)/runner/desktop_rewind.c \
	$(TOPDIR)/runner/desktop_viewport.c \
	$(TOPDIR)/runner/desktop_pacer.c \
	$(TOPDIR)/runner/desktop_audio_rate.c \
	$(TOPDIR)/runner/sdl_main.c \
	$(TOPDIR)/runner/switch_gamepad.c \
	$(TOPDIR)/runner/desktop_present_switch.c \
	$(TOPDIR)/runner/desktop_overlay_switch.c

# Early dirs win bare-header resolution (notably runner/config.h over the
# framework's desktop/config.h). The framework dirs must precede recomp-ui:
# recomp-ui/src/common ships its own sha256.h/crc32.h that would otherwise
# shadow the framework headers verified_rom.c builds against (desktop
# CMake orders them the same way).
GAME_INCLUDES_EARLY := \
	$(TOPDIR)/runner \
	$(TOPDIR)/recomp

GAME_INCLUDES_LATE := \
	$(TOPDIR)/recomp-ui/src \
	$(TOPDIR)/recomp-ui/src/common

GAME_DEFS := \
	-DDKC3_RELEASE_VERSION='"0.0.5-switch"' \
	-DSNESRECOMP_REVERSE_DEBUG=0 \
	-DSNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 \
	-DSYSTEM_VOLUME_MIXER_AVAILABLE=0 \
	-DSDL_MAIN_HANDLED \
	-DDKC3_REAL_HOST_REPORT=1 \
	-DSWITCH_APP_DIR='"$(SWITCH_APP_DIR)"'

# Production forensics (see template): full diagnostic history costs a
# 128 KiB WRAM hash + ring every frame, per-dispatch logging, and
# CGRAM/VRAM scans + rings. Off, like a CMake production build.
GAME_DEFS	+=	-DSNESRECOMP_FRAME_FINGERPRINTS=0 \
			-DSNESRECOMP_DISPATCH_HISTORY=0 \
			-DSNESRECOMP_PPU_DMA_HISTORY=0

# Debug-only Switch extras: make -f Makefile.switch DEBUG=1
# (L3 = quick-save to slot 0, R3 = quick-load). Off by default.
ifdef DEBUG
GAME_DEFS += -DSWITCH_DEBUG
endif

DATA		:=
ROMFS		:=
APP_ICON	:=	$(TOPDIR)/assets/switch/icon.jpg

ifeq ($(DKC3_SWITCH_DYNAMIC),1)
ARCH	:=	$(SWITCH2_SHARED_ARCH)
else
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
endif

CFILES		:=	$(GAME_CFILES) $(SNESRECOMP_SWITCH_CFILES)

_BASENAMES	:=	$(notdir $(CFILES))
ifneq ($(words $(_BASENAMES)),$(words $(sort $(_BASENAMES))))
$(error "duplicate .c basenames across game + runner sources")
endif

INCLUDE		:=	$(foreach dir,$(GAME_INCLUDES_EARLY),-I$(dir)) \
			$(foreach dir,$(SNESRECOMP_SWITCH_INCLUDES),-I$(dir)) \
			$(foreach dir,$(GAME_INCLUDES_LATE),-I$(dir)) \
			-I$(LIBNX)/include \
			-I$(TOPDIR)/$(BUILD)

DEFINES		:=	$(SNESRECOMP_SWITCH_DEFS) $(GAME_DEFS)

# Prefer libounce/Switch 2 portlibs (SDL2/EGL) when present.
SWITCH2_PORTLIBS	:=	$(DEVKITPRO)/portlibs/switch2
ifneq ($(wildcard $(SWITCH2_PORTLIBS)/bin/sdl2-config),)
SDL2_CONFIG		:=	$(SWITCH2_PORTLIBS)/bin/sdl2-config
else
SDL2_CONFIG		:=	sdl2-config
endif

ifeq ($(DKC3_SWITCH_SDL),3)
# The framework pins SNESRECOMP_SDL3=0 for Switch; this later define wins.
DEFINES		+=	-DSNESRECOMP_SDL3=1
DKC3_SDL_CFLAGS	:=	`OUNCE_SHARED=1 $(DEVKITPRO)/portlibs/switch2/bin/aarch64-none-elf-pkg-config --cflags sdl3`
DKC3_SDL_LIBS	:=	`OUNCE_SHARED=1 $(DEVKITPRO)/portlibs/switch2/bin/aarch64-none-elf-pkg-config --libs sdl3`
else
DKC3_SDL_CFLAGS	:=	`$(SDL2_CONFIG) --cflags`
DKC3_SDL_LIBS	:=	`$(SDL2_CONFIG) --libs`
endif

CFLAGS		:=	-g -Wall -O2 -ffunction-sections -flto=auto \
			$(ARCH) $(DEFINES) $(INCLUDE) $(DKC3_SDL_CFLAGS)

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS		:=	-g $(ARCH)
ifeq ($(DKC3_SWITCH_DYNAMIC),1)
LDFLAGS		=	-specs=$(SWITCH2_SHARED_SPECS) -g $(ARCH) -flto=auto \
			-Wl,-Map,$(notdir $*.map)
else
LDFLAGS		=	-specs=$(DEVKITPRO)/libounce/switch.specs -g $(ARCH) -flto=auto \
			-Wl,-Map,$(notdir $*.map)
endif

# GC sections so the never-called desktop launcher entry point
# (Dkc3LauncherRun) and its recomp-ui references are dropped from the
# link; the Switch build never runs the pre-boot UI.
LDFLAGS		+=	-Wl,--gc-sections
# Neutralize SDL2's Switch keyboard pump (no text input in these titles).
LDFLAGS		+=	$(SNESRECOMP_SWITCH_WRAP)

ifneq ($(wildcard $(SWITCH2_PORTLIBS)/lib),)
LIBDIRS		:=	$(SWITCH2_PORTLIBS) $(PORTLIBS) $(LIBNX)
else
LIBDIRS		:=	$(PORTLIBS) $(LIBNX)
endif

# Native GPU path is drako3d (no SDL renderer); SDL stays for audio,
# input, and events only, so -lEGL is gone. DEBUG=1 takes the debug
# drako3d with extra validation, like the drako_basic example.
ifdef DEBUG
DKC3_DRAKO_LIB	:=	-ldrako3dd
else
DKC3_DRAKO_LIB	:=	-ldrako3d
endif
LIBS		:=	$(DKC3_SDL_LIBS) -lm -lnx $(DKC3_DRAKO_LIB)

# SDL/dynamic/DEBUG choices change -D flags, ARCH, specs, and SDL link
# lines without touching any source mtime, so a bare flag flip would
# otherwise relink stale objects (SDL3-compiled TUs against SDL2 and
# vice versa). The stamp records the active combo; flag-sensitive TUs
# depend on it, forcing a rebuild of exactly those files.
DKC3_SWITCH_CONFIG	:=	SDL=$(DKC3_SWITCH_SDL) DYNAMIC=$(DKC3_SWITCH_DYNAMIC) DEBUG=$(DEBUG)
DKC3_FLAG_SENSITIVE	:=	sdl_main.o switch_gamepad.o desktop_present_switch.o desktop_launcher.o
DKC3_CONFIG_STAMP	:=	$(TOPDIR)/$(BUILD)/switch-config.stamp

# The stamp has no source prerequisites, so without FORCE make would
# consider the existing file up to date forever and never refresh it:
# flag flips would then relink stale objects (e.g. SDL2-compiled TUs
# against the SDL3 shared libs). FORCE re-runs the recipe every time;
# the file itself is only rewritten when the combo actually changed, so
# rebuilds stay minimal.
.PHONY: FORCE
FORCE:

$(DKC3_CONFIG_STAMP): FORCE
	@[ -d $(@D) ] || mkdir -p $(@D)
	@echo '$(DKC3_SWITCH_CONFIG)' | cmp -s - $@ 2>/dev/null || echo '$(DKC3_SWITCH_CONFIG)' > $@

$(DKC3_FLAG_SENSITIVE): $(DKC3_CONFIG_STAMP)

# NRO metadata and romfs staging are shared by both flows (outer drive
# and direct build-dir rebuilds) so every NRO embeds the shaders.
ifeq ($(wildcard $(APP_ICON)),)
NROFLAGS += --icon=$(DEVKITPRO)/libounce/default_icon.jpg
else
NROFLAGS += --icon=$(APP_ICON)
endif
NROFLAGS += --nacp=$(TOPDIR)/$(TARGET).nacp

# drako3d shaders: GLSL source in runner/ compiles to romfs blobs at
# build time (uam2 ships with the devkit). The NRO embeds the staging dir.
DKC3_ROMFS_SHADERS	:=	$(TOPDIR)/$(BUILD)/romfs/shaders
DKC3_DKSH		:=	$(DKC3_ROMFS_SHADERS)/switch_present_vsh.dksh \
				$(DKC3_ROMFS_SHADERS)/switch_present_fsh.dksh

$(DKC3_ROMFS_SHADERS)/switch_present_vsh.dksh: $(TOPDIR)/runner/switch_present_vsh.glsl
	@mkdir -p $(@D)
	$(DEVKITPRO)/tools/bin/uam2 -s vert -o $@ $<

$(DKC3_ROMFS_SHADERS)/switch_present_fsh.dksh: $(TOPDIR)/runner/switch_present_fsh.glsl
	@mkdir -p $(@D)
	$(DEVKITPRO)/tools/bin/uam2 -s frag -o $@ $<

ifneq ($(ROMFS),)
NROFLAGS += --romfsdir=$(TOPDIR)/$(ROMFS)
endif
NROFLAGS += --romfsdir=$(TOPDIR)/$(BUILD)/romfs

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(TOPDIR)/$(TARGET)

export VPATH	:=	$(sort $(dir $(CFILES)))

export DEPSDIR	:=	$(TOPDIR)/$(BUILD)

export OFILES_SRC	:=	$(notdir $(CFILES:.c=.o))
export OFILES		:=	$(OFILES_SRC)
export HFILES_BIN	:=

# Link with CC (no C++ in the Switch build); games adding .cpp must
# switch this to $(CXX) like the stock devkitPro template does.
export LD	:=	$(CC)

export INCLUDE	:=	$(INCLUDE)
# Dynamic builds resolve shared libs (SDL3, OunceVK) from the Switch 2
# shared prefix before any static archive of the same name.
ifeq ($(DKC3_SWITCH_DYNAMIC),1)
export LIBPATHS	:=	$(SWITCH2_SHARED_LIBPATHS) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
else
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)
endif

.PHONY: $(BUILD) clean all

all: $(DKC3_DKSH) $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(TOPDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(DKC3_DKSH) $(OUTPUT).nro

$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp

# Explicit link through a response file: generated recomp trees put
# hundreds of objects on the link line, past Windows' CreateProcess
# limit ("Argument list too long"). This overrides switch_rules' %.elf
# pattern rule with the same recipe shape.
$(OUTPUT).elf	:	$(OFILES)
	@echo linking $(notdir $@)
	$(file >$(DEPSDIR)/objects.rsp,$(OFILES))
	@$(LD) $(LDFLAGS) @$(DEPSDIR)/objects.rsp $(LIBPATHS) $(LIBS) -o $@
	@$(NM) -CSn $@ > $(OUTPUT).lst

-include $(DEPENDS)

endif
