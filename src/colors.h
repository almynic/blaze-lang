#ifndef blaze_colors_h
#define blaze_colors_h

#include <stdbool.h>

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_DIM     "\033[2m"

// Foreground colors
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_GRAY    "\033[90m"

// Bright foreground colors
#define COLOR_BRIGHT_RED     "\033[91m"
#define COLOR_BRIGHT_GREEN   "\033[92m"
#define COLOR_BRIGHT_YELLOW  "\033[93m"
#define COLOR_BRIGHT_BLUE    "\033[94m"
#define COLOR_BRIGHT_MAGENTA "\033[95m"
#define COLOR_BRIGHT_CYAN    "\033[96m"

// Check if colors are supported (basic check for TTY)
bool colorsEnabled(void);

// Enable/disable colors globally
void enableColors(bool enable);

// Helper macros for colored output
#define BOLD(str) COLOR_BOLD str COLOR_RESET
#define RED(str) COLOR_RED str COLOR_RESET
#define GREEN(str) COLOR_GREEN str COLOR_RESET
#define YELLOW(str) COLOR_YELLOW str COLOR_RESET
#define BLUE(str) COLOR_BLUE str COLOR_RESET
#define CYAN(str) COLOR_CYAN str COLOR_RESET
#define GRAY(str) COLOR_GRAY str COLOR_RESET

#endif
