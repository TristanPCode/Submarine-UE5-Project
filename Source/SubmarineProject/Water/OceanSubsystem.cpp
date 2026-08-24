#include "OceanSubsystem.h"
#include "WaterRegionActor.h"
#include "WaterRegionDataAsset.h"
#include "OceanHeightFieldGenerator.h"
#include "OceanSurfaceActor.h"
#include "EngineUtils.h"
#include "OceanDebugSettings.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Components/PostProcessComponent.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"

// Console toggle for the surface validation reconstruction overlay.
//   Ocean.DebugSurface 1  -> on-screen residual breakdown + world overlay
//   Ocean.DebugSurface 0  -> off
static TAutoConsoleVariable<int32> CVarOceanDebugSurface(
    TEXT("Ocean.DebugSurface"),
    0,
    TEXT("Show ocean surface validation reconstruction (physics vs visual height)."),
    ECVF_Default);

// Max octave to include in the visual reconstruction.
//   Ocean.DebugMaxOctave 0  -> primary only (matches physics)
//   Ocean.DebugMaxOctave 3  -> all four (full visual)
static TAutoConsoleVariable<int32> CVarOceanDebugMaxOctave(
    TEXT("Ocean.DebugMaxOctave"),
    3,
    TEXT("Highest Gerstner octave included in the CPU visual reconstruction."),
    ECVF_Default);

// ---------------------------------------------------------------------------
//  MPC parameter name constants.
//  Defined once here so a typo in one place does not silently break a write.
// ---------------------------------------------------------------------------

static const FName MPC_WaterHeightP1(TEXT("Ocean_WaterHeightP1"));
static const FName MPC_WaterHeightP2(TEXT("Ocean_WaterHeightP2"));

static const FName MPC_WaterHeightGlobal(TEXT("Ocean_WaterHeightGlobal"));
static const FName MPC_AgitationIntensity(TEXT("Ocean_AgitationIntensity"));
static const FName MPC_Turbidity(TEXT("Ocean_Turbidity"));
static const FName MPC_WaveAmplitude(TEXT("Ocean_WaveAmplitude"));
static const FName MPC_WaveSpeed(TEXT("Ocean_WaveSpeed"));
static const FName MPC_WaveDirection(TEXT("Ocean_WaveDirection"));
static const FName MPC_WaveTilingScale(TEXT("Ocean_WaveTilingScale"));
static const FName MPC_WaveLengthScaleGlobal(TEXT("Ocean_WaveLengthScale"));
static const FName MPC_PrimaryWaveK(TEXT("Ocean_PrimaryWaveK"));
static const FName MPC_WaveK_Oct1(TEXT("Ocean_WaveK_Oct1"));
static const FName MPC_WaveK_Oct2(TEXT("Ocean_WaveK_Oct2"));
static const FName MPC_WaveK_Oct3(TEXT("Ocean_WaveK_Oct3"));
static const FName MPC_WaveK_Oct4(TEXT("Ocean_WaveK_Oct4"));
static const FName MPC_FoamIntensity(TEXT("Ocean_FoamIntensity"));

static const FName MPC_UnderwaterColor(TEXT("Ocean_UnderwaterColor"));
static const FName MPC_WaterShallowColor(TEXT("Ocean_WaterShallowColor"));
static const FName MPC_WaterDeepColor(TEXT("Ocean_WaterDeepColor"));
static const FName MPC_RefractionStrength(TEXT("Ocean_RefractionStrength"));
static const FName MPC_AbsorptionScale(TEXT("Ocean_AbsorptionScale"));

static const FName MPC_IsUnderwaterP1(TEXT("Ocean_IsUnderwater_P1"));
static const FName MPC_IsUnderwaterP2(TEXT("Ocean_IsUnderwater_P2"));

static const FName MPC_WaveAmplitudeP1(TEXT("Ocean_WaveAmplitude_P1"));
static const FName MPC_WaveAmplitudeP2(TEXT("Ocean_WaveAmplitude_P2"));
static const FName MPC_WaveSpeedP1(TEXT("Ocean_WaveSpeed_P1"));
static const FName MPC_WaveSpeedP2(TEXT("Ocean_WaveSpeed_P2"));
static const FName MPC_WaveDirectionP1(TEXT("Ocean_WaveDirection_P1"));
static const FName MPC_WaveDirectionP2(TEXT("Ocean_WaveDirection_P2"));
static const FName MPC_WaveTilingP1(TEXT("Ocean_WaveTilingScale_P1"));
static const FName MPC_WaveTilingP2(TEXT("Ocean_WaveTilingScale_P2"));
static const FName MPC_WaveLengthScaleP1(TEXT("Ocean_WaveLengthScale_P1"));
static const FName MPC_WaveLengthScaleP2(TEXT("Ocean_WaveLengthScale_P2"));
static const FName MPC_UnderwaterColorP1(TEXT("Ocean_UnderwaterColor_P1"));
static const FName MPC_UnderwaterColorP2(TEXT("Ocean_UnderwaterColor_P2"));
static const FName MPC_WaterShallowColorP1(TEXT("Ocean_WaterShallowColor_P1"));
static const FName MPC_WaterShallowColorP2(TEXT("Ocean_WaterShallowColor_P2"));
static const FName MPC_WaterDeepColorP1(TEXT("Ocean_WaterDeepColor_P1"));
static const FName MPC_WaterDeepColorP2(TEXT("Ocean_WaterDeepColor_P2"));
static const FName MPC_AbsorptionScaleP1(TEXT("Ocean_AbsorptionScale_P1"));
static const FName MPC_AbsorptionScaleP2(TEXT("Ocean_AbsorptionScale_P2"));

static const FName MPC_FoamIntensityP1(TEXT("Ocean_FoamIntensity_P1"));
static const FName MPC_FoamIntensityP2(TEXT("Ocean_FoamIntensity_P2"));
static const FName MPC_RefractionStrengthP1(TEXT("Ocean_RefractionStrength_P1"));
static const FName MPC_RefractionStrengthP2(TEXT("Ocean_RefractionStrength_P2"));

// ---------------------------------------------------------------------------
//  Initialize
// ---------------------------------------------------------------------------
void UOceanSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Bind to the world's post-actor-tick so we update before TG_PrePhysics
    // actors tick. We need a world pointer for this, which is not available
    // at Initialize time - bind lazily on the first UpdateOceanFrame call.
    // The actual bind happens in UpdateOceanFrame() when CachedWorld is set.

    // Validate configuration and warn early if anything is missing.
    if (!DefaultRegionData)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanSubsystem] DefaultRegionData is not assigned. ")
            TEXT("Assign it on the GameInstance Blueprint. ")
            TEXT("A hardcoded fallback will be used until then."));
    }

    if (!OceanMPC)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanSubsystem] OceanMPC is not assigned. ")
            TEXT("MPC_Ocean parameters will not be written until it is set."));
    }

    const bool bDebugInit = DebugSettings && DebugSettings->bEnableOceanDebug
        && DebugSettings->bLogInitialization;
    if (bDebugInit)
    {
        UE_LOG(LogTemp, Log, TEXT("[OceanSubsystem] Initialized."));
    }
}

// ---------------------------------------------------------------------------
//  Deinitialize
// ---------------------------------------------------------------------------
void UOceanSubsystem::Deinitialize()
{
    // Unbind world tick if we bound it.
    if (WorldTickHandle.IsValid() && CachedWorld.IsValid())
    {
        FWorldDelegates::OnWorldPreActorTick.Remove(WorldTickHandle);
        WorldTickHandle.Reset();
    }

    RegisteredRegions.Empty();
    FrameCache = FOceanFrameCache();

    Super::Deinitialize();
}

// ---------------------------------------------------------------------------
//  RegisterRegion
// ---------------------------------------------------------------------------
void UOceanSubsystem::RegisterRegion(AWaterRegionActor* Region)
{
    if (!Region || !Region->RegionData) return;
    if (RegisteredRegions.Contains(Region)) return;

    RegisteredRegions.Add(Region);

    // Keep regions sorted by Priority descending so EvaluatePositionInternal
    // naturally encounters higher-priority regions first.
    // Note: TArray::Sort with TObjectPtr elements uses TDereferenceWrapper
    // internally, which dereferences pointers before calling the comparator.
    // The lambda must therefore accept raw references, not TObjectPtr references.
    RegisteredRegions.Sort([](const AWaterRegionActor& A,
        const AWaterRegionActor& B)
        {
            const int32 PrioA = A.RegionData ? A.RegionData->Priority : 0;
            const int32 PrioB = B.RegionData ? B.RegionData->Priority : 0;
            return PrioA > PrioB;
        });

    // Bind the world tick on first registration (world is now fully available).
    if (!WorldTickHandle.IsValid())
    {
        UWorld* World = Region->GetWorld();
        if (World)
        {
            CachedWorld = World;
            WorldTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(
                this, &UOceanSubsystem::UpdateOceanFrame);
        }
    }

    // Notify subscribers (e.g. OceanHeightFieldGenerator) that the
    // region layout has changed and any baked data must be rebuilt.
    OnRegionChanged.Broadcast();

    const bool bDebugInit = DebugSettings && DebugSettings->bEnableOceanDebug
        && DebugSettings->bLogInitialization;
    if (bDebugInit)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[OceanSubsystem] Registered region '%s' (Priority=%d). Total regions: %d"),
            *Region->RegionData->RegionName.ToString(),
            Region->RegionData->Priority,
            RegisteredRegions.Num());
    }
}

