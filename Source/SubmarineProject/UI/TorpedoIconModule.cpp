// Fill out your copyright notice in the Description page of Project Settings.

#include "TorpedoIconModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

// ---------------------------------------------------------------------------
//  BindToDataSource
// ---------------------------------------------------------------------------
void UTorpedoIconModule::BindToDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj))
    {
        if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogTorpedoIcon) : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[TorpedoIcon] BindToDataSource: invalid DataSource"));
        return;
    }

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogTorpedoIcon) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[TorpedoIcon] BindToDataSource: '%s'"), *Obj->GetName());

    if (!CreateMIDs())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[TorpedoIcon] BindToDataSource: MID creation failed for '%s'. "
                "Ensure MatTorpedoIcon and MatReloadOverlay are set in FHUDModuleConfig."),
            *Config.ModuleName.ToString());
    }

    DataSource->GetOnAmmoChangedDelegate().AddDynamic(
        this, &UTorpedoIconModule::OnAmmoChanged);
    DataSource->GetOnFireCooldownDelegate().AddDynamic(
        this, &UTorpedoIconModule::OnFireCooldownComplete);
    DataSource->GetOnReadyToFireDelegate().AddDynamic(
        this, &UTorpedoIconModule::OnReadyToFire);

    // Reload delegates are declared on SubmarineTorpedoComponent, accessed
    // through ITrackableSubmarine. These two are currently not on the interface,
    // so we drive reload state purely from IsReloading() polled in EvaluateState.
    // If OnProgressiveReloadComplete / OnFullReloadComplete are added to the
    // interface later, bind them here.

    // Force a full refresh on first bind
    EvaluateState(/*bForceRefresh=*/true);
}

// ---------------------------------------------------------------------------
//  UnbindFromDataSource
// ---------------------------------------------------------------------------
void UTorpedoIconModule::UnbindFromDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj)) return;

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogTorpedoIcon) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[TorpedoIcon] UnbindFromDataSource: '%s'"), *Obj->GetName());

    DataSource->GetOnAmmoChangedDelegate().RemoveDynamic(
        this, &UTorpedoIconModule::OnAmmoChanged);
    DataSource->GetOnFireCooldownDelegate().RemoveDynamic(
        this, &UTorpedoIconModule::OnFireCooldownComplete);
    DataSource->GetOnReadyToFireDelegate().RemoveDynamic(
        this, &UTorpedoIconModule::OnReadyToFire);

    SetContinuousTickEnabled(false);
    GlowTimer = 0.f;
    FlashIntensity = 0.f;
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
// ---------------------------------------------------------------------------
void UTorpedoIconModule::RefreshVisuals_Implementation()
{
    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogTorpedoIcon) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[TorpedoIcon] RefreshVisuals  Module='%s'"), *Config.ModuleName.ToString());

    // Re-create MIDs if not yet done (fires when Config is valid after SetConfig)
    if (!IconMID)
        CreateMIDs();

    EvaluateState(/*bForceRefresh=*/true);
}

// ---------------------------------------------------------------------------
//  NativeTick
//  Active when: Ready (glow pulse + flash decay) OR Reloading (overlay progress)
// ---------------------------------------------------------------------------
void UTorpedoIconModule::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // Base class gates on bContinuousTickEnabled and DataSource validity
    if (!IconMID) return;

    if (CurrentState == ETorpedoIconState::Ready)
    {
        // Glow pulse: smooth sine wave
        GlowTimer += InDeltaTime;
        const float PulseSpeed = Config.GetFloat(HUDConfigKeys::GlowPulseSpeed, 1.5f);
        const float PulseGlow = FMath::Sin(GlowTimer * PulseSpeed) * 0.5f + 0.5f;

        // Flash decay (one-shot boost on Ready transition)
        if (FlashIntensity > 0.f)
        {
            const float FlashDur = Config.GetFloat(HUDConfigKeys::FlashDuration, 0.3f);
            FlashIntensity = FMath::Max(0.f, FlashIntensity - InDeltaTime / FMath::Max(FlashDur, 0.001f));
        }

        const float FinalGlow = FMath::Clamp(PulseGlow + FlashIntensity, 0.f, 1.f);
        IconMID->SetScalarParameterValue(TEXT("GlowIntensity"), FinalGlow);
    }

    // Poll reload state every tick -- no delegate fires at reload START,
    // so we must detect it here. Also catches progressive reload chaining.
    if (IsValid(DataSource.GetObject()))
    {
        const bool bNowReloading = DataSource->GetIsReloading();
        if (bNowReloading != bIsReloading)
        {
            bIsReloading = bNowReloading;
            if (ReloadOverlayImage)
                ReloadOverlayImage->SetVisibility(
                    bIsReloading
                    ? ESlateVisibility::HitTestInvisible
                    : ESlateVisibility::Hidden);
        }
        if (bIsReloading && ReloadOverlayMID)
        {
            const float Ratio = DataSource->GetReloadRatio();
            ReloadOverlayMID->SetScalarParameterValue(TEXT("ReloadRatio"), Ratio);
        }
    }
}

