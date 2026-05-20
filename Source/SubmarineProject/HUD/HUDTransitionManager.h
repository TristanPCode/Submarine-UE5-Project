#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HUDContextTypes.h"
#include "HUDTransitionManager.generated.h"

class URuntimeMatchSettings;
class UHUDGlobalDefaults;
class USubmarineHUDComponent;
class ASubmarinePawn;

/**
 * UHUDTransitionManager
 *
 * Attach to APlayerController (alongside USubmarineHUDComponent).
 * One instance per local player — never shared.
 *
 * Responsibilities:
 *   - Track current EHUDContext
 *   - Resolve HUD DataAsset from RuntimeMatchSettings + GlobalDefaults
 *   - Drive HUD DataAsset swaps via USubmarineHUDComponent::SwapHUDSettings
 *   - Drive fade in/out on the HUD root widget during context transitions
 *   - Expose current context for query by other systems
 *
 * Fade system:
 *   TransitionToContext() drives RootWidget->SetRenderOpacity:
 *     1->0 (fade out, FadeDuration/2)
 *     swap DataAsset + reinitialize modules
 *     0->1 (fade in, FadeDuration/2)
 *
 * Important design rule:
 *   HUD context is PRESENTATION state, not gameplay state.
 *   Systems call TransitionToContext explicitly — it is never auto-triggered.
 *   Gameplay systems (GameMode, death sequence) call this when appropriate.
 *
 * Split-screen safety:
 *   All operations are scoped to the owning PlayerController.
 *   Never references "the player" as a singleton.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UHUDTransitionManager : public UActorComponent
{
    GENERATED_BODY()

public:
    UHUDTransitionManager();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -----------------------------------------------------------------------
    //  Setup (called by SpawnManagerComponent after player is ready)
    // -----------------------------------------------------------------------

    /**
     * Set the runtime settings and global defaults used for HUD resolution.
     * Must be called before the first TransitionToContext.
     */
    UFUNCTION(BlueprintCallable, Category = "HUDTransition")
    void SetRuntimeSettings(URuntimeMatchSettings* InSettings,
        UHUDGlobalDefaults* InGlobalDefaults);

    // -----------------------------------------------------------------------
    //  Context management
    // -----------------------------------------------------------------------

    /**
     * Transition to a new HUD context.
     * Fade out -> swap DataAsset + reinitialize -> fade in.
     *
     * @param NewContext      The context to transition to.
     * @param FadeDuration    Total duration (fade out + fade in) in seconds.
     *                        Pass 0 for an immediate swap with no fade.
     * @param NewTrackedPawn  Optional: change the tracked submarine during
     *                        the transition (e.g. spectator -> new target).
     *                        Pass nullptr to keep the current tracked submarine.
     */
    UFUNCTION(BlueprintCallable, Category = "HUDTransition")
    void TransitionToContext(EHUDContext NewContext, ASubmarinePawn* NewTrackedPawn = nullptr);

    /** Immediately set context with no fade (for loading screen -> gameplay). */
    UFUNCTION(BlueprintCallable, Category = "HUDTransition")
    void SetContextImmediate(EHUDContext NewContext,
        ASubmarinePawn* NewTrackedPawn = nullptr);

    UFUNCTION(BlueprintPure, Category = "HUDTransition")
    EHUDContext GetCurrentContext() const { return CurrentContext; }

    // -----------------------------------------------------------------------
    //  Module visibility (called after DataAsset swap)
    // -----------------------------------------------------------------------

    /**
     * Apply per-module visibility based on HiddenInContexts.
     * Called automatically after every context transition.
     */
    UFUNCTION(BlueprintCallable, Category = "HUDTransition")
    void ApplyModuleVisibility();

    UFUNCTION()
    void OnScreenFadeAlphaChanged(float Alpha)
    {
        // Alpha: 0 = fully visible, 1 = fully black (same as ClientSetCameraFade)
        // Only mirrors camera if no independent HUD fade is active
        if (!bHUDFadeActive)
            SetHUDOpacity(1.f - Alpha);
    }

    /** Drive root widget opacity. 0 = fully transparent, 1 = fully visible. */
    void SetHUDOpacity(float Opacity);

    /** Set opacity of the darkness overlay (creates it if needed). */
    void SetHUDDarknessOverlayOpacity(float Opacity);

    /* Set Visibility of either way for more dynamic parameters*/
    void SetHUDVisibility(float Opacity, bool bDarknessMode = false) {
        if (bDarknessMode) {
            SetHUDDarknessOverlayOpacity(Opacity);
        }
        else {
            SetHUDOpacity(Opacity);
        }
    }

    /**
     * Fade HUD from 0 to 1 over Duration seconds, independent of camera.
     * Immediately sets opacity to 0 then interpolates to 1.
     * Cancels any in-progress HUD fade.
     */
     /**
      * Fade HUD from invisible to fully visible over Duration seconds.
      * bDarknessMode = false (default): widget opacity 0->1 (modules tick at 0 opacity)
      * bDarknessMode = true:  widget stays at opacity 1, a black overlay fades OUT.
      *                        Modules are fully active and visible through the veil.
      */
    UFUNCTION(BlueprintCallable, Category = "HUDTransition")
    void HUDFadeIn(float Duration, bool bDarknessMode = false);

    /**
     * Fade HUD from fully visible to invisible over Duration seconds.
     * bDarknessMode = false (default): widget opacity 1->0.
     * bDarknessMode = true:  widget stays at opacity 1, a black overlay fades IN.
     */
    UFUNCTION(BlueprintCallable, Category = "HUDTransition")
    void HUDFadeOut(float Duration, bool bDarknessMode = false);

private:

    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------

    EHUDContext CurrentContext = EHUDContext::None;

    UPROPERTY()
    TObjectPtr<URuntimeMatchSettings> RuntimeSettings;

    UPROPERTY()
    TObjectPtr<UHUDGlobalDefaults> GlobalDefaults;

    // TransitionToContext is always immediate -- no internal fade state needed.
    EHUDContext  PendingContext = EHUDContext::None;
    TWeakObjectPtr<ASubmarinePawn> PendingTrackedPawn;

    // -----------------------------------------------------------------------
    //  Independent HUD fade state (HUDFadeIn / HUDFadeOut)
    // -----------------------------------------------------------------------

    bool  bHUDFadeActive = false;   // true while HUDFadeIn/Out is running
    bool  bHUDFadingIn = false;   // true = 0->1, false = 1->0
    bool  bHUDDarknessMode = false;  // true = black overlay instead of widget opacity
    float HUDFadeTimer = 0.f;     // elapsed time in current fade
    float HUDFadeDuration = 1.f;     // total duration of this fade

    /** Applied over the HUD in darkness mode. Created on demand. */
    UPROPERTY()
    TObjectPtr<class UImage> DarknessOverlay;

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    USubmarineHUDComponent* GetHUDComponent() const;

    /**
     * Execute the actual DataAsset swap and module reinitialization.
     * Called in the middle of the fade (between fade-out and fade-in).
     */
    void ExecuteContextSwap(EHUDContext NewContext, ASubmarinePawn* NewPawn);
};