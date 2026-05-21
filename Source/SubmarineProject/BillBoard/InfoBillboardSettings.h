#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InfoBillboardSettings.generated.h"

/**
 * EBillboardEntityType
 *
 * Which type of entity a billboard is being applied to.
 * Used to pick the correct field resolver at runtime.
 */
UENUM(BlueprintType)
enum class EBillboardEntityType : uint8
{
    Submarine,
    Torpedo
};

/**
 * EBillboardRelationship
 *
 * The relationship between the local player and the entity carrying this billboard.
 * Determines which settings from UInfoBillboardContextSettings are used.
 */
UENUM(BlueprintType)
enum class EBillboardRelationship : uint8
{
    Self,               // This is the local player's own submarine
    AlliedPlayer,       // Human ally
    AlliedCPU,          // CPU ally
    EnemyPlayer,        // Human enemy
    EnemyCPU,           // CPU enemy
    OwnTorpedo,         // Torpedo fired by local player
    AlliedPlayerTorpedo,
    AlliedCPUTorpedo,
    EnemyPlayerTorpedo,
    EnemyCPUTorpedo
};

/**
 * UInfoBillboardSettings
 *
 * Configures the appearance and content of one billboard variant.
 * One instance per relationship type (Self, Allied, Enemy, etc).
 *
 * Assign instances to UInfoBillboardContextSettings.
 *
 * Template format for TextTemplate:
 *   Use {FieldName} to insert dynamic values. Supported fields:
 *
 *   Submarine fields:
 *     {Name}                   Display name
 *     {TeamName}               Team name from FMatchTeamSettings
 *     {FactionName}            Faction name from FMatchTeamSettings
 *     {Level}                  Level (future -- returns "1" for now)
 *     {PlayerType}             "Player" or "CPU"
 *     {Relationship}           "Ally", "Enemy", or "Self"
 *     {TorpedoStatus}          "Ready" or "Cooldown"
 *     {NormalAmmo}             Current normal torpedo count
 *     {NormalAmmoMax}          Normal torpedo capacity
 *     {SpecialAmmo}            Current special torpedo count
 *     {SpecialAmmoMax}         Special torpedo capacity
 *
 *   Torpedo fields:
 *     {Name}                   Owner name + torpedo type, e.g. "Player1 - Heavy"
 *     {TorpedoType}            "Light", "Normal", "Heavy", "Seeker", "Radio"
 *     {Damage}                 Attack damage value
 *     {Lifetime}               Max lifetime in seconds
 *     {TimeLeft}               Remaining lifetime (dynamic)
 *     {Owner}                  Display name of the firing submarine
 */
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UInfoBillboardSettings : public UDataAsset
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Visibility
    // -----------------------------------------------------------------------

    /** If false, no billboard is shown for this relationship category. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard")
    bool bVisible = true;

    // -----------------------------------------------------------------------
    //  Text
    // -----------------------------------------------------------------------

    /**
     * Template string with {FieldName} tokens.
     * Example: "{Name} ({TeamName}) - {PlayerType}"
     * Leave empty to show no text.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text",
        meta = (MultiLine = "true"))
    FString TextTemplate;

    /** Font size in Slate units. Ignored if bAutoFitText = true. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text",
        meta = (ClampMin = "4"))
    float FontSize = 14.f;

    /**
     * Multiplier applied to FontSize to estimate rendered text height in pixels.
     * Used for vertical text centering in canvas-slot billboards.
     * Default 1.2 works for most fonts at standard DPI.
     * Increase if text appears too high; decrease if too low.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text",
        meta = (ClampMin = "0.5", ClampMax = "3.0"))
    float TextHeightCoefficient = 1.4f;

    /** Horizontal Offset Text placement.*/
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text")
    float TextOffsetH = 0.f;

    /** Vertical Offset Text placement.*/
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text")
    float TextOffsetV = 0.f;

    /**
     * If true, the font size is automatically reduced to fit the text
     * within the background width. FontSize becomes the maximum allowed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text")
    bool bAutoFitText = false;

    /** Horizontal text alignment within the background. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text")
    TEnumAsByte<EHorizontalAlignment> TextHAlign = HAlign_Center;

    /** Vertical text alignment within the background. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text")
    TEnumAsByte<EVerticalAlignment> TextVAlign = VAlign_Center;

    /** Font typeface. Leave None to use the widget's default font. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text")
    FSlateFontInfo Font;

    /** Base text color. Overridden by bUseTeamColor if true. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text")
    FLinearColor TextColor = FLinearColor::White;

    /** If true, text color matches the entity's team color. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text")
    bool bUseTeamColor = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TextOpacity = 1.f;

    // -----------------------------------------------------------------------
    //  Text bold outline (shadow-based legibility at distance)
    // -----------------------------------------------------------------------

    /**
     * If true, draws a shadow/outline behind the text for legibility at distance.
     * Uses SlateShadow (cheap, no material needed).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text|Outline")
    bool bEnableOutline = false;

    /** Outline/shadow color (usually black or dark team color). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text|Outline",
        meta = (EditCondition = "bEnableOutline"))
    FLinearColor OutlineColor = FLinearColor(0.f, 0.f, 0.f, 1.f);

    /**
     * Outline size in pixels. 1-2 is subtle, 3+ is bold.
     * Larger values improve visibility from far away.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text|Outline",
        meta = (EditCondition = "bEnableOutline", ClampMin = "0.5", ClampMax = "8.0"))
    float OutlineSize = 1.5f;

    // -----------------------------------------------------------------------
    //  Text glow (emissive outline / glow material)
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text|Glow")
    bool bEnableGlow = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text|Glow",
        meta = (EditCondition = "bEnableGlow"))
    FLinearColor GlowColor = FLinearColor(0.2f, 0.6f, 1.f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text|Glow",
        meta = (EditCondition = "bEnableGlow", ClampMin = "0.0"))
    float GlowIntensity = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Text|Glow",
        meta = (EditCondition = "bEnableGlow", ClampMin = "0.0"))
    float GlowRadius = 2.f;

    // -----------------------------------------------------------------------
    //  Background
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Background")
    FLinearColor BackgroundColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);

    /**
     * Background size in screen pixels. (0,0) = use the BP widget's designed size.
     * Set this in the DA to override the widget size for this specific relationship.
     * Also drives text word-wrap: text wraps at this width (if > 0).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Background")
    FVector2D BackgroundSize = FVector2D(0.f, 0.f);

    /**
     * If true, use BackgroundSize as the draw size for UWidgetComponent.
     * Falls back to BP widget desired size when false or BackgroundSize is (0,0).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Background")
    bool bOverrideDrawSize = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Background",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BackgroundOpacity = 0.7f;

    /** Optional background texture (rounded rect, frame, etc.). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Background")
    TObjectPtr<UTexture2D> BackgroundTexture;

    // -----------------------------------------------------------------------
    //  Transform / placement
    // -----------------------------------------------------------------------

    /**
     * World-space offset from the entity's root (in cm).
     * Positive Z = above the entity.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Placement")
    FVector WorldOffset = FVector(0.f, 0.f, 200.f);

    /** Overall billboard opacity multiplier (0 = hidden, 1 = fully visible). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Placement",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OverallOpacity = 1.f;

    /**
     * Screen ZOrder for the billboard widget.
     * Negative values render below the HUD (recommended: -1).
     * HUD modules default to ZOrder=10 on their canvas slots.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Placement")
    int32 BillboardZOrder = -1;

    /**
     * Maximum world distance (cm) at which the billboard is visible.
     * 0 = always visible regardless of distance.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Placement",
        meta = (ClampMin = "0.0"))
    float MaxVisibleDistance = 0.f;

    /**
     * If true, this billboard is always visible regardless of radar identification.
     * Use this for Spectator and DeathReplay contexts where the observer
     * has no radar and should see all billboards unconditionally.
     * If false, the billboard respects bRequireIdentification on the pawn component.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard|Placement")
    bool bIgnoreIdentificationGate = false;
};