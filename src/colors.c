#include "colors.h"
#include <unistd.h>
#include <stdio.h>

static bool colors_enabled = true;

bool colorsEnabled(void) {
    // Check if stdout is a TTY and colors are enabled
    return colors_enabled && isatty(fileno(stderr));
}

void enableColors(bool enable) {
    colors_enabled = enable;
}
