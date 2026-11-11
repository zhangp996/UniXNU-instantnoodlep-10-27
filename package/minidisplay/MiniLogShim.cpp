/* The HakiOS Mini* sources log through hakios_tl and consult the loader's recording buffer. This
 * port has neither, so provide those symbols here and leave the sources unmodified.
 *
 * MiniIOSurfaceDrawn() is the one real gap: the framebuffer asks it whether a layer's surface has
 * been drawn and waits up to a few milliseconds otherwise. The definition lives outside the files
 * this port carries, so answer "ready" and let the copy happen immediately. */
#include <sys/systm.h>
#include <stdarg.h>
#include "MiniIOSurface.h"

extern "C" void
hakios_tl(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	printf("MINI: ");
	vprintf(format, arguments);
	va_end(arguments);
}

/* osfmk/arm64/loader_return.c's recording buffer: { physical address, bytes, kernel address }. */
extern "C" uint64_t loader_recording[3] = { 0, 0, 0 };

extern "C" void
loader_log_line(const char *line)
{
	printf("LOADER: %s\n", line ? line : "");
}

bool
MiniIOSurfaceDrawn(MiniIOSurface *surface, uint64_t milliseconds)
{
	(void)surface;
	(void)milliseconds;
	return true;
}
