// The music (GAME-LAYER.md §2.11, chunk C8): the port of the Swift
// `MusicPlayer` (Sources/Autocraft/MusicPlayer.swift, docs/music.md), on the
// air as KSTR 88.7 Stardust.
//
// The game's own soundtrack (the MP3s in Sources/Autocraft/Resources/Sounds/
// music) and the user's (~/Library/Application Support/Autocraft/Audio/music;
// a file with an own track's name takes its place) play shuffled through the
// core's `ac::MusicQueue`, streamed from disk by `UAcMp3Wave` (AcMp3Loader.h).
// No crossfades: a 6 s gap between songs; after a song that played out (not
// one skipped) 1.5 s, one of the station's ads (the imported
// `/Game/Audio/Ads` waves, +3 dB, their own queue), 2 s. Previous within 3 s
// of a track's start goes back a track. Titles and covers come from the ID3
// tags (TIT2, APIC), else a sidecar .jpg/.png, else a drawn planet (the ads:
// the station's card). The keys: [ and ] vote, F7/F8/F9 previous, pause,
// next (a Slate input pre-processor, so they work in every mode, as Swift's).
// Plays and votes go to a JSON-lines log until the tracking database exists:
// ~/Library/Application Support/Autocraft/Unreal/music.jsonl.
//
// Command line: -AcNoMusic (silent), -AcMusicSeed=N (a fixed shuffle).
// Console: `ac.Music next|previous|toggle|up|down|status`.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "MusicQueue.h"
#include "Noise.h"

#include <optional>

#include "AcMusicPlayer.generated.h"

class UAudioComponent;
class USoundBase;
class UTexture2D;

/// A press on the music player (Swift `MusicCommand`).
enum class EAcMusicCommand : uint8
{
	Down,
	Previous,
	Toggle,
	Next,
	Up,
};

/// The track playing, as the music deck (D9) shows it: `Position` seconds
/// in at `At` (the player's clock, `UAcMusicPlayer::Now`), running on from
/// there while `bPlaying` (not paused, not between tracks).
struct FAcNowPlaying
{
	FString Track;
	FString Title;
	/// The cover (256 px or so); null until it is read.
	TWeakObjectPtr<UTexture2D> Cover;
	/// The cover is the station's card: draw "KSTR" and "88.7 STARDUST" on it.
	bool bStationCover = false;
	double Length = 0;
	double Position = 0;
	double At = 0;
	bool bPlaying = false;
	bool bPaused = false;
	/// 1 up, -1 down, 0 none.
	int32 Vote = 0;
	/// One of the station's ads (or its ident), not a song.
	bool bAd = false;

	double PositionAt(double Now) const { return FMath::Min(Length, Position + (bPlaying ? FMath::Max(0.0, Now - At) : 0.0)); }
};

/// What an MP3's ID3v2 tag says (title, embedded picture).
struct FAcTrackTags
{
	FString Title;
	FString Artist;
	TArray<uint8> Picture;
	FString PictureMime;
};