// ---------------------------------------------------------------------------
//  UnregisterRegion
// ---------------------------------------------------------------------------
void UOceanSubsystem::UnregisterRegion(AWaterRegionActor* Region)
{
    if (!Region) return;
    RegisteredRegions.Remove(Region);
    OnRegionChanged.Broadcast();

    const bool bDebugInit = DebugSettings && DebugSettings->bEnableOceanDebug
        && DebugSettings->bLogInitialization;
    if (bDebugInit)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[OceanSubsystem] Unregistered region '%s'. Total regions: %d"),
            Region->RegionData ? *Region->RegionData->RegionName.ToString() : TEXT("(null)"),
            RegisteredRegions.Num());
    }
}

// ---------------------------------------------------------------------------
//  RegisterSampleActor
//  Registers any actor (CPU submarine, torpedo spawner, etc.) for per-frame
//  cache pre-warming. Prevents duplicate registration.
// ---------------------------------------------------------------------------
void UOceanSubsystem::RegisterSampleActor(AActor* Actor, FName DebugLabel,
    bool bIsLocalPlayer, int32 PlayerIndex)
{
    if (!Actor) return;

    // Prevent duplicates.
    for (const FOceanSampleRequest& Req : RegisteredSampleActors)
    {
        if (Req.Actor == Actor) return;
    }

    FOceanSampleRequest Request;
    Request.Actor = Actor;
    Request.DebugLabel = DebugLabel;
    Request.bIsLocalPlayer = bIsLocalPlayer;
    Request.PlayerIndex = PlayerIndex;
    RegisteredSampleActors.Add(Request);

    const bool bDebugInit = DebugSettings && DebugSettings->bEnableOceanDebug
        && DebugSettings->bLogInitialization;
    if (bDebugInit)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[OceanSubsystem] Registered sample actor '%s' (Label=%s, Player=%s[%d])"),
            *Actor->GetName(),
            *DebugLabel.ToString(),
            bIsLocalPlayer ? TEXT("YES") : TEXT("NO"),
            PlayerIndex);
    }
}

// ---------------------------------------------------------------------------
//  UnregisterSampleActor
// ---------------------------------------------------------------------------
void UOceanSubsystem::UnregisterSampleActor(AActor* Actor)
{
    if (!Actor) return;
    RegisteredSampleActors.RemoveAll([Actor](const FOceanSampleRequest& Req)
        {
            return Req.Actor == Actor;
        });
}

// ---------------------------------------------------------------------------
//  UpdateOceanFrame
//  Called once per frame via OnWorldPreActorTick, before TG_PrePhysics.
//  Invalidates the cache, re-evaluates for all local player positions,
//  then writes MPC parameters.
// ---------------------------------------------------------------------------
void UOceanSubsystem::UpdateOceanFrame(UWorld* World, ELevelTick TickType, float DeltaTime)
{

    // Guard: only process ticks for our cached world.
    // FWorldDelegates fires for every world (including editor preview worlds),
    // so we must filter to the world our regions actually belong to.
    if (!World) return;
    if (World != CachedWorld.Get()) return;

    // Invalidate last frame's cache.
    FrameCache = FOceanFrameCache();

    // ---------------------------------------------------------------------------
    //  Pre-warm registered sample actors.
    //  Player actors fill dedicated PlayerSamples slots for per-player MPC writes.
    //  All other actors fill the general ActorSamples map for query cache hits.
    // ---------------------------------------------------------------------------
    int32 ActivePlayerCount = 0;

    for (const FOceanSampleRequest& Request : RegisteredSampleActors)
    {
        AActor* Actor = Request.Actor.Get();
        if (!Actor) continue; // actor destroyed, skip silently

        const FVector ActorPos = Actor->GetActorLocation();
        FWaterSample Sample = EvaluatePositionInternal(ActorPos);

        if (Request.bIsLocalPlayer && Request.PlayerIndex >= 0 && Request.PlayerIndex < 2)
        {
            FrameCache.PlayerSamples[Request.PlayerIndex] = Sample;
            ActivePlayerCount = FMath::Max(ActivePlayerCount, Request.PlayerIndex + 1);
        }

        // Store in the general map for SampleWaterAt cache hit checks.
        FrameCache.ActorSamples.Add(Actor, Sample);
    }

    // ---------------------------------------------------------------------------
    //  Populate global rendering values from player 0, or fallback.
    // ---------------------------------------------------------------------------
    const FWaterSample& GlobalSource = (ActivePlayerCount > 0)
        ? FrameCache.PlayerSamples[0]
        : GetDefaultSample();

    FrameCache.GlobalWaterHeight = GlobalSource.WaterHeight;
    FrameCache.GlobalAgitation = GlobalSource.AgitationIntensity;
    FrameCache.GlobalTurbidity = GlobalSource.Turbidity;
    FrameCache.GlobalWaveAmplitude = GlobalSource.WaveAmplitude;
    FrameCache.GlobalWaveSpeed = GlobalSource.WaveSpeed;
    FrameCache.GlobalWaveDirection = GlobalSource.WaveDirection;
    FrameCache.GlobalWaveTilingScale = GlobalSource.WaveTilingScale;
    FrameCache.GlobalWaveLengthScale = GlobalSource.WaveLengthScale;

    FrameCache.GlobalWaterShallowColor = GlobalSource.WaterShallowColor;
    FrameCache.GlobalWaterDeepColor = GlobalSource.WaterDeepColor;
    FrameCache.GlobalUnderwaterColor = GlobalSource.UnderwaterColor;
    FrameCache.GlobalAbsorptionScale = GlobalSource.AbsorptionScale;
    FrameCache.GlobalFoamIntensity = GlobalSource.FoamIntensity;
    FrameCache.GlobalRefractionStrength = GlobalSource.RefractionStrength;

    FrameCache.bValid = true;

    WriteMPCParameters();
    // Transition slots are DISABLED: validation showed the 1024 height field
    // is accurate to ~2 UU everywhere including transitions, so slot correction
    // is unnecessary. The slot machinery also produced camera-dependent garbage.
    // Left compiled but uncalled; can be deleted entirely in a later cleanup.
    //UpdateTransitionSlots(DeltaTime);
    //WriteTransitionSlotsMPC();

    // Validation: reconstruct visual height on CPU and compare to physics.
    if (CVarOceanDebugSurface.GetValueOnGameThread() != 0)
    {
        DebugReconstructSurface();
    }

    // ---------------------------------------------------------------------------
    //  Debug
    // ---------------------------------------------------------------------------
    if (DebugSettings && DebugSettings->bEnableOceanDebug)
    {
        DebugLogTimer += DeltaTime;

        if (DebugSettings->bLogFrameCache && DebugLogTimer >= DebugSettings->LogFrequency)
        {
            DebugLogTimer = 0.f;
            UE_LOG(LogTemp, Log, TEXT("========= [OceanSubsystem FrameCache] ========="));
            UE_LOG(LogTemp, Log, TEXT("  GlobalWaterHeight  : %.2f"), FrameCache.GlobalWaterHeight);
            UE_LOG(LogTemp, Log, TEXT("  GlobalAgitation    : %.2f"), FrameCache.GlobalAgitation);
            UE_LOG(LogTemp, Log, TEXT("  GlobalTurbidity    : %.2f"), FrameCache.GlobalTurbidity);
            UE_LOG(LogTemp, Log, TEXT("  GlobalWaveAmp      : %.2f"), FrameCache.GlobalWaveAmplitude);
            UE_LOG(LogTemp, Log, TEXT("  GlobalWaveSpeed    : %.2f"), FrameCache.GlobalWaveSpeed);
            UE_LOG(LogTemp, Log, TEXT("  GlobalWaveDir      : (%.3f, %.3f)"),
                FrameCache.GlobalWaveDirection.X, FrameCache.GlobalWaveDirection.Y);
            UE_LOG(LogTemp, Log, TEXT("  GlobalWaveTiling   : %.0f"), FrameCache.GlobalWaveTilingScale);
            UE_LOG(LogTemp, Log, TEXT("  GlobalAbsorbScale  : %.0f"), FrameCache.GlobalAbsorptionScale);
            UE_LOG(LogTemp, Log, TEXT("  GlobalFoam         : %.2f"), FrameCache.GlobalFoamIntensity);
            UE_LOG(LogTemp, Log, TEXT("  RegisteredActors   : %d"), RegisteredSampleActors.Num());

            for (int32 i = 0; i < FMath::Min(ActivePlayerCount, 2); ++i)
            {
                const FWaterSample& S = FrameCache.PlayerSamples[i];
                UE_LOG(LogTemp, Log,
                    TEXT("  Player[%d] Height=%.2f Agitation=%.2f Turbidity=%.2f Underwater=%s WaveAmp=%.2f"),
                    i, S.WaterHeight, S.AgitationIntensity, S.Turbidity,
                    S.bIsUnderwater ? TEXT("YES") : TEXT("NO"),
                    S.WaveAmplitude);
            }
            UE_LOG(LogTemp, Log, TEXT("==============================================="));
        }

        // Wave amplitude zero warning - helps diagnose missing waves
        if (DebugSettings->bLogMPCWrites && FrameCache.GlobalWaveAmplitude <= 0.f)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[OceanSubsystem] GlobalWaveAmplitude is 0 or negative (%.2f). ")
                TEXT("Check Ocean_WaveAmplitude in MPC_Ocean and WaveAmplitude in your region DAs."),
                FrameCache.GlobalWaveAmplitude);
        }

        DrawDebugVisualization();
    }
}

