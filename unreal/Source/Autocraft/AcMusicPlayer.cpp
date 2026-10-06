// See AcMusicPlayer.h. Swift: Sources/Autocraft/MusicPlayer.swift.
#include "AcMusicPlayer.h"

#include "AcAudioDirector.h"
#include "AcHudStyle.h"
#include "AcLog.h"
#include "AcMp3Loader.h"

#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundWave.h"

#include "Noise.h"

#include <cmath>

namespace
{
	/// The music keys (`GameView.musicKeys`): [ and ] vote, F7-F9 go back,
	/// pause and skip. Plain presses only, no repeats.
	class FAcMusicKeys final : public IInputProcessor
	{
	public:
		explicit FAcMusicKeys(TWeakObjectPtr<UAcMusicPlayer> InPlayer) : Player(InPlayer) {}

		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			const FModifierKeysState& M = Event.GetModifierKeys();
			if (M.IsCommandDown() || M.IsAltDown() || M.IsControlDown()) return false;
			const FKey Key = Event.GetKey();
			TOptional<EAcMusicCommand> C;
			if (Key == EKeys::LeftBracket) C = EAcMusicCommand::Down;
			else if (Key == EKeys::RightBracket) C = EAcMusicCommand::Up;
			else if (Key == EKeys::F7) C = EAcMusicCommand::Previous;
			else if (Key == EKeys::F8) C = EAcMusicCommand::Toggle;
			else if (Key == EKeys::F9) C = EAcMusicCommand::Next;
			if (!C) return false;
			if (!Event.IsRepeat())
			{
				if (UAcMusicPlayer* P = Player.Get()) P->Press(*C);
			}
			return true;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("AcMusicKeys"); }

