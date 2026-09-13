/* Nintendo Switch diagnostics stub for DKC3Recomp.
 *
 * The desktop diagnostics module (diagnostics.c) writes rolling JSON
 * reports, timestamped support bundles, minidumps, and installs POSIX
 * crash handlers â€” none of which fit a seamless handheld build, and its
 * sigaction usage does not compile against newlib/libnx. This stub keeps
 * the sdl_main.c call sites compiling unchanged while doing nothing.
 * Breadcrumbs still reach stderr (nxlink USB log) via host_report.c.
 *
 * Only compiled on __SWITCH__; desktop builds keep diagnostics.c.
 */
#ifdef __SWITCH__

#include "diagnostics.h"

#include <stddef.h>

bool Dkc3DiagnosticsInit(const char *host_name, const char *build_version) {
  (void)host_name;
  (void)build_version;
  return true;
}

void Dkc3DiagnosticsSetPresentation(const char *backend,
                                    const char *screen_filter,
                                    bool audio_available) {
  (void)backend;
  (void)screen_filter;
  (void)audio_available;
}

void Dkc3DiagnosticsHeartbeat(uint64_t frame, uint32_t resume_pc) {
  (void)frame;
  (void)resume_pc;
}

void Dkc3DiagnosticsFatal(const char *message) {
  (void)message;
}

void Dkc3DiagnosticsShutdown(const char *outcome) {
  (void)outcome;
}

#endif /* __SWITCH__ */
