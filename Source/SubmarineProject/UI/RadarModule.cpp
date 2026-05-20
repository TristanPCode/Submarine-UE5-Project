// Fill out your copyright notice in the Description page of Project Settings.

#include "RadarModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Radar/RadarSettings.h"
#include "Radar/RadarComponent.h"
#include "SubmarinePawn.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

// ---------------------------------------------------------------------------
//  NativeOnInitialized
// ---------------------------------------------------------------------------
void URadarModule::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    // Config is NOT set yet at this point (SetConfig fires after NativeOnInitialized).
    // Textures are applied in RefreshVisuals_Implementation instead.

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[RadarModule] NativeOnInitialized  Module='%s'"),
            *Config.ModuleName.ToString());
}

// ---------------------------------------------------------------------------
//  BindToDataSource
// ---------------------------------------------------------------------------
void URadarModule::BindToDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj))
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[RadarModule] BindToDataSource: invalid DataSource"));
        return;
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[RadarModule] BindToDataSource: '%s'"), *Obj->GetName());

    bGeometryCached = false;
    SetContinuousTickEnabled(true);
}

// ---------------------------------------------------------------------------
//  UnbindFromDataSource
// ---------------------------------------------------------------------------
void URadarModule::UnbindFromDataSource()
{
    SetContinuousTickEnabled(false);

    // Clear all icon widgets
    for (auto& Pair : IconPool)
    {
        if (IsValid(Pair.Value) && EntitiesCanvas)
            EntitiesCanvas->RemoveChild(Pair.Value);
    }
    IconPool.Empty();
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
// ---------------------------------------------------------------------------
void URadarModule::RefreshVisuals_Implementation()
{
    // Apply static textures here -- Config is valid by the time RefreshVisuals fires.
    // (NativeOnInitialized fires before SetConfig, so we cannot read textures there.)
    if (BackgroundImage)
    {
        UTexture2D* T = Config.GetTexture(HUDConfigKeys::Background);
        if (T) BackgroundImage->SetBrushFromTexture(T, false);
        if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
            UE_LOG(LogTemp, Log, TEXT("[RadarModule] Background tex: %s"),
            T ? *T->GetName() : TEXT("NULL -- set 'Background' in DA Textures"));

        // Pre-create the MID now so the background never flickers on first scan.
        // Without this, the first scan replaces the texture brush with the MID
        // mid-frame, causing a one-frame black flash.
        if (Config.GetMaterial(TEXT("MatRadarBackground")))
        {
            // Reset MID so CreateBackgroundMID rebuilds it with the new texture
            BackgroundMID = nullptr;
            CreateBackgroundMID();
            // BackgroundMID->SetTextureParameterValue already sets MainTexture
            // inside CreateBackgroundMID, so no need to SetBrushFromTexture here.
        }
        else if (T)
        {
            // No pulse material configured -- use plain texture
            BackgroundImage->SetBrushFromTexture(T, false);
        }
    }

    if (CardinalImage)
    {
        UTexture2D* T = Config.GetTexture(TEXT("CardinalLayer"));
        if (T) CardinalImage->SetBrushFromTexture(T, false);
        if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
            UE_LOG(LogTemp, Log, TEXT("[RadarModule] CardinalLayer tex: %s"),
            T ? *T->GetName() : TEXT("NULL -- optional, set 'CardinalLayer' in DA Textures if needed"));
    }

    if (OverlayImage)
    {
        UTexture2D* T = Config.GetTexture(HUDConfigKeys::Overlay);
        if (T) OverlayImage->SetBrushFromTexture(T, false);
        if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
            UE_LOG(LogTemp, Log, TEXT("[RadarModule] Overlay tex: %s"),
            T ? *T->GetName() : TEXT("NULL -- optional"));
    }

    bGeometryCached = false; // force geometry recache on next tick
}

// ---------------------------------------------------------------------------
//  NativeTick
// ---------------------------------------------------------------------------
void URadarModule::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (!IsValid(DataSource.GetObject())) return;

    // Cache radar geometry on first valid tick
    if (!bGeometryCached)
        CacheRadarGeometry();

    if (!bGeometryCached) return; // canvas not laid out yet

    // Read submarine orientation
    const float SubYaw = DataSource->GetCurrentYaw();

    // Rotate cardinal layer: opposite of entity rotation
    // so North stays fixed in world space
    if (CardinalImage)
        CardinalImage->SetRenderTransformAngle(-SubYaw);

    // Update entity icons
    const TArray<FDetectedEntry>& Entries = DataSource->GetDetectionEntries();

    if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
    {
        ModuleLogTimer += InDeltaTime;
        if (ModuleLogTimer >= 2.f)
        {
            ModuleLogTimer = 0.f;
            if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
                UE_LOG(LogTemp, Log,
                TEXT("[RadarModule] Tick: %d raw entries  SubYaw=%.1f  DataSource=%s"),
                Entries.Num(), SubYaw,
                IsValid(DataSource.GetObject()) ? *DataSource.GetObject()->GetName() : TEXT("NULL"));
            int32 _VisCount = 0;
            for (const FDetectedEntry& E : Entries)
            {
                if (E.DisplayTimeRemaining > 0.f) _VisCount++;
                if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
                    UE_LOG(LogTemp, Log,
                    TEXT("  [RadarModule] Entry: State=%d DisplayTime=%.2f DetTime=%.2f WorldPos=(%.0f,%.0f,%.0f)"),
                    (int32)E.DetectionState,
                    E.DisplayTimeRemaining, E.DetectionTimeRemaining,
                    E.WorldPosition.X, E.WorldPosition.Y, E.WorldPosition.Z);
            }
            if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
                UE_LOG(LogTemp, Log,
                TEXT("[RadarModule] -> %d entries with DisplayTime > 0 (will render icons)"),
                _VisCount);
        }
    }

    UpdateEntityIcons(Entries, SubYaw);

    {
        ASubmarinePawn* SubPawn = Cast<ASubmarinePawn>(DataSource.GetObject());
        URadarComponent* RC = SubPawn ? SubPawn->GetRadarHandler() : nullptr;

        if (!SubPawn)
        {
            // Cast failed -- DataSource is not ASubmarinePawn.
            // This means the tracked actor doesn't inherit from ASubmarinePawn in C++.
            // Pulse glow will not work until this is fixed.
        }
        else if (!RC)
        {
            // SubPawn cast OK but no RadarComponent found.
        }
        else if (RC->GetScanJustTriggered())
        {
            UE_LOG(LogTemp, Log, TEXT("[RadarModule] GetScanJustTriggered loop successful!"));
            RC->ResetScanJustTriggeredFrames(); // Reset frame to enter loop only once
            PulseProgress = 0.f;
            bPulseActive = true;
            if (!BackgroundMID) CreateBackgroundMID();
            // Log ALL material scalar params at pulse start so we can verify the material
            // is receiving the right values and the widget is the right size
            float ReadW = 0, ReadG = 0, ReadP = 0, ReadO = 0;
            BackgroundMID->GetScalarParameterValue(TEXT("PulseWidth"), ReadW);
            BackgroundMID->GetScalarParameterValue(TEXT("PulseGlowIntensity"), ReadG);
            BackgroundMID->GetScalarParameterValue(TEXT("PulseProgress"), ReadP);
            BackgroundMID->GetScalarParameterValue(TEXT("PulseOpacity"), ReadO);
            FVector2D BgActualSize = BackgroundImage
                ? BackgroundImage->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
            if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
                UE_LOG(LogTemp, Log,
                TEXT("[RadarModule] Pulse START: Width=%.3f Glow=%.2f Progress=%.3f Opacity=%.3f "
                    "BG_DesiredSize=(%.0f,%.0f) BG_CachedGeomSize=(%.0f,%.0f)"),
                ReadW, ReadG, ReadP, ReadO,
                BackgroundImage ? BackgroundImage->GetDesiredSize().X : -1.f,
                BackgroundImage ? BackgroundImage->GetDesiredSize().Y : -1.f,
                BgActualSize.X, BgActualSize.Y);
            if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
                UE_LOG(LogTemp, Log,
                TEXT("[RadarModule] Pulse triggered: BackgroundMID=OK"));
        }
    }

    TickPulse(InDeltaTime);
}

