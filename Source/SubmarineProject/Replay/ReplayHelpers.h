#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraEffectType.h"
#include "NiagaraCommon.h"
#include "ReplayHelpers.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;

/**
 * UReplayHelpers
 *
 * Blueprint function library providing a tagged version of
 * SpawnSystemAtLocation. Use this everywhere a standalone Niagara actor
 * is spawned for a dynamic gameplay event (torpedo explosion, submarine
 * death VFX, hit sparks, etc.).
 *
 * The spawned ANiagaraActor is tagged "ReplayDynamic" so
 * UReplayPlaybackComponent can hide it from the dead player's view
 * during death replay cams without affecting other players.
 *
 * Static / ambient VFX (e.g. environmental bubbles) that should always
 * be visible can still use SpawnSystemAtLocation directly.
 */
UCLASS()
class SUBMARINEPROJECT_API UReplayHelpers : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Tag constants
    // -----------------------------------------------------------------------

    /** Tag applied to real dynamic actors (submarines, torpedoes, their standalone VFX). */
    static const FName Tag_ReplayDynamic;

    /** Tag applied to ghost actors spawned by the replay system. */
    static const FName Tag_ReplayGhost;

    // -----------------------------------------------------------------------
    //  Dead player registry
    //
    //  ReplayPlaybackComponent calls RegisterDeadPlayer when playback starts
    //  and UnregisterDeadPlayer when it stops. SpawnGameplayVFXAtLocation
    //  uses this to hide live effects from anyone watching a death replay.
    // -----------------------------------------------------------------------

    static void RegisterDeadPlayer(APlayerController* PC);
    static void UnregisterDeadPlayer(APlayerController* PC);
    static bool IsDeadPlayer(APlayerController* PC);

private:
    static TSet<TWeakObjectPtr<APlayerController>> ActiveDeadPlayers;
public:

    // -----------------------------------------------------------------------
    //  VFX spawn -- live gameplay
    // -----------------------------------------------------------------------

    /**
     * Spawn a Niagara effect for live gameplay (torpedoes, submarine explosions, etc.).
     * No per-player visibility restrictions. Owner = WorldSettings (UE default).
     * All PlayerControllers see it, including any dead player watching a replay.
     */
    UFUNCTION(BlueprintCallable, Category = "VFX",
        meta = (WorldContext = "WorldContextObject"))
    static UNiagaraComponent* SpawnGameplayVFXAtLocation(
        const UObject* WorldContextObject,
        UNiagaraSystem* SystemTemplate,
        FVector         Location,
        FRotator        Rotation = FRotator::ZeroRotator,
        FVector         Scale = FVector(1.f),
        bool            bAutoDestroy = true,
        bool            bAutoActivate = true,
        ENCPoolMethod   PoolingMethod = ENCPoolMethod::None,
        bool            bPreCullCheck = false);

    // -----------------------------------------------------------------------
    //  VFX spawn -- replay playback
    // -----------------------------------------------------------------------

    /**
     * Spawn a Niagara effect for replay VFX events (TickVFXEvents).
     *
     * Creates a dedicated lightweight holder AActor so per-player visibility
     * is fully controllable without touching WorldSettings.
     *
     * DeadPC    : the PlayerController watching the replay -- SEES the effect.
     * All other PlayerControllers: effect is HIDDEN from them.
     *
     * The holder auto-destroys after the Niagara system finishes.
     */
    UFUNCTION(BlueprintCallable, Category = "VFX",
        meta = (WorldContext = "WorldContextObject"))
    static UNiagaraComponent* SpawnReplayVFXAtLocation(
        const UObject* WorldContextObject,
        UNiagaraSystem* SystemTemplate,
        FVector             Location,
        FRotator            Rotation = FRotator::ZeroRotator,
        FVector             Scale = FVector(1.f),
        APlayerController* DeadPC = nullptr,
        bool                bAutoDestroy = true,
        bool                bAutoActivate = true,
        ENCPoolMethod       PoolingMethod = ENCPoolMethod::None);
};