// ---------------------------------------------------------------------------
//  GetWaterHeightAtPosition
// ---------------------------------------------------------------------------
float UOceanSubsystem::GetWaterHeightAtPosition(const FVector& WorldPos) const
{
    return SampleWaterAt(WorldPos).WaterHeight;
}

// ---------------------------------------------------------------------------
//  GetAgitationAtPosition
// ---------------------------------------------------------------------------
float UOceanSubsystem::GetAgitationAtPosition(const FVector& WorldPos) const
{
    return SampleWaterAt(WorldPos).AgitationIntensity;
}

// ---------------------------------------------------------------------------
//  GetRegionalDragMultiplierAt
// ---------------------------------------------------------------------------
float UOceanSubsystem::GetRegionalDragMultiplierAt(const FVector& WorldPos,
    bool bTorpedoQuery) const
{
    // Walk all registered regions and compute a weighted blend of
    // RegionalDragMultiplier, exactly like GetAgitationAtPosition does
    // for AgitationIntensity.
    float TotalWeight = 0.f;
    float BlendedMultiplier = 0.f;

    for (const AWaterRegionActor* Region : RegisteredRegions)
    {
        if (!Region || !Region->RegionData) continue;

        // For torpedo queries, skip regions that don't affect torpedoes.
        if (bTorpedoQuery && !Region->RegionData->bAffectTorpedoes) continue;

        const float Weight = Region->EvaluateWeight(WorldPos);
        if (Weight <= 0.f) continue;

        TotalWeight += Weight;
        BlendedMultiplier += Region->RegionData->RegionalDragMultiplier * Weight;
    }

    if (TotalWeight <= SMALL_NUMBER)
    {
        // No region covers this position -- use default region if assigned.
        if (DefaultRegionData)
            return DefaultRegionData->RegionalDragMultiplier;
        return 1.f;  // safe fallback: no drag change
    }

    // Normalize weighted sum.
    return BlendedMultiplier / TotalWeight;
}

// ---------------------------------------------------------------------------
//  IsPositionUnderwater
// ---------------------------------------------------------------------------
bool UOceanSubsystem::IsPositionUnderwater(const FVector& WorldPos) const
{
    return WorldPos.Z < GetWaterHeightAtPosition(WorldPos);
}

// ---------------------------------------------------------------------------
//  SampleWaterAt
//  The main query entry point. Returns a full FWaterSample.
//  Checks the frame cache first - evaluates regions only on a cache miss.
// ---------------------------------------------------------------------------
FWaterSample UOceanSubsystem::SampleWaterAt(const FVector& WorldPos) const
{
    // Full evaluation: blend all registered regions.
    // The actor cache is pre-warmed in UpdateOceanFrame; physics components
    // calling GetWaterSurfaceZ() arrive here and get a direct evaluation since
    // we do not store world positions in the cache (keeping FOceanFrameCache
    // lightweight). The evaluation cost is O(N regions) which is small.
    // A position-keyed cache is deferred to Phase 6.8 if profiling shows need.
    FWaterSample Result = EvaluatePositionInternal(WorldPos);

    if (DebugSettings && DebugSettings->bEnableOceanDebug
        && DebugSettings->bLogWaterQueries)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[OceanSubsystem] SampleWaterAt (%.0f,%.0f,%.0f) => Height=%.2f ")
            TEXT("Agitation=%.2f WaveAmp=%.2f AbsorbScale=%.0f"),
            WorldPos.X, WorldPos.Y, WorldPos.Z,
            Result.WaterHeight, Result.AgitationIntensity,
            Result.WaveAmplitude, Result.AbsorptionScale);
    }

    return Result;
}

// ---------------------------------------------------------------------------
//  EvaluateGerstnerOctave
//  THE canonical single-octave Gerstner. Both physics (via QuerySurface) and
//  the debug reconstruction use this. The material mirrors this exact formula.
//
//    K_i      = OceanGerstner::WaveK[i] * WaveLengthScale
//    A_i      = BaseAmplitude * OceanGerstner::AmpFraction[i]
//    Dir_i    = BaseDirection rotated by OceanGerstner::RotAngleDeg[i]
//    Omega_i  = BaseSpeed * OceanGerstner::SpeedFraction[i]
//    Phase_i  = K_i * dot(Dir_i, XY) - Omega_i * Time
//    Z_i      = A_i * sin(Phase_i)
// ---------------------------------------------------------------------------
float UOceanSubsystem::EvaluateGerstnerOctave(int32 OctaveIndex,
    const FVector2D& XY, float Time, float BaseAmplitude, float BaseSpeed,
    const FVector2D& BaseDirection, float WaveLengthScale) const
{
    if (OctaveIndex < 0 || OctaveIndex >= OceanGerstner::NumOctaves) return 0.f;
    if (BaseAmplitude <= 0.f) return 0.f;

    const float K = OceanGerstner::WaveK[OctaveIndex] * FMath::Max(WaveLengthScale, 0.0001f);
    const float Amp = BaseAmplitude * OceanGerstner::AmpFraction[OctaveIndex];
    const float Omega = BaseSpeed * OceanGerstner::SpeedFraction[OctaveIndex];

    // Rotate base direction by the per-octave angle.
    const FVector2D BaseDir = BaseDirection.GetSafeNormal();
    const float RotRad = FMath::DegreesToRadians(OceanGerstner::RotAngleDeg[OctaveIndex]);
    const float CosR = FMath::Cos(RotRad);
    const float SinR = FMath::Sin(RotRad);
    const FVector2D Dir(
        BaseDir.X * CosR - BaseDir.Y * SinR,
        BaseDir.X * SinR + BaseDir.Y * CosR);

    const float Phase = K * (Dir.X * XY.X + Dir.Y * XY.Y) - Omega * Time;
    return Amp * FMath::Sin(Phase);
}

// ---------------------------------------------------------------------------
//  EvaluateGerstnerSum
//  Sums octaves [0..MaxOctave] using the region sample at XY.
// ---------------------------------------------------------------------------
float UOceanSubsystem::EvaluateGerstnerSum(const FVector2D& XY, float Time,
    int32 MaxOctave) const
{
    const FWaterSample Sample = SampleWaterAt(FVector(XY.X, XY.Y, 0.f));
    const int32 Clamped = FMath::Clamp(MaxOctave, 0, OceanGerstner::NumOctaves - 1);

    float Sum = 0.f;
    for (int32 i = 0; i <= Clamped; ++i)
    {
        Sum += EvaluateGerstnerOctave(i, XY, Time,
            Sample.WaveAmplitude, Sample.WaveSpeed,
            Sample.WaveDirection, Sample.WaveLengthScale);
    }
    return Sum;
}

// ---------------------------------------------------------------------------
//  QuerySurface
//  The canonical authoritative surface query. Regional base + primary wave.
//  FULL amplitude (no NearSurfaceAlpha -- that is a force concern, not a
//  surface-geometry concern). Reused by Phase 6.6.
// ---------------------------------------------------------------------------
FOceanSurfaceSample UOceanSubsystem::QuerySurface(const FVector2D& XY, float Time) const
{
    FOceanSurfaceSample Out;

    // Probe at the CANONICAL surface Z (the same Z the height field bakes at),
    // NOT at Z=0 and NOT at the caller's depth. This is what makes physics,
    // the height field, and the rendered mesh agree for 3D-volume regions.
    const float ProbeZ = GetDefaultWaterHeight();
    const FWaterSample Sample = SampleWaterAt(FVector(XY.X, XY.Y, ProbeZ));
    Out.RegionalBaseHeight = Sample.WaterHeight;

    // Primary octave only -- the rideable wave.
    Out.PrimaryWaveOffset = EvaluateGerstnerOctave(OceanGerstner::PrimaryOctave,
        XY, Time, Sample.WaveAmplitude, Sample.WaveSpeed,
        Sample.WaveDirection, Sample.WaveLengthScale);

    Out.SurfaceHeight = Out.RegionalBaseHeight + Out.PrimaryWaveOffset;

    // Analytic normal from the primary wave slope (closed form).
    //   dZ/dX = A * K * Dir.X * cos(Phase)
    //   dZ/dY = A * K * Dir.Y * cos(Phase)
    //   Normal = normalize( (-dZ/dX, -dZ/dY, 1) )
    {
        const int32 i = OceanGerstner::PrimaryOctave;
        const float K = OceanGerstner::WaveK[i] * FMath::Max(Sample.WaveLengthScale, 0.0001f);
        const float Amp = Sample.WaveAmplitude * OceanGerstner::AmpFraction[i];
        const float Omega = Sample.WaveSpeed * OceanGerstner::SpeedFraction[i];
        const FVector2D Dir = Sample.WaveDirection.GetSafeNormal();
        const float Phase = K * (Dir.X * XY.X + Dir.Y * XY.Y) - Omega * Time;
        const float CosP = FMath::Cos(Phase);
        const float dZdX = Amp * K * Dir.X * CosP;
        const float dZdY = Amp * K * Dir.Y * CosP;
        Out.SurfaceNormal = FVector(-dZdX, -dZdY, 1.f).GetSafeNormal();
    }

    return Out;
}

