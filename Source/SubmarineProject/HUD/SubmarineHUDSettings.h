#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Widgets/Layout/Anchors.h"
#include "SubmarineHUDDebugSettings.h"
#include "TorpedoCharacteristics.h"
#include "SubmarineHUDSettings.generated.h"

class UBaseHUDModule;
class UMainHUDWidget;

USTRUCT(BlueprintType)
struct FTorpedoTypeIconPair
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UTexture2D> ReadyIcon;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UTexture2D> CooldownIcon;
};

// ============================================================================
//  Standardized FHUDModuleConfig key names
//
//  Use these constants everywhere instead of raw FName literals to prevent
//  runtime typo mistakes. Modules read config values via these keys.
// ============================================================================

namespace HUDConfigKeys
{
    // -- Shared layout / texture keys ----------------------------------------
    static const FName Background = TEXT("Background");
    static const FName Overlay = TEXT("Overlay");

    // -- Health bar ----------------------------------------------------------
    static const FName FillGreen = TEXT("FillGreen");
    static const FName FillYellow = TEXT("FillYellow");
    static const FName FillRed = TEXT("FillRed");
    static const FName TextureWidth = TEXT("TextureWidth");
    static const FName TextureHeight = TEXT("TextureHeight");
    static const FName OffsetLeft = TEXT("OffsetLeft");
    static const FName OffsetRight = TEXT("OffsetRight");
    static const FName ThresholdYellow = TEXT("ThresholdYellow");
    static const FName ThresholdRed = TEXT("ThresholdRed");

    // -- Positional indicator (engine state / pitch) -------------------------
    static const FName Crank = TEXT("Crank");
    static const FName Metal = TEXT("Metal");
    static const FName CrankOffsetX = TEXT("CrankOffsetX");
    static const FName MetalOffsetX = TEXT("MetalOffsetX");
    static const FName MaxPitchAngle = TEXT("MaxPitchAngle");
    static const FName CrankWidth = TEXT("CrankWidth");
    static const FName CrankHeight = TEXT("CrankHeight");
    static const FName MetalWidth = TEXT("MetalWidth");
    static const FName MetalHeight = TEXT("MetalHeight");

    // -- Numeric display (digit atlas) ----------------------------------------
    static const FName DigitAtlas = TEXT("DigitAtlas");
    static const FName AtlasDigitWidth = TEXT("AtlasDigitWidth");
    static const FName AtlasDigitHeight = TEXT("AtlasDigitHeight");
    static const FName AtlasTotalWidth = TEXT("AtlasTotalWidth");
    static const FName AtlasTotalHeight = TEXT("AtlasTotalHeight");
    static const FName AtlasRowIndex = TEXT("AtlasRowIndex");
    static const FName DigitSpacing = TEXT("DigitSpacing");
    static const FName DigitOffsetX = TEXT("DigitOffsetX");
    static const FName DigitOffsetY = TEXT("DigitOffsetY");
    static const FName MaxValue = TEXT("MaxValue");
    static const FName DecimalPlaces = TEXT("DecimalPlaces");
    static const FName DisplayCoefficient = TEXT("DisplayCoefficient");

    // -- Materials -----------------------------------------------------------
    static const FName MatClip = TEXT("MatClip");          // M_HUD_Clip base material
    static const FName MatAtlasSample = TEXT("MatAtlasSample");   // M_HUD_AtlasSample base material

    // -- Normal ammo module child layout (normalized 0..1 relative to parent) -
    static const FName CounterSizeX = TEXT("Counter_SizeX");
    static const FName CounterSizeY = TEXT("Counter_SizeY");
    static const FName CounterPosX = TEXT("Counter_PosX");
    static const FName CounterPosY = TEXT("Counter_PosY");
    static const FName TorpedoSizeX = TEXT("Torpedo_SizeX");
    static const FName TorpedoSizeY = TEXT("Torpedo_SizeY");
    static const FName TorpedoPosX = TEXT("Torpedo_PosX");
    static const FName TorpedoPosY = TEXT("Torpedo_PosY");

    // -- Torpedo icon module -------------------------------------------------
    static const FName MatTorpedoIcon = TEXT("MatTorpedoIcon");
    static const FName MatReloadOverlay = TEXT("MatReloadOverlay");
    static const FName TorpedoReadyTex = TEXT("TorpedoReady");
    static const FName TorpedoCooldownTex = TEXT("TorpedoCooldown");
    static const FName GlowMaskTex = TEXT("GlowMask");       // optional
    static const FName GlowPulseSpeed = TEXT("GlowPulseSpeed");
    static const FName FlashDuration = TEXT("FlashDuration");
    static const FName IconTextureWidth = TEXT("IconTextureWidth");
    static const FName IconTextureHeight = TEXT("IconTextureHeight");

