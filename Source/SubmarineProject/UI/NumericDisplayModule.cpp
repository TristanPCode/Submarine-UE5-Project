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

    // Retry positioning if canvas wasn't laid out yet when first attempted.
    // This is the main fix for digits appearing at (0,0) with wrong size.
    if (!bWidgetsPositioned)
        PositionDigitImages();

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] NativeOnInitialized  Module='%s'  DigitCount=%d"),
            *Config.ModuleName.ToString(), GetDigitCount());
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

    // Compute atlas UV constants
    const float TotalH = Config.GetFloat(HUDConfigKeys::AtlasTotalHeight, 510.f);
    const float TotalW = Config.GetFloat(HUDConfigKeys::AtlasTotalWidth, 842.f);
    const float DigitH = Config.GetFloat(HUDConfigKeys::AtlasDigitHeight, 167.f);
    const float DigitW = Config.GetFloat(HUDConfigKeys::AtlasDigitWidth, 82.f);
    const float RowIndex = Config.GetFloat(HUDConfigKeys::AtlasRowIndex, 0.f);
    const float VSpacing = Config.GetFloat(TEXT("AtlasVSpacing"), 3.f);
    const float HSpacing = Config.GetFloat(TEXT("AtlasHSpacing"), 2.f);
    const float FirstOffX = Config.GetFloat(TEXT("AtlasFirstOffsetX"), 1.f);
    const float FirstOffY = Config.GetFloat(TEXT("AtlasFirstOffsetY"), 2.f);

    // V range for this row (accounts for top offset and row spacing)
    const float RowTopPx = FirstOffY + RowIndex * (DigitH + VSpacing);
    CachedVMin = RowTopPx / FMath::Max(TotalH, 1.f);
    CachedVMax = (RowTopPx + DigitH) / FMath::Max(TotalH, 1.f);

    // U step: width of one digit cell including right spacing
    const float CellW = DigitW + HSpacing;
    CachedUStep = CellW / FMath::Max(TotalW, 1.f);

    // U offset for the very first pixel of digit 0
    CachedUFirstOffset = FirstOffX / FMath::Max(TotalW, 1.f);
    CachedUDigitWidth = DigitW / FMath::Max(TotalW, 1.f); // pure digit width, no spacing

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] Atlas UVs: row=%.0f  VMin=%.4f  VMax=%.4f  UStep=%.4f"),
            RowIndex, CachedVMin, CachedVMax, CachedUStep);

    // Reset last digits to force full refresh on first tick
    const int32 DigitCount = GetDigitCount();
    LastDigits.SetNum(DigitCount);
    for (int32& D : LastDigits) D = -1;

    // Ensure digit widgets exist before creating MIDs.
    // CreateDigitWidgets is normally called from RefreshVisuals, but
    // BindToDataSource may fire before RefreshVisuals on some paths.
    if (!bWidgetsCreated)
        CreateDigitWidgets();

    bMIDsReady = CreateDigitMIDs();
    if (!bMIDsReady)
        UE_LOG(LogTemp, Warning,
            TEXT("[NumericDisplay] BindToDataSource: MID creation failed for '%s'. "
                "Ensure MatAtlasSample is set in config."),
            *Config.ModuleName.ToString());

    // Position widgets now that config is set
    if (!bWidgetsPositioned)
        PositionDigitImages();

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

    for (int32& D : LastDigits) D = -1;
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

    // Create digit widgets here -- Config is valid, DigitCount is readable.
    if (!bWidgetsCreated)
        CreateDigitWidgets();

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

    // Retry positioning every tick until the canvas has a valid size.
    // For dynamically created child widgets, geometry is not available
    // until after the first layout pass (one or more frames after creation).
    if (!bWidgetsPositioned)
        PositionDigitImages();

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
//  GetDigitCount / GetDecimalIndex
// ---------------------------------------------------------------------------
int32 UNumericDisplayModule::GetDigitCount() const
{
    return FMath::Max(1, (int32)Config.GetFloat(TEXT("DigitCount"), 4.f));
}

int32 UNumericDisplayModule::GetDecimalIndex() const
{
    return (int32)Config.GetFloat(TEXT("DecimalIndex"), -1.f);
}

// ---------------------------------------------------------------------------
//  CreateDigitWidgets
//  Dynamically creates UImage widgets for each digit and the decimal dot.
//  Called once from NativeOnInitialized.
// ---------------------------------------------------------------------------
void UNumericDisplayModule::CreateDigitWidgets()
{
    if (bWidgetsCreated) return;
    if (!DigitCanvas) return;

    const int32 DigitCount = GetDigitCount();
    const int32 DecimalIdx = GetDecimalIndex();

    DigitImages.SetNum(DigitCount);

    for (int32 i = 0; i < DigitCount; ++i)
    {
        UImage* Img = NewObject<UImage>(this,
            *FString::Printf(TEXT("Digit_%02d"), i));
        if (!Img) continue;

        DigitCanvas->AddChildToCanvas(Img);
        DigitImages[i] = Img;
    }

    bWidgetsCreated = true;

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] CreateDigitWidgets: created %d digit images "
                "(decimal at %d) for '%s'"),
            DigitCount, DecimalIdx, *Config.ModuleName.ToString());

    PositionDigitImages();
}