// ---------------------------------------------------------------------------
//  EvaluateSlotCorrectionCPU
//  Mirrors the material's transition-slot loop EXACTLY. This is the canonical
//  slot formula -- the material must match THIS, not the other way around.
//  Returns the corrected height (HeightFieldValue with slot overrides applied).
//
//  Convention (must match material):
//    For each active slot:
//      d = dot(XY - Center, Normal)              signed dist, + = inside region
//      t = smoothstep(-BlendRadius, 0, d)        0 outside .. 1 at/inside boundary
//      SlotHeight = lerp(HeightOuter, HeightInner, t)
//      Corrected  = lerp(Corrected, SlotHeight, Weight)
// ---------------------------------------------------------------------------
float UOceanSubsystem::EvaluateSlotCorrectionCPU(const FVector2D& XY,
    float HeightFieldValue) const
{
    float Corrected = HeightFieldValue;

    for (int32 s = 0; s < MaxTransitionSlots; ++s)
    {
        const FTransitionSlotState& Slot = TransitionSlots[s];
        if (Slot.Weight < 0.001f) continue;

        const float d = FVector2D::DotProduct(XY - Slot.BoundaryPoint, Slot.OutwardNormal);
        const float BR = FMath::Max(Slot.BlendRadius, 1.f);
        const float t = FMath::SmoothStep(-BR, 0.f, d);
        const float SlotHeight = FMath::Lerp(Slot.HeightOuter, Slot.HeightInner, t);
        Corrected = FMath::Lerp(Corrected, SlotHeight, Slot.Weight);
    }

    return Corrected;
}

// ---------------------------------------------------------------------------
//  EvaluatePositionInternal
//  Core blending logic. Iterates registered regions sorted by priority.
//  Accumulates weighted contributions. Falls back to DefaultRegionData.
// ---------------------------------------------------------------------------
FWaterSample UOceanSubsystem::EvaluatePositionInternal(const FVector& WorldPos) const
{
    // The default sample is always the baseline.
    // It is treated as a region with weight = 1.0, so that any registered
    // region blends INTO the default rather than replacing it entirely.
    // This ensures a smooth transition at region boundaries -- as EvaluateWeight
    // fades from 0 to 1, the sample continuously interpolates from default to
    // region instead of snapping.
    FWaterSample Accumulated = GetDefaultSample();
    float TotalWeight = 1.f;  // default always contributes weight 1

    for (const TObjectPtr<AWaterRegionActor>& Region : RegisteredRegions)
    {
        if (!Region || !Region->RegionData) continue;

        const float Weight = Region->EvaluateWeight(WorldPos);
        if (Weight <= 0.f) continue;

        // Build a full sample from this region's data asset.
        FWaterSample RegionSample;
        RegionSample.WaterHeight = Region->RegionData->BaseWaterHeight;
        RegionSample.AgitationIntensity = Region->RegionData->AgitationIntensity;
        RegionSample.Turbidity = Region->RegionData->Turbidity;
        RegionSample.UnderwaterColor = Region->RegionData->UnderwaterColor;
        RegionSample.WaveAmplitude = Region->RegionData->WaveAmplitude;
        RegionSample.WaveSpeed = Region->RegionData->WaveSpeed;
        RegionSample.WaveDirection = Region->RegionData->WaveDirection;
        RegionSample.WaveTilingScale = Region->RegionData->WaveTilingScale;
        RegionSample.WaveLengthScale = Region->RegionData->WaveLengthScale;

        RegionSample.WaterShallowColor = Region->RegionData->WaterShallowColor;
        RegionSample.WaterDeepColor = Region->RegionData->WaterDeepColor;
        RegionSample.AbsorptionScale = Region->RegionData->AbsorptionScale;
        RegionSample.FoamIntensity = Region->RegionData->FoamIntensity;
        RegionSample.RefractionStrength = Region->RegionData->RefractionStrength;

        RegionSample.bIsUnderwater = WorldPos.Z < Region->RegionData->BaseWaterHeight;

        if (TotalWeight <= 0.f)
        {
            // First contributing region: replace the default entirely.
            Accumulated = RegionSample;
            TotalWeight = Weight;
        }
        else
        {
            // Subsequent regions: blend proportionally.
            const float BlendAlpha = Weight / (TotalWeight + Weight);
            Accumulated = BlendSamples(Accumulated, RegionSample, BlendAlpha);
            TotalWeight += Weight;
        }
    }

    // Final underwater flag uses the blended water height.
    Accumulated.bIsUnderwater = WorldPos.Z < Accumulated.WaterHeight;

    return Accumulated;
}

// ---------------------------------------------------------------------------
//  BlendSamples
// ---------------------------------------------------------------------------
FWaterSample UOceanSubsystem::BlendSamples(const FWaterSample& A,
    const FWaterSample& B,
    float Alpha)
{
    auto LerpColor = [Alpha](const FLinearColor& Ca, const FLinearColor& Cb) -> FLinearColor
        {
            return FLinearColor(
                FMath::Lerp(Ca.R, Cb.R, Alpha),
                FMath::Lerp(Ca.G, Cb.G, Alpha),
                FMath::Lerp(Ca.B, Cb.B, Alpha),
                1.f);
        };

    auto LerpV2 = [Alpha](const FVector2D& Va, const FVector2D& Vb) -> FVector2D
        {
            return FVector2D(
                FMath::Lerp(Va.X, Vb.X, Alpha),
                FMath::Lerp(Va.Y, Vb.Y, Alpha));
        };

    FWaterSample Result;
    Result.WaterHeight = FMath::Lerp(A.WaterHeight, B.WaterHeight, Alpha);
    Result.AgitationIntensity = FMath::Lerp(A.AgitationIntensity, B.AgitationIntensity, Alpha);
    Result.Turbidity = FMath::Lerp(A.Turbidity, B.Turbidity, Alpha);
    Result.WaveAmplitude = FMath::Lerp(A.WaveAmplitude, B.WaveAmplitude, Alpha);
    Result.WaveSpeed = FMath::Lerp(A.WaveSpeed, B.WaveSpeed, Alpha);
    Result.WaveDirection = LerpV2(A.WaveDirection, B.WaveDirection);
    Result.WaveTilingScale = FMath::Lerp(A.WaveTilingScale, B.WaveTilingScale, Alpha);
    Result.WaveLengthScale = FMath::Lerp(A.WaveLengthScale, B.WaveLengthScale, Alpha);
    Result.AbsorptionScale = FMath::Lerp(A.AbsorptionScale, B.AbsorptionScale, Alpha);
    Result.FoamIntensity = FMath::Lerp(A.FoamIntensity, B.FoamIntensity, Alpha);

    Result.RefractionStrength = FMath::Lerp(A.RefractionStrength, B.RefractionStrength, Alpha);
    Result.UnderwaterColor = LerpColor(A.UnderwaterColor, B.UnderwaterColor);
    Result.WaterShallowColor = LerpColor(A.WaterShallowColor, B.WaterShallowColor);
    Result.WaterDeepColor = LerpColor(A.WaterDeepColor, B.WaterDeepColor);

    // bIsUnderwater recomputed after blending, never blended directly.
    Result.bIsUnderwater = false;

    return Result;
}

