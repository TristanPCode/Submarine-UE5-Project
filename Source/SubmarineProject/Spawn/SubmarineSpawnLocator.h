#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpawnSettings.h"
#include "SubmarineSpawnLocator.generated.h"

class UArrowComponent;
class UBillboardComponent;

/**
 * ASubmarineSpawnLocator
 *
 * Generic spawn marker placed manually in the level.
 * Create ONE Blueprint subclass (BP_SubmarineSpawnLocator) and place instances.
 * All configuration is per-instance via the Details panel.
 *
 * Two non-exclusive ways to make a locator available to a team:
 *
 *   1. GROUP SYSTEM (bAnyTeam = false)
 *      Set GroupIndex to declare which team's "home zone" this locator
 *      belongs to. UTeamSpawnSettings controls which groups each team may
 *      use based on how many teams are active.
 *      Example: GroupIndex=2 means "Team 2 home". In a 2-team game,
 *      TeamSpawnSettings may also give Team 0 access to group 2.
 *
 *   2. ANY TEAM (bAnyTeam = true)
 *      This locator is always available to every team, regardless of
 *      team count or group mappings. GroupIndex is ignored.
 *      Used for: neutral/central spawn areas, fallback zones, etc.
 *
 * Resolution order in SpawnManager:
 *   Pass 1: locators matching the team's allowed groups (via TeamSpawnSettings)
 *   Pass 2: locators with bAnyTeam = true
 *   Pass 3: fallback (center-of-map + progressive offset)
 *
 * Within each pass, locators are sorted by SpawnPriority (lower = preferred).
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API ASubmarineSpawnLocator : public AActor
{
    GENERATED_BODY()

public:
    ASubmarineSpawnLocator();

    // -----------------------------------------------------------------------
    //  Per-instance configuration (editable in Details panel)
    // -----------------------------------------------------------------------

    /**
     * If true, any team may use this locator regardless of TeamIndex.
     * If false, only submarines matching TeamIndex may use it.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnLocator")
    bool bAnyTeam = true;

    /**
     * Spawn locator group index (0-based). Ignored when bAnyTeam = true.
     * Convention:
     *   0..7  = one dedicated group per team (Group N = Team N home zone)
     *   8+    = named shared zones (use bAnyTeam instead for true any-team)
     *
     * Which groups a team may use is resolved by UTeamSpawnSettings at runtime.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnLocator",
        meta = (EditCondition = "!bAnyTeam", ClampMin = "0", ClampMax = "15"))
    int32 GroupIndex = 0;

    /**
     * Which controller types may use this locator.
     * Any = human or CPU, PlayerOnly = human only, CPUOnly = CPU only.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnLocator")
    ESpawnOccupationType OccupationType = ESpawnOccupationType::Any;

    /**
     * Spawn priority. Lower value = preferred first during resolution.
     * When multiple valid locators exist for an entry, the lowest priority
     * value is selected.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnLocator",
        meta = (ClampMin = "0"))
    int32 SpawnPriority = 0;

    // -----------------------------------------------------------------------
    //  Billboard icon
    //
    //  Resolution order:
    //    1. BillboardIconOverride (if set per-instance in Details panel)
    //    2. AnyTeamIcon           (if bAnyTeam = true)
    //    3. PlayerIconsByGroup[GroupIndex]  (if OccupationType == PlayerOnly)
    //    4. CPUIconsByGroup[GroupIndex]     (if OccupationType == CPUOnly)
    //    5. AnyTypeIconsByGroup[GroupIndex] (Any, or fallback for 3/4)
    //    6. nullptr (no icon shown)
    //
    //  Set the default icon arrays in BP_SubmarineSpawnLocator Blueprint
    //  defaults so all instances share the same set automatically.
    //  Override BillboardIconOverride per-instance for special cases.
    // -----------------------------------------------------------------------

    /**
     * Per-instance icon override. Set this in the Details panel to override
     * the automatic icon for this specific locator. Leave null for auto.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnLocator|Icon")
    TObjectPtr<UTexture2D> BillboardIconOverride;

    /** Icon for any-team locators (bAnyTeam = true). Set in BP defaults. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnLocator|Icon|Defaults")
    TObjectPtr<UTexture2D> AnyTeamIcon;

    /**
     * Icons for PlayerOnly locators, indexed by GroupIndex (0-7).
     * Set in BP_SubmarineSpawnLocator defaults to auto-assign icons per team.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnLocator|Icon|Defaults")
    TArray<TObjectPtr<UTexture2D>> PlayerIconsByGroup;

    /** Icons for CPUOnly locators, indexed by GroupIndex. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnLocator|Icon|Defaults")
    TArray<TObjectPtr<UTexture2D>> CPUIconsByGroup;

    /**
     * Icons for Any-type locators, indexed by GroupIndex.
     * Also used as fallback when Player/CPU arrays are too short.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnLocator|Icon|Defaults")
    TArray<TObjectPtr<UTexture2D>> AnyTypeIconsByGroup;

    // -----------------------------------------------------------------------
    //  Runtime state (set by SpawnManager, not authored)
    // -----------------------------------------------------------------------

    /** True when this locator has been assigned to a spawn entry. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpawnLocator|Runtime")
    bool bOccupied = false;

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    /**
     * Returns true if this locator can accept a submarine with the given
     * OccupationType and is not occupied.
     */
    bool CanAccept(ESpawnOccupationType InType) const;

    /** Returns the world transform to use when spawning a submarine here. */
    FTransform GetSpawnTransform() const { return GetActorTransform(); }

    /** Called by SpawnManager to mark this locator as taken. */
    void MarkOccupied() { bOccupied = true; }

    /** Reset occupation state (e.g. for match restart). */
    void ResetOccupation() { bOccupied = false; }

#if WITH_EDITORONLY_DATA

    UPROPERTY()
    TObjectPtr<UArrowComponent> DirectionArrow;

    UPROPERTY()
    TObjectPtr<UBillboardComponent> IconBillboard;

#endif

protected:

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    void UpdateEditorVisualization();
    UTexture2D* ResolveBillboardIcon() const;
#endif
};