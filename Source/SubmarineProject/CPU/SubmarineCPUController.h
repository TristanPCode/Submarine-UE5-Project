#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SubmarineCPUController.generated.h"

/**
 * ASubmarineCPUController
 *
 * Controller for CPU-driven submarines.
 * Uses the EXACT SAME ASubmarinePawn as human players — only the controller
 * differs. This keeps all gameplay/physics/HUD/radar logic fully unified.
 *
 * Currently a stub — no AI behavior yet.
 * The architecture is in place for future USubmarineBrainComponent integration.
 *
 * Key rules enforced by this class:
 *   - IsLocalPlayerController() returns false (inherited from AAIController)
 *   - Never triggers death sequence, replay, or camera transitions
 *   - Never owns HUD widgets
 *   - Never participates in split-screen viewport management
 *
 * Identification pattern used throughout the codebase:
 *   APlayerController* PC = Cast<APlayerController>(SomeController);
 *   if (!PC || !PC->IsLocalPlayerController()) -> CPU/remote path
 */
UCLASS(BlueprintType, Blueprintable)
class SUBMARINEPROJECT_API ASubmarineCPUController : public AAIController
{
    GENERATED_BODY()

public:
    ASubmarineCPUController();

    // Future: USubmarineBrainComponent* Brain = nullptr;
    // Uncomment and implement when AI behavior is added:
    //
    // UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CPU")
    // TObjectPtr<USubmarineBrainComponent> Brain;

    /**
     * Explicit CPU identification.
     * Prefer checking Cast<APlayerController>(Controller) == nullptr in
     * most systems, but this is available for cases that need to distinguish
     * CPU from remote (non-local) human players.
     */
    UFUNCTION(BlueprintPure, Category = "CPU")
    bool IsCPUController() const { return true; }
};