	private:
		TWeakObjectPtr<UAcMusicPlayer> Player;
	};

	/// Swift's `NSColor(calibratedHue:saturation:brightness:alpha:)`.
	FLinearColor Hsb(double H, double S, double B, double A = 1)
	{
		H = H - std::floor(H);
		const double I = std::floor(H * 6), F = H * 6 - I;
		const double P = B * (1 - S), Q = B * (1 - S * F), T = B * (1 - S * (1 - F));
		double R = 0, G = 0, Bl = 0;
		switch (static_cast<int>(I) % 6)
		{
		case 0: R = B; G = T; Bl = P; break;
		case 1: R = Q; G = B; Bl = P; break;
		case 2: R = P; G = B; Bl = T; break;
		case 3: R = P; G = Q; Bl = B; break;
		case 4: R = T; G = P; Bl = B; break;
		default: R = B; G = P; Bl = Q; break;
		}
		return FLinearColor(R, G, Bl, A);
	}

	/// A 256-pixel canvas with Core Graphics' axes (y up), in sRGB values.
	struct FCanvas
	{
		static constexpr int32 Side = 256;
		TArray<FLinearColor> Px;
		FCanvas() { Px.Init(FLinearColor::Black, Side * Side); }

		void Blend(int32 X, int32 Y, const FLinearColor& C, double Cover = 1)
		{
			if (X < 0 || Y < 0 || X >= Side || Y >= Side) return;
			const float A = FMath::Clamp(static_cast<float>(C.A * Cover), 0.f, 1.f);
			FLinearColor& D = Px[(Side - 1 - Y) * Side + X];
			D = FLinearColor(D.R + (C.R - D.R) * A, D.G + (C.G - D.G) * A, D.B + (C.B - D.B) * A, 1);
		}

		/// Gradient stops at `Locs`, `T` 0…1 (clamped: drawn past both ends).
		static FLinearColor Ramp(const TArray<FLinearColor>& Cs, const TArray<double>& Locs, double T)
		{
			T = FMath::Clamp(T, 0.0, 1.0);
			for (int32 I = 1; I < Cs.Num(); ++I)
			{
				if (T <= Locs[I])
				{
					const double U = (T - Locs[I - 1]) / FMath::Max(Locs[I] - Locs[I - 1], 1e-9);
					return FMath::Lerp(Cs[I - 1], Cs[I], static_cast<float>(U));
				}
			}
			return Cs.Last();
		}

		/// A stroke `Width` wide along the points of `Line`.
		void Stroke(const TArray<FVector2D>& Line, double Width, const FLinearColor& C)
		{
			for (int32 Y = 0; Y < Side; ++Y)
			{
				for (int32 X = 0; X < Side; ++X)
				{
					const FVector2D P(X + 0.5, Y + 0.5);
					double Best = 1e9;
					for (int32 I = 1; I < Line.Num(); ++I)
					{
						Best = FMath::Min(Best, FMath::PointDistToSegment(FVector(P, 0), FVector(Line[I - 1], 0), FVector(Line[I], 0)));
					}
					const double Cover = FMath::Clamp(Width / 2 + 0.5 - Best, 0.0, 1.0);
					if (Cover > 0) Blend(X, Y, C, Cover);
				}
			}
		}

		TArray<FColor> Colors() const
		{
			TArray<FColor> Out;
			Out.SetNumUninitialized(Px.Num());
			for (int32 I = 0; I < Px.Num(); ++I)
			{
				const FLinearColor& C = Px[I];
				Out[I] = FColor(FMath::Clamp(FMath::RoundToInt(C.R * 255), 0, 255), FMath::Clamp(FMath::RoundToInt(C.G * 255), 0, 255),
					FMath::Clamp(FMath::RoundToInt(C.B * 255), 0, 255), 255);
			}
			return Out;
		}
	};

	TArray<FVector2D> Arc(FVector2D C, double R, double From, double To)
	{
		TArray<FVector2D> Out;
		for (int32 I = 0; I <= 32; ++I)
		{
			const double A = From + (To - From) * I / 32.0;
			Out.Add(C + FVector2D(std::cos(A), std::sin(A)) * R);
		}
		return Out;
	}

	/// A cover for a track without one (`MusicPlayer.drawnCover`): a planet
	/// rising over dunes under a night sky, its colours from the title.
	TArray<FColor> DrawnCover(const FString& Title)
	{
		uint64 Seed = 1469598103934665603ull;
		for (const TCHAR Ch : Title) Seed = (Seed ^ static_cast<uint64>(Ch)) * 1099511628211ull;
		ac::SeededRandom G(Seed);
		const double Hue = G.unit();
		const double S = FCanvas::Side;
		FCanvas Cv;
		const TArray<FLinearColor> Sky{Hsb(Hue, 0.7, 0.08), Hsb(Hue + 0.08, 0.6, 0.3), Hsb(Hue + 0.15, 0.55, 0.62)};
		for (int32 Y = 0; Y < FCanvas::Side; ++Y)
		{
			// From the top (t 0) down to 0.3 of the height (t 1), and on below.
			const double T = (S - (Y + 0.5)) / (S - S * 0.3);
			const FLinearColor C = FCanvas::Ramp(Sky, {0, 0.6, 1}, T);
			for (int32 X = 0; X < FCanvas::Side; ++X) Cv.Blend(X, Y, C);
		}
		for (int32 I = 0; I < 60; ++I)
		{
			const double R = G.range(0.4, 1.4);
			const double A = G.range(0.3, 0.9);
			const double X0 = G.unit() * S, Y0 = S * 0.45 + G.unit() * S * 0.55;
			const FVector2D Ctr(X0 + R, Y0 + R);
			for (int32 Y = FMath::FloorToInt(Y0) - 1; Y <= FMath::CeilToInt(Y0 + 2 * R) + 1; ++Y)
			{
				for (int32 X = FMath::FloorToInt(X0) - 1; X <= FMath::CeilToInt(X0 + 2 * R) + 1; ++X)
				{
					const double D = FVector2D::Distance(FVector2D(X + 0.5, Y + 0.5), Ctr);
					Cv.Blend(X, Y, FLinearColor(1, 1, 1, A), FMath::Clamp(R + 0.5 - D, 0.0, 1.0));
				}
			}
		}
		// The planet, lit from one side, with a thin ring of haze.
		const FVector2D P(S * G.range(0.3, 0.7), S * 0.52);
		const double R = S * G.range(0.2, 0.3);
		const TArray<FLinearColor> Body{Hsb(Hue + 0.5, 0.35, 0.95), Hsb(Hue + 0.55, 0.6, 0.35), Hsb(Hue + 0.6, 0.7, 0.08)};
		const FVector2D Light(P.X - R * 0.4, P.Y + R * 0.4);
		for (int32 Y = 0; Y < FCanvas::Side; ++Y)
		{
			for (int32 X = 0; X < FCanvas::Side; ++X)
			{
				const FVector2D Q(X + 0.5, Y + 0.5);
				const double Cover = FMath::Clamp(R + 0.5 - FVector2D::Distance(Q, P), 0.0, 1.0);
				if (Cover <= 0) continue;
				// The radial gradient from the lit point (radius 0) to the
				// centre (radius 1.2 r): how far along between the circles.
				const double T = FVector2D::Distance(Q, Light) / (R * 1.2 + FVector2D::Distance(Light, P) * 0.5);
				Cv.Blend(X, Y, FCanvas::Ramp(Body, {0, 0.55, 1}, T), Cover);
			}
		}
		Cv.Stroke(Arc(P, R + 3, 0, 2 * UE_DOUBLE_PI), 3, Hsb(Hue + 0.5, 0.3, 1, 0.35));
		// Two rows of dunes.
		for (int32 K = 0; K < 2; ++K)
		{
			const double Base = S * (K == 0 ? 0.34 : 0.2);
			TArray<double> Top;
			Top.Init(Base, FCanvas::Side);
			double X = 0;
			while (X < S)
			{
				const double W = S * G.range(0.25, 0.45), H = S * G.range(0.03, 0.09);
				// The quadratic from (X, Base) through control (X + 0.6 W, Base + 2 H) to (X + W, Base).
				for (int32 I = 0; I <= 64; ++I)
				{
					const double T = I / 64.0;
					const double Px = (1 - T) * (1 - T) * X + 2 * (1 - T) * T * (X + W * 0.6) + T * T * (X + W);
					const double Py = (1 - T) * (1 - T) * Base + 2 * (1 - T) * T * (Base + H * 2) + T * T * Base;
					const int32 Col = FMath::FloorToInt(Px);
					if (Col >= 0 && Col < FCanvas::Side) Top[Col] = Py;
				}
				X += W;
			}
			const FLinearColor C = Hsb(Hue + 0.05, 0.5, K == 0 ? 0.16 : 0.07);
			for (int32 Col = 0; Col < FCanvas::Side; ++Col)
			{
				for (int32 Y = 0; Y < FCanvas::Side; ++Y)
				{
					const double Cover = FMath::Clamp(Top[Col] - Y, 0.0, 1.0);
					if (Cover > 0) Cv.Blend(Col, Y, C, Cover);
				}
			}
		}
		return Cv.Colors();
	}

	/// The station's card behind "KSTR" (`MusicPlayer.stationCover`): radio
	/// waves rising from a mast. The deck draws the letters on it.
	TArray<FColor> StationCover()
	{
		FCanvas Cv;
		const double S = FCanvas::Side;
		const TArray<FLinearColor> Back{FLinearColor(0.2f, 0.06f, 0.3f), FLinearColor(0.03f, 0.04f, 0.12f)};
		for (int32 Y = 0; Y < FCanvas::Side; ++Y)
		{
			const FLinearColor C = FCanvas::Ramp(Back, {0, 1}, (S - Y) / S);
			for (int32 X = 0; X < FCanvas::Side; ++X) Cv.Blend(X, Y, C);
		}
		const FVector2D Base(S / 2, S * 0.12);
		const FLinearColor Orchid(0.95f, 0.55f, 1.f, 0.9f);
		Cv.Stroke({FVector2D(Base.X - 26, Base.Y), FVector2D(Base.X, S * 0.42), FVector2D(Base.X + 26, Base.Y)}, 5, Orchid);
		for (int32 K = 1; K <= 3; ++K)
		{
			FLinearColor C = Orchid;
			C.A = 0.75f - K * 0.18f;
			Cv.Stroke(Arc(FVector2D(Base.X, S * 0.42), K * 28.0, UE_DOUBLE_PI * 0.2, UE_DOUBLE_PI * 0.8), 4, C);
		}
		return Cv.Colors();
	}

	UTexture2D* Texture(const TArray<FColor>& Pixels, UObject* Outer)
	{
		UTexture2D* T = FAcHudStyle::HudTexture(FImageView(Pixels.GetData(), FCanvas::Side, FCanvas::Side));
		if (T && Outer) T->Rename(nullptr, Outer, REN_DontCreateRedirectors | REN_NonTransactional);
		return T;
	}

	// MARK: ID3

	uint32 BigEndian(const uint8* B) { return (uint32(B[0]) << 24) | (uint32(B[1]) << 16) | (uint32(B[2]) << 8) | uint32(B[3]); }
	uint32 SyncSafe(const uint8* B) { return (uint32(B[0] & 0x7f) << 21) | (uint32(B[1] & 0x7f) << 14) | (uint32(B[2] & 0x7f) << 7) | uint32(B[3] & 0x7f); }

	/// Text in one of ID3's encodings: 0 Latin-1, 1 UTF-16 with a BOM,
	/// 2 UTF-16BE, 3 UTF-8. Stops at the first terminator.
	FString Id3Text(uint8 Encoding, const uint8* B, int32 N)
	{
		FString Out;
		if (Encoding == 1 || Encoding == 2)
		{
			bool bBig = Encoding == 2;
			int32 I = 0;
			if (Encoding == 1 && N >= 2)
			{
				if (B[0] == 0xFE && B[1] == 0xFF) { bBig = true; I = 2; }
				else if (B[0] == 0xFF && B[1] == 0xFE) { bBig = false; I = 2; }
			}
			TArray<UTF16CHAR> Units;
			for (; I + 1 < N; I += 2)
			{
				const UTF16CHAR U = bBig ? UTF16CHAR((B[I] << 8) | B[I + 1]) : UTF16CHAR((B[I + 1] << 8) | B[I]);
				if (U == 0) break;
				Units.Add(U);
			}
			Units.Add(0);
			Out = FString(StringCast<TCHAR>(Units.GetData()).Get());
		}
		else if (Encoding == 3)
		{
			int32 Len = 0;
			while (Len < N && B[Len] != 0) ++Len;
			const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(B), Len);
			Out = FString(Conv.Length(), Conv.Get());
		}
		else
		{
			for (int32 I = 0; I < N && B[I] != 0; ++I) Out.AppendChar(static_cast<TCHAR>(B[I]));
		}
		return Out.TrimStartAndEnd();
	}

	/// Past a terminated string in `Encoding` from `I` (one or two zero bytes).
	int32 SkipText(uint8 Encoding, const uint8* B, int32 I, int32 N)
	{
		if (Encoding == 1 || Encoding == 2)
		{
			while (I + 1 < N && !(B[I] == 0 && B[I + 1] == 0)) I += 2;
			return I + 2;
		}
		while (I < N && B[I] != 0) ++I;
		return I + 1;
	}
}

