// `AcWatchdog`: the stall watchdog (GAME-LAYER.md §2.14 "Log", chunk D10),
// the Unreal side of the Swift `Log.Watchdog` (Sources/Autocraft/Log.swift).
//
// A thread wakes every 50 ms and checks the game thread's heartbeat (beaten
// at the start and the end of every engine frame). When the game thread has
// not beaten for `ac.StallMs` (250 ms), a stall begins; when it beats again
// the game thread logs
//   game thread stalled 612 ms (during: scene; now: idle)
// with the activity it was doing when the stall began and the one it is
// doing now (`FAcActivity`, set by `AC_PERF_SCOPE` and by anyone who wants a
// label). Once a stall passes `ac.StallStackMs` (500 ms), the watchdog
// suspends the game thread for a moment, walks its frame-pointer chain and
// logs the stack (symbolicated after the thread resumes), so a freeze says
// what blocks even when it never ends:
//   stall: game thread stack at 500 ms (during: scene):
//     #0 __psynch_cvwait (libsystem_kernel.dylib)
//     #1 UAcWorldRenderer::Sync(...) (UnrealEditor-Autocraft.dylib) AcWorldRenderer.cpp:120
//
// Runs in the game (`-game`, packaged), not in the editor. `ac.Stall MS`
// sleeps the game thread that long, to see it work.
#pragma once

#include "CoreMinimal.h"

namespace AcWatchdog
{
	/// Start the thread (game thread; once, later calls do nothing). Off in
	/// the editor and in commandlets.
	void Start();
	/// Stop and join the thread.
	void Stop();
	/// The game thread is alive (called by the engine's frame delegates).
	void Beat();
	/// True while the thread runs.
	bool IsRunning();
	/// Stalls seen since launch.
	int32 StallCount();
}

/// Label what the game thread does for stall reports (`Log.during`): the
/// label holds until the scope ends. `Label` must outlive the scope (a
/// literal, or a static string).
struct AUTOCRAFT_API FAcActivity
{
	explicit FAcActivity(const TCHAR* Label);
	~FAcActivity();
	FAcActivity(const FAcActivity&) = delete;
	FAcActivity& operator=(const FAcActivity&) = delete;

	/// The game thread's label now ("idle" outside any scope).
	static const TCHAR* Current();

private:
	const TCHAR* Previous;
};
