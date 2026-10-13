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

// Resolved octaves (see OceanGerstner::ToMPC). Vector params:
//   R,G = K * direction (rad/cm), B = amplitude (cm), A = time phase (rad).
static const FName MPC_Oct[OceanGerstner::NumOctaves] = {
    FName(TEXT("Ocean_Oct1")), FName(TEXT("Ocean_Oct2")),
    FName(TEXT("Ocean_Oct3")), FName(TEXT("Ocean_Oct4")) };

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

    // Material wave set: shape from GetWaveShapeSample (same as physics),
    // amplitude from the global source (player 0). Physics builds its
    // octaves with the same BuildOctaveAt, so only amplitude can differ
    // (until the per-region wave field, roadmap item 5).
    FrameCache.WaveTime = static_cast<float>(World->GetTimeSeconds());
    for (int32 i = 0; i < OceanGerstner::NumOctaves; ++i)
    {
        OceanGerstner::FOctave O = BuildOctaveAt(i, GlobalSource);

        if (i == OceanGerstner::PrimaryOctave)
        {
            // Never fade the primary: physics rides it. Warn once instead.
            if (!bWarnedPrimaryUndersampled && ComputeOctaveDrawableFactor(O) < 1.f)
            {
                bWarnedPrimaryUndersampled = true;
                const float MaxScale = (2.f * PI)
                    / (OceanGerstner::WaveK[i] * SurfaceMinVerticesPerWave * SurfaceVertexSpacing);
                UE_LOG(LogTemp, Warning,
                    TEXT("[OceanSubsystem] Primary wave is too short for the ocean mesh ")
                    TEXT("(spacing %.0f cm, min %.1f verts/wave). Rendered crests will not match ")
                    TEXT("physics. Keep WaveLengthScale <= %.2f or use a denser mesh."),
                    SurfaceVertexSpacing, SurfaceMinVerticesPerWave, MaxScale);
            }
        }
        else
        {
            // Visual-only detail octave: fade out what the mesh cannot draw.
            O.Amp *= ComputeOctaveDrawableFactor(O);
        }

        FrameCache.MPCOctaves[i] = OceanGerstner::ToMPC(O, FrameCache.WaveTime);
    }

    FrameCache.bValid = true;

    WriteMPCParameters();

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
    const OceanGerstner::FOctave O = OceanGerstner::BuildOctave(OctaveIndex,
        BaseAmplitude, BaseSpeed, BaseDirection, WaveLengthScale);
    return OceanGerstner::EvaluateZ(O, XY, Time);
}

// ---------------------------------------------------------------------------
//  GetWaveShapeSample
//  Where the wave SHAPE (speed, direction, wavelength scale) comes from.
//  Global (DefaultRegionData) so the shared surface and physics agree and
//  shape never changes as a submarine moves. Amplitude stays per region.
// ---------------------------------------------------------------------------
FWaterSample UOceanSubsystem::GetWaveShapeSample() const
{
    return GetDefaultSample();
}

// ---------------------------------------------------------------------------
//  SetSurfaceMeshSampling / ComputeOctaveDrawableFactor
//  The ocean mesh is a uniform grid; a wave shorter than a few vertex
//  spacings cannot be drawn and aliases. Detail octaves are faded out
//  (visual only); the primary is never faded because physics rides it.
// ---------------------------------------------------------------------------
void UOceanSubsystem::SetSurfaceMeshSampling(float VertexSpacing, float MinVerticesPerWave)
{
    SurfaceVertexSpacing = FMath::Max(VertexSpacing, 0.f);
    SurfaceMinVerticesPerWave = FMath::Max(MinVerticesPerWave, 2.f);
    bWarnedPrimaryUndersampled = false;
}

float UOceanSubsystem::ComputeOctaveDrawableFactor(const OceanGerstner::FOctave& O) const
{
    if (SurfaceVertexSpacing <= 0.f) return 1.f;
    const float K = static_cast<float>(O.KDir.Size());
    if (K <= KINDA_SMALL_NUMBER) return 1.f;

    const float VertsPerWave = (2.f * PI / K) / SurfaceVertexSpacing;
    return FMath::SmoothStep(0.75f * SurfaceMinVerticesPerWave,
        SurfaceMinVerticesPerWave, VertsPerWave);
}