// MARK: - Tags

bool UAcMusicPlayer::ReadTags(const FString& Path, FAcTrackTags& Out)
{
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
	if (!File || File->TotalSize() < 10) return false;
	uint8 Header[10];
	File->Serialize(Header, 10);
	if (!(Header[0] == 'I' && Header[1] == 'D' && Header[2] == '3')) return false;
	const uint8 Version = Header[3];
	if (Version < 3 || Version > 4) return false;
	const uint32 Size = SyncSafe(Header + 6);
	if (Size == 0 || Size > 64u * 1024 * 1024 || Size + 10 > File->TotalSize()) return false;
	TArray<uint8> Tag;
	Tag.SetNumUninitialized(Size);
	File->Serialize(Tag.GetData(), Size);
	int32 I = 0;
	if (Header[5] & 0x40)
	{
		// An extended header: skip it.
		I = Version == 4 ? SyncSafe(Tag.GetData()) : BigEndian(Tag.GetData()) + 4;
	}
	const int32 N = Tag.Num();
	while (I + 10 <= N)
	{
		const uint8* F = Tag.GetData() + I;
		if (F[0] == 0) break;
		const FString Id(4, reinterpret_cast<const ANSICHAR*>(F));
		const int32 FrameSize = static_cast<int32>(Version == 4 ? SyncSafe(F + 4) : BigEndian(F + 4));
		if (FrameSize <= 0 || I + 10 + FrameSize > N) break;
		const uint8* B = F + 10;
		if (Id == TEXT("TIT2") && Out.Title.IsEmpty())
		{
			Out.Title = Id3Text(B[0], B + 1, FrameSize - 1);
		}
		else if (Id == TEXT("TPE1") && Out.Artist.IsEmpty())
		{
			Out.Artist = Id3Text(B[0], B + 1, FrameSize - 1);
		}
		else if (Id == TEXT("APIC") && Out.Picture.Num() == 0)
		{
			const uint8 Enc = B[0];
			int32 J = 1;
			const int32 MimeStart = J;
			while (J < FrameSize && B[J] != 0) ++J;
			Out.PictureMime = FString(J - MimeStart, reinterpret_cast<const ANSICHAR*>(B + MimeStart));
			J += 1;     // the mime's terminator
			J += 1;     // the picture type
			J = SkipText(Enc, B, J, FrameSize); // the description
			if (J < FrameSize) Out.Picture.Append(B + J, FrameSize - J);
		}
		I += 10 + FrameSize;
	}
	return true;
}

