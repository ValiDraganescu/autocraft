// The leveling tracking database on its own thread (docs/leveling.md,
// "Tracking"): the game thread builds a game's rows (`ac::TrackedGame`, a
// copy of the game as it stands) and queues them; one worker opens the file
// on its first write and upserts each game in the order queued, so the sim
// steps on while SQLite writes. A failed open or write is logged once per
// message, and the game goes on untracked.
//
// Made by `UAcSimSubsystem` for a real window game only: a fixture,
// `-AcNoSave` (every hidden shot and test run), `-AcRunFor` and the
// playground never make one.
#pragma once

#include "CoreMinimal.h"

#include "Tracking.h"
#include "TrackingStore.h"

#include <deque>
#include <memory>

class AUTOCRAFT_API FAcTracker
{
public:
	/// Writes to the database at `InPath` (made on the first write).
	explicit FAcTracker(FString InPath);
	/// Waits for the writes queued so far.
	~FAcTracker();

	/// The build (the game module's binary date) and the OS, as every row
	/// of this run carries them.
	const std::string& Build() const { return BuildText; }
	const std::string& OS() const { return OSText; }

	/// Queues `Game` to be written; returns at once.
	void Write(ac::TrackedGame Game);
	/// Waits until every game queued so far is written.
	void Finish();

	/// Write times so far, milliseconds on the worker: the count, the last
	/// and the slowest (the first includes opening the file).
	struct FStats
	{
		int32 Writes = 0;
		int32 Failures = 0;
		double LastMs = 0.0;
		double MaxMs = 0.0;
		double TotalMs = 0.0;
	};
	FStats Stats() const;

	const FString& Path() const { return DatabasePath; }

private:
	void Drain();

	FString DatabasePath;
	std::string BuildText;
	std::string OSText;

	mutable FCriticalSection Lock;
	std::deque<ac::TrackedGame> Queue;
	bool bRunning = false;
	bool bBroken = false;
	FStats Counted;
	/// Used only by the one running `Drain`.
	std::unique_ptr<ac::TrackingStore> Store;
	TSet<FString> Logged;
};
