#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpectatorTrackerComponent.generated.h"

class ASubmarinePawn;
class USubmarineHUDComponent;

/**
 * USpectatorTrackerComponent
 *
 * Attach to APlayerController.
 * Resolves the "best" submarine to track when entering Spectator or Replay mode.
 *
 * Resolution order:
 *   1. Local player's own submarine (if still alive)
 *   2. Any other local player's submarine
 *   3. Any human-controlled submarine
 *   4. Any submarine in the world
 *
 * Also exposes NextTarget() / PreviousTarget() for cycling through available
 * submarines (future: input binding for spectator switching).
 *
 * Called by HUDTransitionManager when context switches to Spectator or Replay.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API USpectatorTrackerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USpectatorTrackerComponent();

    /**
     * Find and return the best submarine to track right now.
     * Returns nullptr if no submarines are in the world.
     * Also calls SetTrackedSubmarine on the HUDComponent automatically.
     */
    UFUNCTION(BlueprintCallable, Category = "Spectator")
    ASubmarinePawn* AutoSelectTarget();

    /**
     * Cycle to the next submarine in the world (for spectator switching).
     * Returns the newly tracked submarine.
     */
    UFUNCTION(BlueprintCallable, Category = "Spectator")
    ASubmarinePawn* NextTarget();

    /**
     * Cycle to the previous submarine in the world.
     */
    UFUNCTION(BlueprintCallable, Category = "Spectator")
    ASubmarinePawn* PreviousTarget();

    UFUNCTION(BlueprintPure, Category = "Spectator")
    ASubmarinePawn* GetCurrentTarget() const { return CurrentTarget.Get(); }

private:

    TWeakObjectPtr<ASubmarinePawn> CurrentTarget;

    /** Collect all live submarines sorted by priority. */
    TArray<ASubmarinePawn*> CollectAvailableTargets() const;

    /** Apply the target to the HUDComponent. */
    void ApplyTarget(ASubmarinePawn* Target);
};