FString UAcMusicPlayer::TitleFromId(const FString& Id)
{
	FString Spaced = Id.Replace(TEXT("_"), TEXT(" ")).Replace(TEXT("-"), TEXT(" "));
	// Swift's `capitalized`: each word's first letter up, the rest down.
	bool bStart = true;
	for (TCHAR& C : Spaced)
	{
		C = bStart ? FChar::ToUpper(C) : FChar::ToLower(C);
		bStart = FChar::IsWhitespace(C);
	}
	return Spaced;
}

UTexture2D* UAcMusicPlayer::MakeStationCover(UObject* Outer)
{
	return Texture(StationCover(), Outer);
}

FString UAcMusicPlayer::OwnMusicFolder()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Resources/Sounds/music")));
}

FString UAcMusicPlayer::LogPath()
{
	return FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("Library/Application Support/Autocraft/Unreal/music.jsonl"));
}

// MARK: - Life

UAcMusicPlayer* UAcMusicPlayer::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAcMusicPlayer>() : nullptr;
}

void UAcMusicPlayer::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bSilent = FParse::Param(FCommandLine::Get(), TEXT("AcNoMusic"));
	bSeeded = FParse::Value(FCommandLine::Get(), TEXT("AcMusicSeed="), Seed);

	// Every track on hand, sorted by id; a folder track with an own track's
	// id takes its place (`MusicPlayer.tracks`).
	TMap<FString, FTrack> ById;
	for (const FString& Path : UAcMp3Loader::FindMp3s(OwnMusicFolder()))
	{
		const FString Id = FPaths::GetBaseFilename(Path);
		ById.Add(Id, {Path, Id, TEXT("own"), {}});
	}
	for (const FString& Path : UAcMp3Loader::FindMp3s(UAcMp3Loader::UserMusicFolder()))
	{
		const FString Id = FPaths::GetBaseFilename(Path);
		ById.Add(Id, {Path, Id, TEXT("folder"), {}});
	}
	ById.KeySort(TLess<FString>());
	for (auto& Pair : ById)
	{
		(Pair.Value.Source == TEXT("own") ? Own : Folder)++;
		Tracks.Add(Pair.Value);
	}
	// The station's ads: the imported waves, with their titles (Sounds.json).
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* AdTable = nullptr;
	if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Audio/Sounds.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root && Root->TryGetObjectField(TEXT("ads"), AdTable))
	{
		TArray<FString> Ids;
		for (const auto& Pair : (*AdTable)->Values) Ids.Add(FString(Pair.Key));
		Ids.Sort();
		for (const FString& Id : Ids)
		{
			const TSharedPtr<FJsonObject> Ad = (*AdTable)->GetObjectField(Id);
			Ads.Add({{}, Id, TEXT("ad"), Ad->GetStringField(TEXT("asset"))});
			Titles.Add(Id, Ad->GetStringField(TEXT("title")));
		}
	}
	TArray<int32> Songs, Spots;
	for (int32 I = 0; I < Tracks.Num(); ++I) Songs.Add(I);
	for (int32 I = 0; I < Ads.Num(); ++I) Spots.Add(I);
	if (bSeeded)
	{
		Rng = ac::SeededRandom(Seed);
		Queue.emplace(std::vector<int32>(Songs.GetData(), Songs.GetData() + Songs.Num()), *Rng);
		AdQueue.emplace(std::vector<int32>(Spots.GetData(), Spots.GetData() + Spots.Num()), *Rng);
	}
	else
	{
		Queue.emplace(std::vector<int32>(Songs.GetData(), Songs.GetData() + Songs.Num()));
		AdQueue.emplace(std::vector<int32>(Spots.GetData(), Spots.GetData() + Spots.Num()));
	}
	ReadVotes();

	if (FSlateApplication::IsInitialized())
	{
		Keys = MakeShared<FAcMusicKeys>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Keys);
	}
	Command = IConsoleManager::Get().RegisterConsoleCommand(TEXT("ac.Music"),
		TEXT("The music player: ac.Music next|previous|toggle|up|down|status"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const FString A = Args.Num() ? Args[0].ToLower() : TEXT("status");
			if (A == TEXT("next")) Press(EAcMusicCommand::Next);
			else if (A == TEXT("previous") || A == TEXT("prev")) Press(EAcMusicCommand::Previous);
			else if (A == TEXT("toggle") || A == TEXT("pause")) Press(EAcMusicCommand::Toggle);
			else if (A == TEXT("up")) Press(EAcMusicCommand::Up);
			else if (A == TEXT("down")) Press(EAcMusicCommand::Down);
			const TOptional<FAcNowPlaying> Np = NowPlaying();
			UE_LOG(LogAutocraft, Log, TEXT("audio: music %s"), Np ? *FString::Printf(TEXT("%s \"%s\" %.1f/%.1f s%s%s vote %d"), *Np->Track, *Np->Title,
				Np->PositionAt(Clock), Np->Length, Np->bPaused ? TEXT(" paused") : TEXT(""), Np->bAd ? TEXT(" (ad)") : TEXT(""), Np->Vote) : TEXT("none"));
		}),
		ECVF_Default);
	UE_LOG(LogAutocraft, Log, TEXT("audio: music %d own + %d folder tracks, %d ads%s"), Own, Folder, Ads.Num(), bSilent ? TEXT(" (off: -AcNoMusic)") : TEXT(""));
	bReady = true;
}

