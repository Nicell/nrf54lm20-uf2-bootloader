#pragma once
#include <stdio.h>
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...) do { if (getenv("VERBOSE")) { printf("  [inf] " __VA_ARGS__); printf("\n"); } } while (0)
#define LOG_ERR(...) do { if (getenv("VERBOSE")) { printf("  [err] " __VA_ARGS__); printf("\n"); } } while (0)
#define LOG_WRN LOG_ERR
#include <stdlib.h>
