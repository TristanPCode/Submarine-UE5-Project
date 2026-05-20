// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

// ---------------------------------------------------------------------------
//  InputKey -- main intercept point for keyboard/mouse button events
// ---------------------------------------------------------------------------
bool USubmarineViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
    // Gamepads are handled by the device mapper -- pass straight through.
    if (EventArgs.Key.IsGamepadKey())
        return Super::InputKey(EventArgs);

    // Check if this key has been assigned to a specific local player.
    const int32* OwnerIdx = KeyboardPartitionMap.Find(EventArgs.Key);
    if (!OwnerIdx)
    {
        // Unpartitioned key (Escape, F-keys, etc.) -- broadcast normally.
        return Super::InputKey(EventArgs);
    }

    // Route to the owning player only.
    if (RouteKeyEventToPlayer(EventArgs, *OwnerIdx))
        return true;  // consumed

    // Fallback: routing failed (PC not ready?) -- broadcast normally.
    return Super::InputKey(EventArgs);
}

// ---------------------------------------------------------------------------
//  InputAxis -- intercept mouse movement and scroll
//  In UE5.7 UGameViewportClient::InputAxis uses FInputKeyEventArgs.
// ---------------------------------------------------------------------------
bool USubmarineViewportClient::InputAxis(const FInputKeyEventArgs& EventArgs)
{
    // Gamepads pass through -- device mapper handles routing.
    if (EventArgs.Key.IsGamepadKey())
        return Super::InputAxis(EventArgs);

    // Mouse axes: route to owning player if partitioned.
    const int32* OwnerIdx = KeyboardPartitionMap.Find(EventArgs.Key);
    if (!OwnerIdx)
        return Super::InputAxis(EventArgs);

    if (RouteAxisEventToPlayer(EventArgs, *OwnerIdx))
        return true;

    return Super::InputAxis(EventArgs);
}

// ---------------------------------------------------------------------------
//  RebuildKeyboardPartition
// ---------------------------------------------------------------------------
void USubmarineViewportClient::RebuildKeyboardPartition(
    const TMap<int32, UInputMappingContext*>& LocalPlayerToIMC)
{
    KeyboardPartitionMap.Empty();

    // Walk each player's IMC and register every key binding they own.
    for (const auto& Pair : LocalPlayerToIMC)
    {
        const int32 PlayerIdx = Pair.Key;
        const UInputMappingContext* IMC = Pair.Value;
        if (!IMC) continue;

        for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
        {
            const FKey& Key = Mapping.Key;

            // Skip gamepad keys -- those are handled by device mapper.
            if (Key.IsGamepadKey()) continue;

            // If this key is already claimed by another player, warn.
            if (const int32* Existing = KeyboardPartitionMap.Find(Key))
            {
                if (*Existing != PlayerIdx)
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[ViewportClient] Key '%s' claimed by both P%d and P%d -- "
                            "P%d wins (check for IMC overlap)"),
                        *Key.GetDisplayName().ToString(),
                        *Existing, PlayerIdx, *Existing);
                    continue;  // first claimant wins
                }
            }

            KeyboardPartitionMap.Add(Key, PlayerIdx);
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[ViewportClient] RebuildKeyboardPartition: %d keys partitioned across %d players"),
        KeyboardPartitionMap.Num(), LocalPlayerToIMC.Num());

    // Always log the partition map after rebuild so you can verify key assignments
    LogPartitionMap();
}

// ---------------------------------------------------------------------------
//  AssignKeyToPlayer
// ---------------------------------------------------------------------------
void USubmarineViewportClient::AssignKeyToPlayer(FKey Key, int32 LocalPlayerIndex)
{
    KeyboardPartitionMap.Add(Key, LocalPlayerIndex);
    UE_LOG(LogTemp, Log,
        TEXT("[ViewportClient] Manually assigned key '%s' to P%d"),
        *Key.GetDisplayName().ToString(), LocalPlayerIndex);
}

// ---------------------------------------------------------------------------
//  UnassignKey
// ---------------------------------------------------------------------------
void USubmarineViewportClient::UnassignKey(FKey Key)
{
    KeyboardPartitionMap.Remove(Key);
}

// ---------------------------------------------------------------------------
//  ClearKeyboardPartition
// ---------------------------------------------------------------------------
void USubmarineViewportClient::ClearKeyboardPartition()
{
    KeyboardPartitionMap.Empty();
    UE_LOG(LogTemp, Log, TEXT("[ViewportClient] Keyboard partition cleared"));
}

// ---------------------------------------------------------------------------
//  GetOwnerForKey
// ---------------------------------------------------------------------------
int32 USubmarineViewportClient::GetOwnerForKey(FKey Key) const
{
    const int32* Idx = KeyboardPartitionMap.Find(Key);
    return Idx ? *Idx : -1;
}

// ---------------------------------------------------------------------------
//  LogPartitionMap
// ---------------------------------------------------------------------------
void USubmarineViewportClient::LogPartitionMap() const
{
    UE_LOG(LogTemp, Log, TEXT("[ViewportClient] --- Keyboard Partition Map (%d entries) ---"),
        KeyboardPartitionMap.Num());

    // Group by player for readability
    TMap<int32, TArray<FString>> ByPlayer;
    for (const auto& Pair : KeyboardPartitionMap)
        ByPlayer.FindOrAdd(Pair.Value).Add(Pair.Key.GetDisplayName().ToString());

    for (const auto& PlayerPair : ByPlayer)
    {
        FString Keys = FString::Join(PlayerPair.Value, TEXT(", "));
        UE_LOG(LogTemp, Log, TEXT("[ViewportClient]   P%d: %s"),
            PlayerPair.Key, *Keys);
    }
}

// ---------------------------------------------------------------------------
//  RouteKeyEventToPlayer
// ---------------------------------------------------------------------------
bool USubmarineViewportClient::RouteKeyEventToPlayer(
    const FInputKeyEventArgs& EventArgs, int32 LocalPlayerIndex) const
{
    APlayerController* PC = GetPCForLocalPlayer(LocalPlayerIndex);
    if (!PC) return false;

    // Build a new EventArgs targeting this specific PC's viewport
    // (keeps the event data intact, just changes the target)
    FInputKeyEventArgs RoutedEvent = EventArgs;
    PC->InputKey(RoutedEvent);
    return true;
}

// ---------------------------------------------------------------------------
//  RouteAxisEventToPlayer
//  In UE5.7 axis events share FInputKeyEventArgs with key events.
// ---------------------------------------------------------------------------
bool USubmarineViewportClient::RouteAxisEventToPlayer(
    const FInputKeyEventArgs& EventArgs, int32 LocalPlayerIndex) const
{
    APlayerController* PC = GetPCForLocalPlayer(LocalPlayerIndex);
    if (!PC) return false;

    // In UE5.7 both key and axis events go through InputKey
    FInputKeyEventArgs RoutedEvent = EventArgs;
    PC->InputKey(RoutedEvent);
    return true;
}

// ---------------------------------------------------------------------------
//  GetPCForLocalPlayer
// ---------------------------------------------------------------------------
APlayerController* USubmarineViewportClient::GetPCForLocalPlayer(
    int32 LocalPlayerIndex) const
{
    UGameInstance* GI = GetGameInstance();
    if (!GI) return nullptr;

    const TArray<ULocalPlayer*>& LPs = GI->GetLocalPlayers();
    if (!LPs.IsValidIndex(LocalPlayerIndex)) return nullptr;

    ULocalPlayer* LP = LPs[LocalPlayerIndex];
    return LP ? LP->GetPlayerController(GetWorld()) : nullptr;
}