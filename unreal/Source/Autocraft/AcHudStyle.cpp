#include "AcHudStyle.h"

#include "AcLog.h"

#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"

bool FAcHudStyle::bInitialized = false;

namespace
{
	/// One plate's texture and its brush. Sizes in points.
	struct FPlateArt
	{
		const TCHAR* Asset;
		/// The plate's size as baked (`Chrome.plate`'s `size`), without the pad.
		FVector2f Size;
		/// The largest corner cut on the left/right and on the top/bottom.
		float CutX;
		/// Vertical margins: 0 means the plate keeps its baked height (the
		/// margins meet in the middle).
		bool bStretchY;
	};

	const FPlateArt PlateArt[(int32)EAcPlate::Count] = {
		{TEXT("/Game/UI/Hud/T_PlateBar.T_PlateBar"), {340, 42}, 14, false},
		{TEXT("/Game/UI/Hud/T_PlateScore.T_PlateScore"), {220, 42}, 14, false},
		{TEXT("/Game/UI/Hud/T_PlateLevel.T_PlateLevel"), {360, 82}, 14, true},
	};

	FSlateBrush GPlates[(int32)EAcPlate::Count];
	FSlateBrush GOre, GHydrogen, GSupply, GWhite;
	TSharedPtr<FCompositeFont> GFont;
	TArray<UObject*> GRooted;

	UObject* LoadRooted(const TCHAR* Path)
	{
		UObject* O = LoadObject<UObject>(nullptr, Path, nullptr, LOAD_NoWarn);
		if (!O)
		{
			UE_LOG(LogAutocraft, Warning, TEXT("hud: missing %s (run unreal/Tools/Editor/import_hud.py)"), Path);
			return nullptr;
		}
		O->AddToRoot();
		GRooted.Add(O);
		return O;
	}

	/// A brush on a texture, `Points` big.
	void ImageBrush(FSlateBrush& B, const TCHAR* Path, FVector2f Points)
	{
		B = FSlateBrush();
		B.DrawAs = ESlateBrushDrawType::Image;
		B.ImageSize = FVector2D(Points);
		if (UObject* T = LoadRooted(Path)) B.SetResourceObject(T);
		else B.TintColor = FSlateColor(FLinearColor(1, 1, 1, 0));
	}

	/// sRGB transfer, one channel.
	double SrgbToLinear(double C)
	{
		C = FMath::Clamp(C, 0.0, 1.0);
		return C <= 0.04045 ? C / 12.92 : FMath::Pow((C + 0.055) / 1.055, 2.4);
	}

	double LinearToSrgb(double L)
	{
		const double C = FMath::Clamp(L, 0.0, 1.0);
		return C <= 0.0031308 ? C * 12.92 : 1.055 * FMath::Pow(C, 1.0 / 2.4) - 0.055;
	}

	/// The HUD's transfer: the inverse of what Slate writes to the screen
	/// (on the Mac pow(1/2.2), elsewhere the sRGB curve).
	float ToLinear(double C)
	{
#if PLATFORM_MAC
		return (float)FMath::Pow(FMath::Clamp(C, 0.0, 1.0), 2.2);
#else
		return (float)SrgbToLinear(C);
#endif
	}

	double ToSrgb(float L)
	{
#if PLATFORM_MAC
		return FMath::Pow(FMath::Clamp((double)L, 0.0, 1.0), 1.0 / 2.2);
#else
		return LinearToSrgb(L);
#endif
	}

	struct FPlayerPaint
	{
		const TCHAR* Name;
		double R, G, B;
	};
	// make_materials.py PALETTE (player 0 and 1 are MaterialLibrary.teams).
	const FPlayerPaint Players[FAcHudStyle::MaxPlayers] = {
		{TEXT("Blue"), 0.14, 0.24, 0.72},
		{TEXT("Red"), 0.70, 0.09, 0.07},
		{TEXT("Green"), 0.16, 0.56, 0.18},
		{TEXT("Gold"), 0.86, 0.64, 0.10},
		{TEXT("Violet"), 0.50, 0.20, 0.76},
		{TEXT("Teal"), 0.06, 0.58, 0.62},
		{TEXT("Orange"), 0.92, 0.40, 0.06},
		{TEXT("Pink"), 0.88, 0.32, 0.60},
	};
}