// ---------------------------------------------------------------------------
//  BuildOctaveAt
//  The one octave builder used by physics, debug and the MPC write.
// ---------------------------------------------------------------------------
OceanGerstner::FOctave UOceanSubsystem::BuildOctaveAt(int32 OctaveIndex,
    const FWaterSample& AmpSource) const
{
    const FWaterSample Shape = GetWaveShapeSample();
    return OceanGerstner::BuildOctave(OctaveIndex,
        AmpSource.WaveAmplitude, Shape.WaveSpeed,
        Shape.WaveDirection, Shape.WaveLengthScale);
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
        Sum += OceanGerstner::EvaluateZ(BuildOctaveAt(i, Sample), XY, Time);
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

    // Primary octave only -- the rideable wave. Same builder as the MPC write.
    const OceanGerstner::FOctave O =
        BuildOctaveAt(OceanGerstner::PrimaryOctave, Sample);
    const float Phase = OceanGerstner::Phase(O, XY, Time);
    Out.PrimaryWaveOffset = O.Amp * FMath::Sin(Phase);

    Out.SurfaceHeight = Out.RegionalBaseHeight + Out.PrimaryWaveOffset;

    // Analytic normal from the primary wave slope (closed form).
    //   dZ/dX = A * KDir.X * cos(Phase)
    //   dZ/dY = A * KDir.Y * cos(Phase)
    //   Normal = normalize( (-dZ/dX, -dZ/dY, 1) )
    {
        const float CosP = FMath::Cos(Phase);
        const float dZdX = O.Amp * static_cast<float>(O.KDir.X) * CosP;
        const float dZdY = O.Amp * static_cast<float>(O.KDir.Y) * CosP;
        Out.SurfaceNormal = FVector(-dZdX, -dZdY, 1.f).GetSafeNormal();
    }

    return Out;
}