// ---------------------------------------------------------------------------
//  CacheRadarGeometry
// ---------------------------------------------------------------------------
void URadarModule::CacheRadarGeometry()
{
    if (!RadarCanvas) return;

    const FVector2D CanvasSize = RadarCanvas->GetCachedGeometry().GetLocalSize();
    if (CanvasSize.IsNearlyZero()) return;

    // Radar is centered in the canvas
    RadarCenter = CanvasSize * 0.5f;

    // RadarRadius in screen pixels: convert from original texture space
    const float TexW = FMath::Max(Config.GetFloat(HUDConfigKeys::TextureWidth, 1080.f), 1.f);
    const float RadiusPx = Config.GetFloat(TEXT("RadarRadius"), 540.f); // original texture px
    RadarRadiusPx = (RadiusPx / TexW) * CanvasSize.X;

    bGeometryCached = true;

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleLayout : false))
        UE_LOG(LogTemp, Log,
            TEXT("[RadarModule] CacheRadarGeometry: center=(%.0f,%.0f) radius=%.1f px  TextureW=%.0f  RadiusPx_authored=%.0f"),
            RadarCenter.X, RadarCenter.Y, RadarRadiusPx,
            Config.GetFloat(HUDConfigKeys::TextureWidth, 1080.f),
            Config.GetFloat(TEXT("RadarRadius"), 540.f));
}

