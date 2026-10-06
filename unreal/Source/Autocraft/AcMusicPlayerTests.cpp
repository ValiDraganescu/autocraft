// The music player's pieces that need no audio device: the tags, the
// titles, the queue rules. Run:
//   UnrealEditor Autocraft.uproject -ExecCmds="Automation RunTests Autocraft.Audio.Music; Quit"
//     -unattended -nullrhi -nosplash -nosound -log
#include "AcMusicPlayer.h"
#include "AcMp3Loader.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcMusicTagsTest, "Autocraft.Audio.Music.Tags", Flags)
bool FAcMusicTagsTest::RunTest(const FString&)
{
	const TArray<FString> Files = UAcMp3Loader::FindMp3s(UAcMusicPlayer::OwnMusicFolder());
	TestEqual(TEXT("the soundtrack: 7 songs × 2 takes"), Files.Num(), 14);
	for (const FString& F : Files)
	{
		FAcTrackTags Tags;
		const FString Id = FPaths::GetBaseFilename(F);
		if (!TestTrue(FString::Printf(TEXT("%s has a tag"), *Id), UAcMusicPlayer::ReadTags(F, Tags))) continue;
		TestFalse(FString::Printf(TEXT("%s has a title"), *Id), Tags.Title.IsEmpty());
		TestTrue(FString::Printf(TEXT("%s has a JPEG cover (%d bytes)"), *Id, Tags.Picture.Num()),
			Tags.Picture.Num() > 1000 && Tags.Picture[0] == 0xFF && Tags.Picture[1] == 0xD8);
		AddInfo(FString::Printf(TEXT("%s: \"%s\" by %s, %s %d bytes"), *Id, *Tags.Title, *Tags.Artist, *Tags.PictureMime, Tags.Picture.Num()));
	}
	FAcTrackTags Tags;
	if (UAcMusicPlayer::ReadTags(FPaths::Combine(UAcMusicPlayer::OwnMusicFolder(), TEXT("aftermath_1.mp3")), Tags))
	{
		TestEqual(TEXT("aftermath_1's title"), Tags.Title, FString(TEXT("Aftermath (1)")));
		TestEqual(TEXT("its artist"), Tags.Artist, FString(TEXT("starhaser")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcMusicTitlesTest, "Autocraft.Audio.Music.Titles", Flags)
bool FAcMusicTitlesTest::RunTest(const FString&)
{
	// Swift: `id.replacingOccurrences("_", " ").replacingOccurrences("-", " ").capitalized`.
	TestEqual(TEXT("underscores"), UAcMusicPlayer::TitleFromId(TEXT("rising_pressure_2")), FString(TEXT("Rising Pressure 2")));
	TestEqual(TEXT("dashes and case"), UAcMusicPlayer::TitleFromId(TEXT("my-SONG_name")), FString(TEXT("My Song Name")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcMusicQueueTest, "Autocraft.Audio.Music.Queue", Flags)
bool FAcMusicQueueTest::RunTest(const FString&)
{
	// The station's queue: every track once a round, never the same track
	// twice across a round's end; previous within 3 s goes back.
	ac::SeededRandom G(42);
	ac::MusicQueue<int32> Q(std::vector<int32>{0, 1, 2, 3, 4, 5, 6}, G);
	TSet<int32> Round;
	int32 Last = -1;
	bool bRepeat = false;
	for (int32 K = 0; K < 70; ++K)
	{
		const int32 T = *Q.current();
		if (K % 7 == 0) Round.Reset();
		Round.Add(T);
		if (T == Last) bRepeat = true;
		Last = T;
		if (K % 7 == 6) TestEqual(TEXT("each round plays all 7"), Round.Num(), 7);
		Q.next(G);
	}
	TestFalse(TEXT("never the same track twice in a row"), bRepeat);
	Q.next(G);
	TestTrue(TEXT("previous 2 s in goes back"), Q.previous(2.0));
	TestFalse(TEXT("previous 5 s in starts over"), Q.previous(5.0));
	return true;
}

#endif
