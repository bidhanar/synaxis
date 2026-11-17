// Microsoft's WebView2.h includes "EventToken.h"; MinGW-w64 names the file
// eventtoken.h. On Windows' case-insensitive file system that's the same
// file, but not when cross-compiling from Linux.
#pragma once
#include <eventtoken.h>
