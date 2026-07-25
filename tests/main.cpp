// The doctest entry point, on its own so the 7,000-line header is compiled with
// its implementation exactly once. Every other test file includes the header
// without the define and compiles in a fraction of the time.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