UCLASS()
class AUTOCRAFT_API UAcMusicPlayer : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/// The station, on the faceplate.
	static constexpr const TCHAR* Station = TEXT("KSTR 88.7 Stardust");
	/// The pause between songs, seconds; before an ad and after one.
	static constexpr double Gap = 6.0;
	static constexpr double BeforeAd = 1.5;
	static constexpr double AfterAd = 2.0;
	/// The ads play this much over the songs (+3 dB).
	static constexpr float AdGain = 1.41f;

	static UAcMusicPlayer* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bReady; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	void Press(EAcMusicCommand Command);

	/// The track shown (the one loaded, else the next up); unset with none.
	TOptional<FAcNowPlaying> NowPlaying() const;
	/// Bumped at every change the deck shows, and every 10 s.
	int32 Revision() const { return Rev; }
	/// The player's clock (seconds of ticks), for `FAcNowPlaying::PositionAt`.
	double Now() const { return Clock; }
	bool IsPaused() const { return bPaused; }
	/// Own and folder tracks on hand.
	int32 OwnCount() const { return Own; }
	int32 FolderCount() const { return Folder; }
	/// Silence (the menu's Sound toggle); the station keeps playing.
	void SetMuted(bool bInMuted);

	/// A track's title: its tag, else the file name in title case
	/// (`aftermath_1` → "Aftermath 1").
	static FString TitleFromId(const FString& Id);
	/// Read an MP3's ID3v2 tag (v2.3 and v2.4). False when it has none.
	static bool ReadTags(const FString& Path, FAcTrackTags& Out);
	/// The soundtrack's folder (the Swift game's Resources/Sounds/music).
	static FString OwnMusicFolder();
	/// The station's card (the ads' cover, without its letters: the deck
	/// draws "KSTR" on it), a new texture in `Outer` (D9's sample shots).
	static UTexture2D* MakeStationCover(UObject* Outer);

private:
	struct FTrack
	{
		FString Path;
		/// The file's name without its extension: plays and votes go under it.
		FString Id;
		/// "own", "folder" or "ad".
		FString Source;
		/// An ad's imported wave (its asset path).
		FString Asset;
	};
	/// What `ac::MusicQueue` shuffles: an index into `Tracks` or `Ads`.
	using FQueue = ac::MusicQueue<int32>;

	struct FPlay
	{
		FString Track, Title, Source;
		double Length = 0;
		FDateTime Started;
		double Played = 0, Heard = 0;
		FString Ended;
	};

	void Advance();
	void NextSong();
	void NextAd();
	void Start();
	bool Load(bool bAd, int32 Index);
	void Ended(int32 ForGeneration);
	void Finish(const TCHAR* How);
	void Toggle();
	void Vote(int32 V);
	double Position() const;
	bool IsPlaying() const;
	bool OnAd() const;
	const FTrack* LoadedTrack() const;
	FString TitleOf(const FTrack& T) const;
	void ReadInfo(const FTrack& T);
	void Changed() { ++Rev; LastSync = Clock; }
	void StopVoice();
	void Record(const TSharedRef<class FJsonObject>& Line) const;
	void ReadVotes();
	UWorld* PlayWorld() const;
	static FString LogPath();

	TArray<FTrack> Tracks;
	TArray<FTrack> Ads;
	std::optional<FQueue> Queue;
	std::optional<FQueue> AdQueue;
	bool bAdNext = false;
	int32 Own = 0, Folder = 0;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Voice;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> Sound;
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Covers;
	TSet<FString> StationCovers;

	int32 Generation = 0;
	/// The track in the voice: ad or song, its index, its length.
	struct FLoaded { bool bAd = false; int32 Index = 0; double Length = 0; };
	TOptional<FLoaded> Loaded;
	/// Seconds played of the track loaded (ticks while it plays).
	double Played = 0;
	bool bPaused = false;
	bool bMuted = false;
	/// The voice's volume: the song's or ad's gain × the settings (E7).
	float VoiceGain(bool bAd) const;
	float AppliedGain = -1.f;
	TOptional<double> GapUntil;
	TOptional<FPlay> Play;
	TMap<FString, FString> Titles;
	TMap<FString, int32> Votes;
	int32 Rev = 0;
	double LastSync = 0;
	double Clock = 0;
	bool bReady = false;
	bool bSilent = false;
	bool bStarted = false;
	/// A finish that arrived from the audio side, handled on the next tick.
	TOptional<int32> PendingEnd;
	uint64 Seed = 0;
	bool bSeeded = false;
	/// -AcMusicSeed: the shuffles from this generator (else the system's).
	std::optional<ac::SeededRandom> Rng;

	TSharedPtr<class IInputProcessor> Keys;
	class IConsoleObject* Command = nullptr;
};