// ---------------------------------------------------------------------------
//  GetDefaultSample
// ---------------------------------------------------------------------------
FWaterSample UOceanSubsystem::GetDefaultSample() const
{
    FWaterSample Sample;
    if (DefaultRegionData)
    {
        Sample.WaterHeight = DefaultRegionData->BaseWaterHeight;
        Sample.AgitationIntensity = DefaultRegionData->AgitationIntensity;
        Sample.Turbidity = DefaultRegionData->Turbidity;
        Sample.UnderwaterColor = DefaultRegionData->UnderwaterColor;
        Sample.WaveAmplitude = DefaultRegionData->WaveAmplitude;
        Sample.WaveSpeed = DefaultRegionData->WaveSpeed;
        Sample.WaveDirection = DefaultRegionData->WaveDirection;
        Sample.WaveTilingScale = DefaultRegionData->WaveTilingScale;
        Sample.WaveLengthScale = DefaultRegionData->WaveLengthScale;

        Sample.WaterShallowColor = DefaultRegionData->WaterShallowColor;
        Sample.WaterDeepColor = DefaultRegionData->WaterDeepColor;
        Sample.AbsorptionScale = DefaultRegionData->AbsorptionScale;
        Sample.FoamIntensity = DefaultRegionData->FoamIntensity;
        Sample.RefractionStrength = DefaultRegionData->RefractionStrength;
    }
    else
    {
        // Safe hardcoded fallback. Logged as warning during Initialize().
        Sample.WaterHeight = 0.f;
        Sample.AgitationIntensity = 0.f;
        Sample.Turbidity = 0.2f;
        Sample.UnderwaterColor = FLinearColor(0.02f, 0.12f, 0.25f, 1.f);
        Sample.WaveAmplitude = 80.f;
        Sample.WaveSpeed = 1.f;
        Sample.WaveDirection = FVector2D(1.f, 0.f);
        Sample.WaveTilingScale = 50000.f;

        Sample.WaterShallowColor = FLinearColor(0.05f, 0.25f, 0.30f, 1.f);
        Sample.WaterDeepColor = FLinearColor(0.01f, 0.05f, 0.15f, 1.f);
        Sample.AbsorptionScale = 50000.f;
        Sample.FoamIntensity = 0.2f;
        Sample.RefractionStrength = 0.5f;
    }
    return Sample;
}

// ---------------------------------------------------------------------------
//  WriteMPCParameters
//  One-way write from CPU authority to the rendering bridge.
//  Nothing in gameplay ever reads from these parameters.
// ---------------------------------------------------------------------------
void UOceanSubsystem::WriteMPCParameters() const
{
    if (!OceanMPC || !CachedWorld.IsValid()) return;

    UMaterialParameterCollectionInstance* MPCI =
        CachedWorld->GetParameterCollectionInstance(OceanMPC);
    if (!MPCI) return;

    // Setting parameters
    MPCI->SetScalarParameterValue(MPC_WaterHeightP1, FrameCache.PlayerSamples[0].WaterHeight);
    MPCI->SetScalarParameterValue(MPC_WaterHeightP2, FrameCache.PlayerSamples[1].WaterHeight);

    MPCI->SetScalarParameterValue(MPC_WaterHeightGlobal, FrameCache.GlobalWaterHeight);
    MPCI->SetScalarParameterValue(MPC_AgitationIntensity, FrameCache.GlobalAgitation);
    MPCI->SetScalarParameterValue(MPC_Turbidity, FrameCache.GlobalTurbidity);
    MPCI->SetScalarParameterValue(MPC_WaveAmplitude, FrameCache.GlobalWaveAmplitude);
    MPCI->SetScalarParameterValue(MPC_WaveSpeed, FrameCache.GlobalWaveSpeed);
    MPCI->SetVectorParameterValue(MPC_WaveDirection, FLinearColor(FrameCache.GlobalWaveDirection.X, FrameCache.GlobalWaveDirection.Y, 0.f, 0.f));
    MPCI->SetScalarParameterValue(MPC_WaveTilingScale, FrameCache.GlobalWaveTilingScale);
    MPCI->SetScalarParameterValue(MPC_WaveLengthScaleGlobal, FrameCache.GlobalWaveLengthScale);

    // Canonical per-octave wave numbers (single source of truth in
    // OceanGerstner::WaveK). The material reads these so its Gerstner uses
    // identical k values to physics. PrimaryWaveK = octave 0.
    MPCI->SetScalarParameterValue(MPC_PrimaryWaveK, OceanGerstner::WaveK[0]);
    MPCI->SetScalarParameterValue(MPC_WaveK_Oct1, OceanGerstner::WaveK[0]);
    MPCI->SetScalarParameterValue(MPC_WaveK_Oct2, OceanGerstner::WaveK[1]);
    MPCI->SetScalarParameterValue(MPC_WaveK_Oct3, OceanGerstner::WaveK[2]);
    MPCI->SetScalarParameterValue(MPC_WaveK_Oct4, OceanGerstner::WaveK[3]);
    MPCI->SetScalarParameterValue(MPC_FoamIntensity, FrameCache.GlobalFoamIntensity);

    MPCI->SetVectorParameterValue(MPC_UnderwaterColor, FrameCache.GlobalUnderwaterColor);
    MPCI->SetVectorParameterValue(MPC_WaterShallowColor, FrameCache.GlobalWaterShallowColor);
    MPCI->SetVectorParameterValue(MPC_WaterDeepColor, FrameCache.GlobalWaterDeepColor);
    const bool bAbsorbWriteOK = MPCI->SetScalarParameterValue(MPC_AbsorptionScale, FrameCache.GlobalAbsorptionScale);
    MPCI->SetScalarParameterValue(MPC_RefractionStrength, FrameCache.GlobalRefractionStrength);

    // -----------------------------------------------------------------------
    //  Per-player rendering parameters
    //  Each player samples the region they are currently in independently.
    //  The material uses PlayerIndex (0 or 1) to select the correct set.
    // -----------------------------------------------------------------------

    MPCI->SetScalarParameterValue(MPC_IsUnderwaterP1, FrameCache.PlayerSamples[0].bIsUnderwater ? 1.f : 0.f);
    MPCI->SetScalarParameterValue(MPC_IsUnderwaterP2, FrameCache.PlayerSamples[1].bIsUnderwater ? 1.f : 0.f);

    // Per-player wave parameters.
    MPCI->SetScalarParameterValue(MPC_WaveAmplitudeP1, FrameCache.PlayerSamples[0].WaveAmplitude);
    MPCI->SetScalarParameterValue(MPC_WaveAmplitudeP2, FrameCache.PlayerSamples[1].WaveAmplitude);
    MPCI->SetScalarParameterValue(MPC_WaveSpeedP1, FrameCache.PlayerSamples[0].WaveSpeed);
    MPCI->SetScalarParameterValue(MPC_WaveSpeedP2, FrameCache.PlayerSamples[1].WaveSpeed);
    MPCI->SetScalarParameterValue(MPC_WaveTilingP1, FrameCache.PlayerSamples[0].WaveTilingScale);
    MPCI->SetScalarParameterValue(MPC_WaveTilingP2, FrameCache.PlayerSamples[1].WaveTilingScale);
    MPCI->SetScalarParameterValue(MPC_WaveLengthScaleP1, FrameCache.PlayerSamples[0].WaveLengthScale);
    MPCI->SetScalarParameterValue(MPC_WaveLengthScaleP2, FrameCache.PlayerSamples[1].WaveLengthScale);
    MPCI->SetVectorParameterValue(MPC_WaveDirectionP1,
        FLinearColor(FrameCache.PlayerSamples[0].WaveDirection.X,
            FrameCache.PlayerSamples[0].WaveDirection.Y, 0.f, 0.f));
    MPCI->SetVectorParameterValue(MPC_WaveDirectionP2,
        FLinearColor(FrameCache.PlayerSamples[1].WaveDirection.X,
            FrameCache.PlayerSamples[1].WaveDirection.Y, 0.f, 0.f));

    // Per-player water colors and rendering.
    MPCI->SetVectorParameterValue(MPC_UnderwaterColorP1, FrameCache.PlayerSamples[0].UnderwaterColor);
    MPCI->SetVectorParameterValue(MPC_UnderwaterColorP2, FrameCache.PlayerSamples[1].UnderwaterColor);
    MPCI->SetVectorParameterValue(MPC_WaterShallowColorP1, FrameCache.PlayerSamples[0].WaterShallowColor);
    MPCI->SetVectorParameterValue(MPC_WaterShallowColorP2, FrameCache.PlayerSamples[1].WaterShallowColor);
    MPCI->SetVectorParameterValue(MPC_WaterDeepColorP1, FrameCache.PlayerSamples[0].WaterDeepColor);
    MPCI->SetVectorParameterValue(MPC_WaterDeepColorP2, FrameCache.PlayerSamples[1].WaterDeepColor);
    MPCI->SetScalarParameterValue(MPC_AbsorptionScaleP1, FrameCache.PlayerSamples[0].AbsorptionScale);
    MPCI->SetScalarParameterValue(MPC_AbsorptionScaleP2, FrameCache.PlayerSamples[1].AbsorptionScale);
    MPCI->SetScalarParameterValue(MPC_FoamIntensityP1, FrameCache.PlayerSamples[0].FoamIntensity);
    MPCI->SetScalarParameterValue(MPC_FoamIntensityP2, FrameCache.PlayerSamples[1].FoamIntensity);
    MPCI->SetScalarParameterValue(MPC_RefractionStrengthP1, FrameCache.PlayerSamples[0].RefractionStrength);
    MPCI->SetScalarParameterValue(MPC_RefractionStrengthP2, FrameCache.PlayerSamples[1].RefractionStrength);

    // AssignPlayerPostProcessIndices is called once after players exist.
    // It sets PlayerIndex (0 or 1) on each player's post-process MID
    // so the material can select the correct P1/P2 parameter set.
    if (!bPlayerIndicesAssigned)
    {
        AssignPlayerPostProcessIndices();
    }

    // Diagnostic: log absorption write result periodically so we can confirm
    // the MPC parameter name matches and the write succeeds.
    // Uses DebugLogTimer so it respects LogFrequency like other ocean logs.
    if (DebugSettings && DebugSettings->bEnableOceanDebug
        && DebugSettings->bLogMPCWrites)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[OceanSubsystem] ABSORPTION DIAGNOSTIC: ")
            TEXT("MPC_AbsorptionScale write=%s  value=%.2f  ")
            TEXT("If false: parameter 'Ocean_AbsorptionScale' not found in MPC_Ocean."),
            bAbsorbWriteOK ? TEXT("OK") : TEXT("FAILED"),
            FrameCache.GlobalAbsorptionScale);
    }

    if (DebugSettings && DebugSettings->bEnableOceanDebug
        && DebugSettings->bLogMPCWrites)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[OceanSubsystem] MPC written: ")
            TEXT("HeightP1=%.1f HeightP2=%.1f ")
            TEXT("WaveAmp=%.1f WaveSpeed=%.2f ")
            TEXT("AbsorbScale=%.0f FoamIntensity=%.2f"),
            FrameCache.PlayerSamples[0].WaterHeight,
            FrameCache.PlayerSamples[1].WaterHeight,
            FrameCache.GlobalWaveAmplitude,
            FrameCache.GlobalWaveSpeed,
            FrameCache.GlobalAbsorptionScale,
            FrameCache.GlobalFoamIntensity);
    }

    // -----------------------------------------------------------------------
    //  Wave diagnostic (every 5s) -- confirms MPC values are reaching GPU.
    //  If GlobalWaveAmplitude shows correctly here but waves are NOT visible,
    //  your Gerstner material node is NOT reading Ocean_WaveAmplitude from MPC.
    //  Check that the Gerstner amplitude input is connected to the MPC node,
    //  not a hardcoded constant.
    // -----------------------------------------------------------------------
    static float WaveDiagTimer = 0.f;
    WaveDiagTimer += 0.016f;
    if (WaveDiagTimer >= 5.f)
    {
        WaveDiagTimer = 0.f;
        UE_LOG(LogTemp, Log,
            TEXT("[OceanSubsystem|WaveDiag] GlobalAmp=%.1f  "
                "P1_Amp=%.1f  P2_Amp=%.1f  "
                "GlobalSpeed=%.2f  Dir=(%.2f,%.2f)  "
                "P1_Height=%.1f  P2_Height=%.1f"),
            FrameCache.GlobalWaveAmplitude,
            FrameCache.PlayerSamples[0].WaveAmplitude,
            FrameCache.PlayerSamples[1].WaveAmplitude,
            FrameCache.GlobalWaveSpeed,
            FrameCache.GlobalWaveDirection.X,
            FrameCache.GlobalWaveDirection.Y,
            FrameCache.PlayerSamples[0].WaterHeight,
            FrameCache.PlayerSamples[1].WaterHeight);
    }
}

