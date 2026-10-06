#include "AcCabArt.h"

#include "AcHudStyle.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "UObject/Package.h"

UTexture2D* AcCabArt::ToTexture(const FAcArtImage& Image, const TCHAR* Name)
{
	if (!Image.IsValid()) return nullptr;
	UTexture2D* T = UTexture2D::CreateTransient(Image.Width, Image.Height, PF_B8G8R8A8,
		// A unique name: a second texture under a name still held would replace a live object.
		MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(Name)));
	if (!T) return nullptr;
	T->SRGB = true;
	T->Filter = TF_Bilinear;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	T->LODGroup = TEXTUREGROUP_UI;
	T->NeverStream = true;
	FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Image.Pixels.GetData(), Image.Pixels.Num() * sizeof(FColor));
	// Slate on the Mac writes pow(1/2.2), not sRGB: re-encode so the art
	// lands on screen as Core Graphics drew it (FAcHudStyle::Srgb).
	FAcHudStyle::CompensateForSlate(TArrayView<FColor>(static_cast<FColor*>(Data), Image.Pixels.Num()));
	Mip.BulkData.Unlock();
	T->UpdateResource();
	return T;
}

#if !PLATFORM_MAC
// Elsewhere: flat stand-ins in the art's colours (the drawing is Core Graphics).
namespace
{
	FAcArtImage Flat(FVector2D Points, double Scale, FColor Color)
	{
		FAcArtImage I;
		I.Points = Points;
		I.Width = FMath::Max(1, (int32)FMath::CeilToDouble(Points.X * Scale));
		I.Height = FMath::Max(1, (int32)FMath::CeilToDouble(Points.Y * Scale));
		I.Pixels.Init(Color, I.Width * I.Height);
		return I;
	}
	const FColor Navy(28, 20, 10, 255);  // BGRA of the armour's middle tone
}

FAcArtImage AcCabArt::Face(int32 K, const FAcCabLayout& L)
{
	const FAcRect& R = L.Faces[FMath::Clamp(K, 0, 2)].Flat;
	return Flat(FVector2D(R.W, R.H), 2, Navy);
}
FAcArtImage AcCabArt::DashTop(const FAcCabLayout& L)
{
	return Flat(FVector2D(L.Size.X, FMath::RoundHalfFromZero(L.Top() + 38)), 1, FColor(5, 5, 5, 255));
}
TArray<FAcArtBand> AcCabArt::Framed(const FAcCabLayout& L)
{
	const double Bottom = FMath::RoundHalfFromZero(L.Top() + 38);
	return {FAcArtBand{FAcRect(0, 0, L.Size.X, Bottom), Flat(FVector2D(L.Size.X, Bottom), 1, FColor(5, 5, 5, 255))}};
}
FAcArtImage AcCabArt::ViewFrame(FVector2D Size, double) { return Flat(Size, 0.25, FColor(0, 0, 0, 0)); }
FAcArtImage AcCabArt::ButtonFace(double Side, bool bEnabled)
{
	return Flat(FVector2D(Side, Side), 2, bEnabled ? FColor(60, 30, 15, 255) : FColor(20, 18, 16, 255));
}
FAcArtImage AcCabArt::Scanlines(FVector2D Size) { return Flat(Size, 1, FColor(0, 0, 0, 0)); }
FAcArtImage AcCabArt::Plate(FVector2D Size, FAcCuts, bool, int32)
{
	return Flat(Size + FVector2D(2 * PlatePad), 2, FColor(40, 25, 15, 230));
}
#endif
