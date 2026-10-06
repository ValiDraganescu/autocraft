// Force-included into every AutocraftCore file by AutocraftCore.Build.cs
// (Unreal builds only; core-tests never sees it). The engine-free core has no
// AUTOCRAFTCORE_API macros, and Unreal's Mac toolchain hides functions by
// default (-fvisibility-ms-compat), so in the modular editor build the game
// module could not link against the core's functions. This makes every
// symbol the core defines visible to the modules that use it.
#pragma once
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC visibility push(default)
#endif
