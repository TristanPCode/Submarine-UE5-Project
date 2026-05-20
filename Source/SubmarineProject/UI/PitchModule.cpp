// Fill out your copyright notice in the Description page of Project Settings.

#include "PitchModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/Image.h"

// ---------------------------------------------------------------------------
//  BindToDataSource
// ---------------------------------------------------------------------------
void UPitchModule::BindToDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj))
    {
        if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogPitch) : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[PitchModule] BindToDataSource: invalid DataSource"));
        return;
    }

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogPitch) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[PitchModule] BindToDataSource: '%s'"), *Obj->GetName());

    LastPitch = FLT_MAX; // force refresh on first tick
    SetContinuousTickEnabled(true);
}

// ---------------------------------------------------------------------------
//  UnbindFromDataSource
// ---------------------------------------------------------------------------
void UPitchModule::UnbindFromDataSource()
{
    UObject* Obj = DataSource.GetObject();

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogPitch) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[PitchModule] UnbindFromDataSource: '%s'"),
            IsValid(Obj) ? *Obj->GetName() : TEXT("None"));

    SetContinuousTickEnabled(false);
    LastPitch = FLT_MAX;
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
// ---------------------------------------------------------------------------
void UPitchModule::RefreshVisuals_Implementation()
{
    // Apply textures via parent
    Super::RefreshVisuals_Implementation();

    if (!IsValid(DataSource.GetObject())) return;

    const float CurrentPitch = DataSource->GetCurrentPitch();

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogPitch) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[PitchModule] RefreshVisuals: pitch=%.2f"), CurrentPitch);

    ApplyPitch(CurrentPitch);
}

// ---------------------------------------------------------------------------
//  NativeTick
// ---------------------------------------------------------------------------
void UPitchModule::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // Gate checked in base class -- only runs when bContinuousTickEnabled
    if (!IsValid(DataSource.GetObject())) return;

    const float CurrentPitch = DataSource->GetCurrentPitch();

    // Only update if pitch changed beyond tolerance
    if (FMath::Abs(CurrentPitch - LastPitch) < PitchChangeTolerance) return;

    ApplyPitch(CurrentPitch);
}

// ---------------------------------------------------------------------------
//  ApplyPitch
//
//  Normalizes pitch to 0..1 and interpolates between min/max pixel offsets.
//
//  Alpha = (Pitch - (-MaxAngle)) / (2 * MaxAngle)   -- 0 = full down, 1 = full up
//  CrankY = Lerp(MinCrankY, MaxCrankY, Alpha)
// ---------------------------------------------------------------------------
void UPitchModule::ApplyPitch(float PitchDegrees)
{
    LastPitch = PitchDegrees;

    const float MaxAngle = Config.GetFloat(HUDConfigKeys::MaxPitchAngle, 30.f);
    const float ClampedPitch = FMath::Clamp(PitchDegrees, -MaxAngle, MaxAngle);

    // Normalize: 0 = full down, 1 = full up
    const float InvertedAlpha = (MaxAngle > 0.f)
        ? (ClampedPitch + MaxAngle) / (2.f * MaxAngle)
        : 0.5f;

    float Alpha = 1.f - InvertedAlpha;

    // Interpolate Y positions from pixel config
    const float MinCrankY = Config.GetFloat(TEXT("MinCrankY"), 0.f);
    const float MaxCrankY = Config.GetFloat(TEXT("MaxCrankY"), 0.f);
    const float MinMetalY = Config.GetFloat(TEXT("MinMetalY"), 0.f);
    const float MaxMetalY = Config.GetFloat(TEXT("MaxMetalY"), 0.f);

    const float CrankY = FMath::Lerp(MinCrankY, MaxCrankY, Alpha);
    const float MetalY = FMath::Lerp(MinMetalY, MaxMetalY, Alpha);

    const bool bInvertCrank = false;
    const bool bInvertMetal = false;

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogPitch) : false))
        UE_LOG(LogTemp, Verbose,
            TEXT("[PitchModule] ApplyPitch: pitch=%.2f  alpha=%.3f  CrankY=%.1f  MetalY=%.1f"),
            PitchDegrees, Alpha, CrankY, MetalY);

    UpdateElementPositions(CrankY, bInvertCrank, MetalY, bInvertMetal);

    // Metal vertical flip: mirrors the texture when nose is pointing down.
    // Controlled by "FlipMetalOnNegativePitch" float in DA (0=off, 1=on).
    if (MetalImage)
    {
        const bool bShouldFlip = PitchDegrees < 0.f;
        MetalImage->SetRenderScale(bShouldFlip
            ? FVector2D(1.f, -1.f)
            : FVector2D(1.f, 1.f));
    }
}