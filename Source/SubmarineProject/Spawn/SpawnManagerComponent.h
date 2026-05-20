#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpawnSettings.h"
#include "TeamSpawnSettings.h"
#include "SubmarinePlayerController.h"
#include "InputMappingContext.h"
#include "SpawnManagerComponent.generated.h"

class URuntimeMatchSettings;
class UHUDGlobalDefaults;
class ASubmarineSpawnLocator;
class ASubmarinePawn;
class ASubmarineCPUController;
class USubmarineHUDComponent;
class UHUDTransitionManager;
class UInputMappingContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllSubmarinesSpawned);

/**
 * USpawnManagerComponent
 *
 * Owned by ASubmarineGameMode.
 * Resolves spawn assignments from URuntimeMatchSettings, then executes spawning.
 *
 * Two-phase design:
 *
 *   Phase 1 — ResolveSpawnEntries():
 *     Reads URuntimeMatchSettings, collects ASubmarineSpawnLocator actors,
 *     builds TArray<FSpawnedSubmarineEntry> with all assignments determined.
 *     NO pawns are spawned. NO controllers are created.
 *     This phase can run before assets finish loading.
 *
 *   Phase 2 — ExecuteSpawn():
 *     Spawns all pawns and controllers.
 *     Configures split-screen viewport if needed.
 *     Creates second local player if needed.
 *     Assigns HUD to each local player controller.
 *     Broadcasts OnAllSubmarinesSpawned when complete.
 *
 * Resolution order for spawn locators:
 *   1. Locators matching exact TeamIndex + OccupationType
 *   2. Locators matching TeamIndex + Any occupation
 *   3. AnyTeam locators matching OccupationType
 *   4. AnyTeam + Any occupation
 *   5. Fallback: center-of-map + progressive offset (200cm steps)
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API USpawnManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USpawnManagerComponent();

    // -----------------------------------------------------------------------
    //  Configuration
    // -----------------------------------------------------------------------

    /**
     * Team spawn mapping for this map.
     * Defines which locator groups each team may use based on how many teams
     * are active. Assign a DA_TeamSpawnSettings asset here (per-map).
     * If null, each team only uses its home group (GroupIndex == TeamIndex).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
    TObjectPtr<UTeamSpawnSettings> TeamSpawnSettings;

    /**
     * Fallback spawn origin when no locators are available.
     * Progressive offset along X axis: (200,0,0), (400,0,0), etc.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
    FVector FallbackSpawnOrigin = FVector::ZeroVector;

    /** Step size (cm) for progressive fallback offset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn",
        meta = (ClampMin = "100"))
    float FallbackSpawnStep = 200.f;

    /**
     * Input Mapping Context for keyboard players.
     * Assign IMC_Keyboard in the Blueprint editor.
     * Created in UE5: Content Browser -> Input -> Input Mapping Context.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TObjectPtr<UInputMappingContext> KeyboardMappingContext;

    /**
     * Input Mapping Context for gamepad players.
     * Assign IMC_Gamepad in the Blueprint editor.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TObjectPtr<UInputMappingContext> GamepadMappingContext;

    /**
     * When true, all spawn locators sharing the same SpawnPriority value
     * within a given pass are collected and one is chosen randomly.
     * When false, the first (lowest-index after sorting) is always taken.
     *
     * Also affects bAnyTeam locators: when true and group passes are exhausted,
     * a random available bAnyTeam locator is chosen instead of the first one.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
    bool bRandomizeEqualPriority = false;



    // -----------------------------------------------------------------------
    //  Two-phase spawn API
    // -----------------------------------------------------------------------

    /**
     * Phase 1: Resolve spawn entries from runtime settings.
     * Call this before ExecuteSpawn().
     * Safe to call before assets are loaded.
     *
     * @param RuntimeSettings  The resolved runtime match settings.
     */
    UFUNCTION(BlueprintCallable, Category = "Spawn")
    void ResolveSpawnEntries(URuntimeMatchSettings* RuntimeSettings);

    /**
     * Phase 2: Execute all spawns.
     * Requires Phase 1 to have run first.
     * Also configures split-screen and HUD.
     *
     * @param RuntimeSettings   The same settings used in Phase 1.
     * @param GlobalHUDDefaults The project-wide HUD defaults (from GameMode).
     */
    UFUNCTION(BlueprintCallable, Category = "Spawn")
    void ExecuteSpawn(URuntimeMatchSettings* RuntimeSettings,
        UHUDGlobalDefaults* GlobalHUDDefaults);

    /**
     * Reset all locator occupation states.
     * Call before a match restart to allow re-use of the same locators.
     */
    UFUNCTION(BlueprintCallable, Category = "Spawn")
    void ResetSpawnLocators();

    // -----------------------------------------------------------------------
    //  Data access
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Spawn")
    const TArray<FSpawnedSubmarineEntry>& GetSpawnEntries() const
    {
        return SpawnEntries;
    }

    /** Find the entry for a given pawn (null if not found). */
    const FSpawnedSubmarineEntry* FindEntryForPawn(ASubmarinePawn* Pawn) const;

    /** Find the entry for a given controller (null if not found). */
    const FSpawnedSubmarineEntry* FindEntryForController(AController* Controller) const;

    // -----------------------------------------------------------------------
    //  Events
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Spawn|Events")
    FOnAllSubmarinesSpawned OnAllSubmarinesSpawned;

private:

    TArray<FSpawnedSubmarineEntry> SpawnEntries;
    bool bEntriesResolved = false;
    int32 FallbackSpawnCount = 0;  // tracks how many fallback spawns have been used

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    /**
     * Collect all ASubmarineSpawnLocator actors in the world,
     * sorted by SpawnPriority (ascending).
     */
    TArray<ASubmarineSpawnLocator*> CollectSortedLocators() const;

    /**
     * Find the best available locator for an entry.
     * Resolution order: exact match group -> team only -> any team.
     * Returns nullptr if no locator is available (triggers fallback).
     */
    ASubmarineSpawnLocator* FindBestLocatorForTeam(
        TArray<ASubmarineSpawnLocator*>& Locators,
        int32 TeamIndex,
        int32 TeamsInPlay,
        ESpawnOccupationType OccupationType) const;

    /**
     * Build a fallback spawn transform using progressive offset.
     * Each call increments the internal fallback counter.
     */
    FTransform BuildFallbackTransform();

    /**
     * Configure the viewport for split-screen.
     * Called during ExecuteSpawn when LocalPlayerCount >= 2.
     */
    void ConfigureSplitScreen(bool bEnable);

    /**
     * Ensure a second local player exists and return their PlayerController.
     * Creates the local player if it doesn't exist yet.
     */
    APlayerController* EnsureLocalPlayer(int32 LocalPlayerIndex);

    /**
     * Attach USubmarineHUDComponent and UHUDTransitionManager to a
     * PlayerController if not already present.
     * Then transitions to the correct gameplay context.
     */
    void InitializeHUDForPlayer(
        APlayerController* PC,
        ASubmarinePawn* TrackedPawn,
        URuntimeMatchSettings* RuntimeSettings,
        UHUDGlobalDefaults* GlobalHUDDefaults,
        bool bSplitScreen, int32 LocalPlayerIndex);

    ASubmarineSpawnLocator* PickFromGroups(
    TArray<ASubmarineSpawnLocator*>& Locators,
    const TArray<int32>& Groups,
    ESpawnOccupationType OccupationType) const;
};