// ---------------------------------------------------------------------------
//  TickPulse
// ---------------------------------------------------------------------------

void URadarModule::TickPulse(float DeltaTime)
{
    if (!bPulseActive) return;
    if (!BackgroundMID)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[RadarModule] TickPulse: bPulseActive=true but BackgroundMID=null. "
                "Pulse was triggered but material is missing -- see CreateBackgroundMID logs."));
        bPulseActive = false;
        return;
    }

    const float PulseDuration = Config.GetFloat(TEXT("PulseDuration"), 0.5f);
    PulseProgress += DeltaTime / FMath::Max(PulseDuration, 0.01f);

    if (PulseProgress >= 1.f)
    {
        PulseProgress = 1.f;
        bPulseActive = false;

        // Clear pulse parameters so the ring fully disappears
        BackgroundMID->SetScalarParameterValue(TEXT("PulseProgress"), 1.f);
        BackgroundMID->SetScalarParameterValue(TEXT("PulseOpacity"), 0.f);
        return;
    }

    const float PulseOpacity = 1.f - PulseProgress;   // fades as it expands

    BackgroundMID->SetScalarParameterValue(TEXT("PulseProgress"), PulseProgress);
    BackgroundMID->SetScalarParameterValue(TEXT("PulseOpacity"), PulseOpacity);
}

// ---------------------------------------------------------------------------
//  CreateBackgroundMID
// ---------------------------------------------------------------------------

