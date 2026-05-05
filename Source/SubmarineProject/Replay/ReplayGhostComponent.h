#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReplaySettings.h"
#include "ReplayGhostComponent.generated.h"

/**
 * UReplayGhostComponent
 *
 * Add this component to your ASubmarinePawn and ATorpedoPawn Blueprints.
 *
 * Responsibilities:
 *   1. Marks the owning actor as a "ReplayDynamic" actor at BeginPlay so the
 *      replay system can find and hide it during death cams without explicit
 *      type checks.
 *
 *   2. Provides CloneComponentsOntoGhost() — called by UReplayPlaybackComponent
 *      when spawning a ghost for this actor. It copies:
 *        - Every UStaticMeshComponent  (mesh asset + materials + relative transform)
 *        - Every UNiagaraComponent     (system asset + relative transform, activated)
 *      If you add audio, decals, or other component types in the future, add
 *      them in CopyExtraComponents() which is called at the end of cloning.
 *      That single function is the only place you ever need to touch for new
 *      VFX/SFX types.
 *
 *   3. Ghost components are set up with:
 *        - NoCollision profile
 *        - bCastShadow = false
 *        - SimulatePhysics = false
 *        - GenerateOverlapEvents = false
 *      so they never interact with gameplay.
 *
 * Blueprint setup:
 *   Open your BP_Submarine and BP_Torpedo blueprints.
 *   Add UReplayGhostComponent in the component list.
 *   No further configuration needed.
 */
UCLASS(ClassGroup = (Replay), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UReplayGhostComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UReplayGhostComponent();

    virtual void BeginPlay() override;

    /**
     * Copies all visual components (StaticMesh, Niagara) from the owning actor
     * onto TargetGhost. Called by UReplayPlaybackComponent when spawning a ghost.
     *
     * The root component of TargetGhost is set to the first cloned mesh so the
     * ghost has a valid scene root.
     */
    UFUNCTION(BlueprintCallable, Category = "Replay|Ghost")
    void CloneComponentsOntoGhost(AActor* TargetGhost) const;

    // -----------------------------------------------------------------------
    //  Debug
    // -----------------------------------------------------------------------

    /** Assign the game-wide ReplaySettings DataAsset here in the editor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Debug")
    TObjectPtr<UReplaySettings> Settings;

private:
    void CopyStaticMeshComponents(AActor* Target) const;
    void CopyNiagaraComponents(AActor* Target) const;

    /**
     * Override point for future component types (audio, decals, cables, etc.).
     * Called at the end of CloneComponentsOntoGhost. Default implementation
     * does nothing.
     */
    virtual void CopyExtraComponents(AActor* Target) const {}

    /** Applies ghost-safe flags to a primitive component. */
    static void ApplyGhostFlags(UPrimitiveComponent* Prim);

    const UReplaySettings* GetSettings() const;
};