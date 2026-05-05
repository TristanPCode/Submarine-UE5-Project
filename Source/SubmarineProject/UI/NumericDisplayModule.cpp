// Fill out your copyright notice in the Description page of Project Settings.

#include "NumericDisplayModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Materials/MaterialInstanceDynamic.h"

// ---------------------------------------------------------------------------
//  NativeOnInitialized
// ---------------------------------------------------------------------------
void UNumericDisplayModule::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    // Cache image pointers for indexed access in tick
    DigitImages[0] = Digit0Image;
    DigitImages[1] = Digit1Image;
    DigitImages[2] = Digit2Image;
    DigitImages[3] = Digit3Image;

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] NativeOnInitialized  Module='%s'"),
            *Config.ModuleName.ToString());

    // Position digit images now that layout is available
    if (!bDigitImagesPositioned)
        PositionDigitImages();
}

// ---------------------------------------------------------------------------
//  BindToDataSource
// ---------------------------------------------------------------------------
void UNumericDisplayModule::BindToDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj))
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[NumericDisplay] BindToDataSource: invalid DataSource"));
        return;
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] BindToDataSource: '%s'"), *Obj->GetName());

    // Pre-compute atlas UV constants for the assigned row
    const float TotalH = Config.GetFloat(HUDConfigKeys::AtlasTotalHeight, 501.f);
    const float DigitH = Config.GetFloat(HUDConfigKeys::AtlasDigitHeight, 167.f);
    const float RowIndex = Config.GetFloat(HUDConfigKeys::AtlasRowIndex, 0.f);

    CachedVMin = (RowIndex * DigitH) / FMath::Max(TotalH, 1.f);
    CachedVMax = ((RowIndex + 1.f) * DigitH) / FMath::Max(TotalH, 1.f);
    CachedUStep = 1.f / 10.f;  // 10 digits per row

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] Atlas UVs: row=%.0f  VMin=%.4f  VMax=%.4f  UStep=%.4f"),
            RowIndex, CachedVMin, CachedVMax, CachedUStep);

    // Reset last digits to force full refresh on first tick
    for (int32 i = 0; i < MaxDigitCount; ++i)
        LastDigits[i] = -1;

    // Create MIDs
    bMIDsReady = CreateDigitMIDs();
    if (!bMIDsReady)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NumericDisplay] BindToDataSource: MID creation failed for '%s'. "
                "Ensure 'MatAtlasSample' material is set in FHUDModuleConfig::Materials."),
            *Config.ModuleName.ToString());
    }

    SetContinuousTickEnabled(true);
}

// ---------------------------------------------------------------------------
//  UnbindFromDataSource
// ---------------------------------------------------------------------------
void UNumericDisplayModule::UnbindFromDataSource()
{
    UObject* Obj = DataSource.GetObject();

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] UnbindFromDataSource: '%s'"),
            IsValid(Obj) ? *Obj->GetName() : TEXT("None"));

    SetContinuousTickEnabled(false);
    bMIDsReady = false;

    for (int32 i = 0; i < MaxDigitCount; ++i)
        LastDigits[i] = -1;
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
// ---------------------------------------------------------------------------
void UNumericDisplayModule::RefreshVisuals_Implementation()
{
    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleRefresh : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] RefreshVisuals  Module='%s'"),
            *Config.ModuleName.ToString());

    // Background
    if (BackgroundImage)
    {
        if (UTexture2D* BgTex = Config.GetTexture(HUDConfigKeys::Background))
            BackgroundImage->SetBrushFromTexture(BgTex, false);
    }

    if (!IsValid(DataSource.GetObject())) return;

    UpdateAllDigits(GetDisplayValue());
}

// ---------------------------------------------------------------------------
//  NativeTick
// ---------------------------------------------------------------------------
void UNumericDisplayModule::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (!bMIDsReady) return;
    if (!IsValid(DataSource.GetObject())) return;

    UpdateAllDigits(GetDisplayValue());
}

