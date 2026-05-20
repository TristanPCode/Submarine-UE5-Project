#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "InfoBillboardSettings.h"
#include "SubmarineInfoBillboardComponent.generated.h"

class UInfoBillboardSettings;
class UInfoBillboardContextSettings;
class UInfoBillboardWidget;
class URuntimeMatchSettings;

/**
 * USubmarineInfoBillboardComponent
 *
 * Attach to ASubmarinePawn or ATorpedoPawn.
 * Manages a world-space UInfoBillboardWidget shown above the entity.
 *
 * Each frame:
 *   1. Determines the billboard relationship from the local player's perspective
 *   2. Resolves UInfoBillboardSettings from the current HUD context
 *   3. Applies settings to the widget (text, colors, visibility)
 *   4. Updates dynamic fields ({TimeLeft} etc.)
 *
 * The component is always present on the pawn but may render nothing
 * if the active context settings say bVisible = false.
 *
 * Ownership: created by the GameMode (via SpawnManager) after spawn,
 * NOT in the pawn constructor. This allows context-aware initialization.
 *
 * For now, attach manually to BP_SubmarinePawn and BP_TorpedoPawn.
 * Future: SpawnManager adds it dynamically.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API USubmarineInfoBillboardComponent : public UWidgetComponent
{
    GENERATED_BODY()

public:
    USubmarineInfoBillboardComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -----------------------------------------------------------------------
    //  Setup (called after spawn)
    // -----------------------------------------------------------------------

    /**
     * Initialize with match settings and entity type.
     * Call after SpawnManager places the pawn.
     *
     * @param InEntityType   Submarine or Torpedo
     * @param InTeamIndex    Team index of this entity (0-based)
     * @param bInIsCPU       True if CPU-controlled
     * @param InOwnerName    Display name for torpedo owner (torpedoes only)
     */
    UFUNCTION(BlueprintCallable, Category = "Billboard")
    void InitializeBillboard(
        EBillboardEntityType InEntityType,
        int32                InTeamIndex,
        bool                 bInIsCPU,
        const FString& InOwnerName = TEXT(""));

    /**
     * Update the active billboard settings for the current HUD context.
     * Called by HUDTransitionManager when context changes.
     */
    UFUNCTION(BlueprintCallable, Category = "Billboard")
    void ApplyContextSettings(UInfoBillboardContextSettings* ContextSettings,
        int32 LocalPlayerTeamIndex, APlayerController* ObserverPC = nullptr);

    /**
     * Register a local player's team index for per-observer relationship evaluation.
     * Called by GameMode once per local player during spawn setup.
     * Enables P2 to see correct billboard relationships from their perspective.
     */
    void AddLocalPlayerTeam(int32 LocalPlayerIndex, int32 TeamIndex);

    /**
     * Compute relationship from a specific PC's perspective.
     * Public so BillboardDisplayComponent can call it per-player.
     */
    EBillboardRelationship ComputeRelationshipForPC(
        int32 LocalTeamIndex, APlayerController* ObserverPC) const
    {
        return ComputeRelationship(LocalTeamIndex, false, ObserverPC);
    }

    /** Evaluate the text template and return the resolved string. Public for BillboardDisplayComponent. */
    FString GetEvaluatedText(const FString& Template) const
    {
        return EvaluateTemplate(Template);
    }

    /** Identification gate check for a specific PC. */
    bool CheckIdentificationGateForPC(APlayerController* ObserverPC) const;

    /** Called by ScreenFadeComponent when fade alpha changes. */
    UFUNCTION()
    void OnScreenFadeAlphaChanged(float Alpha);


    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------

    // In SubmarineInfoBillboardComponent.h, add:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
    FString EntityTeamName;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
    FString EntityDisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Billboard")
    EBillboardEntityType EntityType = EBillboardEntityType::Submarine;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
    int32 EntityTeamIndex = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
    bool bEntityIsCPU = false;

    /** For torpedoes: display name of the firing submarine. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
    FString TorpedoOwnerName;

    /**
     * If true, this billboard is only visible when the local player's tracked
     * submarine has IDENTIFIED (not just detected) this entity on radar.
     * Applies in Gameplay context. Always visible in Spectator/Replay.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Billboard")
    bool bRequireIdentification = true;

    // ADD to SubmarineInfoBillboardComponent.h private section:
    bool CheckIdentificationGate() const;

private:

    UPROPERTY()
    TObjectPtr<UInfoBillboardWidget> BillboardWidget;

    UPROPERTY()
    TObjectPtr<UInfoBillboardSettings> ActiveSettings;

    float TickAccumulator = 0.f;

    /**
     * Per-local-player team indices, indexed by LocalPlayerIndex.
     * Populated by AddLocalPlayerTeam() during spawn setup.
     * Used in Tick to re-evaluate relationship from each observer's perspective.
     */
    TArray<int32> LocalPlayerTeamIndices;

    /** Cached context settings for per-observer re-evaluation in Tick. */
    UPROPERTY()
    TObjectPtr<UInfoBillboardContextSettings> CachedBillboardCtx;

    /** Last observer PC index used for re-evaluation (avoids redundant ApplyContextSettings calls). */
    int32 LastObserverPCIdx = -1;

    /** True while the screen fade alpha is > 0 -- billboard is hidden during fades. */
    bool bSuppressedByFade = false;

    /** Determine the relationship between the local player and this entity. */
    EBillboardRelationship ComputeRelationship(int32 LocalPlayerTeamIndex,
        bool bLocalPlayerIsCPU, APlayerController* ObserverPC = nullptr) const;

    /** Evaluate the template string and return the resolved display text. */
    FString EvaluateTemplate(const FString& Template) const;

    /** Replace all known {Field} tokens in the template. */
    FString ResolveField(const FString& FieldName) const;
};