void UAcMusicPlayer::Deinitialize()
{
	Finish(TEXT("quit"));
	StopVoice();
	if (Keys && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Keys);
	Keys.Reset();
	if (Command) IConsoleManager::Get().UnregisterConsoleObject(Command);
	Command = nullptr;
	bReady = false;
	Super::Deinitialize();
}

TStatId UAcMusicPlayer::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAcMusicPlayer, STATGROUP_Tickables);
}

UWorld* UAcMusicPlayer::PlayWorld() const
{
	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	return World && World->bAllowAudioPlayback && World->HasBegunPlay() ? World : nullptr;
}

void UAcMusicPlayer::Tick(float DeltaTime)
{
	Clock += DeltaTime;
	if (PendingEnd)
	{
		const int32 G = *PendingEnd;
		PendingEnd.Reset();
		if (G == Generation) Ended(G);
	}
	if (!bStarted && !bSilent && PlayWorld())
	{
		bStarted = true;
		Start();
	}
	if (GapUntil && !bPaused && Clock >= *GapUntil) Advance();
	if (IsPlaying())
	{
		Played += DeltaTime;
		if (Play)
		{
			Play->Played += DeltaTime;
			// Heard only when it can be (Swift: `audible: !muted && music > 0`).
			if (!bMuted && AcSound::MusicGain() > 0.f) Play->Heard += DeltaTime;
		}
	}
	// The player's volumes (`UAcSettings`: MasterVolume, bMusic, MusicVolume), live.
	if (Voice && Loaded)
	{
		const float Gain = VoiceGain(Loaded->bAd);
		if (Gain != AppliedGain)
		{
			AppliedGain = Gain;
			Voice->SetVolumeMultiplier(Gain);
		}
	}
	if (Clock - LastSync > 10) Changed();
}

