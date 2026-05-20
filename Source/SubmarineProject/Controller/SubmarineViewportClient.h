#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "SubmarineViewportClient.generated.h"

/**
 * USubmarineViewportClient
 *
 * Subclass of UGameViewportClient that handles split-keyboard routing.
 *
 * Problem it solves:
 *   The keyboard is a single FInputDeviceId shared by all LocalPlayers.
 *   UE5 broadcasts ALL keyboard events to ALL PlayerControllers, making
 *   split-keyboard (P0=WASD, P1=ArrowKeys) impossible with the device
 *   mapper alone. This class intercepts raw keyboard events BEFORE they
 *   are broadcast and routes each key only to its assigned LocalPlayer.
 *
 * Gamepads are NOT handled here -- they are correctly routed via
 *   IPlatformInputDeviceMapper::Internal_ChangeInputDeviceUserMapping()
 *   in ASubmarinePlayerController::AssignInputDevice().
 *
 * Setup:
 *   Set GameViewportClientClassName in DefaultEngine.ini:
 *   [/Script/Engine.Engine]
 *   GameViewportClientClassName=/Script/SubmarineProject.SubmarineViewportClient
 *
 *   Then call RebuildKeyboardPartition() from SpawnManager after all
 *   player devices have been assigned.
 */
UCLASS()
class SUBMARINEPROJECT_API USubmarineViewportClient : public UGameViewportClient
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  UGameViewportClient overrides
    // -----------------------------------------------------------------------

    /**
     * Intercepts raw key events before UE broadcasts them to all controllers.
     * Keyboard events are routed to the owning LocalPlayer via KeyboardPartitionMap.
     * Gamepad events fall through to Super (handled by device mapper).
     */
    virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;

    /**
     * Intercepts raw axis events (mouse movement, scroll, stick).
     * In UE5.7 this uses the same FInputKeyEventArgs signature as InputKey.
     * Mouse axes are routed to the owning LocalPlayer if partitioned.
     * Gamepad axes fall through to Super.
     */
    virtual bool InputAxis(const FInputKeyEventArgs& EventArgs) override;

    // -----------------------------------------------------------------------
    //  Partition management
    // -----------------------------------------------------------------------

    /**
     * Rebuild the keyboard partition map from RuntimeMatchSettings.
     * Call this from SpawnManager after all AssignInputDevice() calls complete.
     *
     * This reads the IMC assigned to each keyboard player, extracts the keys
     * bound in that IMC, and maps each key to its LocalPlayerIndex.
     *
     * Keys that appear in NO player's IMC are unpartitioned and broadcast
     * normally (e.g. Escape, F keys, debug keys).
     *
     * @param LocalPlayerToIMC   Map of LocalPlayerIndex -> their active IMC.
     *                           Pass both keyboard IMCs even if one player
     *                           uses a gamepad -- unowned keys stay unpartitioned.
     */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void RebuildKeyboardPartition(
        const TMap<int32, UInputMappingContext*>& LocalPlayerToIMC);

    /**
     * Manually assign a specific key to a local player.
     * Useful for edge cases or debug overrides.
     */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void AssignKeyToPlayer(FKey Key, int32 LocalPlayerIndex);

    /**
     * Remove a key from the partition map (it will broadcast to all players).
     */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void UnassignKey(FKey Key);

    /** Clear the entire partition map. All keyboard events broadcast normally. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void ClearKeyboardPartition();

    /** Returns the LocalPlayerIndex owning this key, or -1 if unpartitioned. */
    UFUNCTION(BlueprintPure, Category = "Input")
    int32 GetOwnerForKey(FKey Key) const;

    /** Debug: log the full partition map to output. */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void LogPartitionMap() const;

private:

    /**
     * Maps each keyboard/mouse key to the LocalPlayerIndex that owns it.
     * Keys not present here are unpartitioned (broadcast to all).
     *
     * Built by RebuildKeyboardPartition() from IMC bindings.
     * Persists for the lifetime of the match.
     */
    TMap<FKey, int32> KeyboardPartitionMap;

    /**
     * Route a keyboard event to a specific LocalPlayer's PlayerController.
     * Returns true if the event was consumed (routed successfully).
     */
    bool RouteKeyEventToPlayer(const FInputKeyEventArgs& EventArgs,
        int32 LocalPlayerIndex) const;

    /**
     * Route a mouse axis event to a specific LocalPlayer's PlayerController.
     * Returns true if the event was consumed.
     */
    bool RouteAxisEventToPlayer(const FInputKeyEventArgs& EventArgs,
        int32 LocalPlayerIndex) const;

    /** Get a PlayerController by LocalPlayer index. Returns null if not found. */
    APlayerController* GetPCForLocalPlayer(int32 LocalPlayerIndex) const;
};