// ---------------------------------------------------------------------------
//  GetDisplayValue  (override in subclasses)
// ---------------------------------------------------------------------------
float UNumericDisplayModule::GetDisplayValue() const
{
    return 0.f;
}

// ---------------------------------------------------------------------------
//  CreateDigitMIDs
// ---------------------------------------------------------------------------
bool UNumericDisplayModule::CreateDigitMIDs()
{
    UMaterialInterface* BaseMat = Config.GetMaterial(HUDConfigKeys::MatAtlasSample);
    if (!BaseMat)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NumericDisplay] CreateDigitMIDs: no '%s' material in config for '%s'"),
            *HUDConfigKeys::MatAtlasSample.ToString(), *Config.ModuleName.ToString());
        return false;
    }

    // Get the atlas texture from config -- all MIDs share it
    UTexture2D* AtlasTex = Config.GetTexture(HUDConfigKeys::DigitAtlas);
    if (!AtlasTex)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NumericDisplay] CreateDigitMIDs: no '%s' texture in config for '%s'"),
            *HUDConfigKeys::DigitAtlas.ToString(), *Config.ModuleName.ToString());
        return false;
    }

    bool bAllCreated = true;
    for (int32 i = 0; i < MaxDigitCount; ++i)
    {
        if (!DigitImages[i])
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[NumericDisplay] CreateDigitMIDs: Digit%dImage is null for '%s'. "
                    "Ensure Blueprint has all 4 UImage widgets bound."),
                i, *Config.ModuleName.ToString());
            bAllCreated = false;
            continue;
        }

        DigitMIDs[i] = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (!DigitMIDs[i])
        {
            bAllCreated = false;
            continue;
        }

        // Set shared atlas texture on all instances
        DigitMIDs[i]->SetTextureParameterValue(TEXT("Atlas"), AtlasTex);
        DigitImages[i]->SetBrushFromMaterial(DigitMIDs[i]);
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] CreateDigitMIDs: %s for '%s'"),
            bAllCreated ? TEXT("all 4 MIDs created") : TEXT("some MIDs FAILED"),
            *Config.ModuleName.ToString());

    return bAllCreated;
}

// ---------------------------------------------------------------------------
//  PositionDigitImages
//
//  Places digit images in the canvas using config offsets.
//  Pixel values are converted to ratios using AtlasTotalWidth as reference
//  so they scale proportionally with the module.
// ---------------------------------------------------------------------------
void UNumericDisplayModule::PositionDigitImages()
{
    if (!DigitCanvas) return;

    const FVector2D CanvasSize = DigitCanvas->GetCachedGeometry().GetLocalSize();
    if (CanvasSize.IsNearlyZero()) return; // not laid out yet -- called again from NativeOnInitialized

    const float AtlasW = Config.GetFloat(HUDConfigKeys::AtlasTotalWidth, 820.f);
    const float DigitW = Config.GetFloat(HUDConfigKeys::AtlasDigitWidth, 82.f);
    const float AtlasH = Config.GetFloat(HUDConfigKeys::AtlasTotalHeight, 501.f);
    const float DigitH = Config.GetFloat(HUDConfigKeys::AtlasDigitHeight, 167.f);
    const float Spacing = Config.GetFloat(HUDConfigKeys::DigitSpacing, 0.f);
    const float OriginX = Config.GetFloat(HUDConfigKeys::DigitOffsetX, 0.f);
    const float OriginY = Config.GetFloat(HUDConfigKeys::DigitOffsetY, 0.f);

    // Reference dimensions for ratio conversion
    // We use AtlasW/AtlasH as the "original resolution" for X/Y respectively
    const float RefW = FMath::Max(AtlasW, 1.f);
    const float RefH = FMath::Max(AtlasH, 1.f);

    const float DigitW_screen = (DigitW / RefW) * CanvasSize.X;
    const float DigitH_screen = (DigitH / RefH) * CanvasSize.Y;
    const float Spacing_screen = (Spacing / RefW) * CanvasSize.X;
    const float OriginX_screen = (OriginX / RefW) * CanvasSize.X;
    const float OriginY_screen = (OriginY / RefH) * CanvasSize.Y;

    for (int32 i = 0; i < MaxDigitCount; ++i)
    {
        if (!DigitImages[i]) continue;

        UCanvasPanelSlot* CanvaSlot = Cast<UCanvasPanelSlot>(DigitImages[i]->Slot);
        if (!CanvaSlot) continue;

        const float X = OriginX_screen + i * (DigitW_screen + Spacing_screen);
        CanvaSlot->SetPosition(FVector2D(X, OriginY_screen));
        CanvaSlot->SetSize(FVector2D(DigitW_screen, DigitH_screen));
        CanvaSlot->SetAutoSize(false);
    }

    bDigitImagesPositioned = true;

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleLayout : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] PositionDigitImages: canvas=(%.0f,%.0f)  "
                "digit=(%.1f,%.1f)  spacing=%.1f  origin=(%.1f,%.1f)"),
            CanvasSize.X, CanvasSize.Y,
            DigitW_screen, DigitH_screen, Spacing_screen,
            OriginX_screen, OriginY_screen);
}

