// The console's dashboard geometry (chunk D2; GAME-LAYER.md §2.4 "Console"):
// the Swift `Console.layOut` (Sources/Autocraft/Console.swift:503) and
// `Cab.Layout` (ConsoleCab.swift:31), with `Projection` (ConsoleCab.swift:739).
//
// Coordinates are **Swift console points, y up** from the window's bottom
// left, exactly as the Swift code has them, so the numbers port one to one.
// `FAcCabLayout::ToSlate` turns them into Slate's y-down points.
//
// Flat or warped: the Swift dashboard sets its three faces in perspective
// (the side faces rise toward the pillars, the centre leans back), as the
// console always does (`bWarped`; SAcConsole warps each face's flat drawing
// into `Faces[k].Quad`, see AcConsoleWarp.h). With `bWarped` false every
// face is its own rectangle and the dashboard's top a level ledge (the cvar
// `ac.ConsoleWarp 0`, for checking the flat drawing).
#pragma once

#include "CoreMinimal.h"

/// A rectangle in Swift points (y up): origin bottom left.
struct FAcRect
{
	double X = 0, Y = 0, W = 0, H = 0;

	FAcRect() = default;
	FAcRect(double InX, double InY, double InW, double InH) : X(InX), Y(InY), W(InW), H(InH) {}

	double MinX() const { return X; }
	double MinY() const { return Y; }
	double MaxX() const { return X + W; }
	double MaxY() const { return Y + H; }
	double MidX() const { return X + W / 2; }
	double MidY() const { return Y + H / 2; }
	bool Contains(FVector2D P) const { return P.X >= X && P.X < X + W && P.Y >= Y && P.Y < Y + H; }
	/// `CGRect.insetBy(dx:dy:)`.
	FAcRect Inset(double Dx, double Dy) const { return FAcRect(X + Dx, Y + Dy, W - 2 * Dx, H - 2 * Dy); }
	FAcRect Offset(double Dx, double Dy) const { return FAcRect(X + Dx, Y + Dy, W, H); }
	bool IsEmpty() const { return W <= 0 || H <= 0; }
};

/// A plane seen in perspective: the projective map taking a flat rectangle
/// onto the four corners it is seen at (bottom left, bottom right, top
/// right, top left). Heckbert's square-to-quad, as Swift's `Projection`.
struct AUTOCRAFT_API FAcProjection
{
	FAcProjection();
	FAcProjection(const FAcRect& Flat, const FVector2D Quad[4]);

	FVector2D operator()(FVector2D P) const;
	/// Back from the corners to the rectangle.
	FAcProjection Inverse() const;

	/// Row-major 3×3.
	double M[3][3];
};

/// A face of the dashboard: where its art and screen are laid out flat, and
/// the corners it is seen at (bottom left, bottom right, top right, top left).
struct FAcCabFace
{
	FAcRect Flat;
	FVector2D Quad[4];
	FAcProjection Projection() const { return FAcProjection(Flat, Quad); }
};

/// The console's looks (`Console.Style`): the top-down view's colony console
/// (holograms, lit button faces), a driven unit's (first and third person),
/// and a machine's cab, the window's frame rising from the dashboard.
enum class EAcConsoleStyle : uint8
{
	Rts,
	Cockpit,
	Cab,
};

/// What the cab's two lamps say (`Cab.Lamps`): amber and steady at rest,
/// amber pulsing while the machine works, green while it is locked down.
enum class EAcCabLamps : uint8
{
	Idle,
	Working,
	Locked,
};

/// The console's layout for one view size: `Console.layOut` and `Cab.Layout`.
struct AUTOCRAFT_API FAcCabLayout
{
	/// The card's grid (`Console.columns`, `rows`) and its buttons.
	static constexpr int32 Columns = 5;
	static constexpr int32 Rows = 3;
	static constexpr double Button = 46;
	static constexpr double Gap = 4;
	/// `Cab.Layout.corner`.
	static constexpr double Corner = 16;

	/// `View`: the view's size in points. `MinimapSize`: the minimap's own
	/// size (`GameController.makeMinimap(scale: 1, height: 180)`), which sets
	/// the map well's width.
	static FAcCabLayout Make(FVector2D View, FVector2D MinimapSize, bool bWarped = false);
	/// The minimap's size for a map whose ground is `Width` × `Depth` cells
	/// (`makeMinimap(scale: 1, height: 180)`).
	static FVector2D MinimapSizeFor(double Width, double Depth, double Height = 180);

	/// `Cab.Layout.rail`, `pillar`.
	static double Rail(double Height) { return FMath::Max(18.0, FMath::RoundHalfFromZero(Height * 0.026)); }
	static double Pillar(double Width) { return FMath::Max(22.0, FMath::RoundHalfFromZero(Width * 0.032)); }

	FVector2D Size;
	bool bWarped = false;

	// Console.layOut
	/// The screens laid out flat: minimap, selection, card (wells).
	FAcRect MapWell, CenterWell, CardWell;
	/// Where the minimap sits in its well, and its scale (`minimap.setScale`).
	FAcRect Minimap;
	double MinimapScale = 1;

	// Cab.Layout
	/// The opening the frame leaves (the `.cab` style; D3).
	FAcRect Window;
	/// The dashboard's faces: left, centre, right.
	FAcCabFace Faces[3];
	/// The faces' top edges, left to right (outer left, centre left, centre
	/// right, outer right).
	FVector2D Brow[4];
	/// The dashboard's top where it meets the glass, left to right.
	FVector2D Skyline[4];
	/// The instrument blocks by the pillars: left (the music player's) and right.
	FAcRect Blocks[2];
	/// How high the centre face reaches (`top` in `Cab.Layout.init`).
	double FaceTop = 0;

	// Console.apply
	/// The view switch ("VIEW [1ST|3RD] V") over the card's top right.
	FAcRect ViewSwitch;
	/// Where the card's note stands (centre of its baseline).
	FVector2D NoteAt;

	/// The dashboard's highest point (`Cab.Layout.top`).
	double Top() const;
	/// How high the dashboard reaches at `X` (`dashTop(at:)`).
	double DashTop(double X) const;
	/// How high the console hides the view in its middle (`Console.cover`):
	/// the free camera keeps the map's near edge above it.
	double Cover() const { return DashTop(Size.X / 2) + 8; }
	/// The two lamps upright on the pillars over the dashboard (`Cab.Layout.lamps`;
	/// the `.cab` look): 0 left, 1 right.
	FVector2D Lamp(int32 K) const
	{
		return FVector2D(FMath::RoundHalfFromZero(K == 0 ? Window.MinX() / 2 : (Window.MaxX() + Size.X) / 2), Top() + 40);
	}
	/// The face whose quad as seen holds `P` (Swift points), and where `P` is
	/// on that face laid out flat (`face.projection.inverse`); -1 if on none.
	int32 FaceAt(FVector2D P, FVector2D* Flat = nullptr) const;
	/// The card's button `Slot` (0…14, row by row from the top left).
	FAcRect SlotRect(int32 Slot) const;
	/// The music player's screen in its block (`Cab.deckScreen`).
	static FAcRect DeckScreen(const FAcRect& Block);

	/// Swift points (y up) → Slate points (y down) in a view this size.
	FVector2D ToSlate(FVector2D P) const { return FVector2D(P.X, Size.Y - P.Y); }
	FVector2D FromSlate(FVector2D P) const { return FVector2D(P.X, Size.Y - P.Y); }
	/// The rectangle's top-left corner and size in Slate points.
	FVector2D SlateTopLeft(const FAcRect& R) const { return FVector2D(R.X, Size.Y - R.MaxY()); }
};