// MARK: - Pressing

void UAcMusicPlayer::Press(EAcMusicCommand C)
{
	switch (C)
	{
	case EAcMusicCommand::Toggle: Toggle(); break;
	case EAcMusicCommand::Next:
		// During an ad (or the gaps round it) the queue still stands on the
		// song that played out: on to the one after it.
		Finish(TEXT("next"));
		bAdNext = false;
		NextSong();
		bPaused = false;
		Start();
		break;
	case EAcMusicCommand::Previous:
	{
		// During an ad, back to the start of the song before it.
		const bool bAd = OnAd();
		const bool bBack = bAd ? false : Queue->previous(Position());
		Finish(bAd || bBack ? TEXT("previous") : TEXT("replay"));
		bAdNext = false;
		bPaused = false;
		Start();
		break;
	}
	case EAcMusicCommand::Up: Vote(1); break;
	case EAcMusicCommand::Down: Vote(-1); break;
	}
}

void UAcMusicPlayer::Toggle()
{
	if (bPaused)
	{
		bPaused = false;
		if (GapUntil)
		{
			// The last track had ended: on to what was next.
			Advance();
		}
		else if (!Loaded || (Play && !Play->Ended.IsEmpty()))
		{
			Start();
		}
		else
		{
			if (Voice) Voice->SetPaused(false);
			Changed();
		}
	}
	else
	{
		bPaused = true;
		if (Voice) Voice->SetPaused(true);
		Changed();
	}
}

void UAcMusicPlayer::Vote(int32 V)
{
	const FTrack* T = LoadedTrack();
	if (!T && Queue)
	{
		if (const std::optional<int32> I = Queue->current()) T = &Tracks[*I];
	}
	if (!T) return;
	const int32 Was = Votes.FindRef(T->Id);
	const int32 Now = Was == V ? 0 : V;
	if (Now == 0) Votes.Remove(T->Id);
	else Votes.Add(T->Id, Now);
	TSharedRef<FJsonObject> Line = MakeShared<FJsonObject>();
	Line->SetStringField(TEXT("kind"), TEXT("vote"));
	Line->SetStringField(TEXT("track"), T->Id);
	Line->SetNumberField(TEXT("vote"), Now);
	Line->SetStringField(TEXT("at"), FDateTime::UtcNow().ToIso8601());
	Record(Line);
	Changed();
}

// MARK: - Playing

void UAcMusicPlayer::NextSong()
{
	if (Rng) Queue->next(*Rng);
	else Queue->next();
}