// ---------------------------------------------------------------------------
//  DecomposeValue
//
//  XXX.X format:
//    Digits[0] = hundreds digit
//    Digits[1] = tens digit
//    Digits[2] = units digit
//    Digits[3] = tenths digit
// ---------------------------------------------------------------------------
void UNumericDisplayModule::DecomposeValue(float Value,
    int32 OutDigits[MaxDigitCount]) const
{
    const float MaxVal = Config.GetFloat(HUDConfigKeys::MaxValue, 999.9f);
    const float Clamped = FMath::Clamp(Value, 0.f, MaxVal);

    // Round to 1 decimal place to avoid floating point drift (e.g. 12.95 -> 13.0)
    const int32 AsInt = FMath::RoundToInt(Clamped * 10.f);

    const int32 Tenths = AsInt % 10;
    const int32 Units = (AsInt / 10) % 10;
    const int32 Tens = (AsInt / 100) % 10;
    const int32 Hundreds = (AsInt / 1000) % 10;

    OutDigits[0] = Hundreds;
    OutDigits[1] = Tens;
    OutDigits[2] = Units;
    OutDigits[3] = Tenths;
}

// ---------------------------------------------------------------------------
//  PushDigitToMaterial
// ---------------------------------------------------------------------------
void UNumericDisplayModule::PushDigitToMaterial(int32 SlotIndex, int32 DigitValue)
{
    if (!DigitMIDs[SlotIndex]) return;

    const int32 D = FMath::Clamp(DigitValue, 0, 9);

    const float UMin = D * CachedUStep;
    const float UMax = UMin + CachedUStep;

    DigitMIDs[SlotIndex]->SetScalarParameterValue(TEXT("UMin"), UMin);
    DigitMIDs[SlotIndex]->SetScalarParameterValue(TEXT("UMax"), UMax);
    DigitMIDs[SlotIndex]->SetScalarParameterValue(TEXT("VMin"), CachedVMin);
    DigitMIDs[SlotIndex]->SetScalarParameterValue(TEXT("VMax"), CachedVMax);
}

// ---------------------------------------------------------------------------
//  UpdateAllDigits
// ---------------------------------------------------------------------------
void UNumericDisplayModule::UpdateAllDigits(float Value)
{
    int32 NewDigits[MaxDigitCount];
    DecomposeValue(Value, NewDigits);

    for (int32 i = 0; i < MaxDigitCount; ++i)
    {
        if (NewDigits[i] == LastDigits[i]) continue; // skip unchanged digits

        PushDigitToMaterial(i, NewDigits[i]);
        LastDigits[i] = NewDigits[i];
    }
}