void URadarModule::CreateBackgroundMID()
{
    if (!BackgroundImage)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RadarModule] CreateBackgroundMID: BackgroundImage is NULL -- add it to BP_RadarModule"));
        return;
    }

    UMaterialInterface* Base = Config.GetMaterial(TEXT("MatRadarBackground"));
    if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
        UE_LOG(LogTemp, Log,
        TEXT("[RadarModule] CreateBackgroundMID: MatRadarBackground=%s"),
        Base ? *Base->GetName() : TEXT("NULL -- assign MatRadarBackground in HUD DA Radar module config"));

    if (!Base)
    {
        // Fall back to the existing brush material on BackgroundImage
        Base = Cast<UMaterialInterface>(
            BackgroundImage->GetBrush().GetResourceObject());
        UE_LOG(LogTemp, Log,
            TEXT("[RadarModule] CreateBackgroundMID: fallback brush material=%s"),
            Base ? *Base->GetName() : TEXT("NULL -- BackgroundImage has no material brush either"));
    }
    if (!Base)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[RadarModule] CreateBackgroundMID FAILED: no material found. "
                "Pulse effect will not appear. "
                "Set MatRadarBackground key in DA Radar module Materials map."));
        return;
    }

    BackgroundMID = UMaterialInstanceDynamic::Create(Base, this);
    if (BackgroundMID)
    {
        BackgroundImage->SetBrushFromMaterial(BackgroundMID);
        if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
            UE_LOG(LogTemp, Log,
            TEXT("[RadarModule] CreateBackgroundMID: MID created OK from '%s'"),
            *Base->GetName());

        // Set MainTexture parameter so the background radar texture still shows
        // after we replace the widget brush with this MID.
        // Without this the background goes black when the pulse fires.
        if (UTexture2D* BGTex = Config.GetTexture(HUDConfigKeys::Background))
        {
            BackgroundMID->SetTextureParameterValue(TEXT("MainTexture"), BGTex);
            if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
                UE_LOG(LogTemp, Log,
                TEXT("[RadarModule] CreateBackgroundMID: MainTexture set to '%s'"),
                *BGTex->GetName());
        }
        else
            UE_LOG(LogTemp, Warning,
                TEXT("[RadarModule] CreateBackgroundMID: no Background texture in config -- "
                    "MainTexture parameter not set, background will be black"));

        // Set pulse shape parameters to sensible defaults if not overridden in DA
        const float PulseWidth = Config.GetFloat(TEXT("PulseWidth"), 0.15f);
        const float PulseGlow = Config.GetFloat(TEXT("PulseGlowIntensity"), 2.f);
        BackgroundMID->SetScalarParameterValue(TEXT("PulseWidth"), PulseWidth);
        BackgroundMID->SetScalarParameterValue(TEXT("PulseGlowIntensity"), PulseGlow);
        // Set pulse colour (white = neutral, tints with PulseOpacity)
        const FLinearColor PulseColour = Config.GetColor(TEXT("PulseColour"), FLinearColor::White);
        BackgroundMID->SetVectorParameterValue(TEXT("PulseColour"), PulseColour);
        // Verify vector parameter exists in the material
        FLinearColor ReadBack;
        const bool bHasPulseColour = BackgroundMID->GetVectorParameterValue(
            FHashedMaterialParameterInfo(TEXT("PulseColour")), ReadBack);
        if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
            UE_LOG(LogTemp, Log,
            TEXT("[RadarModule] PulseColour param: exists=%s  set=(%.2f,%.2f,%.2f)"),
            bHasPulseColour ? TEXT("YES") : TEXT("NO -- name mismatch in material"),
            PulseColour.R, PulseColour.G, PulseColour.B);
        // Initialise pulse as invisible (progress=0 means ring at center, opacity=0 = hidden)
        BackgroundMID->SetScalarParameterValue(TEXT("PulseProgress"), 0.f);
        BackgroundMID->SetScalarParameterValue(TEXT("PulseOpacity"), 0.f);

        // Verify parameters
        float TestVal = 0.f;
        const bool bHasPulseProgress = BackgroundMID->GetScalarParameterValue(TEXT("PulseProgress"), TestVal);
        const bool bHasPulseOpacity = BackgroundMID->GetScalarParameterValue(TEXT("PulseOpacity"), TestVal);
        if (ShouldLog(DebugSettings ? DebugSettings->bLogRadar : false))
            UE_LOG(LogTemp, Log,
            TEXT("[RadarModule] Material params: PulseProgress=%s  PulseOpacity=%s  "
                "PulseWidth=%.2f  PulseGlow=%.2f"),
            bHasPulseProgress ? TEXT("OK") : TEXT("MISSING"),
            bHasPulseOpacity ? TEXT("OK") : TEXT("MISSING"),
            PulseWidth, PulseGlow);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[RadarModule] CreateBackgroundMID: UMaterialInstanceDynamic::Create returned null"));
    }
}