void UAcMusicPlayer::NextAd()
{
	if (Rng) AdQueue->next(*Rng);
	else AdQueue->next();
}

bool UAcMusicPlayer::OnAd() const { return (Loaded && Loaded->bAd) || bAdNext; }

const UAcMusicPlayer::FTrack* UAcMusicPlayer::LoadedTrack() const
{
	if (!Loaded) return nullptr;
	const TArray<FTrack>& From = Loaded->bAd ? Ads : Tracks;
	return From.IsValidIndex(Loaded->Index) ? &From[Loaded->Index] : nullptr;
}

bool UAcMusicPlayer::IsPlaying() const
{
	return Loaded && !bPaused && !GapUntil && Play && Play->Ended.IsEmpty() && Voice;
}

void UAcMusicPlayer::Advance()
{
	GapUntil.Reset();
	if (bAdNext)
	{
		bAdNext = false;
		if (const std::optional<int32> Ad = AdQueue->current())
		{
			NextAd();
			if (Load(true, *Ad)) return;
		}
	}
	NextSong();
	Start();
}

void UAcMusicPlayer::Start()
{
	for (int32 K = 0; K < FMath::Max(static_cast<int32>(Queue->order.size()), 1); ++K)
	{
		const std::optional<int32> I = Queue->current();
		if (!I) return;
		if (Load(false, *I)) return;
		NextSong();
	}
}

void UAcMusicPlayer::StopVoice()
{
	++Generation;
	if (Voice)
	{
		Voice->OnAudioFinishedNative.RemoveAll(this);
		Voice->Stop();
		Voice->DestroyComponent();
	}
	Voice = nullptr;
}

bool UAcMusicPlayer::Load(bool bAd, int32 Index)
{
	UWorld* World = PlayWorld();
	if (!World || bSilent) return true;
	const FTrack& T = (bAd ? Ads : Tracks)[Index];
	USoundBase* NewSound = nullptr;
	double Length = 0;
	if (bAd)
	{
		USoundWave* Wave = LoadObject<USoundWave>(nullptr, *T.Asset);
		NewSound = Wave;
		Length = Wave ? Wave->Duration : 0;
	}
	else if (UAcMp3Wave* Wave = UAcMp3Loader::LoadMp3(T.Path, this))
	{
		NewSound = Wave;
		Length = Wave->Info.Duration;
	}
	if (!NewSound || Length <= 0)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("audio: cannot read %s"), *(bAd ? T.Asset : T.Path));
		return false;
	}
	StopVoice();
	Sound = NewSound;
	Voice = UGameplayStatics::CreateSound2D(World, Sound, VoiceGain(bAd), 1.f, 0.f, nullptr, true, false);
	if (!Voice) return true;
	const int32 G = Generation;
	Voice->OnAudioFinishedNative.AddWeakLambda(this, [this, G](UAudioComponent*) { PendingEnd = G; });
	Loaded = FLoaded{bAd, Index, Length};
	Played = 0;
	GapUntil.Reset();
	Play = FPlay{T.Id, TitleOf(T), T.Source, Length, FDateTime::UtcNow(), 0, 0, {}};
	UE_LOG(LogAutocraft, Log, TEXT("audio: playing %s"), *FPaths::GetCleanFilename(bAd ? T.Id + TEXT(".wav") : T.Path));
	Voice->Play();
	if (bPaused) Voice->SetPaused(true);
	ReadInfo(T);
	Changed();
	return true;
}

void UAcMusicPlayer::Ended(int32 ForGeneration)
{
	if (ForGeneration != Generation || !Loaded) return;
	Played = Loaded->Length;
	Finish(TEXT("end"));
	const bool bWasAd = Loaded->bAd;
	bAdNext = !bWasAd && Ads.Num() > 0;
	GapUntil = Clock + (bAdNext ? BeforeAd : bWasAd ? AfterAd : Gap);
	Changed();
}

void UAcMusicPlayer::Finish(const TCHAR* How)
{
	if (!Play || !Play->Ended.IsEmpty()) return;
	Play->Ended = How;
	if (const FTrack* T = LoadedTrack()) Play->Title = TitleOf(*T);
	TSharedRef<FJsonObject> Line = MakeShared<FJsonObject>();
	Line->SetStringField(TEXT("kind"), TEXT("play"));
	Line->SetStringField(TEXT("track"), Play->Track);
	Line->SetStringField(TEXT("title"), Play->Title);
	Line->SetStringField(TEXT("source"), Play->Source);
	Line->SetNumberField(TEXT("length"), FMath::RoundToDouble(Play->Length * 10) / 10);
	Line->SetStringField(TEXT("started"), Play->Started.ToIso8601());
	Line->SetNumberField(TEXT("played"), FMath::RoundToDouble(Play->Played * 10) / 10);
	Line->SetNumberField(TEXT("heard"), FMath::RoundToDouble(Play->Heard * 10) / 10);
	Line->SetStringField(TEXT("ended"), Play->Ended);
	Record(Line);
}

