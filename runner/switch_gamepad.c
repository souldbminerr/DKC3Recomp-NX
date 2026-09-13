/* Raw SDL_Joystick gamepad path for Nintendo Switch homebrew.
 *
 * Button/axis indices follow the Switch HID order used by the portlibs
 * SDL2 driver (devkitPro sdl2-demo/sdl2-simple examples):
 *   buttons: 0 A, 1 B, 2 X, 3 Y, 4 L-Stick, 5 R-Stick, 6 L, 7 R,
 *            8 ZL, 9 ZR, 10 Plus, 11 Minus, 12 Left, 13 Up, 14 Right,
 *            15 Down.
 *   axes: 0 left-X, 1 left-Y, 2 right-X, 3 right-Y.
 * Sticks opened here are independent of the desktop SDL_GameController
 * handles in sdl_main.c, which stay NULL on Switch.
 */
#ifdef __SWITCH__

#include "switch_gamepad.h"

#include <SDL.h>

#include <stdint.h>
#include <string.h>

enum {
  kSwitchPadCapacity = 2,
  kSwitchBtnA = 0,
  kSwitchBtnB = 1,
  kSwitchBtnX = 2,
  kSwitchBtnY = 3,
  kSwitchBtnLStick = 4,
  kSwitchBtnRStick = 5,
  kSwitchBtnL = 6,
  kSwitchBtnR = 7,
  kSwitchBtnZL = 8,
  kSwitchBtnZR = 9,
  kSwitchBtnPlus = 10,
  kSwitchBtnMinus = 11,
  kSwitchBtnLeft = 12,
  kSwitchBtnUp = 13,
  kSwitchBtnRight = 14,
  kSwitchBtnDown = 15,
  kSwitchAxisLeftX = 0,
  kSwitchAxisLeftY = 1,
  kSwitchAxisRightX = 2,
  kSwitchAxisRightY = 3,
};

static SDL_Joystick *s_sticks[kSwitchPadCapacity];
static SDL_JoystickID s_stick_ids[kSwitchPadCapacity];

static int FindStick(SDL_JoystickID id) {
  for (int i = 0; i < kSwitchPadCapacity; i++) {
    if (s_sticks[i] && s_stick_ids[i] == id) return i;
  }
  return -1;
}

static int FindFreeSlot(void) {
  for (int i = 0; i < kSwitchPadCapacity; i++) {
    if (!s_sticks[i]) return i;
  }
  return -1;
}

int Dkc3SwitchRefreshPads(void) {
  int opened = 0;
  /* Drop detached sticks first so their slots are reusable. */
  for (int i = 0; i < kSwitchPadCapacity; i++) {
    if (s_sticks[i] && !SDL_JoystickGetAttached(s_sticks[i])) {
      SDL_JoystickClose(s_sticks[i]);
      s_sticks[i] = NULL;
    }
    if (s_sticks[i]) opened++;
  }
  int count = SDL_NumJoysticks();
  for (int device = 0; device < count; device++) {
    if (opened >= kSwitchPadCapacity) break;
    /* SDL_JoystickGetDeviceInstanceID needs SDL 2.0.9+; portlibs ships
     * 2.28.x. Fall back to opening by index when it reports invalid. */
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device);
    if (id >= 0 && FindStick(id) >= 0) continue;
    SDL_Joystick *stick = SDL_JoystickOpen(device);
    if (!stick) continue;
    SDL_JoystickID opened_id = SDL_JoystickInstanceID(stick);
    if (FindStick(opened_id) >= 0) {
      SDL_JoystickClose(stick);
      continue;
    }
    int slot = FindFreeSlot();
    if (slot < 0) {
      SDL_JoystickClose(stick);
      break;
    }
    s_sticks[slot] = stick;
    s_stick_ids[slot] = opened_id;
    opened++;
    fprintf(stderr, "[SwitchPad] opened #%d: %s\n", slot,
            SDL_JoystickName(stick) ? SDL_JoystickName(stick) : "(null)");
  }
  if (opened == 0) {
    static bool s_warned;
    if (!s_warned) {
      s_warned = true;
      fprintf(stderr, "[SwitchPad] no joysticks (sdl=%d)\n",
              SDL_NumJoysticks());
    }
  }
  return opened;
}

void Dkc3SwitchClosePads(void) {
  for (int i = 0; i < kSwitchPadCapacity; i++) {
    if (s_sticks[i]) SDL_JoystickClose(s_sticks[i]);
    s_sticks[i] = NULL;
  }
}

static uint8_t SwitchTriggerAsAxis(SDL_Joystick *stick, int button) {
  return SDL_JoystickGetButton(stick, button) ? 255 : 0;
}

