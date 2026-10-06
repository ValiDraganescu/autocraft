#include "AcPlayerController.h"

#include "AcPointer.h"

AAcPlayerController::AAcPlayerController()
{
	PointerComponent = CreateDefaultSubobject<UAcPointer>(TEXT("Pointer"));
	// The game is played with the pointer (the RTS pawn sets the input mode).
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}
