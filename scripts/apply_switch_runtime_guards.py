"""Re-apply Switch-only guards to DKC3's adapted runtime copies.

Upstream's apply_dkc3_runtime_patches.py regenerates patched-runtime/ from
the pinned snesrecomp plus cmake/runtime-patches/*.hunks. Two Switch cuts
cannot live in the submodule (the hunk applier fails closed on drift), so
they are applied here, after adaptation, with the same exact-count policy:

  1. tier2_record() early-return under __SWITCH__ (skips the per-LLE-call
     coverage table + RAM-routine snapshots; desktop tooling only).
  2. __attribute__((unused)) on tier2_verbose() (its only Switch-reachable
     caller is the desktop branch of the manifest writer).

Usage: apply_switch_runtime_guards.py --patched <patched-runtime dir>
Exits nonzero without writing anything unless every anchor matches exactly
once, so a moved upstream runtime fails the build instead of silently
dropping the guard.
"""

from __future__ import annotations

import argparse
import pathlib

T2_ANCHOR = (
    "static void tier2_record(uint32_t site, uint32_t target, uint8_t mx,\n"
    "                         uint8_t kind, int clean) {"
)
T2_GUARD = (
    "#ifdef __SWITCH__\n"
    "    /* Production handheld: the coverage table, RAM-routine snapshots,\n"
    "     * and JSONL journal feed desktop AOT burn-down tooling only.\n"
    "     * Nothing on Switch reads them, so skip the per-LLE-call\n"
    "     * bookkeeping entirely. */\n"
    "    (void)site;\n"
    "    (void)target;\n"
    "    (void)mx;\n"
    "    (void)kind;\n"
    "    (void)clean;\n"
    "    return;\n"
    "#endif\n"
)
VERBOSE_OLD = "static int tier2_verbose(void) {"
VERBOSE_NEW = "static int __attribute__((unused)) tier2_verbose(void) {"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--patched", required=True)
    args = ap.parse_args()
    bridge = pathlib.Path(args.patched) / "interp_bridge.c"
    data = bridge.read_bytes()
    nl = "\r\n" if b"\r\n" in data[:2000] else "\n"

    anchor = T2_ANCHOR.replace("\n", nl).encode()
    if data.count(anchor) != 1:
        print(f"switch guards: tier2_record anchor found "
              f"{data.count(anchor)}x (expected 1); refusing to patch",
              flush=True)
        return 1
    guard = T2_GUARD.replace("\n", nl).encode()
    data = data.replace(anchor, anchor + nl.encode() + guard, 1)

    if data.count(VERBOSE_OLD.encode()) != 1:
        print("switch guards: tier2_verbose anchor not unique; "
              "refusing to patch", flush=True)
        return 1
    data = data.replace(VERBOSE_OLD.encode(), VERBOSE_NEW.encode(), 1)

    bridge.write_bytes(data)
    print(f"switch guards: tier2_record + tier2_verbose guarded in {bridge}",
          flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