    // -- Special ammo grid ---------------------------------------------------
    static const FName MatSpecialSlot = TEXT("MatSpecialSlot");
    static const FName GridColumns = TEXT("GridColumns");
    static const FName GridRowAlignment = TEXT("GridRowAlignment"); // 0=Left,1=Right,2=Center
    static const FName SlotAspectRatio = TEXT("SlotAspectRatio");  // W/H of one slot icon
    static const FName SlotSpacingX = TEXT("SlotSpacingX");     // authored in original tex pixels
    static const FName SlotSpacingY = TEXT("SlotSpacingY");
}

// ============================================================================
//  FEngineStateEntry
//
//  Per-state configuration for UPositionalIndicatorModule subclasses.
//  Stores pixel-based offsets for the crank and metal elements at one
//  ELinearSpeedState. Array index matches ELinearSpeedState cast to int32.
// ============================================================================
USTRUCT(BlueprintType)
struct FEngineStateEntry
{
    GENERATED_BODY()

    /**
     * Vertical pixel offset of the Crank element from the top of the texture.
     * Interpreted as ratio at runtime: CrankY / TextureHeight.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EngineState")
    float CrankY = 0.f;

    /**
     * Vertical pixel offset of the Metal element from the top of the texture.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EngineState")
    float MetalY = 0.f;

    /**
     * If true, CrankY is measured from the BOTTOM instead of the top.
     * FinalY = TextureHeight - CrankY.
     * Does NOT flip the texture visually.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EngineState")
    bool bInvertCrank = false;

    /**
     * If true, MetalY is measured from the BOTTOM instead of the top.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EngineState")
    bool bInvertMetal = false;

    /** Flip the Crank texture vertically (visual only, does not change position). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EngineState")
    bool bFlipCrankTexture = false;

    /** Flip the Metal texture vertically (visual only, does not change position). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EngineState")
    bool bFlipMetalTexture = false;
};

// -----------------------------------------------------------------------
//  FHUDModuleConfig
//
//  Configuration for a single HUD module.
//  Stored in USubmarineHUDSettings::Modules array (ordered).
//  Passed verbatim to UBaseHUDModule::SetConfig() at widget creation.
// -----------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FHUDModuleConfig
{
    GENERATED_BODY()

    // -- Identity ----------------------------------------------------------

    /**
     * Unique name for this module entry.
     * Used as a key for lookup and for debug logging.
     * Examples: "HealthBar", "Speedometer", "AmmoNormal", "Radar"
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
    FName ModuleName = NAME_None;

    /**
     * Widget class to instantiate for this module.
     * Must be a subclass of UBaseHUDModule.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
    TSubclassOf<UBaseHUDModule> ModuleClass;

    /** If false, this module is skipped during HUD initialisation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
    bool bEnabled = true;

    /* Set the Z order for the module */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module", meta = (ClampMin = "0"))
    int32 ZOrder = 10;

    /**
     * Child module class used for slot instantiation in composite modules
     * (e.g. USpecialAmmoModule uses this for USpecialTorpedoSlotModule).
     */
     /**
      * Blueprint subclass for the numeric counter child (NormalAmmoModule).
      * Set to BP_AmmoCounterModule_C so DigitCanvas BindWidget resolves.
      * If unset, base C++ class is used and digits are invisible.
      */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
    TSubclassOf<UBaseHUDModule> CounterModuleClass;

