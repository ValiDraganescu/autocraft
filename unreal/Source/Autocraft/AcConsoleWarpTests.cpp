// Automation tests for the console's warp (chunk D3): the faces' projection
// and its inverse, and the console's hit tests and mouse hand-over through
// the warped faces. Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Console; Quit" -TestExit="Automation Test Queue Empty"
#include "AcCab.h"
#include "SAcConsole.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Widgets/SLeafWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/// Records the mouse events the console hands it (in its own box).
	class SAcPointerLog : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAcPointerLog) {}
		SLATE_END_ARGS()
		void Construct(const FArguments&) {}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(10, 10); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			return Layer;
		}
		virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
		{
			Log.Add(FString::Printf(TEXT("down %.1f,%.1f of %.1fx%.1f"), G.AbsoluteToLocal(E.GetScreenSpacePosition()).X,
				G.AbsoluteToLocal(E.GetScreenSpacePosition()).Y, G.GetLocalSize().X, G.GetLocalSize().Y));
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override
		{
			Moves.Add(FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition())));
			return FReply::Handled();
		}
		virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override
		{
			Log.Add(TEXT("up"));
			return FReply::Handled().ReleaseMouseCapture();
		}
		TArray<FString> Log;
		TArray<FVector2D> Moves;
	};

	FPointerEvent Mouse(FVector2D At, bool bDown)
	{
		TSet<FKey> Pressed;
		if (bDown) Pressed.Add(EKeys::LeftMouseButton);
		return FPointerEvent(0, At, At, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcConsoleProjectionTest, "Autocraft.Console.Projection", Flags)
bool FAcConsoleProjectionTest::RunTest(const FString&)
{
	const FAcCabLayout L = FAcCabLayout::Make(FVector2D(1600, 1000), FVector2D(220, 170), true);
	TestTrue(TEXT("the sides rise to the pillars"), L.Faces[0].Quad[3].Y > L.FaceTop && L.Faces[2].Quad[2].Y > L.FaceTop);
	for (int32 K = 0; K < 3; ++K)
	{
		const FAcCabFace& F = L.Faces[K];
		const FAcProjection P = F.Projection();
		const FVector2D Corners[4] = {FVector2D(F.Flat.MinX(), F.Flat.MinY()), FVector2D(F.Flat.MaxX(), F.Flat.MinY()),
			FVector2D(F.Flat.MaxX(), F.Flat.MaxY()), FVector2D(F.Flat.MinX(), F.Flat.MaxY())};
		for (int32 C = 0; C < 4; ++C)
		{
			TestTrue(FString::Printf(TEXT("face %d corner %d onto its quad"), K, C), P(Corners[C]).Equals(F.Quad[C], 1e-6));
		}
		const FVector2D Mid(F.Flat.MidX(), F.Flat.MidY());
		FVector2D Back;
		TestEqual(FString::Printf(TEXT("face %d found where it is seen"), K), L.FaceAt(P(Mid), &Back), K);
		TestTrue(FString::Printf(TEXT("face %d back to its flat point"), K), Back.Equals(Mid, 1e-6));
	}
	TestEqual(TEXT("over the dashboard is on no face"), L.FaceAt(FVector2D(800, 600)), -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcConsoleWarpInputTest, "Autocraft.Console.WarpInput", Flags)
bool FAcConsoleWarpInputTest::RunTest(const FString&)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("no Slate application"));
		return true;
	}
	const FVector2D View(1600, 1000);
	TSharedRef<SAcConsole> Console = SNew(SAcConsole);
	TSharedRef<SAcPointerLog> Map = SNew(SAcPointerLog);
	Console->SetMinimap(Map);
	const FGeometry G = FGeometry::MakeRoot(FVector2f(View), FSlateLayoutTransform());
	Console->Tick(G, 0, 0);  // lays out (warped)
	const FAcCabLayout& L = Console->Layout();
	TestTrue(TEXT("laid out warped"), L.bWarped);

	// The card: a button's middle as seen picks that button.
	FAcCardInfo Info;
	Info.Title = TEXT("Citadel");
	FAcCardButton B;
	B.Slot = 7;
	B.Key = TEXT("4");
	Info.Buttons.Add(B);
	Console->Show(Info, 0);
	const FAcRect Slot = L.SlotRect(7);
	const FVector2D Seen = L.ToSlate(L.Faces[2].Projection()(FVector2D(Slot.MidX(), Slot.MidY())));
	TestEqual(TEXT("button under its warped middle"), Console->ButtonAt(Seen), TOptional<int32>(0));
	TestFalse(TEXT("no button under its flat middle"), Console->ButtonAt(L.ToSlate(FVector2D(Slot.MidX(), Slot.MidY()))).IsSet());

	// The minimap: a press at its middle as seen reaches it at its own
	// middle; the drag keeps going to it off the map; the release lets go.
	const FAcRect& M = L.Minimap;
	const FVector2D Mid = L.ToSlate(L.Faces[0].Projection()(FVector2D(M.MidX(), M.MidY())));
	const FReply Down = Console->OnMouseButtonDown(G, Mouse(Mid, true));
	TestTrue(TEXT("press handled"), Down.IsEventHandled());
	TestTrue(TEXT("the console holds the drag"), Down.GetMouseCaptor().Get() == &Console.Get());
	TestEqual(TEXT("press at the map's middle"), Map->Log.Num() > 0 ? Map->Log[0] : FString(),
		FString::Printf(TEXT("down %.1f,%.1f of %.1fx%.1f"), M.W / 2, M.H / 2, M.W, M.H));
	const FVector2D Off = L.ToSlate(L.Faces[0].Projection()(FVector2D(M.MaxX() + 40, M.MidY())));
	Console->OnMouseMove(G, Mouse(Off, true));
	TestTrue(TEXT("the drag runs on off the map"), Map->Moves.Num() == 1 && FMath::IsNearlyEqual(Map->Moves[0].X, M.W + 40, 0.01));
	const FReply Up = Console->OnMouseButtonUp(G, Mouse(Off, false));
	TestTrue(TEXT("release lets go"), Up.ShouldReleaseMouse() && Map->Log.Last() == TEXT("up"));
	// MinimapPoint: Swift's, in the map's own points (y up).
	const TOptional<FVector2D> Own = Console->MinimapPoint(Mid, 6);
	TestTrue(TEXT("minimap point"), Own.IsSet() && Own->Equals(FVector2D(M.W / 2, M.H / 2) / L.MinimapScale, 1e-6));
	return true;
}

#endif