double UAcMusicPlayer::Position() const
{
	if (!Loaded) return 0;
	if (GapUntil || (Play && !Play->Ended.IsEmpty())) return Loaded->Length;
	return FMath::Min(Played, Loaded->Length);
}

float UAcMusicPlayer::VoiceGain(const bool bAd) const
{
	// An ad plays +3 dB over the songs (`AdGain`), never past full.
	const float Music = AcSound::MusicGain();
	const float Gain = bAd ? FMath::Min(Music * AdGain, 1.f) : Music;
	return (bMuted ? 0.f : Gain) * AcSound::MasterGain();
}

void UAcMusicPlayer::SetMuted(bool bInMuted)
{
	bMuted = bInMuted;
	if (Voice && Loaded)
	{
		Voice->SetVolumeMultiplier(VoiceGain(Loaded->bAd));
	}
}

// MARK: - What it shows

TOptional<FAcNowPlaying> UAcMusicPlayer::NowPlaying() const
{
	const FTrack* T = LoadedTrack();
	if (!T && Queue)
	{
		if (const std::optional<int32> I = Queue->current()) T = &Tracks[*I];
	}
	if (!T) return {};
	FAcNowPlaying Np;
	Np.Track = T->Id;
	Np.Title = TitleOf(*T);
	if (const TObjectPtr<UTexture2D>* C = Covers.Find(T->Id)) Np.Cover = C->Get();
	Np.bStationCover = StationCovers.Contains(T->Id);
	Np.Length = Loaded ? Loaded->Length : 0;
	Np.Position = Position();
	Np.At = Clock;
	Np.bPlaying = IsPlaying();
	Np.bPaused = bPaused;
	Np.Vote = Votes.FindRef(T->Id);
	Np.bAd = T->Source == TEXT("ad");
	return Np;
}

FString UAcMusicPlayer::TitleOf(const FTrack& T) const
{
	if (const FString* Title = Titles.Find(T.Id)) return *Title;
	return TitleFromId(T.Id);
}

void UAcMusicPlayer::ReadInfo(const FTrack& T)
{
	if (Covers.Contains(T.Id)) return;
	FAcTrackTags Tags;
	if (!T.Path.IsEmpty() && ReadTags(T.Path, Tags) && !Tags.Title.IsEmpty()) Titles.Add(T.Id, Tags.Title);
	UTexture2D* Cover = Tags.Picture.Num() ? FAcHudStyle::ImportHudTexture(Tags.Picture) : nullptr;
	if (!Cover && !T.Path.IsEmpty())
	{
		for (const TCHAR* Ext : {TEXT("jpg"), TEXT("jpeg"), TEXT("png")})
		{
			TArray<uint8> Data;
			const FString Side = FPaths::ChangeExtension(T.Path, Ext);
			if (FPaths::FileExists(Side) && FFileHelper::LoadFileToArray(Data, *Side) && (Cover = FAcHudStyle::ImportHudTexture(Data)))
			{
				break;
			}
		}
	}
	if (!Cover)
	{
		if (T.Source == TEXT("ad"))
		{
			Cover = Texture(StationCover(), this);
			StationCovers.Add(T.Id);
		}
		else
		{
			Cover = Texture(DrawnCover(TitleOf(T)), this);
		}
	}
	Covers.Add(T.Id, Cover);
	Changed();
}

// MARK: - The log (until the tracking database)

void UAcMusicPlayer::Record(const TSharedRef<FJsonObject>& Line) const
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	if (!FJsonSerializer::Serialize(Line, W)) return;
	Out += TEXT("\n");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(LogPath()), true);
	FFileHelper::SaveStringToFile(Out, *LogPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

void UAcMusicPlayer::ReadVotes()
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *LogPath())) return;
	for (const FString& L : Lines)
	{
		TSharedPtr<FJsonObject> O;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(L), O) || !O) continue;
		if (O->GetStringField(TEXT("kind")) != TEXT("vote")) continue;
		const FString Track = O->GetStringField(TEXT("track"));
		const int32 V = static_cast<int32>(O->GetNumberField(TEXT("vote")));
		if (V == 0) Votes.Remove(Track);
		else Votes.Add(Track, V);
	}
}