// ---------------------------------------------------------------------------
//  PositionDigitImages
//  All pixel values from config are in original texture space.
//  Converted to screen pixels via ratio * CanvasSize.
// ---------------------------------------------------------------------------
void UNumericDisplayModule::PositionDigitImages()
{
    if (!DigitCanvas || DigitImages.IsEmpty()) return;

    const FVector2D CanvasSize = DigitCanvas->GetCachedGeometry().GetLocalSize();
    if (CanvasSize.IsNearlyZero())
    {
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] PositionDigitImages: canvas size ZERO for '%s' -- retrying next tick"),
            *Config.ModuleName.ToString());
        return;
    }

    // Reference dimensions for ratio conversion
    const float RefW = FMath::Max(Config.GetFloat(HUDConfigKeys::AtlasTotalWidth, 842.f), 1.f);
    const float RefH = FMath::Max(Config.GetFloat(HUDConfigKeys::AtlasTotalHeight, 510.f), 1.f);

    // We use the module background size as reference for digit positions
    // so they scale correctly when the module is resized.
    const float BgW = FMath::Max(Config.GetFloat(HUDConfigKeys::TextureWidth, 524.f), 1.f);
    const float BgH = FMath::Max(Config.GetFloat(HUDConfigKeys::TextureHeight, 364.f), 1.f);

    // Convert to screen space
    auto ToScreenX = [&](float Px) { return (Px / BgW) * CanvasSize.X; };
    auto ToScreenY = [&](float Px) { return (Px / BgH) * CanvasSize.Y; };

    const float DigitW_px = Config.GetFloat(HUDConfigKeys::AtlasDigitWidth, 82.f);
    const float DigitH_px = Config.GetFloat(HUDConfigKeys::AtlasDigitHeight, 167.f);
    const float OriginX_px = Config.GetFloat(HUDConfigKeys::DigitOffsetX, 142.f);
    const float OriginY_px = Config.GetFloat(HUDConfigKeys::DigitOffsetY, 33.f);
    const float Spacing_px = Config.GetFloat(HUDConfigKeys::DigitSpacing, 33.f);
    const float BigSpacing_px = Config.GetFloat(TEXT("BigSpacing"), 0.f);

    const int32 DecimalIdx = GetDecimalIndex();
    const int32 DigitCount = DigitImages.Num();

    // Digit width and height in screen pixels
    const float DW = ToScreenX(DigitW_px);
    const float DH = ToScreenY(DigitH_px);

    float CursorX = ToScreenX(OriginX_px);
    const float OriginY = ToScreenY(OriginY_px);

    for (int32 i = 0; i < DigitCount; ++i)
    {
        if (!IsValid(DigitImages[i])) continue;

        UCanvasPanelSlot* CanvaSlot = Cast<UCanvasPanelSlot>(DigitImages[i]->Slot);
        if (!CanvaSlot) continue;

        CanvaSlot->SetPosition(FVector2D(CursorX, OriginY));
        CanvaSlot->SetSize(FVector2D(DW, DH));
        CanvaSlot->SetAutoSize(false);
        CanvaSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
        CanvaSlot->SetAlignment(FVector2D::ZeroVector);

        // Advance cursor for the NEXT digit
        // If the NEXT digit is at the decimal index, use BigSpacing
        if (i < DigitCount - 1)
        {
            const bool bNextIsDotSide = (DecimalIdx > 0 && (i + 1) == DecimalIdx);
            const float NextSpacing_px = bNextIsDotSide ? BigSpacing_px : Spacing_px;
            CursorX += ToScreenX(NextSpacing_px);
        }
    }

    bWidgetsPositioned = true;

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleLayout : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] PositionDigitImages: canvas=(%.0f,%.0f)  "
                "digit=(%.1f,%.1f)  normalGap=%.1f  bigGap=%.1f"),
            CanvasSize.X, CanvasSize.Y, DW, DH,
            ToScreenX(Spacing_px), ToScreenX(BigSpacing_px));
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

    const int32 DigitCount = DigitImages.Num();
    DigitMIDs.SetNum(DigitCount);

    bool bAllOk = true;
    for (int32 i = 0; i < DigitCount; ++i)
    {
        if (!IsValid(DigitImages[i]))
        {
            bAllOk = false;
            continue;
        }

        DigitMIDs[i] = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (!DigitMIDs[i])
        {
            bAllOk = false;
            continue;
        }

        DigitMIDs[i]->SetTextureParameterValue(TEXT("Atlas"), AtlasTex);
        DigitImages[i]->SetBrushFromMaterial(DigitMIDs[i]);
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] CreateDigitMIDs: %s  count=%d  module='%s'"),
            bAllOk ? TEXT("OK") : TEXT("PARTIAL"),
            DigitCount, *Config.ModuleName.ToString());

    return bAllOk;
}

