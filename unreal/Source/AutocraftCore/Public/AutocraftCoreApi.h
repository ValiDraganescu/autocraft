// AUTOCRAFTCORE_API exports the core's classes and functions from its module
// in Unreal's modular (editor) builds, where Unreal defines it: on Windows a
// DLL exports nothing unmarked, and on the Mac the toolchain hides functions
// by default. A packaged game is one executable, and there it is empty.
// core-tests build without Unreal, so it is empty there too.
#pragma once

// Unreal defines AUTOCRAFTCORE_API as DLLEXPORT or DLLIMPORT, which its
// platform headers define; the core includes none of them, so these are the
// same as the platform headers' (WindowsPlatform.h, ApplePlatform.h).
#if defined(AUTOCRAFTCORE_API) && !defined(DLLEXPORT)
#if defined(_WIN32)
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#else
#define DLLEXPORT __attribute__((visibility("default")))
#define DLLIMPORT __attribute__((visibility("default")))
#endif
#endif

#ifndef AUTOCRAFTCORE_API
#define AUTOCRAFTCORE_API
#endif