// ---------------------------------------------------------------------------
//  UpdateEntityIcons
// ---------------------------------------------------------------------------
void URadarModule::UpdateEntityIcons(
    const TArray<FDetectedEntry>& Entries,
    float SubYaw)
{
    if (!EntitiesCanvas || !IsValid(DataSource.GetObject())) return;

    // --- Remove icons for entries that no longer exist ---
    TArray<FGuid> ToRemove;
    for (auto& Pair : IconPool)
    {
        bool bFound = false;
        for (const FDetectedEntry& E : Entries)
            if (E.ActorGuid == Pair.Key) { bFound = true; break; }

        if (!bFound)
        {
            if (IsValid(Pair.Value))
                EntitiesCanvas->RemoveChild(Pair.Value);
            ToRemove.Add(Pair.Key);
        }
    }
    for (const FGuid& G : ToRemove) IconPool.Remove(G);

    // Get submarine world position for projection
    // We need the raw world position. Since ITrackableSubmarine doesn't expose
    // GetActorLocation directly, we cast through UObject.
    AActor* OwnerActor = Cast<AActor>(DataSource.GetObject());
    if (!OwnerActor) return;
    const FVector SubPos = OwnerActor->GetActorLocation();

    // --- Update / create icons for current entries ---
    for (const FDetectedEntry& Entry : Entries)
    {
        // Skip entries with no display time remaining (not yet scanned)
        if (Entry.DisplayTimeRemaining <= 0.f) continue;

        // Project world position to radar space
        FVector2D RadarPos;
        const bool bVisible = ProjectToRadar(Entry.WorldPosition, SubPos, SubYaw, RadarPos);

        // Find or create icon
        TObjectPtr<UImage>* ExistingPtr = IconPool.Find(Entry.ActorGuid);
        UImage* Icon = ExistingPtr ? ExistingPtr->Get() : nullptr;

        if (!Icon)
        {
            // Create new icon widget
            Icon = NewObject<UImage>(this,
                *FString::Printf(TEXT("RadarIcon_%s"),
                    *Entry.ActorGuid.ToString()));
            if (!Icon) continue;

            EntitiesCanvas->AddChildToCanvas(Icon);
            IconPool.Add(Entry.ActorGuid, Icon);

            // Set pivot to center so rotation is around the icon center
            if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Icon->Slot))
                S->SetAlignment(FVector2D(0.5f, 0.5f));
        }

        // Hide if outside radar circle threshold
        Icon->SetVisibility(bVisible
            ? ESlateVisibility::HitTestInvisible
            : ESlateVisibility::Hidden);

        if (!bVisible) continue;

        // --- Position ---
        if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Icon->Slot))
        {
            // Icon size: configurable, default 24x24 screen px scaled from config
            const float TexW = FMath::Max(Config.GetFloat(HUDConfigKeys::TextureWidth, 1080.f), 1.f);
            const float IconSzPx = Config.GetFloat(TEXT("IconSize"), 32.f);
            const float IconSz = (IconSzPx / TexW) * (RadarRadiusPx * 2.f);

            S->SetPosition(RadarPos);
            S->SetSize(FVector2D(IconSz, IconSz));
            S->SetAutoSize(false);
            S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
        }

        // --- Texture ---
        if (UTexture2D* Tex = SelectIconTexture(Entry))
            Icon->SetBrushFromTexture(Tex, false);

        // --- Rotation ---
        switch (Entry.EntityType)
        {
        case ERadarEntityType::Torpedo:
            Icon->SetRenderTransformAngle(Entry.IconRotation);
            break;
        default:
            // Submarines and unknowns: no rotation
            Icon->SetRenderTransformAngle(0.f);
            break;
        }

        // --- Opacity (fade) ---
        Icon->SetRenderOpacity(ComputeIconOpacity(Entry));
    }
}

