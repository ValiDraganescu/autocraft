#include "AcIcons.h"

#include "AcLog.h"

#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "UObject/UObjectGlobals.h"

const FSlateBrush* FAcIcons::Brush(const FString& Name)
{
	// Brushes live as long as the process (the textures stay rooted).
	static TMap<FString, TUniquePtr<FSlateBrush>> Cache;
	if (Name.IsEmpty()) return nullptr;
	if (const TUniquePtr<FSlateBrush>* Found = Cache.Find(Name)) return Found->Get();
	const FString Asset = TEXT("T_Icon_") + Name.Replace(TEXT("."), TEXT("_"));
	const FString Path = FString::Printf(TEXT("/Game/UI/Icons/%s.%s"), *Asset, *Asset);
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn);
	TUniquePtr<FSlateBrush> B;
	if (Texture)
	{
		Texture->AddToRoot();
		B = MakeUnique<FSlateBrush>();
		B->DrawAs = ESlateBrushDrawType::Image;
		B->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		B->SetResourceObject(Texture);
	}
	else
	{
		UE_LOG(LogAutocraft, Warning, TEXT("console: no icon %s (run Tools/hud/bake_icons.sh and Tools/Editor/import_icons.py)"), *Name);
	}
	const FSlateBrush* Out = B.Get();
	Cache.Add(Name, MoveTemp(B));
	return Out;
}
