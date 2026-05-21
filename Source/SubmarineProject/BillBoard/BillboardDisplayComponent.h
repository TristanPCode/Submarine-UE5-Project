#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Billboard/InfoBillboardSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Match/RuntimeMatchSettings.h"
#include "BillboardDisplayComponent.generated.h"

class USubmarineInfoBillboardComponent;
class UInfoBillboardWidget;
class UInfoBillboardContextSettings;
class UCanvasPanel;
class UOverlay;

/**
 * UBillboardDisplayComponent
 *
 * Attach to APlayerController. One instance per local player.
 *
 * Owns a pool of UInfoBillboardWidget instances added to the player's
 * screen-space HUD canvas. Each tick, projects world-space billboard
 * locations into screen space and positions the widgets accordingly.
 *
 * This gives per-player billboard visibility with fixed screen size
 * regardless of world distance -- exactly like most games do it.
 *
 * Architecture:
 *   - USubmarineInfoBillboardComponent stays on the pawn: stores identity data
 *     (team, type, display name) and the resolved UInfoBillboardSettings.
 *   - UBillboardDisplayComponent on the PC: does all rendering, per player.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UBillboardDisplayComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBillboardDisplayComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    /**
     * Set the billboard context used to resolve per-relationship settings.
     * Call after match starts and when HUD context changes.
     */
    UFUNCTION(BlueprintCallable, Category = "Billboard")
    void SetBillboardContext(UInfoBillboardContextSettings* Context,
        int32 LocalPlayerTeamIndex, URuntimeMatchSettings* InMatchSettings = nullptr);

    // Called when the full set of billboard sources is already known (avoids world scan)
    void SetBillboardContextWithSources(
        UInfoBillboardContextSettings* Context,
        int32 InLocalTeamIndex,
        URuntimeMatchSettings* InMatchSettings,
        const TArray<USubmarineInfoBillboardComponent*>& KnownSources);

    /**
     * Widget class to instantiate for each billboard entry.
     * Set this to your BP_InfoBillboardWidget class in the BP subclass or DA.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Billboard")
    TSubclassOf<UInfoBillboardWidget> WidgetClass;

    /** Optional debug settings for log gating. Assign same DA as your HUD settings. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Billboard")
    TObjectPtr<USubmarineHUDDebugSettings> DebugSettings;

    /** Match settings used to look up team colors. Set via SetBillboardContext. */
    UPROPERTY()
    TObjectPtr<URuntimeMatchSettings> MatchSettings;

    /** Local player team index (set by GameMode after spawn). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
    int32 LocalTeamIndex = 0;

private:

    // Remove TObjectPtr from FBillboardEntry — use raw pointer, GC managed via parallel array
    struct FBillboardEntry
    {
        TWeakObjectPtr<USubmarineInfoBillboardComponent> Source;
        UInfoBillboardWidget* Widget = nullptr;  // raw, lifetime owned by TrackedWidgets
        UInfoBillboardSettings* CachedSettings = nullptr; // raw, DA assets are always rooted
        bool  bVisible = false;
        float DiagAccumulator = 0.f;
    };

    // This UPROPERTY array is what actually keeps the widgets alive through GC
    UPROPERTY()
    TArray<TObjectPtr<UInfoBillboardWidget>> TrackedWidgets;

    UPROPERTY()
    TObjectPtr<UInfoBillboardContextSettings> BillboardCtx;

    UPROPERTY()
    TArray<TObjectPtr<UInfoBillboardWidget>> WidgetPool;

    TArray<FBillboardEntry> Entries;

    float TickAccumulator = 0.f;
    float LogicAccumulator = 0.f;

    /** Rebuild the entry list from all USubmarineInfoBillboardComponent actors in the world. */
    void RebuildEntries();

    /** Get or create a widget from the pool. */
    UInfoBillboardWidget* AcquireWidget();

    APlayerController* GetPC() const;
};