    /**
     * Child module class used for slot instantiation in composite modules
     * (e.g. USpecialAmmoModule uses this for USpecialTorpedoSlotModule).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
    TSubclassOf<UBaseHUDModule> SlotModuleClass;

    // -- Layout (UMG-native) -----------------------------------------------

    /**
     * UMG anchor points (Min/Max in 0..1 screen space).
     * Maps directly to UCanvasPanelSlot::SetAnchors().
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
    FAnchors Anchors = FAnchors(0.f, 0.f, 0.f, 0.f);

    /**
     * Pivot point of the widget (0..1 relative to the widget size).
     * (0,0) = top-left, (0.5,0.5) = center, (1,1) = bottom-right.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
    FVector2D Pivot = FVector2D(0.f, 0.f);

    /**
     * Pixel offset applied after anchor resolution.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
    FVector2D PositionOffset = FVector2D::ZeroVector;

    /**
     * Explicit size in pixels. (0,0) = let UMG size automatically.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
    FVector2D SizeOverride = FVector2D::ZeroVector;

    // -- Generic parameters ------------------------------------------------

    /** Named textures (backgrounds, icons, fill bars, etc.). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
    TMap<FName, TObjectPtr<UTexture2D>> Textures;

    /** Named float parameters (thresholds, speeds, offsets, sizes, etc.). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
    TMap<FName, float> Floats;

    /** Named colors (fill, background, warning, critical states, etc.). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
    TMap<FName, FLinearColor> Colors;

    /**
     * Base material references.
     * Standard keys: HUDConfigKeys::MatClip, HUDConfigKeys::MatAtlasSample.
     * Modules create dynamic instances from these at BindToDataSource time.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
    TMap<FName, TObjectPtr<UMaterialInterface>> Materials;

    // -- Engine state / pitch ----------------------------------------------

    /**
     * Per-state position data for UPositionalIndicatorModule subclasses.
     * Array index must match (int32)ELinearSpeedState cast.
     * Order: BackwardMAX=0, BackwardMED=1, BackwardMIN=2, Stand=3,
     *        ForwardMIN=4, ForwardMED=5, ForwardMAX=6.
     * Leave empty for non-positional modules.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EngineState")
    TArray<FEngineStateEntry> StateEntries;

    // -- Special ammo grid -------------------------------------------------

    /**
     * Icon texture per torpedo type.
     * Each slot reads its icon from this map using its SlotType.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmmoGrid")
    TMap<ETorpedoType, FTorpedoTypeIconPair> IconPerType;

    /**
     * If false: slots are auto-filled row by row using GridColumns.
     * If true:  RowCounts defines slots per row explicitly.
     *   - Overflow (sum > capacity): trim from last rows.
     *   - Underflow (sum < capacity): append to last row, adding rows if needed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmmoGrid")
    bool bUseCustomRowCounts = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmmoGrid",
        meta = (EditCondition = "bUseCustomRowCounts"))
    TArray<int32> RowCounts;

    // -- Helpers -----------------------------------------------------------

    float GetFloat(FName Key, float Default = 0.f) const
    {
        const float* Val = Floats.Find(Key);
        return Val ? *Val : Default;
    }

    UTexture2D* GetTexture(FName Key) const
    {
        const TObjectPtr<UTexture2D>* Val = Textures.Find(Key);
        return Val ? Val->Get() : nullptr;
    }

    UMaterialInterface* GetMaterial(FName Key) const
    {
        const TObjectPtr<UMaterialInterface>* Val = Materials.Find(Key);
        return Val ? Val->Get() : nullptr;
    }

    UTexture2D* GetIconForType(ETorpedoType Type, bool bReady) const
    {
        const FTorpedoTypeIconPair* Val = IconPerType.Find(Type);
        if (!Val) return nullptr;
        return bReady ? Val->ReadyIcon.Get() : Val->CooldownIcon.Get();
    }

    FLinearColor GetColor(FName Key, FLinearColor Default = FLinearColor::White) const
    {
        const FLinearColor* Val = Colors.Find(Key);
        return Val ? *Val : Default;
    }
};

// -----------------------------------------------------------------------
//  USubmarineHUDSettings
//
//  One DataAsset = one complete HUD configuration.
//  Create one for Gameplay, one for Spectator (or share if identical).
// -----------------------------------------------------------------------
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API USubmarineHUDSettings : public UDataAsset
{
    GENERATED_BODY()

public:

    /**
     * Widget class used as the root HUD container.
     * Must be a subclass of UMainHUDWidget.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
    TSubclassOf<UMainHUDWidget> WidgetClass;

    /**
     * Ordered list of module configurations.
     * Modules are created in array order -- last entry renders on top.
     * Disabled entries (bEnabled = false) are skipped silently.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TArray<FHUDModuleConfig> Modules;

    /**
     * Debug logging settings for the entire HUD system.
     * Assign a USubmarineHUDDebugSettings DataAsset here.
     * If null, all HUD logging is disabled.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Debug")
    TObjectPtr<USubmarineHUDDebugSettings> DebugSettings;

    /**
     * The pixel resolution all module positions (anchors, offsets, sizes) are
     * authored against. Must match the design size of your BP_MainHUDWidget
     * RootCanvas. Typically 1920x1080 for full-screen or 960x1080 for
     * split-screen-specific DAs.
     *
     * UMG scales this to fit the actual viewport automatically when the widget
     * is added via AddToPlayerScreen. In split-screen each player gets their
     * own half-viewport and UMG scales from this resolution to fit it.
     *
     * Default: (1920, 1080). Set to (960, 1080) if your split-screen DA
     * was designed for a half-width layout.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
    FVector2D DesignResolution = FVector2D(1920.f, 1080.f);

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    const FHUDModuleConfig* FindModule(FName ModuleName) const
    {
        for (const FHUDModuleConfig& Config : Modules)
            if (Config.ModuleName == ModuleName)
                return &Config;
        return nullptr;
    }

    /** Safe log helper -- returns false (disables log) if DebugSettings is null. */
    bool ShouldLog(bool bFlag) const
    {
        return DebugSettings && bFlag;
    }
};