void FAcHudStyle::Initialize()
{
	if (bInitialized) return;
	bInitialized = true;

	for (int32 I = 0; I < (int32)EAcPlate::Count; ++I)
	{
		const FPlateArt& A = PlateArt[I];
		FSlateBrush& B = GPlates[I];
		const FVector2f Full = A.Size + FVector2f(2 * PlatePad);
		ImageBrush(B, A.Asset, Full * PlateScale);
		B.DrawAs = ESlateBrushDrawType::Box;
		// The corners (pad, cut, and the glow line 4.5 in) never stretch.
		const float MX = (PlatePad + A.CutX + 6) / Full.X;
		const float MY = A.bStretchY ? (PlatePad + A.CutX + 6) / Full.Y : 0.5f;
		B.Margin = FMargin(MX, MY, MX, MY);
	}
	ImageBrush(GOre, TEXT("/Game/UI/Hud/T_IconOre.T_IconOre"), {24, 24});
	ImageBrush(GHydrogen, TEXT("/Game/UI/Hud/T_IconHydrogen.T_IconHydrogen"), {24, 24});
	ImageBrush(GSupply, TEXT("/Game/UI/Hud/T_IconSupply.T_IconSupply"), {24, 24});
	GWhite = *FCoreStyle::Get().GetBrush("GenericWhiteBox");

	GFont = MakeShared<FCompositeFont>();
	static const TCHAR* const Faces[][2] = {
		{TEXT("Medium"), TEXT("/Game/UI/Fonts/FF_BarlowCondensed_Medium.FF_BarlowCondensed_Medium")},
		{TEXT("SemiBold"), TEXT("/Game/UI/Fonts/FF_BarlowCondensed_SemiBold.FF_BarlowCondensed_SemiBold")},
		{TEXT("Bold"), TEXT("/Game/UI/Fonts/FF_BarlowCondensed_Bold.FF_BarlowCondensed_Bold")},
	};
	int32 Loaded = 0;
	for (const auto& Face : Faces)
	{
		if (UObject* F = LoadRooted(Face[1]))
		{
			GFont->DefaultTypeface.Fonts.Add(FTypefaceEntry(Face[0]));
			GFont->DefaultTypeface.Fonts.Last().Font = FFontData(F);
			++Loaded;
		}
	}
	if (Loaded == 0) GFont.Reset();
	UE_LOG(LogAutocraft, Log, TEXT("hud: style ready (%d font faces, %d textures)"), Loaded, GRooted.Num() - Loaded);
}

void FAcHudStyle::Shutdown()
{
	for (UObject* O : GRooted)
	{
		if (IsValid(O)) O->RemoveFromRoot();
	}
	GRooted.Reset();
	GFont.Reset();
	bInitialized = false;
}

FLinearColor FAcHudStyle::Srgb(const double R, const double G, const double B, const double A)
{
	return FLinearColor(ToLinear(R), ToLinear(G), ToLinear(B), (float)A);
}

FLinearColor FAcHudStyle::World(const double R, const double G, const double B, const double A)
{
	return FLinearColor((float)SrgbToLinear(R), (float)SrgbToLinear(G), (float)SrgbToLinear(B), (float)A);
}

double FAcHudStyle::HudSrgb(const float Linear)
{
	return ToSrgb(Linear);
}

FLinearColor FAcHudStyle::HudToWorld(const FLinearColor& Hud)
{
	return World(ToSrgb(Hud.R), ToSrgb(Hud.G), ToSrgb(Hud.B), Hud.A);
}

uint8 FAcHudStyle::SlateTextureByte(const uint8 Srgb)
{
	static const TArray<uint8> Table = [] {
		TArray<uint8> T;
		T.SetNum(256);
		for (int32 I = 0; I < 256; ++I)
			T[I] = (uint8)FMath::Clamp(FMath::RoundToInt(255.0 * LinearToSrgb(ToLinear(I / 255.0))), 0, 255);
		return T;
	}();
	return Table[Srgb];
}

void FAcHudStyle::CompensateForSlate(const TArrayView<FColor> Pixels)
{
#if PLATFORM_MAC
	for (FColor& C : Pixels)
	{
		C.R = SlateTextureByte(C.R);
		C.G = SlateTextureByte(C.G);
		C.B = SlateTextureByte(C.B);
	}
#endif
}