// ---------------------------------------------------------------------------
//  ProjectToRadar
// ---------------------------------------------------------------------------
bool URadarModule::ProjectToRadar(
    const FVector& WorldPos,
    const FVector& SubPos,
    float          SubYaw,
    FVector2D& OutPos) const
{
    const URadarSettings* RS = GetDefault<URadarSettings>();
    const float WorldRange = Config.GetFloat(TEXT("RadarWorldRange"),
        RS ? RS->RadarWorldRange : 10000.f);
    const float HideThresh = Config.GetFloat(TEXT("EntityHideThreshold"),
        RS ? RS->EntityHideThreshold : 0.9f);

    // World-space relative vector (horizontal plane only)
    const FVector Relative = WorldPos - SubPos;

    // Rotate by -SubmarineYaw so submarine forward -> "up" on radar
    const FVector Rotated = Relative.RotateAngleAxis(-SubYaw, FVector::UpVector);

    // Map to 2D radar space:
    //   X -> up on radar
    //   Y -> right on radar
    // NorthDirection (from DA) lets designers remap which world axis is "up" on the radar.
    // Default FVector2D(1,0) = world +X is north (UE default forward).
    const URadarSettings* _RS = GetDefault<URadarSettings>();
    FVector2D North = (_RS ? _RS->NorthDirection : FVector2D(1.f, 0.f)).GetSafeNormal();
    if (North.IsNearlyZero()) North = FVector2D(1.f, 0.f);
    // Project relative vector onto North (up) and Right (perpendicular)
    const FVector2D RelXY = FVector2D(Rotated.X, Rotated.Y);
    const FVector2D NorthPerp = FVector2D(-North.Y, North.X); // 90deg CW = right
    const float UpComponent = FVector2D::DotProduct(RelXY, North);      // positive = ahead
    const float RightComponent = FVector2D::DotProduct(RelXY, NorthPerp);  // positive = right
    const FVector2D RadarDir = FVector2D(RightComponent, -UpComponent);    // UMG: +X right, +Y down

    // Normalize to radar circle
    const float NormalizedDist = RadarDir.Size() / FMath::Max(WorldRange, 1.f);
    const FVector2D NormalizedDir = (NormalizedDist > 0.f)
        ? RadarDir / (WorldRange)
        : FVector2D::ZeroVector;

    // Convert to screen pixels
    OutPos = RadarCenter + NormalizedDir * RadarRadiusPx;

    return NormalizedDist <= HideThresh;
}

// ---------------------------------------------------------------------------
//  SelectIconTexture
// ---------------------------------------------------------------------------
UTexture2D* URadarModule::SelectIconTexture(const FDetectedEntry& Entry) const
{
    UTexture2D* Tex = nullptr;
    FName Key = NAME_None;

    switch (Entry.EntityType)
    {
    case ERadarEntityType::Submarine:
        Key = TEXT("IconSubmarine");  break;
    case ERadarEntityType::Torpedo:
        Key = TEXT("IconTorpedo");    break;
    default:
        Key = TEXT("IconUnknown");    break;
    }
    Tex = Config.GetTexture(Key);

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogRadar) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[RadarModule] SelectIconTexture: Type=%d  Key='%s'  Result=%s"),
            (int32)Entry.EntityType, *Key.ToString(),
            Tex ? *Tex->GetName() : TEXT("NULL -- missing in DA Textures"));

    return Tex;
}

// ---------------------------------------------------------------------------
//  ComputeIconOpacity
// ---------------------------------------------------------------------------
float URadarModule::ComputeIconOpacity(const FDetectedEntry& Entry) const
{
    const URadarSettings* RS = GetDefault<URadarSettings>();
    const float FadeDur = RS ? RS->FadeDuration : 1.f;

    if (FadeDur <= 0.f) return 1.f;
    return FMath::Clamp(Entry.DisplayTimeRemaining / FadeDur, 0.f, 1.f);
}