size_t Dkc3SwitchReadPads(Dkc3GamepadState *out, size_t capacity) {
  size_t reported = 0;
  if (!out || capacity == 0) return 0;
  for (int i = 0; i < kSwitchPadCapacity && reported < capacity; i++) {
    SDL_Joystick *stick = s_sticks[i];
    if (!stick || !SDL_JoystickGetAttached(stick)) continue;
    Dkc3GamepadState *pad = &out[reported++];
    memset(pad, 0, sizeof *pad);
    uint32_t buttons = 0;
    if (SDL_JoystickGetButton(stick, kSwitchBtnUp)) buttons |= kDkc3GamepadDpadUp;
    if (SDL_JoystickGetButton(stick, kSwitchBtnDown)) buttons |= kDkc3GamepadDpadDown;
    if (SDL_JoystickGetButton(stick, kSwitchBtnLeft)) buttons |= kDkc3GamepadDpadLeft;
    if (SDL_JoystickGetButton(stick, kSwitchBtnRight)) buttons |= kDkc3GamepadDpadRight;
    /* Label semantics: the A-labelled button sets the A bit, and the
     * Switch bindings pass bits straight to the same-named SNES slots
     * (Switch labels already sit at the SNES positions). */
    if (SDL_JoystickGetButton(stick, kSwitchBtnA)) buttons |= kDkc3GamepadA;
    if (SDL_JoystickGetButton(stick, kSwitchBtnB)) buttons |= kDkc3GamepadB;
    if (SDL_JoystickGetButton(stick, kSwitchBtnX)) buttons |= kDkc3GamepadX;
    if (SDL_JoystickGetButton(stick, kSwitchBtnY)) buttons |= kDkc3GamepadY;
    if (SDL_JoystickGetButton(stick, kSwitchBtnPlus)) buttons |= kDkc3GamepadStart;
    if (SDL_JoystickGetButton(stick, kSwitchBtnMinus)) buttons |= kDkc3GamepadBack;
    if (SDL_JoystickGetButton(stick, kSwitchBtnL)) buttons |= kDkc3GamepadLeftShoulder;
    if (SDL_JoystickGetButton(stick, kSwitchBtnR)) buttons |= kDkc3GamepadRightShoulder;
    if (SDL_JoystickGetButton(stick, kSwitchBtnLStick)) buttons |= kDkc3GamepadLeftStick;
    if (SDL_JoystickGetButton(stick, kSwitchBtnRStick)) buttons |= kDkc3GamepadRightStick;
    int axes = SDL_JoystickNumAxes(stick);
    Sint16 lx = axes > kSwitchAxisLeftX ? SDL_JoystickGetAxis(stick, kSwitchAxisLeftX) : 0;
    Sint16 ly = axes > kSwitchAxisLeftY ? SDL_JoystickGetAxis(stick, kSwitchAxisLeftY) : 0;
    pad->left_x = lx;
    /* Desktop gamepad state uses up-positive Y; SDL joysticks are
     * down-positive. */
    pad->left_y = (ly == INT16_MIN) ? INT16_MAX : (int16_t)-ly;
    pad->right_x = axes > kSwitchAxisRightX ? SDL_JoystickGetAxis(stick, kSwitchAxisRightX) : 0;
    Sint16 ry = axes > kSwitchAxisRightY ? SDL_JoystickGetAxis(stick, kSwitchAxisRightY) : 0;
    pad->right_y = (ry == INT16_MIN) ? INT16_MAX : (int16_t)-ry;
    /* ZL/ZR are digital on Switch hardware; report them as full-scale
     * trigger axes so the shared L/R trigger bindings fire. */
    pad->left_trigger = SwitchTriggerAsAxis(stick, kSwitchBtnZL);
    pad->right_trigger = SwitchTriggerAsAxis(stick, kSwitchBtnZR);
    /* Left stick doubles as a DPad past the deflection threshold so the
     * game is fully playable without touching the DPad. */
    if (lx < -DKC3_SWITCH_STICK_DPAD_DEADZONE) buttons |= kDkc3GamepadDpadLeft;
    if (lx > DKC3_SWITCH_STICK_DPAD_DEADZONE) buttons |= kDkc3GamepadDpadRight;
    if (ly < -DKC3_SWITCH_STICK_DPAD_DEADZONE) buttons |= kDkc3GamepadDpadUp;
    if (ly > DKC3_SWITCH_STICK_DPAD_DEADZONE) buttons |= kDkc3GamepadDpadDown;
    pad->buttons = buttons;
    /* Edge log: proves hardware events reach the host (visible with
     * log.flag file logging or nxlink). */
    {
      static uint32_t s_last_buttons[kSwitchPadCapacity];
      if (buttons != s_last_buttons[i]) {
        s_last_buttons[i] = buttons;
        fprintf(stderr,
                "[SwitchPad] pad%d buttons=0x%08x lt=%u rt=%u lx=%d ly=%d\n",
                i, buttons, pad->left_trigger, pad->right_trigger,
                pad->left_x, pad->left_y);
      }
    }
  }
  return reported;
}

#endif /* __SWITCH__ */