// ---------------------------------------------------------------------------
//  EvaluateState
// ---------------------------------------------------------------------------
void UTorpedoIconModule::EvaluateState(bool bForceRefresh)
{
    if (!IsValid(DataSource.GetObject())) return;

    const bool bReloading = DataSource->GetIsReloading();
    const bool bOnCooldown = DataSource->GetFireCooldownRatio() < 1.f;
    const bool bNoAmmo = DataSource->GetNormalAmmoCount() <= 0;

    // Icon state depends ONLY on fire cooldown -- not reload.
    // Reloading is tracked separately for the overlay.
    // Ready   = can fire (no cooldown)
    // Cooldown = fire cooldown active
    // Reloading state drives overlay independently.
    const ETorpedoIconState NewState =
        (bOnCooldown || bNoAmmo) ? ETorpedoIconState::Cooldown :
        ETorpedoIconState::Ready;

    // Store reload state so NativeTick can drive the overlay independently.
    bIsReloading = bReloading;

    if (NewState == CurrentState && !bForceRefresh) return;

    bJustBecameReady = (CurrentState != ETorpedoIconState::Ready)
        && (NewState == ETorpedoIconState::Ready);

    if (bJustBecameReady)
        FlashIntensity = 1.f;

    CurrentState = NewState;

    ApplyStateToMaterials();

    // Always tick while bound -- needed to poll reload state each frame
    // since no delegate fires at reload START.
    SetContinuousTickEnabled(true);

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogTorpedoIcon) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[TorpedoIcon] EvaluateState: state=%d  Module='%s'"),
            (int32)CurrentState, *Config.ModuleName.ToString());
}

// ---------------------------------------------------------------------------
//  ApplyStateToMaterials
// ---------------------------------------------------------------------------
void UTorpedoIconModule::ApplyStateToMaterials()
{
    if (!IconMID) return;

    const bool bReady = (CurrentState == ETorpedoIconState::Ready);

    // Select icon texture
    UTexture2D* IconTex = bReady
        ? Config.GetTexture(HUDConfigKeys::TorpedoReadyTex)
        : Config.GetTexture(HUDConfigKeys::TorpedoCooldownTex);

    if (ShouldLog(DebugSettings ? DebugSettings->bLogTorpedoIcon : false))
        UE_LOG(LogTemp, Log,
            TEXT("[TorpedoIcon] ApplyState: module='%s' state=%s tex=%s"),
            *Config.ModuleName.ToString(),
            bReady ? TEXT("Ready") : TEXT("Cooldown"),
            IconTex ? *IconTex->GetName() : TEXT("NULL -- check TorpedoReady/TorpedoCooldown keys in DA"));
    if (IconTex)
        IconMID->SetTextureParameterValue(TEXT("MainTexture"), IconTex);

    // Set optional glow mask (white fallback handled in material)
    UTexture2D* GlowMask = Config.GetTexture(HUDConfigKeys::GlowMaskTex);
    if (GlowMask)
        IconMID->SetTextureParameterValue(TEXT("GlowMask"), GlowMask);

    // bIsReady flag for material branching
    IconMID->SetScalarParameterValue(TEXT("bIsReady"), bReady ? 1.f : 0.f);

    // Reset glow to 0 when not ready
    if (!bReady)
    {
        IconMID->SetScalarParameterValue(TEXT("GlowIntensity"), 0.f);
        GlowTimer = 0.f;
    }

    // Overlay is independent of icon state -- show whenever reloading,
    // even if fire cooldown is also active (both can be true simultaneously).
    if (ReloadOverlayImage)
        ReloadOverlayImage->SetVisibility(
            bIsReloading ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
}

// ---------------------------------------------------------------------------
//  CreateMIDs
// ---------------------------------------------------------------------------
bool UTorpedoIconModule::CreateMIDs()
{
    bool bSuccess = true;

    // Icon MID
    if (UMaterialInterface* Base = Config.GetMaterial(HUDConfigKeys::MatTorpedoIcon))
    {
        IconMID = UMaterialInstanceDynamic::Create(Base, this);
        if (IconMID && IconImage)
        {
            IconImage->SetBrushFromMaterial(IconMID);
            IconMID->SetScalarParameterValue(TEXT("GlowPulseSpeed"),
                Config.GetFloat(HUDConfigKeys::GlowPulseSpeed, 1.5f));
        }
        else bSuccess = false;
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[TorpedoIcon] CreateMIDs: missing MatTorpedoIcon in config '%s'"),
            *Config.ModuleName.ToString());
        bSuccess = false;
    }

    // Reload overlay MID
    if (UMaterialInterface* Base = Config.GetMaterial(HUDConfigKeys::MatReloadOverlay))
    {
        ReloadOverlayMID = UMaterialInstanceDynamic::Create(Base, this);
        if (ReloadOverlayMID && ReloadOverlayImage)
            ReloadOverlayImage->SetBrushFromMaterial(ReloadOverlayMID);
        else bSuccess = false;
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[TorpedoIcon] CreateMIDs: missing MatReloadOverlay in config '%s'"),
            *Config.ModuleName.ToString());
        bSuccess = false;
    }

    return bSuccess;
}

// ---------------------------------------------------------------------------
//  Delegate callbacks
// ---------------------------------------------------------------------------
void UTorpedoIconModule::OnAmmoChanged(int32 NormalCount, int32 SpecialCount)
{
    EvaluateState();
}

void UTorpedoIconModule::OnFireCooldownComplete()
{
    EvaluateState();
}

void UTorpedoIconModule::OnReadyToFire()
{
    EvaluateState();
}

void UTorpedoIconModule::OnProgressiveReloadComplete()
{
    EvaluateState();
}

void UTorpedoIconModule::OnFullReloadComplete()
{
    EvaluateState();
}