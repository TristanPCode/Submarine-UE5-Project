// Fill out your copyright notice in the Description page of Project Settings.

#include "HealthBarModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

// ---------------------------------------------------------------------------
//  BindToDataSource
// ---------------------------------------------------------------------------
void UHealthBarModule::BindToDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj))
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[HealthBarModule] BindToDataSource: invalid DataSource -- skipping"));
        return;
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBarModule] BindToDataSource: '%s'"), *Obj->GetName());

    // Create the fill material dynamic instance (once per data source)
    if (!CreateFillMID())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HealthBarModule] BindToDataSource: failed to create FillMID. "
                "Ensure 'MatClip' material is set in FHUDModuleConfig::Materials."));
    }

    // Bind damage delegate -- fires RefreshVisuals indirectly via OnDamaged
    DataSource->GetOnDamagedDelegate().AddDynamic(this, &UHealthBarModule::OnDamaged);

    // No tick -- health bar is purely event-driven
}

// ---------------------------------------------------------------------------
//  UnbindFromDataSource
// ---------------------------------------------------------------------------
void UHealthBarModule::UnbindFromDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj)) return;

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBarModule] UnbindFromDataSource: '%s'"), *Obj->GetName());

    DataSource->GetOnDamagedDelegate().RemoveDynamic(this, &UHealthBarModule::OnDamaged);

    // Invalidate cached ratio so next bind forces a full refresh
    LastHealthRatio = -1.f;
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
//
//  Called on SetConfig, SetDataSource, and after damage events.
//  Reads current health from DataSource and pushes to material.
// ---------------------------------------------------------------------------
void UHealthBarModule::RefreshVisuals_Implementation()
{
    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleRefresh : false))
        UE_LOG(LogTemp, Log, TEXT("[HealthBarModule] RefreshVisuals"));

    // Apply background and overlay textures (done once, idempotent)
    if (BackgroundImage)
    {
        if (UTexture2D* BgTex = Config.GetTexture(HUDConfigKeys::Background))
            BackgroundImage->SetBrushFromTexture(BgTex, /*bMatchSize=*/false);
    }

    if (OverlayImage)
    {
        if (UTexture2D* OvTex = Config.GetTexture(HUDConfigKeys::Overlay))
            OverlayImage->SetBrushFromTexture(OvTex, /*bMatchSize=*/false);
    }

    // Push health to fill material
    const float HealthRatio = IsValid(DataSource.GetObject())
        ? DataSource->GetHealthRatio()
        : 0.f;

    PushHealthToMaterial(HealthRatio);
}

// ---------------------------------------------------------------------------
//  OnDamaged  (delegate callback)
// ---------------------------------------------------------------------------
void UHealthBarModule::OnDamaged(float DamageAmount, AActor* DamageCauser)
{
    if (!IsValid(DataSource.GetObject())) return;

    const float NewRatio = DataSource->GetHealthRatio();

    // Skip if ratio unchanged (e.g. invincibility frames or overkill)
    if (FMath::IsNearlyEqual(NewRatio, LastHealthRatio, 0.001f)) return;

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleRefresh : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBarModule] OnDamaged: damage=%.1f  ratio=%.3f -> %.3f"),
            DamageAmount, LastHealthRatio, NewRatio);

    PushHealthToMaterial(NewRatio);
}

// ---------------------------------------------------------------------------
//  PushHealthToMaterial
// ---------------------------------------------------------------------------
void UHealthBarModule::PushHealthToMaterial(float HealthRatio)
{
    LastHealthRatio = FMath::Clamp(HealthRatio, 0.f, 1.f);

    if (!FillMID || !FillImage)
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleRefresh : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[HealthBarModule] PushHealthToMaterial: FillMID or FillImage is null"));
        return;
    }

    // Select fill texture by health state
    UTexture2D* FillTex = SelectFillTexture(LastHealthRatio);
    if (FillTex)
        FillMID->SetTextureParameterValue(TEXT("MainTexture"), FillTex);

    // Compute and push clip ratio
    const float ClipRight = ComputeClipRatio(LastHealthRatio);
    FillMID->SetScalarParameterValue(TEXT("ClipRatioRight"), ClipRight);

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleRefresh : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBarModule] PushHealthToMaterial: ratio=%.3f  clip=%.4f  tex='%s'"),
            LastHealthRatio, ClipRight, FillTex ? *FillTex->GetName() : TEXT("null"));
}

// ---------------------------------------------------------------------------
//  ComputeClipRatio
//
//  All pixel values converted to UV-space ratios at runtime.
//  OffsetLeft and OffsetRight are stored in pixels relative to TextureWidth.
//
//  Formula:
//    OffsetLeftRatio  = OffsetLeft  / TextureWidth
//    OffsetRightRatio = OffsetRight / TextureWidth
//    UsableWidth      = 1 - OffsetLeftRatio - OffsetRightRatio
//    ClipRatioRight   = OffsetLeftRatio + UsableWidth * HealthRatio
// ---------------------------------------------------------------------------
float UHealthBarModule::ComputeClipRatio(float HealthRatio) const
{
    const float TexW = Config.GetFloat(HUDConfigKeys::TextureWidth, 1078.f);
    const float OffLeft = Config.GetFloat(HUDConfigKeys::OffsetLeft, 0.f);
    const float OffRight = Config.GetFloat(HUDConfigKeys::OffsetRight, 0.f);

    const float LeftRatio = OffLeft / FMath::Max(TexW, 1.f);
    const float RightRatio = OffRight / FMath::Max(TexW, 1.f);
    const float UsableWidth = FMath::Max(0.f, 1.f - LeftRatio - RightRatio);

    return LeftRatio + UsableWidth * FMath::Clamp(HealthRatio, 0.f, 1.f);
}

// ---------------------------------------------------------------------------
//  SelectFillTexture
// ---------------------------------------------------------------------------
UTexture2D* UHealthBarModule::SelectFillTexture(float HealthRatio) const
{
    const float ThreshRed = Config.GetFloat(HUDConfigKeys::ThresholdRed, 0.25f);
    const float ThreshYellow = Config.GetFloat(HUDConfigKeys::ThresholdYellow, 0.5f);

    if (HealthRatio <= ThreshRed)
        return Config.GetTexture(HUDConfigKeys::FillRed);
    if (HealthRatio <= ThreshYellow)
        return Config.GetTexture(HUDConfigKeys::FillYellow);
    return Config.GetTexture(HUDConfigKeys::FillGreen);
}

// ---------------------------------------------------------------------------
//  CreateFillMID
// ---------------------------------------------------------------------------
bool UHealthBarModule::CreateFillMID()
{
    UMaterialInterface* BaseMat = Config.GetMaterial(HUDConfigKeys::MatClip);
    if (!BaseMat)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HealthBarModule] CreateFillMID: no '%s' material in config for module '%s'"),
            *HUDConfigKeys::MatClip.ToString(), *Config.ModuleName.ToString());
        return false;
    }

    FillMID = UMaterialInstanceDynamic::Create(BaseMat, this);
    if (!FillMID)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[HealthBarModule] CreateFillMID: UMaterialInstanceDynamic::Create returned null"));
        return false;
    }

    if (FillImage)
    {
        FillImage->SetBrushFromMaterial(FillMID);

        if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
            UE_LOG(LogTemp, Log,
                TEXT("[HealthBarModule] CreateFillMID: FillMID created and applied to FillImage"));
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HealthBarModule] CreateFillMID: FillImage is null. "
                "Ensure your Blueprint has a UImage named 'FillImage'."));
    }

    return true;
}