// ---------------------------------------------------------------------------
//  UpdateTransitionSlots
//  Called each frame. Manages which regions occupy the 8 precision slots.
//
//  Algorithm:
//    1. For each registered region, compute boundary distance to nearest camera.
//    2. Assign/keep slots for the 8 closest boundaries within CullMargin.
//    3. Fade out slots for regions that moved out of range.
//    4. Fade in slots for newly assigned regions.
//    5. Anti-pop: slots only released after fade reaches 0.
// ---------------------------------------------------------------------------
void UOceanSubsystem::UpdateTransitionSlots(float DeltaTime)
{
    UWorld* World = CachedWorld.Get();
    if (!World) return;

    // Collect local camera positions (up to 2 for split-screen).
    TArray<FVector2D, TInlineAllocator<2>> CameraPositions;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC || !PC->IsLocalController()) continue;
        FVector Loc; FRotator Rot;
        PC->GetPlayerViewPoint(Loc, Rot);
        CameraPositions.Add(FVector2D(Loc.X, Loc.Y));
    }
    if (CameraPositions.IsEmpty()) return;

    const float DefaultHeight = GetDefaultWaterHeight();

    // Compute priority score for each registered region.
    // Score = max(0, CullMargin - MinCameraDistanceToBoundary).
    // Higher score = higher priority for a slot.
    struct FRegionCandidate
    {
        AWaterRegionActor* Region = nullptr;
        FVector2D          BoundaryPt = FVector2D::ZeroVector;
        FVector2D          Normal = FVector2D(1.f, 0.f);
        float              Score = 0.f;
        float              CamDist = FLT_MAX;
    };

    TArray<FRegionCandidate> Candidates;
    Candidates.Reserve(RegisteredRegions.Num());

    for (const TObjectPtr<AWaterRegionActor>& Region : RegisteredRegions)
    {
        if (!Region || !Region->RegionData) continue;

        // Find nearest camera and its distance to the boundary.
        float MinDist = FLT_MAX;
        FVector2D BestBoundary;
        FVector2D BestNormal;
        bool bGotBoundary = false;

        for (const FVector2D& CamXY : CameraPositions)
        {
            FVector2D BPt, BNorm;
            if (Region->GetNearestBoundaryPoint(CamXY, BPt, BNorm))
            {
                const float D = (CamXY - BPt).Size();
                if (D < MinDist)
                {
                    MinDist = D;
                    BestBoundary = BPt;
                    BestNormal = BNorm;
                    bGotBoundary = true;
                }
            }
        }
        if (!bGotBoundary) continue;

        // CullMargin = BlendRadius * 3 ensures slot is assigned well before
        // the transition becomes visible to the camera.
        const float CullMargin = Region->RegionData->BlendRadius * 3.f + 2000.f;
        const float Score = FMath::Max(0.f, CullMargin - MinDist);
        if (Score <= 0.f) continue;

        FRegionCandidate C;
        C.Region = Region.Get();
        C.BoundaryPt = BestBoundary;
        C.Normal = BestNormal;
        C.Score = Score;
        C.CamDist = MinDist;
        Candidates.Add(C);
    }

    // Sort by score descending (highest priority first).
    Candidates.Sort([](const FRegionCandidate& A, const FRegionCandidate& B)
        {
            return A.Score > B.Score;
        });

    // Mark existing slots for update or fade-out.
    for (int32 s = 0; s < MaxTransitionSlots; ++s)
    {
        FTransitionSlotState& Slot = TransitionSlots[s];
        if (!Slot.Region.IsValid())
        {
            // Slot is free -- mark as fully faded to allow reassignment.
            Slot.bFadingOut = true;
            Slot.Weight = 0.f;
            continue;
        }

        // Check if this region is still in the candidate list.
        bool bStillNeeded = false;
        for (int32 c = 0; c < FMath::Min(MaxTransitionSlots, Candidates.Num()); ++c)
        {
            if (Candidates[c].Region == Slot.Region.Get())
            {
                bStillNeeded = true;
                // Update boundary data each frame (camera moved).
                Slot.BoundaryPoint = Candidates[c].BoundaryPt;
                Slot.OutwardNormal = Candidates[c].Normal;
                Slot.CameraDistance = Candidates[c].CamDist;
                break;
            }
        }

        if (!bStillNeeded && !Slot.bFadingOut)
        {
            Slot.bFadingOut = true;
        }
    }

    // Assign free slots to top candidates that don't have a slot yet.
    for (int32 c = 0; c < Candidates.Num() && c < MaxTransitionSlots; ++c)
    {
        AWaterRegionActor* CandRegion = Candidates[c].Region;

        // Check if already has a slot.
        bool bAlreadyAssigned = false;
        for (int32 s = 0; s < MaxTransitionSlots; ++s)
        {
            if (TransitionSlots[s].Region.Get() == CandRegion)
            {
                bAlreadyAssigned = true;
                break;
            }
        }
        if (bAlreadyAssigned) continue;

        // Find a free slot (weight == 0 and fading out).
        for (int32 s = 0; s < MaxTransitionSlots; ++s)
        {
            FTransitionSlotState& Slot = TransitionSlots[s];
            if (Slot.Weight < 0.001f && (Slot.bFadingOut || !Slot.Region.IsValid()))
            {
                // Assign this candidate to the slot.
                Slot.Region = CandRegion;
                Slot.BoundaryPoint = Candidates[c].BoundaryPt;
                Slot.OutwardNormal = Candidates[c].Normal;
                Slot.HeightInner = CandRegion->RegionData->BaseWaterHeight;
                Slot.HeightOuter = DefaultHeight;
                Slot.BlendRadius = CandRegion->RegionData->BlendRadius;
                Slot.Weight = 0.f;
                Slot.bFadingOut = false;
                Slot.CameraDistance = Candidates[c].CamDist;
                break;
            }
        }
    }

    // Update fade weights.
    for (int32 s = 0; s < MaxTransitionSlots; ++s)
    {
        FTransitionSlotState& Slot = TransitionSlots[s];
        if (Slot.bFadingOut)
        {
            Slot.Weight = FMath::Max(0.f, Slot.Weight - SlotFadeSpeed * DeltaTime);
            if (Slot.Weight <= 0.f)
            {
                // Fully faded -- release the slot.
                Slot.Region.Reset();
                Slot.bFadingOut = false;
            }
        }
        else if (Slot.Region.IsValid())
        {
            Slot.Weight = FMath::Min(1.f, Slot.Weight + SlotFadeSpeed * DeltaTime);
        }
    }
}