UTexture2D* FAcHudStyle::HudTexture(const FImageView& Image)
{
	FImage Copy;
	Image.CopyTo(Copy, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	CompensateForSlate(TArrayView<FColor>(reinterpret_cast<FColor*>(Copy.RawData.GetData()), Copy.SizeX * Copy.SizeY * Copy.NumSlices));
	return FImageUtils::CreateTexture2DFromImage(Copy);
}

UTexture2D* FAcHudStyle::ImportHudTexture(const TArrayView64<const uint8> Encoded)
{
	FImage Image;
	if (!FImageUtils::DecompressImage(Encoded.GetData(), Encoded.Num(), Image)) return nullptr;
	return HudTexture(Image);
}

FLinearColor FAcHudStyle::Blend(const FLinearColor& A, const double Fraction, const FLinearColor& B)
{
	auto Mix = [Fraction](float X, float Y) { return ToSrgb(X) * (1 - Fraction) + ToSrgb(Y) * Fraction; };
	return Srgb(Mix(A.R, B.R), Mix(A.G, B.G), Mix(A.B, B.B), A.A * (1 - Fraction) + B.A * Fraction);
}

FLinearColor FAcHudStyle::Health(const double Fraction)
{
	if (Fraction > 0.6) return Srgb(0.2, 0.95, 0.3);
	if (Fraction > 0.3) return Srgb(1.0, 0.82, 0.15);
	return Srgb(1.0, 0.22, 0.14);
}

FLinearColor FAcHudStyle::PlayerColor(const int64 P)
{
	const FPlayerPaint& C = Players[FMath::Clamp<int64>(P, 0, MaxPlayers - 1)];
	return Srgb(C.R, C.G, C.B);
}

FLinearColor FAcHudStyle::PlayerTextColor(const int64 P, const double Toward)
{
	return Blend(PlayerColor(P), Toward, FLinearColor::White);
}

FString FAcHudStyle::PlayerName(const int64 P)
{
	return Players[FMath::Clamp<int64>(P, 0, MaxPlayers - 1)].Name;
}

FSlateFontInfo FAcHudStyle::Font(const float SwiftPoints, const EAcFontWeight Weight)
{
	Initialize();
	// Slate sizes are points at 96 dpi; a Swift point is a pixel at 1x.
	const float Size = SwiftPoints * 72.0f / 96.0f;
	if (!GFont) return FCoreStyle::GetDefaultFontStyle("Bold", Size);
	static const FName Names[] = {TEXT("Medium"), TEXT("SemiBold"), TEXT("Bold")};
	return FSlateFontInfo(GFont, Size, Names[(int32)Weight]);
}

FSlateFontInfo FAcHudStyle::Mono(const float SwiftPoints)
{
	return FCoreStyle::GetDefaultFontStyle("Mono", SwiftPoints * 72.0f / 96.0f);
}

const FSlateBrush* FAcHudStyle::Plate(const EAcPlate Which)
{
	Initialize();
	return &GPlates[(int32)Which];
}

void FAcHudStyle::PaintPlate(FSlateWindowElementList& Out, const int32 Layer, const FGeometry& Geometry, const EAcPlate Which,
	const FSlateRect& Rect, const float Opacity)
{
	const FVector2f Size = FVector2f(Rect.GetSize()) + FVector2f(2 * PlatePad);
	const FVector2f TopLeft = FVector2f(Rect.GetTopLeft()) - FVector2f(PlatePad);
	FSlateDrawElement::MakeBox(Out, Layer,
		Geometry.ToPaintGeometry(FVector2D(Size * PlateScale), FSlateLayoutTransform(1.0f / PlateScale, FVector2D(TopLeft))),
		Plate(Which), ESlateDrawEffect::None, FLinearColor(1, 1, 1, Opacity));
}

const FSlateBrush* FAcHudStyle::OreIcon()
{
	Initialize();
	return &GOre;
}

const FSlateBrush* FAcHudStyle::HydrogenIcon()
{
	Initialize();
	return &GHydrogen;
}

const FSlateBrush* FAcHudStyle::SupplyIcon()
{
	Initialize();
	return &GSupply;
}

const FSlateBrush* FAcHudStyle::White()
{
	Initialize();
	return &GWhite;
}
