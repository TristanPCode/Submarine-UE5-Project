// Fill out your copyright notice in the Description page of Project Settings.

#include "EngineStateModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/Image.h"

// ---------------------------------------------------------------------------
//  BindToDataSource
// ---------------------------------------------------------------------------
void UEngineStateModule::BindToDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj))
    {
        if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogEngineState) : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[EngineStateModule] BindToDataSource: invalid DataSource"));
        return;
    }

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogEngineState) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[EngineStateModule] BindToDataSource: '%s'"), *Obj->GetName());

    // Log the config state at bind time so we can catch "Config empty at bind" issues
    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleLayout && DebugSettings->bLogEngineState) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[EngineState] Config at bind: Module='%s'  StateEntries=%d  TexH=%.0f  TexW=%.0f"),
            *Config.ModuleName.ToString(),
            Config.StateEntries.Num(),
            Config.GetFloat(HUDConfigKeys::TextureHeight, -1.f),
            Config.GetFloat(HUDConfigKeys::TextureWidth, -1.f));

    DataSource->GetOnLinearStateChangedDelegate().AddDynamic(
        this, &UEngineStateModule::OnLinearStateChanged);

    // No tick -- purely event-driven
    bFirstUpdate = true;

    // Read and apply the CURRENT state immediately after binding.
    // The delegate only fires on CHANGES -- if the submarine is already at
    // Stand when we bind, we will never receive a transition to Stand.
    const ELinearSpeedState CurrentState = DataSource->GetLinearSpeedState();
    if (ShouldLog(DebugSettings ? DebugSettings->bLogEngineState : false))
        UE_LOG(LogTemp, Log,
            TEXT("[EngineState] Initial state after bind: %d"), (int32)CurrentState);
    ApplyState(CurrentState);
}

// ---------------------------------------------------------------------------
//  UnbindFromDataSource
// ---------------------------------------------------------------------------
void UEngineStateModule::UnbindFromDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj)) return;

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogEngineState) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[EngineStateModule] UnbindFromDataSource: '%s'"), *Obj->GetName());

    DataSource->GetOnLinearStateChangedDelegate().RemoveDynamic(
        this, &UEngineStateModule::OnLinearStateChanged);

    bFirstUpdate = true;
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
//
//  Called on SetConfig and SetDataSource. Reads current state from DataSource
//  (if valid) and applies positions. Calls parent to apply static textures.
// ---------------------------------------------------------------------------
void UEngineStateModule::RefreshVisuals_Implementation()
{
    // Apply textures via parent
    Super::RefreshVisuals_Implementation();

    if (!IsValid(DataSource.GetObject())) return;

    const ELinearSpeedState CurrentState = DataSource->GetLinearSpeedState();

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogEngineState) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[EngineStateModule] RefreshVisuals: state=%d"),
            (int32)CurrentState);

    ApplyState(CurrentState);
}

// ---------------------------------------------------------------------------
//  OnLinearStateChanged  (delegate callback)
// ---------------------------------------------------------------------------
void UEngineStateModule::OnLinearStateChanged(ELinearSpeedState NewState)
{
    // Skip redundant updates (unless first update after bind)
    if (!bFirstUpdate && NewState == LastState) return;
    bFirstUpdate = false;

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogEngineState) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[EngineStateModule] OnLinearStateChanged: %d -> %d"),
            (int32)LastState, (int32)NewState);

    ApplyState(NewState);
}

// ---------------------------------------------------------------------------
//  ApplyState
// ---------------------------------------------------------------------------
void UEngineStateModule::ApplyState(ELinearSpeedState State)
{
    LastState = State;

    const int32 StateIndex = (int32)State;
    const TArray<FEngineStateEntry>& Entries = Config.StateEntries;

    if (!Entries.IsValidIndex(StateIndex))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[EngineStateModule] ApplyState: StateEntries has %d entries "
                "but state index is %d. Fill all 7 entries in FHUDModuleConfig."),
            Entries.Num(), StateIndex);
        return;
    }

    const FEngineStateEntry& Entry = Entries[StateIndex];

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleLayout && DebugSettings->bLogEngineState) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[EngineStateModule] ApplyState: idx=%d  CrankY=%.1f(inv=%d)  MetalY=%.1f(inv=%d)"),
            StateIndex,
            Entry.CrankY, Entry.bInvertCrank ? 1 : 0,
            Entry.MetalY, Entry.bInvertMetal ? 1 : 0);

    UpdateElementPositions(
        Entry.CrankY, Entry.bInvertCrank,
        Entry.MetalY, Entry.bInvertMetal);

    // --- TEXTURE FLIP (new) ---
    // Applied via render transform on the image widget.
    // Scale Y = -1 flips the texture vertically without changing position.
    // The render transform pivot defaults to (0.5, 0.5) = center of widget.
    if (CrankImage)
        CrankImage->SetRenderScale(
            Entry.bFlipCrankTexture
            ? FVector2D(1.f, -1.f)
            : FVector2D(1.f,  1.f));

    if (MetalImage)
        MetalImage->SetRenderScale(
            Entry.bFlipMetalTexture
            ? FVector2D(1.f, -1.f)
            : FVector2D(1.f,  1.f));
}