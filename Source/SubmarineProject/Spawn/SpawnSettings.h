#pragma once

#include "CoreMinimal.h"
#include "MatchSettingsDataAsset.h"
#include "SpawnSettings.generated.h"

class ASubmarinePawn;
class ASubmarineSpawnLocator;

/**
 * ESpawnOccupationType
 *
 * Controls which controller types may use a given spawn locator.
 */
UENUM(BlueprintType)
enum class ESpawnOccupationType : uint8
{
    /** Any controller type (human or CPU). */
    Any,

    /** Only human (local or remote) players. */
    PlayerOnly,

    /** Only CPU-controlled submarines. */
    CPUOnly
};

/**
 * FSpawnedSubmarineEntry
 *
 * Resolved spawn assignment for one submarine.
 * Built by USpawnManagerComponent::ResolveSpawnEntries() BEFORE any pawn is
 * spawned. Pawn and controller pointers are filled during ExecuteSpawn().
 *
 * This struct is the single source of truth for "who spawns where with what".
 */
USTRUCT(BlueprintType)
struct FSpawnedSubmarineEntry
{
    GENERATED_BODY()

    // -----------------------------------------------------------------------
    //  Identity (set during resolution, immutable after)
    // -----------------------------------------------------------------------

    /** Stable unique identifier for this entry. */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    FGuid EntryGuid;

    /** True if controlled by a local human player. */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    bool bIsLocalPlayer = false;

    /** True if controlled by a remote (networked) player. */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    bool bIsRemotePlayer = false;

    /** True if controlled by a CPU. */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    bool bIsCPU = false;

    /** Team index (0-based). */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    int32 TeamIndex = 0;

    /**
     * Local player index (0-based).
     * -1 for CPU and remote players.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    int32 LocalPlayerIndex = -1;

    /** Display name shown in HUD and kill feed. */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    FString DisplayName;

    /** Level of this submarine (from RuntimeMatchSettings). */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    int32 Level = 1;

    /**
     * Blueprint subclass of ASubmarinePawn to spawn.
     * Resolved from RuntimeMatchSettings with DefaultSubmarineClass fallback.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    TSubclassOf<ASubmarinePawn> SubmarineClass;

    // -----------------------------------------------------------------------
    //  Spawn locator (set during resolution)
    // -----------------------------------------------------------------------

    /**
     * The spawn locator assigned to this entry.
     * May be null if fallback offset spawning was used.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    TWeakObjectPtr<ASubmarineSpawnLocator> AssignedSpawnLocator;

    /**
     * Fallback spawn transform (used when no locator is available).
     * Only valid when AssignedSpawnLocator is null.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    FTransform FallbackSpawnTransform;

    // -----------------------------------------------------------------------
    //  Runtime pointers (filled during ExecuteSpawn)
    // -----------------------------------------------------------------------

    /** The spawned submarine pawn. Null before ExecuteSpawn(). */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    TWeakObjectPtr<ASubmarinePawn> SpawnedPawn;

    /** The controller assigned to the pawn. Null before ExecuteSpawn(). */
    UPROPERTY(BlueprintReadOnly, Category = "Spawn")
    TWeakObjectPtr<AController> AssignedController;

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    bool IsHumanControlled() const { return bIsLocalPlayer || bIsRemotePlayer; }

    /**
     * Get the spawn transform for this entry.
     * Uses locator transform if available, otherwise fallback transform.
     */
    FTransform GetSpawnTransform() const;
};