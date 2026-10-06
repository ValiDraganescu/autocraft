#include "AcLauncherArt.h"

#include "AcHudStyle.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FString FAcLauncherArt::Folder()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Content-src/launcher")));
}

FAcLauncherArt::FAcLauncherArt()
{
	TArray<FString> Jpg, Png;
	IFileManager::Get().FindFiles(Jpg, *FPaths::Combine(Folder(), TEXT("*.jpg")), true, false);
	IFileManager::Get().FindFiles(Png, *FPaths::Combine(Folder(), TEXT("*.png")), true, false);
	Files = Jpg;
	Files.Append(Png);
	Files.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::IgnoreCase) < 0; });
	Slots.SetNum(Files.Num());
	if (Files.IsEmpty()) return;
	int32 N = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcLauncherArt="), N) && N >= 1) PinnedIndex = (N - 1) % Files.Num();
	StartIndex = PinnedIndex != INDEX_NONE ? PinnedIndex : FMath::RandRange(0, Files.Num() - 1);
}

FAcLauncherArt::FSlot* FAcLauncherArt::Load(const int32 I)
{
	if (Files.IsEmpty()) return nullptr;
	FSlot& S = Slots[((I % Files.Num()) + Files.Num()) % Files.Num()];
	if (S.bTried) return S.Texture ? &S : nullptr;
	S.bTried = true;
	const FString& Name = Files[((I % Files.Num()) + Files.Num()) % Files.Num()];
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Folder(), Name))) return nullptr;
	S.Texture = FAcHudStyle::ImportHudTexture(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()));
	if (!S.Texture) return nullptr;
	S.Brush.SetResourceObject(S.Texture);
	S.Brush.ImageSize = FVector2f(S.Texture->GetSizeX(), S.Texture->GetSizeY());
	S.Brush.DrawAs = ESlateBrushDrawType::Image;
	return &S;
}

const FSlateBrush* FAcLauncherArt::Brush(const int32 I)
{
	const FSlot* S = Load(I);
	return S ? &S->Brush : nullptr;
}

FVector2f FAcLauncherArt::Size(const int32 I)
{
	const FSlot* S = Load(I);
	return S ? S->Brush.ImageSize : FVector2f::ZeroVector;
}

void FAcLauncherArt::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FSlot& S : Slots) Collector.AddReferencedObject(S.Texture);
}
