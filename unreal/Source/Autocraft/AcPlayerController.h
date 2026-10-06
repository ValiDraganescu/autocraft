// `AAcPlayerController`: the local player's controller (GAME-LAYER.md §3.2).
// It holds the pointer (`UAcPointer`, chunk D4: hover, cursors, tip, clicks,
// selection); the RTS pawn (A3) owns pan and zoom, and the pilot chunk (E2)
// adds driving and leaving here (bind `UAcPointer::OnTakeOver`).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "AcPlayerController.generated.h"

class UAcPointer;

UCLASS()
class AUTOCRAFT_API AAcPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAcPlayerController();

	UAcPointer* Pointer() const { return PointerComponent; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Autocraft")
	TObjectPtr<UAcPointer> PointerComponent;
};
