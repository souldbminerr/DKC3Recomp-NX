#ifndef DKC3_SWITCH_GAMEPAD_H
#define DKC3_SWITCH_GAMEPAD_H

#include <stddef.h>

#include "desktop_input.h"

/* Raw SDL_Joystick gamepad path for Nintendo Switch homebrew.
 *
 * The Switch portlibs SDL2 joystick driver exposes each controller with
 * HID-ordered button indices (see the devkitPro sdl2-simple/sdl2-demo
 * examples), and there is no mapping database to rely on in applet mode,
 * so the desktop SDL_GameController layer is bypassed here. Buttons are
 * reported with SDL label semantics (A = the A-labelled button), and the
 * Switch pad bindings map them straight through (A->A, B->B, X->X, Y->Y):
 * Switch labels already sit at the SNES positions, unlike the crossed
 * Xbox-physical mapping the desktop defaults use. Plus -> Start,
 * Minus -> Select, ZL/ZR (digital on Switch) -> full-scale trigger axes
 * -> SNES L/R.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Open up to `capacity` joysticks (Joy-Cons, Pro Controller). Returns the
 * number opened. Safe to call repeatedly; already-open sticks are kept. */
int Dkc3SwitchRefreshPads(void);

/* Close every stick opened by Dkc3SwitchRefreshPads. */
void Dkc3SwitchClosePads(void);

/* Fill up to `capacity` Dkc3GamepadState entries from the open sticks.
 * Returns the number of attached sticks reported. */
size_t Dkc3SwitchReadPads(Dkc3GamepadState *out, size_t capacity);

/* Left-stick deflection that counts as a DPad press (raw SDL units). */
#define DKC3_SWITCH_STICK_DPAD_DEADZONE 8000

#ifdef __cplusplus
}
#endif

#endif
