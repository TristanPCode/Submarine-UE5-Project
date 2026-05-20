#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MatchSettingsDataAsset.h"          // EInputDeviceType
#include "SubmarinePlayerController.generated.h"

class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;

/**
 * ASubmarinePlayerController
 *
 * Replaces the default APlayerController for all local players.
 * Reparent BP_PlayerController to this class.
 *
 * KEY RESPONSIBILITY: Hard input device filtering.
 *
 * UE5 routes ALL keyboard and gamepad events to ALL local PlayerControllers
 * by default. InputMappingContexts provide soft filtering (no binding fires)
 * but events still arrive. This class adds a HARD filter in InputKey/InputAxis
 * that swallows events from devices not assigned to this controller.
 *
 * This guarantees:
 *   - P1=Gamepad never sees keyboard events (even from raw BindKey)
 *   - P2=Keyboard never sees gamepad events
 *   - Fully deterministic, not relying on IMC absence to silence input
 *
 * Future online multiplayer:
 *   On a dedicated server, this controller exists only server-side with no
 *   local input -- InputKey is never called, so the filter costs nothing.
 *   On listen server with local player, works identically to local-only.
 */
UCLASS()
class SUBMARINEPROJECT_API ASubmarinePlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ASubmarinePlayerController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // -----------------------------------------------------------------------
    //  Device assignment (set by SpawnManagerComponent at spawn time)
    // -----------------------------------------------------------------------

    /**
     * The input device assigned to this controller.
     * Set once during InitializeHUDForPlayer, never changed at runtime
     * (runtime switching is a future menu feature).
     *
     * NOT replicated -- local presentation state only.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input")
    EInputDeviceType AssignedInputDevice = EInputDeviceType::Keyboard;

    /**
     * Assign a device to this controller and immediately apply the correct IMC.
     * Call from SpawnManagerComponent after possessing a pawn.
     *
     * @param Device        The device this controller owns.
     * @param KeyboardIMC   IMC to add when Device == Keyboard.
     * @param GamepadIMC    IMC to add when Device == Gamepad0/1/2/3.
     */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void AssignInputDevice(
        EInputDeviceType Device,
        UInputMappingContext* KeyboardIMC,
        UInputMappingContext* GamepadIMC);

    /**
     * Clear all IMCs and re-assign from the current AssignedInputDevice.
     * Call when the pawn changes (e.g. after spectator spawn).
     */
    UFUNCTION(BlueprintCallable, Category = "Input")
    void ReapplyInputMappings(
        UInputMappingContext* KeyboardIMC,
        UInputMappingContext* GamepadIMC);

    // -----------------------------------------------------------------------
    //  Helpers (callable from Blueprint for debugging)
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsKeyboardAssigned() const
    {
        return AssignedInputDevice == EInputDeviceType::Keyboard;
    }

    UFUNCTION(BlueprintPure, Category = "Input")
    bool IsGamepadAssigned() const
    {
        return AssignedInputDevice != EInputDeviceType::Keyboard;
    }

    /** Returns the gamepad slot index (0-3). -1 if keyboard is assigned. */
    UFUNCTION(BlueprintPure, Category = "Input")
    int32 GetGamepadSlot() const;

    /**
     * The keyboard IMC assigned to this PC. Cached so USubmarineViewportClient
     * can rebuild the key partition map when players are assigned.
     * Null for gamepad players.
     */
    UPROPERTY()
    TObjectPtr<UInputMappingContext> CachedKeyboardIMC;


    /**
     * Returns true if the given FKey should be accepted by this controller
     * based on AssignedInputDevice.
     *
     * Rules:
     *   Keyboard assigned -> accept only keyboard + mouse keys
     *   Gamepad0 assigned -> accept only gamepad keys from ControllerId==0
     *   Gamepad1 assigned -> accept only gamepad keys from ControllerId==1
     *   (etc.)
     */
    bool IsInputDeviceAllowed(const FKey& Key, int32 ControllerId) const;

private:

    UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem() const;
};