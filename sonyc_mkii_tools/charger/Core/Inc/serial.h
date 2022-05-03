#pragma once

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

void debug_printf(const char * restrict fmt, ...) __attribute__ ((format (printf, 1, 2)));
