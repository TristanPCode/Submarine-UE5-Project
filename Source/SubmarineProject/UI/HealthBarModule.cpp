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
        if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogHealthBar) : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[HealthBarModule] BindToDataSource: invalid DataSource -- skipping"));
        return;
    }

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogHealthBar) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBarModule] BindToDataSource: '%s'"), *Obj->GetName());

    // Diagnostic: confirm widget pointers are bound from Blueprint
    if (ShouldLog(DebugSettings ? DebugSettings->bLogHealthBar : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBar] Widget pointers -- BackgroundImage=%s  FillImage=%s  OverlayImage=%s"),
            BackgroundImage ? TEXT("OK") : TEXT("NULL -- check BindWidget name in BP"),
            FillImage ? TEXT("OK") : TEXT("NULL -- check BindWidget name in BP"),
            OverlayImage ? TEXT("OK") : TEXT("NULL -- check BindWidget name in BP"));

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

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogHealthBar) : false))
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
    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogHealthBar) : false))
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

    // --- Fill ---
    // FillMID is created in BindToDataSource. On the very first RefreshVisuals
    // call (triggered by SetConfig before any DataSource is set), FillMID will
    // be null -- that is expected and not an error.
    if (!FillMID && IsValid(DataSource.GetObject()))
    {
        // DataSource is valid but MID is missing -- something went wrong in Bind.
        // Attempt a recovery create here.
        UE_LOG(LogTemp, Warning,
            TEXT("[HealthBar] RefreshVisuals: FillMID is null but DataSource is valid. "
                "Attempting recovery CreateFillMID."));
        CreateFillMID();
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

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogHealthBar) : false))
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

    if (!FillMID)
    {
        // Only warn once to avoid log spam -- this is expected before BindToDataSource
        UE_LOG(LogTemp, Verbose,
            TEXT("[HealthBar] PushHealthToMaterial: FillMID is null (expected before Bind)"));
        return;
    }

    if (!FillImage)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HealthBar] PushHealthToMaterial: FillImage is null. "
                "Widget 'FillImage' not found in Blueprint."));
        return;
    }

    // Select fill texture by health state
    UTexture2D* FillTex = SelectFillTexture(LastHealthRatio);

    UE_LOG(LogTemp, Log,
        TEXT("[HealthBar] PushHealthToMaterial: ratio=%.3f  tex=%s"),
        LastHealthRatio,
        FillTex ? *FillTex->GetName() : TEXT("NULL -- fill texture key missing in DA"));

    if (FillTex)
        FillMID->SetTextureParameterValue(TEXT("MainTexture"), FillTex);
    else
        UE_LOG(LogTemp, Warning,
            TEXT("[HealthBar] No fill texture for ratio %.3f. "
                "Check DA: FillGreen/FillYellow/FillRed keys in Textures map, "
                "ThresholdYellow=%.2f, ThresholdRed=%.2f"),
            LastHealthRatio,
            Config.GetFloat(HUDConfigKeys::ThresholdYellow, 0.5f),
            Config.GetFloat(HUDConfigKeys::ThresholdRed, 0.25f));

    // Compute and push clip ratio
    const float ClipRight = ComputeClipRatio(LastHealthRatio);
    FillMID->SetScalarParameterValue(TEXT("ClipRatioRight"), ClipRight);

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogHealthBar) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBar] ClipRatioRight=%.4f  (TexW=%.0f  OffL=%.0f  OffR=%.0f)"),
            ClipRight,
            Config.GetFloat(HUDConfigKeys::TextureWidth, 1078.f),
            Config.GetFloat(HUDConfigKeys::OffsetLeft, 0.f),
            Config.GetFloat(HUDConfigKeys::OffsetRight, 0.f));
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
    // --- Diagnostic dump so you can see exactly what the config contains ---
    if (ShouldLog(DebugSettings ? DebugSettings->bLogHealthBar : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBar] CreateFillMID for module '%s'"),
            *Config.ModuleName.ToString());

    if (ShouldLog(DebugSettings ? DebugSettings->bLogHealthBar : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBar]   FillImage widget pointer: %s"),
            FillImage ? TEXT("OK") : TEXT("NULL"));

    // Check all expected texture keys
    const FName TexKeys[] = {
        HUDConfigKeys::Background, HUDConfigKeys::FillGreen,
        HUDConfigKeys::FillYellow, HUDConfigKeys::FillRed, HUDConfigKeys::Overlay
    };
    for (const FName& Key : TexKeys)
    {
        UTexture2D* T = Config.GetTexture(Key);
        if (ShouldLog(DebugSettings ? DebugSettings->bLogHealthBar : false))
            UE_LOG(LogTemp, Log, TEXT("[HealthBar]   Texture '%s': %s"),
                *Key.ToString(), T ? *T->GetName() : TEXT("NULL"));
    }

    // Check material key
    UMaterialInterface* BaseMat = Config.GetMaterial(HUDConfigKeys::MatClip);
    if (ShouldLog(DebugSettings ? DebugSettings->bLogHealthBar : false))
        UE_LOG(LogTemp, Log, TEXT("[HealthBar]   Material 'MatClip': %s"),
            BaseMat ? *BaseMat->GetName() : TEXT("NULL -- THIS IS WHY FillImage IS INVISIBLE"));

    if (!BaseMat)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HealthBar] CreateFillMID FAILED: 'MatClip' material not found in "
                "FHUDModuleConfig::Materials for module '%s'. "
                "Open your DA, go to the HealthBar module entry, expand Materials, "
                "add a key 'MatClip' and assign your M_HUD_Clip material asset."),
            *Config.ModuleName.ToString());
        return false;
    }

    FillMID = UMaterialInstanceDynamic::Create(BaseMat, this);
    if (!FillMID)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[HealthBar] CreateFillMID: UMaterialInstanceDynamic::Create returned null"));
        return false;
    }

    if (FillImage)
    {
        FillImage->SetBrushFromMaterial(FillMID);
        UE_LOG(LogTemp, Log,
            TEXT("[HealthBar] CreateFillMID: FillMID created and applied to FillImage -- OK"));
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HealthBar] CreateFillMID: FillMID created but FillImage is null. "
                "In your BP_HealthBarModule, the UImage widget must be named exactly "
                "'FillImage' (case-sensitive) for BindWidget to find it."));
        return false;
    }

    return true;
}