// ---------------------------------------------------------------------------
//  EvaluatePositionInternal
//  Core blending logic. Starts from DefaultRegionData, then applies each
//  region from LOWEST to HIGHEST priority:
//      Accumulated = lerp(Accumulated, Region, Weight)
//  So a region at weight 1 fully replaces everything below it (a position
//  deep inside OpenOcean gets exactly OpenOcean's values), a feathered edge
//  (0 < Weight < 1) fades smoothly into what is underneath, and the higher
//  priority region wins where regions overlap ("higher value wins before
//  blending", WaterRegionDataAsset::Priority).
//
//  Previous version started the default at weight 1 and blended each region
//  with Weight / (TotalWeight + Weight), so a region at full weight only
//  reached 50% (log 2026-10-13: height 8990 inside a BaseWaterHeight=10000
//  region).
// ---------------------------------------------------------------------------
FWaterSample UOceanSubsystem::EvaluatePositionInternal(const FVector& WorldPos) const
{
    FWaterSample Accumulated = GetDefaultSample();

    // RegisteredRegions is sorted by Priority DESCENDING (RegisterRegion),
    // so iterate backwards: lowest priority first, highest applied last.
    for (int32 r = RegisteredRegions.Num() - 1; r >= 0; --r)
    {
        const AWaterRegionActor* Region = RegisteredRegions[r];
        if (!Region || !Region->RegionData) continue;

        const float Weight = FMath::Clamp(Region->EvaluateWeight(WorldPos), 0.f, 1.f);
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

        Accumulated = BlendSamples(Accumulated, RegionSample, Weight);
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

    // Resolved octaves for MF_Ocean_HeightOffset (single wave definition).
    bool bOctWriteOK = true;
    for (int32 i = 0; i < OceanGerstner::NumOctaves; ++i)
    {
        bOctWriteOK &= MPCI->SetVectorParameterValue(MPC_Oct[i], FrameCache.MPCOctaves[i]);
    }
    static bool bWarnedOctMissing = false;
    if (!bOctWriteOK && !bWarnedOctMissing)
    {
        bWarnedOctMissing = true;
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanSubsystem] Ocean_Oct1..4 vector params missing in MPC_Ocean. ")
            TEXT("Add them or the material waves will be flat."));
    }

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
//    PrimaryWave    = octave 0 Gerstner
//    DetailWave     = octaves 1..MaxOctave Gerstner
//    PhysicsHeight  = RegionalBase + PrimaryWave        (what the sub rides)
//    VisualHeight   = HeightFieldRB + PrimaryWave + DetailWave
//    Residual_Base  = | HeightFieldRB - RegionalBase |  (height field accuracy)
//    Residual_Prim  = | (HeightFieldRB+PrimaryWave) - PhysicsHeight |
//    Residual_Full  = | VisualHeight - PhysicsHeight |  (includes visual detail)
//    MatPrimWave    = octave 0 rebuilt from the packed MPC Ocean_Oct1 value
//    Residual_MatPrim = | MatPrimWave - PrimaryWave |   (material input vs physics)
//    Red sphere     = HeightFieldRB + MPC waves (where the screen should be)
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

        // Sample at the canonical surface Z (same probe as the height field).
        const FWaterSample S = SampleWaterAt(FVector(XY.X, XY.Y, GetDefaultWaterHeight()));
        const float PrimW = OceanGerstner::EvaluateZ(BuildOctaveAt(0, S), XY, Time);

        float DetailW = 0.f;
        for (int32 i = 1; i <= MaxOct; ++i)
        {
            DetailW += OceanGerstner::EvaluateZ(BuildOctaveAt(i, S), XY, Time);
        }

        // Material-side reconstruction from the EXACT values written to MPC
        // this frame (what MF_Ocean_HeightOffset receives).
        const float MatPrimW = OceanGerstner::EvaluateZFromMPC(FrameCache.MPCOctaves[0], XY);
        float MatW = MatPrimW;
        for (int32 i = 1; i <= MaxOct; ++i)
        {
            MatW += OceanGerstner::EvaluateZFromMPC(FrameCache.MPCOctaves[i], XY);
        }
        const float MatVisualHeight = HeightFieldRB + MatW;
        const float ResidualMatPrim = FMath::Abs(MatPrimW - PrimW);

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
        Msg(KeyBase + 11, FColor::White,
            FString::Printf(TEXT("  MatPrimWave    = %.1f  (from MPC Ocean_Oct1)"), MatPrimW));
        Msg(KeyBase + 12,
            ResidualMatPrim < 5.f ? FColor::Green : FColor::Red,
            FString::Printf(TEXT("  Residual_MatPrim = %.1f  (MPC vs physics, target <5)"), ResidualMatPrim));
        Msg(KeyBase + 13, FColor::White,
            FString::Printf(TEXT("  Amp local=%.1f  MPC=%.1f  T=%.2f  T_MPC=%.2f"),
                S.WaveAmplitude, FrameCache.MPCOctaves[0].B, Time, FrameCache.WaveTime));

        // --- World overlay: vertical marks at the player's XY ---
        const FVector Base(P.X, P.Y, RegionalBase);
        DrawDebugLine(World, Base, Base + FVector(0, 0, 2000.f), FColor::White, false, 0.f, 0, 8.f);
        // Physics height mark (yellow sphere)
        DrawDebugSphere(World, FVector(P.X, P.Y, PhysicsHeight), 120.f, 8, FColor::Yellow, false, 0.f, 0, 6.f);
        // Visual height mark (orange sphere)
        DrawDebugSphere(World, FVector(P.X, P.Y, VisualHeight), 90.f, 8, FColor::Orange, false, 0.f, 0, 4.f);
        // Regional base mark (white sphere)
        DrawDebugSphere(World, FVector(P.X, P.Y, RegionalBase), 60.f, 8, FColor::White, false, 0.f, 0, 3.f);
        // Material height mark (red sphere): height field + waves from MPC
        DrawDebugSphere(World, FVector(P.X, P.Y, MatVisualHeight), 75.f, 8, FColor::Red, false, 0.f, 0, 4.f);

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