// ---------------------------------------------------------------------------
//  DecomposeValue
//
//  Converts a float value into individual digit integers.
//  The decimal index determines where the implicit decimal point sits,
//  but does NOT add an extra digit — it's purely for display.
//
//  Example with DigitCount=4, DecimalIndex=3 (XXX.X):
//    Value = 524.4 -> AsInt = 5244 (shift by number of fractional digits)
//    digits = [5, 2, 4, 4]
//
//  Example with DigitCount=2, DecimalIndex=-1 (XX):
//    Value = 7 -> digits = [0, 7]
// ---------------------------------------------------------------------------
void UNumericDisplayModule::DecomposeValue(float Value,
    TArray<int32>& OutDigits,
    int32          DecimalIndex) const
{
    const float MaxVal = Config.GetFloat(HUDConfigKeys::MaxValue, 999.9f);
    const float Clamped = FMath::Clamp(Value, 0.f, MaxVal);
    const int32 DigitCount = GetDigitCount();

    OutDigits.SetNum(DigitCount);

    // Number of fractional digits = total digits after the decimal index
    // DecimalIndex = position of the first fractional digit
    // e.g. DecimalIndex=3 with 4 digits -> 1 fractional digit
    const int32 FractionalDigits = (DecimalIndex >= 0 && DecimalIndex < DigitCount)
        ? (DigitCount - DecimalIndex) : 0;

    // Scale value to integer (e.g. 524.4 with 1 fractional -> 5244)
    const int32 Scale = FMath::RoundToInt(FMath::Pow(10.f, (float)FractionalDigits));
    const int32 AsInt = FMath::RoundToInt(Clamped * Scale);

    // Extract digits from least significant to most significant
    int32 Remaining = AsInt;
    for (int32 i = DigitCount - 1; i >= 0; --i)
    {
        OutDigits[i] = Remaining % 10;
        Remaining /= 10;
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleRefresh : false))
    {
        FString DigStr;
        for (int32 D : OutDigits) DigStr += FString::Printf(TEXT("%d"), D);
        if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false)) {
            UE_LOG(LogTemp, Log,
                TEXT("[NumericDisplay] DecomposeValue: raw=%.2f coeff=1 clamped=%.2f asInt=%d digits=%s"),
                Value, Clamped, AsInt, *DigStr);
        }
    }
}

// ---------------------------------------------------------------------------
//  PushDigitToMaterial
// ---------------------------------------------------------------------------
void UNumericDisplayModule::PushDigitToMaterial(int32 SlotIndex, int32 DigitValue)
{
    if (!DigitMIDs.IsValidIndex(SlotIndex) || !DigitMIDs[SlotIndex]) return;

    const int32 D = FMath::Clamp(DigitValue, 0, 9);

    // UMin: left edge of this digit cell (start of digit 0 + D full cells)
    // UMax: UMin + pure digit width (NOT including the spacing gap)
    // CachedUStep = (DigitW + HSpacing) / TotalW  -- cell pitch
    // CachedUFirstOffset = FirstOffsetX / TotalW  -- first pixel offset
    const float UMin = CachedUFirstOffset + D * CachedUStep;
    const float UMax = UMin + CachedUDigitWidth; // CachedUDigitWidth = DigitW/TotalW

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleRefresh : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NumericDisplay] PushDigit slot=%d digit=%d UMin=%.5f UMax=%.5f VMin=%.4f VMax=%.4f"),
            SlotIndex, D, UMin, UMax, CachedVMin, CachedVMax);

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
    // Apply optional display coefficient from DA (key "DisplayCoefficient").
    // Example: raw speed 2000 cm/s * 0.00125 = 2.5 displayed.
    // Default 1.0 = no scaling.
    const float Coeff = Config.GetFloat(TEXT("DisplayCoefficient"), 1.f);
    Value *= Coeff;

    const int32 DigitCount = GetDigitCount();
    const int32 DecimalIdx = GetDecimalIndex();

    TArray<int32> NewDigits;
    DecomposeValue(Value, NewDigits, DecimalIdx);

    if (LastDigits.Num() != DigitCount)
    {
        LastDigits.SetNum(DigitCount);
        for (int32& D : LastDigits) D = -1;
    }

    for (int32 i = 0; i < DigitCount; ++i)
    {
        if (!NewDigits.IsValidIndex(i)) continue;
        if (NewDigits[i] == LastDigits[i]) continue; // skip unchanged

        PushDigitToMaterial(i, NewDigits[i]);
        LastDigits[i] = NewDigits[i];
    }
}