// ---------------------------------------------------------------------------
//  WriteTransitionSlotsMPC
//  Writes all 8 slot parameters to MPC_Ocean.
//  The material evaluates each active slot's signed half-plane distance,
//  blends heights, and overrides the height field in transition zones.
// ---------------------------------------------------------------------------
void UOceanSubsystem::WriteTransitionSlotsMPC() const
{
    UWorld* World = CachedWorld.Get();
    if (!World || !OceanMPC) return;

    UMaterialParameterCollectionInstance* MPCI =
        World->GetParameterCollectionInstance(OceanMPC);
    if (!MPCI) return;

    for (int32 s = 0; s < MaxTransitionSlots; ++s)
    {
        const FTransitionSlotState& Slot = TransitionSlots[s];
        const FString Suffix = FString::FromInt(s);

        MPCI->SetScalarParameterValue(
            *FString::Printf(TEXT("Slot_%d_CenterX"), s), Slot.BoundaryPoint.X);
        MPCI->SetScalarParameterValue(
            *FString::Printf(TEXT("Slot_%d_CenterY"), s), Slot.BoundaryPoint.Y);
        MPCI->SetScalarParameterValue(
            *FString::Printf(TEXT("Slot_%d_NormalX"), s), Slot.OutwardNormal.X);
        MPCI->SetScalarParameterValue(
            *FString::Printf(TEXT("Slot_%d_NormalY"), s), Slot.OutwardNormal.Y);
        MPCI->SetScalarParameterValue(
            *FString::Printf(TEXT("Slot_%d_HeightInner"), s), Slot.HeightInner);
        MPCI->SetScalarParameterValue(
            *FString::Printf(TEXT("Slot_%d_HeightOuter"), s), Slot.HeightOuter);
        MPCI->SetScalarParameterValue(
            *FString::Printf(TEXT("Slot_%d_BlendRadius"), s), Slot.BlendRadius);
        MPCI->SetScalarParameterValue(
            *FString::Printf(TEXT("Slot_%d_Weight"), s), Slot.Weight);
    }
}

// ---------------------------------------------------------------------------
//  AssignPlayerPostProcessIndices
//  Sets a "PlayerIndex" scalar (0 or 1) on each local player's post-process
//  Material Instance Dynamic so the underwater post-process material knows
//  which per-player MPC parameters to sample (P1 vs P2).
//
//  Called from WriteMPCParameters once, the first frame all local players
//  have been registered. Safe to call multiple times -- exits early if done.
//
//  Prerequisite: the underwater post-process material must have a scalar
//  parameter named "PlayerIndex". The post-process component must be on
//  the PlayerController (added via BP or C++ in your HUD initialization).
// ---------------------------------------------------------------------------
void UOceanSubsystem::AssignPlayerPostProcessIndices() const
{
    UWorld* World = CachedWorld.Get();
    if (!World) return;

    // Require both local players to exist before assigning.
    // In single-player, only P0 exists -- we assign index 0 and finish.
    int32 AssignedCount = 0;

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC || !PC->IsLocalController()) continue;

        const ULocalPlayer* LP = Cast<ULocalPlayer>(PC->Player);
        if (!LP) continue;

        // Player index: 0 for the first local player, 1 for the second.
        const int32 PlayerIdx = LP->GetControllerId();

        // Find post-process components on the PlayerController.
        TArray<UPostProcessComponent*> PPComps;
        PC->GetComponents<UPostProcessComponent>(PPComps);

        for (UPostProcessComponent* PPC : PPComps)
        {
            if (!PPC) continue;
            for (FWeightedBlendable& Blendable : PPC->Settings.WeightedBlendables.Array)
            {
                UMaterialInstanceDynamic* MID =
                    Cast<UMaterialInstanceDynamic>(Blendable.Object);
                if (!MID) continue;

                // Only set PlayerIndex if the parameter exists in this MID.
                // SetScalarParameterValue returns false if the param is absent,
                // so we won't pollute non-underwater materials.
                float Dummy;
                if (MID->GetScalarParameterValue(TEXT("PlayerIndex"), Dummy))
                {
                    MID->SetScalarParameterValue(TEXT("PlayerIndex"), (float)PlayerIdx);
                    ++AssignedCount;

                    UE_LOG(LogTemp, Log,
                        TEXT("[OceanSubsystem] AssignPlayerPostProcessIndices: ")
                        TEXT("PC='%s'  PlayerIndex=%d  MID='%s'"),
                        *PC->GetName(), PlayerIdx, *MID->GetName());
                }
            }
        }
    }

    if (AssignedCount > 0)
    {
        bPlayerIndicesAssigned = true;
        UE_LOG(LogTemp, Log,
            TEXT("[OceanSubsystem] Player post-process indices assigned (%d MIDs). ")
            TEXT("Per-player underwater rendering is active."),
            AssignedCount);
    }
}

// ---------------------------------------------------------------------------
//  GetDefaultWaterHeight
//  Returns the BaseWaterHeight of the DefaultRegionData asset.
//  Used by UOceanHeightFieldGenerator as the surface-level Z probe when
//  sampling GetWaterHeightAtPosition on its 2D grid.
// ---------------------------------------------------------------------------
float UOceanSubsystem::GetDefaultWaterHeight() const
{
    if (DefaultRegionData)
    {
        return DefaultRegionData->BaseWaterHeight;
    }
    return 0.f;
}

// ---------------------------------------------------------------------------
//  GetSurfaceHeightAtXY
//  The single canonical "surface height at this XY" probe. Evaluates regions
//  at the canonical surface Z so every surface consumer agrees.
// ---------------------------------------------------------------------------
float UOceanSubsystem::GetSurfaceHeightAtXY(const FVector2D& XY) const
{
    const float ProbeZ = GetDefaultWaterHeight();
    return SampleWaterAt(FVector(XY.X, XY.Y, ProbeZ)).WaterHeight;
}

// ---------------------------------------------------------------------------
//  DrawDebugVisualization
// ---------------------------------------------------------------------------
void UOceanSubsystem::DrawDebugVisualization() const
{
    if (!DebugSettings || !DebugSettings->bEnableOceanDebug) return;

    UWorld* World = CachedWorld.Get();
    if (!World) return;

    const float Duration = DebugSettings->DebugDrawDuration;
    const float Thickness = DebugSettings->DebugLineThickness;

    // Resolve which actor types to draw.
   // bDrawWaterHeightAtActors is the legacy combined toggle (draws both).
    const bool bDrawPlayers = DebugSettings->bDrawWaterHeightAtPlayers;
    const bool bDrawCPUs = DebugSettings->bDrawWaterHeightAtCPUs;

    if (!bDrawPlayers && !bDrawCPUs) return;

    // Draw debug lines for all registered sample actors.
    // Player actors use their dedicated color; CPU actors use a distinct color.
    for (const FOceanSampleRequest& Request : RegisteredSampleActors)
    {
        // Filter by actor type based on active toggles.
        if (Request.bIsLocalPlayer && !bDrawPlayers) continue;
        if (!Request.bIsLocalPlayer && !bDrawCPUs) continue;

        AActor* Actor = Request.Actor.Get();
        if (!Actor) continue;

        const FVector ActorLoc = Actor->GetActorLocation();

        float WaterH = 0.f;

        if (Request.bIsLocalPlayer && Request.PlayerIndex >= 0 && Request.PlayerIndex < 2)
        {
            WaterH = FrameCache.PlayerSamples[Request.PlayerIndex].WaterHeight;
        }
        else if (const FWaterSample* Cached = FrameCache.ActorSamples.Find(Actor))
        {
            WaterH = Cached->WaterHeight;
        }
        else
        {
            continue;
        }

        // Color: player 0 = cyan, player 1 = orange, CPU = green.
        FColor LineColor = FColor(0, 255, 100);
        if (Request.bIsLocalPlayer)
        {
            LineColor = (Request.PlayerIndex == 0)
                ? FColor(0, 200, 255)
                : FColor(255, 180, 0);
        }

        // Vertical line from actor to water surface.
        DrawDebugLine(World,
            FVector(ActorLoc.X, ActorLoc.Y, ActorLoc.Z),
            FVector(ActorLoc.X, ActorLoc.Y, WaterH),
            LineColor, false, Duration, 0, Thickness);

        // Cross at water surface level.
        DrawDebugLine(World,
            FVector(ActorLoc.X - 100.f, ActorLoc.Y, WaterH),
            FVector(ActorLoc.X + 100.f, ActorLoc.Y, WaterH),
            LineColor, false, Duration, 0, Thickness);
        DrawDebugLine(World,
            FVector(ActorLoc.X, ActorLoc.Y - 100.f, WaterH),
            FVector(ActorLoc.X, ActorLoc.Y + 100.f, WaterH),
            LineColor, false, Duration, 0, Thickness);
    }
}

