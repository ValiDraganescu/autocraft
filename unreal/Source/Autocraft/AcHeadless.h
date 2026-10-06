// Headless runs (agents' test and shot runs: `-RenderOffscreen`, `-nullrhi`
// or `-AcHeadless`) must never touch the user's mouse or focus: no capture,
// no lock, no cursor warp. The pawns ask `AcHeadless::Is()` before changing
// the input mode, and `AcHeadless::FreeMouse` turns the viewport's own
// capture-on-launch off.
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace AcHeadless
{
	inline bool Is()
	{
		static const bool bHeadless = FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"))
			|| FParse::Param(FCommandLine::Get(), TEXT("nullrhi"))
			|| FParse::Param(FCommandLine::Get(), TEXT("AcHeadless"));
		return bHeadless;
	}

	inline void FreeMouse(const UWorld* World)
	{
		if (!Is() || !World) return;
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
			Viewport->SetMouseLockMode(EMouseLockMode::DoNotLock);
			Viewport->SetHideCursorDuringCapture(false);
		}
	}
}