// ---------------------------------------------------------------------------
//  GetHeightFieldGenerator
//  Lazily finds and caches the height field generator in the world.
// ---------------------------------------------------------------------------
UOceanHeightFieldGenerator* UOceanSubsystem::GetHeightFieldGenerator() const
{
    if (CachedHeightFieldGen.IsValid()) return CachedHeightFieldGen.Get();

    UWorld* World = CachedWorld.Get();
    if (!World) return nullptr;

    for (TActorIterator<AOceanSurfaceActor> It(World); It; ++It)
    {
        AOceanSurfaceActor* Surface = *It;
        if (Surface && Surface->HeightFieldGenerator)
        {
            CachedHeightFieldGen = Surface->HeightFieldGenerator;
            return CachedHeightFieldGen.Get();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  DebugReconstructSurface
//  For each local player, reconstructs the VISUAL height on the CPU term by
//  term and compares it to the PHYSICS height, printing the residual.
//
//  Terms (all at the player's XY, current time):
//    RegionalBase   = GetWaterHeightAtPosition (exact, no waves)
//    HeightFieldRB  = bilinear readback of the baked texture
//    SlotCorrected  = HeightFieldRB with transition-slot overrides applied
//    PrimaryWave    = octave 0 Gerstner
//    DetailWave     = octaves 1..MaxOctave Gerstner
//    PhysicsHeight  = RegionalBase + PrimaryWave        (what the sub rides)
//    VisualHeight   = SlotCorrected + PrimaryWave + DetailWave
//    Residual_Base  = | SlotCorrected - RegionalBase |  (height field accuracy)
//    Residual_Prim  = | (SlotCorrected+PrimaryWave) - PhysicsHeight |
//    Residual_Full  = | VisualHeight - PhysicsHeight |  (includes visual detail)
// ---------------------------------------------------------------------------
void UOceanSubsystem::DebugReconstructSurface() const
{
    UWorld* World = CachedWorld.Get();
    if (!World || !GEngine) return;

    const float Time = World->GetTimeSeconds();
    const int32 MaxOct = FMath::Clamp(
        CVarOceanDebugMaxOctave.GetValueOnGameThread(), 0, OceanGerstner::NumOctaves - 1);

    UOceanHeightFieldGenerator* Gen = GetHeightFieldGenerator();

    int32 PlayerNum = 0;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC || !PC->IsLocalController()) continue;

        APawn* Pawn = PC->GetPawn();
        if (!Pawn) continue;

        const FVector P = Pawn->GetActorLocation();
        const FVector2D XY(P.X, P.Y);

        // --- Term-by-term reconstruction ---
        // RegionalBase uses the CANONICAL surface probe (same Z as the height
        // field and QuerySurface), NOT the submarine's actual depth -- so all
        // surface consumers are compared apples-to-apples.
        const float RegionalBase = GetSurfaceHeightAtXY(XY);

        float HeightFieldRB = RegionalBase; // fallback if no generator
        bool bHaveRB = false;
        if (Gen && Gen->HasCPUReadback())
        {
            bHaveRB = Gen->SampleHeightFieldBilinear(XY, HeightFieldRB);
        }

        // Diagnostic only -- the material has NO slot loop, so this is unused.
        const float SlotCorrected = EvaluateSlotCorrectionCPU(XY, HeightFieldRB);

        // Sample at the canonical surface Z (same probe as the height field).
        const FWaterSample S = SampleWaterAt(FVector(XY.X, XY.Y, GetDefaultWaterHeight()));
        const float PrimW = EvaluateGerstnerOctave(0, XY, Time,
            S.WaveAmplitude, S.WaveSpeed, S.WaveDirection, S.WaveLengthScale);

        float DetailW = 0.f;
        for (int32 i = 1; i <= MaxOct; ++i)
        {
            DetailW += EvaluateGerstnerOctave(i, XY, Time,
                S.WaveAmplitude, S.WaveSpeed, S.WaveDirection, S.WaveLengthScale);
        }

        const float PhysicsHeight = RegionalBase + PrimW;

        // VisualHeight = what the MATERIAL renders = raw height field + waves.
        const float VisualHeight = HeightFieldRB + PrimW + DetailW;

        const float ResidualBase = FMath::Abs(HeightFieldRB - RegionalBase);
        const float ResidualPrim = FMath::Abs((HeightFieldRB + PrimW) - PhysicsHeight);
        const float ResidualFull = FMath::Abs(VisualHeight - PhysicsHeight);

        // --- On-screen text ---
        const int32 KeyBase = 7000 + PlayerNum * 20;
        auto Msg = [&](int32 Key, const FColor& C, const FString& Text)
            {
                GEngine->AddOnScreenDebugMessage(Key, 0.f, C, Text);
            };

        Msg(KeyBase + 0, FColor::Cyan,
            FString::Printf(TEXT("[P%d] --- Ocean Surface Validation ---"), PlayerNum));
        Msg(KeyBase + 1, FColor::White,
            FString::Printf(TEXT("  RegionalBase   = %.1f"), RegionalBase));
        Msg(KeyBase + 2, FColor::White,
            FString::Printf(TEXT("  HeightFieldRB  = %.1f %s"), HeightFieldRB,
                bHaveRB ? TEXT("") : TEXT("(no readback)")));
        Msg(KeyBase + 3, FColor::White,
            FString::Printf(TEXT("  SlotCorrected  = %.1f (UNUSED diag)"), SlotCorrected));
        Msg(KeyBase + 4, FColor::White,
            FString::Printf(TEXT("  PrimaryWave    = %.1f"), PrimW));
        Msg(KeyBase + 5, FColor::White,
            FString::Printf(TEXT("  DetailWave(1-%d)= %.1f"), MaxOct, DetailW));
        Msg(KeyBase + 6, FColor::Yellow,
            FString::Printf(TEXT("  PhysicsHeight  = %.1f"), PhysicsHeight));
        Msg(KeyBase + 7, FColor::Yellow,
            FString::Printf(TEXT("  VisualHeight   = %.1f"), VisualHeight));
        Msg(KeyBase + 8,
            ResidualBase < 50.f ? FColor::Green : FColor::Red,
            FString::Printf(TEXT("  Residual_Base  = %.1f  (target <50)"), ResidualBase));
        Msg(KeyBase + 9,
            ResidualPrim < 50.f ? FColor::Green : FColor::Red,
            FString::Printf(TEXT("  Residual_Prim  = %.1f  (target <50)"), ResidualPrim));
        Msg(KeyBase + 10, FColor::Orange,
            FString::Printf(TEXT("  Residual_Full  = %.1f  (incl. visual detail)"), ResidualFull));

        // --- World overlay: vertical marks at the player's XY ---
        const FVector Base(P.X, P.Y, RegionalBase);
        DrawDebugLine(World, Base, Base + FVector(0, 0, 2000.f), FColor::White, false, 0.f, 0, 8.f);
        // Physics height mark (yellow sphere)
        DrawDebugSphere(World, FVector(P.X, P.Y, PhysicsHeight), 120.f, 8, FColor::Yellow, false, 0.f, 0, 6.f);
        // Visual height mark (orange sphere)
        DrawDebugSphere(World, FVector(P.X, P.Y, VisualHeight), 90.f, 8, FColor::Orange, false, 0.f, 0, 4.f);
        // Regional base mark (white sphere)
        DrawDebugSphere(World, FVector(P.X, P.Y, RegionalBase), 60.f, 8, FColor::White, false, 0.f, 0, 3.f);

        // --- Surface patch grid -------------------------------------------
        // Draws a cyan wireframe sheet at the PHYSICS surface height
        // (QuerySurface) in a grid around the submarine, so you can see
        // exactly where physics thinks the surface is and compare it to the
        // rendered mesh. With amplitude 0 this is flat at the regional height
        // and steps across region boundaries -- the transition made visible.
        {
            const int32 GridHalf = 4;          // 9x9 grid of points
            const float GridSpacing = 2000.f;  // UU between grid points

            for (int32 gy = -GridHalf; gy <= GridHalf; ++gy)
            {
                for (int32 gx = -GridHalf; gx <= GridHalf; ++gx)
                {
                    const FVector2D GXY(P.X + gx * GridSpacing, P.Y + gy * GridSpacing);
                    const FOceanSurfaceSample GS = QuerySurface(GXY, Time);
                    const FVector Pt(GXY.X, GXY.Y, GS.SurfaceHeight);

                    // Connect to +X neighbor.
                    if (gx < GridHalf)
                    {
                        const FVector2D NXY(GXY.X + GridSpacing, GXY.Y);
                        const FOceanSurfaceSample NS = QuerySurface(NXY, Time);
                        DrawDebugLine(World, Pt,
                            FVector(NXY.X, NXY.Y, NS.SurfaceHeight),
                            FColor::Cyan, false, 0.f, 0, 2.f);
                    }
                    // Connect to +Y neighbor.
                    if (gy < GridHalf)
                    {
                        const FVector2D NXY(GXY.X, GXY.Y + GridSpacing);
                        const FOceanSurfaceSample NS = QuerySurface(NXY, Time);
                        DrawDebugLine(World, Pt,
                            FVector(NXY.X, NXY.Y, NS.SurfaceHeight),
                            FColor::Cyan, false, 0.f, 0, 2.f);
                    }
                }
            }
        }

        ++PlayerNum;
    }
}