#include "LayeredSplineWaterRegion.h"
#include "WaterRegionDataAsset.h"
#include "Components/SplineComponent.h"
#include "ProceduralMeshComponent.h"

#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#endif

ALayeredSplineWaterRegion::ALayeredSplineWaterRegion()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create the two default layer splines as named components.
    // These are directly visible in the UE5 component hierarchy and fully
    // editable in the viewport -- this is the key difference from the old
    // struct-array approach where splines were hidden inside a USTRUCT.

    LayerSpline_0 = CreateDefaultSubobject<USplineComponent>(TEXT("LayerSpline_0"));
    LayerSpline_0->SetupAttachment(GetRootComponent());
    LayerSpline_0->SetClosedLoop(true);
    LayerSpline_0->ClearSplinePoints(false);
    LayerSpline_0->AddSplineLocalPoint(FVector(3000.f, 0.f, 0.f));
    LayerSpline_0->AddSplineLocalPoint(FVector(0.f, 3000.f, 0.f));
    LayerSpline_0->AddSplineLocalPoint(FVector(-3000.f, 0.f, 0.f));
    LayerSpline_0->AddSplineLocalPoint(FVector(0.f, -3000.f, 0.f));
    LayerSpline_0->UpdateSpline();

    LayerSpline_1 = CreateDefaultSubobject<USplineComponent>(TEXT("LayerSpline_1"));
    LayerSpline_1->SetupAttachment(GetRootComponent());
    LayerSpline_1->SetClosedLoop(true);
    LayerSpline_1->ClearSplinePoints(false);
    LayerSpline_1->AddSplineLocalPoint(FVector(2000.f, 0.f, 0.f));
    LayerSpline_1->AddSplineLocalPoint(FVector(0.f, 2000.f, 0.f));
    LayerSpline_1->AddSplineLocalPoint(FVector(-2000.f, 0.f, 0.f));
    LayerSpline_1->AddSplineLocalPoint(FVector(0.f, -2000.f, 0.f));
    LayerSpline_1->UpdateSpline();

    // Populate metadata for the two default layers.
    // LayerAltitude is what the designer adjusts in SplineLayers[N].
    FSplineLayer Info0;
    Info0.LayerAltitude = 50000.f;
    Info0.SplineComponentName = TEXT("LayerSpline_0");
    Info0.LayerLabel = TEXT("Surface");

    FSplineLayer Info1;
    Info1.LayerAltitude = 0.f;
    Info1.SplineComponentName = TEXT("LayerSpline_1");
    Info1.LayerLabel = TEXT("Deep");

    SplineLayers.Add(Info0);
    SplineLayers.Add(Info1);
}

// ---------------------------------------------------------------------------
//  PostInitializeComponents
//  Runs after the actor's components are initialized and after BP serialized
//  values have been applied. This is the safe place to repair SplineComponentName
//  fields that were serialized as None by old BP instances.
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    RepairSplineComponentNames();
    SortAndRebuild();

    UE_LOG(LogTemp, Log,
        TEXT("[LayeredSplineWaterRegion] '%s' PostInitializeComponents: "
            "%d layers registered."),
        *GetName(), SplineLayers.Num());
}

// ---------------------------------------------------------------------------
//  OnConstruction
//  Called in the editor every time a property changes or the actor is moved.
//  We use this to keep spline altitudes in sync and refresh the debug visualization.
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    RepairSplineComponentNames();

    // Only run SortAndRebuild if every layer's spline component is actually
    // registered. OnConstruction is also called by RerunConstructionScripts,
    // which temporarily destroys dynamic (NewObject) components before
    // recreating constructor ones -- at that moment dynamic splines don't
    // exist yet and RebuildLayerCache would silently mark their caches invalid.
    // We detect this by checking that every SplineComponentName resolves.
    bool bAllSplineComponentsPresent = true;
    for (int32 i = 0; i < SplineLayers.Num(); ++i)
    {
        if (!GetLayerSpline(i))
        {
            bAllSplineComponentsPresent = false;
            UE_LOG(LogTemp, Verbose,
                TEXT("[LayeredSplineWaterRegion] '%s' OnConstruction: "
                    "Layer[%d] '%s' spline not found yet, skipping SortAndRebuild."),
                *GetName(), i, *SplineLayers[i].LayerLabel.ToString());
            break;
        }
    }

    if (bAllSplineComponentsPresent)
    {
        SortAndRebuild();
    }

#if WITH_EDITOR
    if (bDrawLayerConnections && bAllSplineComponentsPresent)
    {
        DrawLayerConnectionLines();
    }
#endif
}

// ---------------------------------------------------------------------------
//  RepairSplineComponentNames
//  If SplineComponentName is None for a layer, tries to find a matching
//  spline component by its expected name (LayerSpline_N) and fills it in.
//  This fixes old BP instances serialized before SplineComponentName existed.
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::RepairSplineComponentNames()
{
    TArray<USplineComponent*> AllSplines;
    GetComponents<USplineComponent>(AllSplines);

    for (int32 i = 0; i < SplineLayers.Num(); ++i)
    {
        if (!SplineLayers[i].SplineComponentName.IsNone()) continue;

        // Try the expected name for this index.
        const FName ExpectedName = FName(*FString::Printf(TEXT("LayerSpline_%d"), i));
        bool bFound = false;

        for (USplineComponent* S : AllSplines)
        {
            if (S && S->GetFName() == ExpectedName)
            {
                SplineLayers[i].SplineComponentName = ExpectedName;
                bFound = true;

                UE_LOG(LogTemp, Log,
                    TEXT("[LayeredSplineWaterRegion] '%s' RepairSplineComponentNames: "
                        "Layer[%d] '%s' repaired -> '%s'"),
                    *GetName(), i,
                    *SplineLayers[i].LayerLabel.ToString(),
                    *ExpectedName.ToString());
                break;
            }
        }

        if (!bFound)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[LayeredSplineWaterRegion] '%s' RepairSplineComponentNames: "
                    "Layer[%d] '%s': could not find '%s'. "
                    "The spline component may be missing. "
                    "Try deleting and re-placing the BP instance."),
                *GetName(), i,
                *SplineLayers[i].LayerLabel.ToString(),
                *ExpectedName.ToString());
        }
    }
}

// ---------------------------------------------------------------------------
//  GetLayerSpline
//  Looks up the spline component by stored name. Works for both default
//  components (created via CreateDefaultSubobject) and runtime-created ones
//  (created via NewObject + RegisterComponent).
// ---------------------------------------------------------------------------
USplineComponent* ALayeredSplineWaterRegion::GetLayerSpline(int32 LayerIndex) const
{
    if (!SplineLayers.IsValidIndex(LayerIndex)) return nullptr;

    const FName CompName = SplineLayers[LayerIndex].SplineComponentName;
    if (CompName.IsNone()) return nullptr;

    // FindComponentByTag is not appropriate here. We search by name among
    // all SplineComponents attached to this actor.
    TArray<USplineComponent*> Splines;
    GetComponents<USplineComponent>(Splines);

    for (USplineComponent* Spline : Splines)
    {
        if (Spline && Spline->GetFName() == CompName)
        {
            return Spline;
        }
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
//  BeginPlay
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::BeginPlay()
{
    SortAndRebuild();

    Super::BeginPlay();

    // Validate layer count and spline state.
    if (SplineLayers.Num() < 2)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[LayeredSplineWaterRegion] '%s' has fewer than 2 layers. ")
            TEXT("Add at least one more layer for valid evaluation."),
            *GetName());
    }

    for (int32 i = 0; i < SplineLayers.Num(); ++i)
    {
        USplineComponent* Spline = GetLayerSpline(i);
        if (!Spline)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[LayeredSplineWaterRegion] '%s' Layer[%d] '%s': spline '%s' not found. ")
                TEXT("Open the BP, find SplineLayers[%d].SplineComponentName and set it to 'LayerSpline_%d'."),
                *GetName(), i,
                *SplineLayers[i].LayerLabel.ToString(),
                *SplineLayers[i].SplineComponentName.ToString(),
                i, i);
        }
        else
        {
            UE_LOG(LogTemp, Log,
                TEXT("[LayeredSplineWaterRegion] '%s' Layer[%d] '%s': OK (Alt=%.1f, Points=%d, CacheValid=%s)"),
                *GetName(), i,
                *SplineLayers[i].LayerLabel.ToString(),
                SplineLayers[i].LayerAltitude,
                Spline->GetNumberOfSplinePoints(),
                (PolygonCaches.IsValidIndex(i) && PolygonCaches[i].bValid) ? TEXT("YES") : TEXT("NO"));
        }
    }

    // Log pairing results unconditionally (not inside the layer loop) so they
    // always appear regardless of which layer is being iterated.
    if (bLogPairing)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[LayeredSplineWaterRegion] '%s' Pairings: %d total"),
            *GetName(), LayerPairings.Num());

        for (int32 p = 0; p < LayerPairings.Num(); ++p)
        {
            const FLayerPairing& Pair = LayerPairings[p];
            UE_LOG(LogTemp, Log,
                TEXT("[LayeredSplineWaterRegion] '%s' Pairing[%d->%d]: "
                    "MostIs=%s  MostToLeast.Num=%d  ExtraVertices=%d  "
                    "LowerCacheValid=%s  UpperCacheValid=%s"),
                *GetName(), p, p + 1,
                Pair.bLowerIsMost ? TEXT("Lower") : TEXT("Upper"),
                Pair.MostToLeast.Num(),
                Pair.ExtraVertices.Num(),
                (PolygonCaches.IsValidIndex(p) && PolygonCaches[p].bValid) ? TEXT("YES") : TEXT("NO"),
                (PolygonCaches.IsValidIndex(p + 1) && PolygonCaches[p + 1].bValid) ? TEXT("YES") : TEXT("NO"));
        }
    }
}

// ---------------------------------------------------------------------------
//  EndPlay - flush persistent debug lines when actor is destroyed
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if WITH_EDITOR
    if (UWorld* World = GetWorld())
    {
        FlushPersistentDebugLines(World);
    }
#endif

    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
//  Destroyed - fires when actor deleted in editor viewport (world still valid)
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::Destroyed()
{
#if WITH_EDITOR
    // BeginDestroy fires too late -- GetWorld() returns null at that point.
    // Destroyed() fires while the actor is still fully in the world,
    // so FlushPersistentDebugLines works correctly here.
    if (UWorld* World = GetWorld())
    {
        FlushPersistentDebugLines(World);
    }
#endif

    Super::Destroyed();
}

// ---------------------------------------------------------------------------
//  CallInEditor buttons
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::AddLayerFromEditor()
{
    AddLayer(NewLayerAltitude, NewLayerLabel);
}

void ALayeredSplineWaterRegion::RemoveLayerFromEditor()
{
    RemoveLayer(LayerIndexToRemove);
}

// ---------------------------------------------------------------------------
//  AddLayer
// ---------------------------------------------------------------------------
int32 ALayeredSplineWaterRegion::AddLayer(float Altitude, FName Label)
{
    const int32 NewIndex = SplineLayers.Num();
    const FName CompName = FName(*FString::Printf(TEXT("LayerSpline_%d"), NewIndex));

#if WITH_EDITOR
    Modify();
#endif

    USplineComponent* NewSpline = NewObject<USplineComponent>(this, CompName,
        RF_Transactional);
    NewSpline->RegisterComponent();
    NewSpline->AttachToComponent(GetRootComponent(),
        FAttachmentTransformRules::KeepRelativeTransform);
    // RF_Transactional + AddInstanceComponent ensures UE5 serializes this
    // component into the level file on save. Without AddInstanceComponent,
    // the component exists at runtime but is lost when the level is reloaded.
    AddInstanceComponent(NewSpline);
    // CreationMethod = Instance enables the full spline editing UI in the editor.
    NewSpline->CreationMethod = EComponentCreationMethod::Instance;
    NewSpline->SetClosedLoop(true);
    NewSpline->ClearSplinePoints(false);
    // Z is set to 0 here; SyncSplineAltitudes will move it to Altitude after sort.
    NewSpline->AddSplineLocalPoint(FVector(2000.f, 0.f, 0.f));
    NewSpline->AddSplineLocalPoint(FVector(0.f, 2000.f, 0.f));
    NewSpline->AddSplineLocalPoint(FVector(-2000.f, 0.f, 0.f));
    NewSpline->AddSplineLocalPoint(FVector(0.f, -2000.f, 0.f));
    NewSpline->UpdateSpline();

    FSplineLayer NewInfo;
    NewInfo.LayerAltitude = Altitude;
    NewInfo.SplineComponentName = CompName;
    NewInfo.LayerLabel = Label.IsNone()
        ? FName(*FString::Printf(TEXT("Layer_%d"), NewIndex))
        : Label;

    SplineLayers.Add(NewInfo);
    SortAndRebuild();
    SyncSplineAltitudes();

    UE_LOG(LogTemp, Log,
        TEXT("[LayeredSplineWaterRegion] '%s' AddLayer: created '%s' at Alt=%.1f. "
            "Total layers: %d"),
        *GetName(), *CompName.ToString(), Altitude, SplineLayers.Num());

#if WITH_EDITOR
    // Mark the level as modified and force the component hierarchy to refresh.
    // NOTE: RerunConstructionScripts is intentionally NOT called here.
    // It destroys all components not created via CreateDefaultSubobject,
    // which would immediately destroy the LayerSpline we just created.
    MarkPackageDirty();
    if (GEngine)
    {
        GEngine->BroadcastLevelActorListChanged();
    }
#endif

    return SplineLayers.Num() - 1;
}

// ---------------------------------------------------------------------------
//  RemoveLayer
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::RemoveLayer(int32 LayerIndex)
{
    if (SplineLayers.Num() <= 2)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[LayeredSplineWaterRegion] Cannot remove layer: minimum 2 layers required."));
        return;
    }

    if (!SplineLayers.IsValidIndex(LayerIndex)) return;

    USplineComponent* Spline = GetLayerSpline(LayerIndex);
    if (Spline)
    {
        RemoveInstanceComponent(Spline);
        Spline->DestroyComponent();
    }

    SplineLayers.RemoveAt(LayerIndex);
    SortAndRebuild();
}

// ---------------------------------------------------------------------------
//  SortAndRebuild
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::SortAndRebuild()
{
    SplineLayers.Sort([](const FSplineLayer& A, const FSplineLayer& B)
        {
            return A.LayerAltitude < B.LayerAltitude;
        });

    RebuildAllCaches();
    RebuildLayerPairings();
    SyncSplineAltitudes();
}

// ---------------------------------------------------------------------------
//  SyncSplineAltitudes
//  Moves each spline component's relative Z to match its LayerAltitude so
//  the splines visually sit at the correct height in the editor viewport.
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::SyncSplineAltitudes()
{
    for (int32 i = 0; i < SplineLayers.Num(); ++i)
    {
        USplineComponent* S = GetLayerSpline(i);
        if (!S) continue;

        FVector Loc = S->GetRelativeLocation();
        const float OldZ = Loc.Z;
        Loc.Z = SplineLayers[i].LayerAltitude;
        S->SetRelativeLocation(Loc);

        if (!FMath::IsNearlyEqual(OldZ, Loc.Z, 0.1f))
        {
            UE_LOG(LogTemp, Log,
                TEXT("[LayeredSplineWaterRegion] '%s' SyncSplineAltitudes: "
                    "Layer[%d] '%s' Z: %.1f -> %.1f"),
                *GetName(), i,
                *SplineLayers[i].LayerLabel.ToString(),
                OldZ, Loc.Z);
        }
    }
}

// ---------------------------------------------------------------------------
//  RebuildAllCaches
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::RebuildAllCaches()
{
    PolygonCaches.SetNum(SplineLayers.Num());
    for (int32 i = 0; i < SplineLayers.Num(); ++i)
    {
        RebuildLayerCache(i);
    }
}

// ---------------------------------------------------------------------------
//  RebuildLayerCache
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::RebuildLayerCache(int32 Index)
{
    if (!SplineLayers.IsValidIndex(Index)) return;

    FLayerPolygonCache& Cache = PolygonCaches[Index];
    Cache.bValid = false;
    Cache.Altitude = SplineLayers[Index].LayerAltitude;
    Cache.Points.Reset();
    Cache.Centroid = FVector2D::ZeroVector;

    USplineComponent* S = GetLayerSpline(Index);
    if (!S)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[LayeredSplineWaterRegion] '%s' RebuildLayerCache[%d] '%s': "
                "spline '%s' not found -- cache stays invalid."),
            *GetName(), Index,
            *SplineLayers[Index].LayerLabel.ToString(),
            *SplineLayers[Index].SplineComponentName.ToString());
        return;
    }

    const float Len = S->GetSplineLength();
    if (Len <= 0.f)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[LayeredSplineWaterRegion] '%s' RebuildLayerCache[%d] '%s': "
                "spline length is 0 -- cache stays invalid."),
            *GetName(), Index, *SplineLayers[Index].LayerLabel.ToString());
        return;
    }

    Cache.Points.Reserve(SplineSampleCount);
    for (int32 i = 0; i < SplineSampleCount; ++i)
    {
        const float   T = (float)i / (float)SplineSampleCount;
        const FVector P = S->GetLocationAtDistanceAlongSpline(
            T * Len, ESplineCoordinateSpace::Local);
        Cache.Points.Add(FVector2D(P.X, P.Y));
    }

    Cache.bValid = (Cache.Points.Num() >= 3);
    Cache.Centroid = ComputeCentroid(Cache.Points);

    UE_LOG(LogTemp, Log,
        TEXT("[LayeredSplineWaterRegion] '%s' RebuildLayerCache[%d] '%s': "
            "Alt=%.1f  Len=%.1f  Points=%d  bValid=%s"),
        *GetName(), Index,
        *SplineLayers[Index].LayerLabel.ToString(),
        Cache.Altitude, Len, Cache.Points.Num(),
        Cache.bValid ? TEXT("YES") : TEXT("NO"));
}

// ---------------------------------------------------------------------------
//  RebuildLayerPairings
//
//  Goal: every point in BOTH layers must be represented in the interpolated
//  polygon. The interpolated polygon uses the larger layer as its base.
//
//  Algorithm:
//  Step 1 - Initial pairing (Larger -> Smaller by angle):
//    For each point in the larger layer, find the angularly closest point
//    in the smaller layer. Multiple larger points may map to the same
//    smaller point at this stage.
//
//  Step 2 - Find orphaned smaller-layer points:
//    Any smaller-layer point that has NO larger-layer point mapped to it
//    is an orphan. If orphans exist, the interpolated polygon would skip
//    regions of the smaller layer's boundary entirely.
//
//  Step 3 - Reassign to cover orphans:
//    For each orphaned smaller-layer point, find the larger-layer point
//    that is angularly closest to it AND currently maps to a non-orphaned
//    smaller point (so we are overriding a "duplicate" mapping, not the
//    only mapping). Reassign that larger-layer point to the orphan.
//    Repeat until all smaller-layer points are covered.
//    Overriden mapping can create new orphans, depending on the solution
//    activated in the settings, reassignment or new points can be done
//
//  The result: every smaller-layer point is paired with at least one
//  larger-layer point, guaranteeing full boundary coverage.
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::RebuildLayerPairings()
{
    const int32 NumLayers = PolygonCaches.Num();
    LayerPairings.SetNum(FMath::Max(0, NumLayers - 1));

    // Angular distance helper, result in [0, PI].
    auto AngDist = [](float A, float B) -> float
        {
            float D = FMath::Abs(A - B);
            if (D > PI) D = 2.f * PI - D;
            return D;
        };

    for (int32 PairIdx = 0; PairIdx < NumLayers - 1; ++PairIdx)
    {
        const FLayerPolygonCache& Lower = PolygonCaches[PairIdx];
        const FLayerPolygonCache& Upper = PolygonCaches[PairIdx + 1];
        FLayerPairing& Pairing = LayerPairings[PairIdx];
        Pairing.MostToLeast.Reset();
        Pairing.ExtraVertices.Reset();

        if (!Lower.bValid || !Upper.bValid) continue;

        // -------------------------------------------------------------------
        //  Determine MostLayer and LeastLayer.
        // -------------------------------------------------------------------
        const bool bLowerIsMost = (Lower.Points.Num() >= Upper.Points.Num());
        Pairing.bLowerIsMost = bLowerIsMost;

        const FLayerPolygonCache& Most = bLowerIsMost ? Lower : Upper;
        const FLayerPolygonCache& Least = bLowerIsMost ? Upper : Lower;

        const int32 NMost = Most.Points.Num();
        const int32 NLeast = Least.Points.Num();

        // Precompute angles for both layers.
        TArray<float> MostAngles, LeastAngles;
        MostAngles.Reserve(NMost);
        LeastAngles.Reserve(NLeast);
        for (const FVector2D& P : Most.Points)
            MostAngles.Add(AngleFromOrigin(P, Most.Centroid));
        for (const FVector2D& P : Least.Points)
            LeastAngles.Add(AngleFromOrigin(P, Least.Centroid));

        // -------------------------------------------------------------------
        //  Step 1: Initial greedy pairing Most -> Least by closest angle.
        //  Every Most point gets exactly one Least target.
        // -------------------------------------------------------------------
        Pairing.MostToLeast.SetNum(NMost);
        for (int32 i = 0; i < NMost; ++i)
        {
            int32 Best = 0;
            float BestD = FLT_MAX;
            for (int32 j = 0; j < NLeast; ++j)
            {
                const float D = AngDist(MostAngles[i], LeastAngles[j]);
                if (D < BestD) { BestD = D; Best = j; }
            }
            Pairing.MostToLeast[i] = Best;
        }

        // -------------------------------------------------------------------
        //  Step 2: Find orphaned Least points (referenced by no Most point).
        // -------------------------------------------------------------------
        TArray<int32> LeastRefCount;
        LeastRefCount.Init(0, NLeast);
        for (int32 Ref : Pairing.MostToLeast) LeastRefCount[Ref]++;

        TArray<int32> Orphans;
        for (int32 j = 0; j < NLeast; ++j)
        {
            if (LeastRefCount[j] == 0) Orphans.Add(j);
        }

        if (Orphans.Num() == 0) continue; // no orphans, pairing is complete

        // -------------------------------------------------------------------
        //  Step 3: Resolve orphans using the selected mode.
        // -------------------------------------------------------------------
        if (OrphanMode == ELayerPairingOrphanMode::DuplicateVertex)
        {
            // DuplicateVertex
            // For each orphan L1:
            //   Find M1 = angularly closest Most point to L1.
            //   If M1's current target L2 has refcount > 1:
            //     Reassign M1 from L2 to L1. Simple, no new orphan.
            //   Else (L2 would become orphan):
            //     Create a duplicate vertex M1b (same position as M1,
            //     inserted immediately after M1 in polygon order).
            //     M1b targets L1. M1 keeps targeting L2.
            //     The spline is NOT modified -- M1b exists only here.

            for (int32 OrphanJ : Orphans)
            {
                // Find closest Most point to this orphan.
                int32 M1 = 0;
                float BestD = FLT_MAX;
                for (int32 i = 0; i < NMost; ++i)
                {
                    const float D = AngDist(MostAngles[i], LeastAngles[OrphanJ]);
                    if (D < BestD) { BestD = D; M1 = i; }
                }

                const int32 L2 = Pairing.MostToLeast[M1];

                if (LeastRefCount[L2] > 1)
                {
                    // Safe reassignment: L2 still has other Most points.
                    LeastRefCount[L2]--;
                    LeastRefCount[OrphanJ]++;
                    Pairing.MostToLeast[M1] = OrphanJ;
                }
                else
                {
                    // L2 would become orphan -- create duplicate vertex M1b.
                    FExtraPairingVertex Extra;
                    Extra.InsertAfterMostIndex = M1;
                    Extra.TargetLeastIndex = OrphanJ;
                    Pairing.ExtraVertices.Add(Extra);
                    LeastRefCount[OrphanJ]++;
                }
            }
        }
        else
        {
            // CascadeReassign
            // For each orphan L1: assign M1 to L1. M1's old target L2 may
            // become orphan. Find M2 (best for L2 excluding already-reassigned
            // Most points). Continue until we find a Most point whose old
            // target Lk already has another reference (loop guaranteed to
            // terminate since NLeast < NMost).

            TArray<bool> MostReassigned;
            MostReassigned.Init(false, NMost);

            for (int32 OrphanJ : Orphans)
            {
                // Find best unblocked Most point for this orphan.
                int32 M1 = -1;
                float BestD = FLT_MAX;
                for (int32 i = 0; i < NMost; ++i)
                {
                    if (MostReassigned[i]) continue;
                    const float D = AngDist(MostAngles[i], LeastAngles[OrphanJ]);
                    if (D < BestD) { BestD = D; M1 = i; }
                }

                if (M1 < 0)
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[LayeredSplineWaterRegion]: No unblocked Most point "
                            "for orphan %d in pair [%d,%d]. Increase SplineSampleCount."),
                        OrphanJ, PairIdx, PairIdx + 1);
                    continue;
                }

                // Cascade: keep reassigning until we land on a safe target.
                int32 CurrentMost = M1;
                int32 TargetLeast = OrphanJ;

                while (true)
                {
                    const int32 OldTarget = Pairing.MostToLeast[CurrentMost];

                    LeastRefCount[OldTarget]--;
                    LeastRefCount[TargetLeast]++;
                    Pairing.MostToLeast[CurrentMost] = TargetLeast;
                    MostReassigned[CurrentMost] = true;

                    // If OldTarget still has a reference, we're done.
                    if (LeastRefCount[OldTarget] > 0) break;

                    // OldTarget became orphan -- find next Most point for it.
                    int32 NextMost = -1;
                    float BestD2 = FLT_MAX;
                    for (int32 i = 0; i < NMost; ++i)
                    {
                        if (MostReassigned[i]) continue;
                        const float D = AngDist(MostAngles[i], LeastAngles[OldTarget]);
                        if (D < BestD2) { BestD2 = D; NextMost = i; }
                    }

                    if (NextMost < 0)
                    {
                        // No more unblocked points -- safety exit.
                        UE_LOG(LogTemp, Warning,
                            TEXT("[LayeredSplineWaterRegion] cascade: "
                                "could not resolve all orphans in pair [%d,%d]."),
                            PairIdx, PairIdx + 1);
                        break;
                    }

                    CurrentMost = NextMost;
                    TargetLeast = OldTarget;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  BuildInterpolatedPolygon
//  Constructs the interpolated 2D boundary polygon at blend factor T.
//  Always iterates MostLayer's points. Extra vertices from O1 are inserted
//  immediately after their InsertAfterMostIndex position.
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::BuildInterpolatedPolygon(int32 PairIdx, float T,
    TArray<FVector2D>& OutPolygon) const
{
    if (!LayerPairings.IsValidIndex(PairIdx)) return;
    if (!PolygonCaches.IsValidIndex(PairIdx) ||
        !PolygonCaches.IsValidIndex(PairIdx + 1)) return;

    const FLayerPairing& Pairing = LayerPairings[PairIdx];
    const FLayerPolygonCache& Lower = PolygonCaches[PairIdx];
    const FLayerPolygonCache& Upper = PolygonCaches[PairIdx + 1];

    const FLayerPolygonCache& Most = Pairing.bLowerIsMost ? Lower : Upper;
    const FLayerPolygonCache& Least = Pairing.bLowerIsMost ? Upper : Lower;

    // T=0 means at Lower altitude, T=1 means at Upper altitude.
    // If LowerIsMost, Most points lerp toward Least as T increases.
    // If UpperIsMost, Most points lerp toward Least as T decreases.
    const float LerpT = Pairing.bLowerIsMost ? T : (1.f - T);

    const int32 NMost = Most.Points.Num();
    OutPolygon.Reset(NMost + Pairing.ExtraVertices.Num());

    for (int32 i = 0; i < NMost; ++i)
    {
        const int32 LeastIdx = Pairing.MostToLeast.IsValidIndex(i)
            ? Pairing.MostToLeast[i]
            : (i % Least.Points.Num());

        OutPolygon.Add(FMath::Lerp(Most.Points[i], Least.Points[LeastIdx], LerpT));

        // Insert any O1 extra vertices that follow this Most index.
        for (const FExtraPairingVertex& Extra : Pairing.ExtraVertices)
        {
            if (Extra.InsertAfterMostIndex == i &&
                Least.Points.IsValidIndex(Extra.TargetLeastIndex))
            {
                // M1b: same position as M1 in Most, lerps toward L1 in Least.
                // At T=0 (LerpT=0) this is identical to M1's position --
                // the polygon is unchanged at the MostLayer altitude.
                OutPolygon.Add(FMath::Lerp(Most.Points[i],
                    Least.Points[Extra.TargetLeastIndex], LerpT));
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  EvaluateWeight  (corrected 3D interpolated polygon approach)
// ---------------------------------------------------------------------------
float ALayeredSplineWaterRegion::EvaluateWeight(const FVector& WorldPos) const
{
    if (!RegionData || PolygonCaches.Num() < 2) return 0.f;

    const FVector    LocalPos = GetActorTransform().InverseTransformPosition(WorldPos);
    const FVector2D  LocalXY = FVector2D(LocalPos.X, LocalPos.Y);
    const float      LocalZ = LocalPos.Z;
    const float      BlendRadius = FMath::Max(RegionData->BlendRadius, 1.f);
    const int32     NumLayers = PolygonCaches.Num();

    if (bLogEvaluateWeight)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[LayeredSplineWaterRegion] '%s' EvaluateWeight: "
                "WorldPos=(%.1f,%.1f,%.1f)  LocalPos=(%.1f,%.1f,%.1f)  "
                "NumLayers=%d  AltRange=[%.1f..%.1f]  BlendRadius=%.1f"),
            *GetName(),
            WorldPos.X, WorldPos.Y, WorldPos.Z,
            LocalPos.X, LocalPos.Y, LocalPos.Z,
            NumLayers,
            PolygonCaches[0].Altitude,
            PolygonCaches[NumLayers - 1].Altitude,
            BlendRadius);
    }

    // Handle points outside the altitude range of all layers.
    if (LocalZ <= PolygonCaches[0].Altitude)
    {
        if (!bClampToOuterLayers)
        {
            if (bLogEvaluateWeight) UE_LOG(LogTemp, Log,
                TEXT("[LayeredSplineWaterRegion] '%s' -> Below lowest layer (%.1f <= %.1f), bClampToOuterLayers=false -> weight=0"),
                *GetName(), LocalZ, PolygonCaches[0].Altitude);
            return 0.f;
        }
        if (!PolygonCaches[0].bValid)
        {
            if (bLogEvaluateWeight) UE_LOG(LogTemp, Warning,
                TEXT("[LayeredSplineWaterRegion] '%s' -> Cache[0] invalid -> weight=0"),
                *GetName());
            return 0.f;
        }
        const bool bInside = IsPointInPolygon2D(PolygonCaches[0].Points, LocalXY);
        if (bLogEvaluateWeight) UE_LOG(LogTemp, Log,
            TEXT("[LayeredSplineWaterRegion] '%s' -> Clamped to Layer[0], InPolygon=%s"),
            *GetName(), bInside ? TEXT("YES") : TEXT("NO"));
        if (!bInside) return 0.f;
        const float EdgeDist = DistanceToPolygonEdge2D(PolygonCaches[0].Points, LocalXY);
        if (EdgeDist >= BlendRadius) return 1.f;
        return FMath::SmoothStep(0.f, BlendRadius, EdgeDist);
    }

    if (LocalZ >= PolygonCaches[NumLayers - 1].Altitude)
    {
        if (!bClampToOuterLayers)
        {
            if (bLogEvaluateWeight) UE_LOG(LogTemp, Log,
                TEXT("[LayeredSplineWaterRegion] '%s' -> Above highest layer (%.1f >= %.1f), bClampToOuterLayers=false -> weight=0"),
                *GetName(), LocalZ, PolygonCaches[NumLayers - 1].Altitude);
            return 0.f;
        }
        const FLayerPolygonCache& TopCache = PolygonCaches[NumLayers - 1];
        if (!TopCache.bValid)
        {
            if (bLogEvaluateWeight) UE_LOG(LogTemp, Warning,
                TEXT("[LayeredSplineWaterRegion] '%s' -> TopCache invalid -> weight=0"),
                *GetName());
            return 0.f;
        }
        const bool bInside = IsPointInPolygon2D(TopCache.Points, LocalXY);
        if (bLogEvaluateWeight) UE_LOG(LogTemp, Log,
            TEXT("[LayeredSplineWaterRegion] '%s' -> Clamped to Layer[%d], InPolygon=%s"),
            *GetName(), NumLayers - 1, bInside ? TEXT("YES") : TEXT("NO"));
        if (!bInside) return 0.f;
        const float EdgeDist = DistanceToPolygonEdge2D(TopCache.Points, LocalXY);
        if (EdgeDist >= BlendRadius) return 1.f;
        return FMath::SmoothStep(0.f, BlendRadius, EdgeDist);
    }

    // Find the bracketing layer pair.
    int32 LowerIndex = 0;
    for (int32 i = 0; i < NumLayers - 1; ++i)
    {
        if (LocalZ >= PolygonCaches[i].Altitude &&
            LocalZ < PolygonCaches[i + 1].Altitude)
        {
            LowerIndex = i;
            break;
        }
    }

    const FLayerPolygonCache& LowerCache = PolygonCaches[LowerIndex];
    const FLayerPolygonCache& UpperCache = PolygonCaches[LowerIndex + 1];

    if (!LowerCache.bValid || !UpperCache.bValid)
    {
        if (bLogEvaluateWeight) UE_LOG(LogTemp, Warning,
            TEXT("[LayeredSplineWaterRegion] '%s' -> LowerCache[%d].bValid=%s  UpperCache[%d].bValid=%s -> weight=0"),
            *GetName(), LowerIndex, LowerCache.bValid ? TEXT("Y") : TEXT("N"),
            LowerIndex + 1, UpperCache.bValid ? TEXT("Y") : TEXT("N"));
        return 0.f;
    }
    if (!LayerPairings.IsValidIndex(LowerIndex))
    {
        if (bLogEvaluateWeight) UE_LOG(LogTemp, Warning,
            TEXT("[LayeredSplineWaterRegion] '%s' -> No pairing for index %d -> weight=0"),
            *GetName(), LowerIndex);
        return 0.f;
    }

    const float AltRange = UpperCache.Altitude - LowerCache.Altitude;
    const float T = (AltRange > SMALL_NUMBER)
        ? FMath::Clamp((LocalZ - LowerCache.Altitude) / AltRange, 0.f, 1.f)
        : 0.f;

    TArray<FVector2D> InterpPolygon;
    BuildInterpolatedPolygon(LowerIndex, T, InterpPolygon);

    const bool bInside = (InterpPolygon.Num() >= 3) && IsPointInPolygon2D(InterpPolygon, LocalXY);

    if (bLogEvaluateWeight)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[LayeredSplineWaterRegion] '%s' -> BracketPair[%d,%d]  "
                "LowerAlt=%.1f  UpperAlt=%.1f  T=%.3f  "
                "InterpPolyPoints=%d  InPolygon=%s"),
            *GetName(), LowerIndex, LowerIndex + 1,
            LowerCache.Altitude, UpperCache.Altitude, T,
            InterpPolygon.Num(),
            bInside ? TEXT("YES") : TEXT("NO"));
    }

    if (!bInside) return 0.f;

    const float EdgeDist = DistanceToPolygonEdge2D(InterpPolygon, LocalXY);
    const float Weight = (EdgeDist >= BlendRadius)
        ? 1.f
        : FMath::SmoothStep(0.f, BlendRadius, EdgeDist);

    if (bLogEvaluateWeight)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[LayeredSplineWaterRegion] '%s' -> EdgeDist=%.1f  FinalWeight=%.3f"),
            *GetName(), EdgeDist, Weight);
    }

    return Weight;
}

// ---------------------------------------------------------------------------
//  GetNearestBoundaryPoint
//  Delegates to the outermost layer's spline (layer 0 = deepest/largest,
//  which defines the outer boundary of the region).
//  Iterates the layer 0 spline component directly.
// ---------------------------------------------------------------------------
bool ALayeredSplineWaterRegion::GetNearestBoundaryPoint(
    const FVector2D& QueryXY,
    FVector2D& OutBoundaryPoint,
    FVector2D& OutOutwardNormal) const
{
    // Use the outermost (first) layer spline as the boundary.
    USplineComponent* OuterSpline = GetLayerSpline(0);
    if (!OuterSpline) return false;

    const int32 NumPoints = OuterSpline->GetNumberOfSplinePoints();
    if (NumPoints < 3) return false;

    // Sample the spline into a temporary polygon and run the same
    // nearest-edge algorithm as ASplineWaterRegion.
    // We use a coarser sample than SplineSampleCount (32 points max)
    // since this is called once per slot per frame, not per pixel.
    const int32 SampleCount = FMath::Min(NumPoints, 32);
    TArray<FVector2D> Points;
    Points.Reserve(SampleCount);
    for (int32 i = 0; i < SampleCount; ++i)
    {
        const float T = (float)i / (float)SampleCount;
        const FVector WorldPt = OuterSpline->GetLocationAtTime(
            T * OuterSpline->Duration, ESplineCoordinateSpace::World);
        Points.Add(FVector2D(WorldPt.X, WorldPt.Y));
    }

    float BestDistSq = FLT_MAX;
    FVector2D BestPoint = Points[0];
    FVector2D BestNormal(1.f, 0.f);

    const int32 N = Points.Num();
    FVector2D Centroid = FVector2D::ZeroVector;
    for (const FVector2D& P : Points) Centroid += P;
    Centroid /= (float)N;

    for (int32 i = 0, j = N - 1; i < N; j = i++)
    {
        const FVector2D& Pi = Points[i];
        const FVector2D& Pj = Points[j];
        const FVector2D Edge = Pi - Pj;
        const float EdgeLenSq = Edge.SizeSquared();
        if (EdgeLenSq < 0.001f) continue;

        const float T = FMath::Clamp(
            FVector2D::DotProduct(QueryXY - Pj, Edge) / EdgeLenSq, 0.f, 1.f);
        const FVector2D Closest = Pj + T * Edge;
        const float DistSq = (QueryXY - Closest).SizeSquared();

        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            BestPoint = Closest;
            const FVector2D EdgeNorm = Edge.GetSafeNormal();
            FVector2D Candidate(-EdgeNorm.Y, EdgeNorm.X);
            if (FVector2D::DotProduct(Candidate, Closest - Centroid) < 0.f)
                Candidate = -Candidate;
            BestNormal = Candidate;
        }
    }

    OutBoundaryPoint = BestPoint;
    OutOutwardNormal = BestNormal;
    return true;
}

// ---------------------------------------------------------------------------
//  Static helpers
// ---------------------------------------------------------------------------
bool ALayeredSplineWaterRegion::IsPointInPolygon2D(const TArray<FVector2D>& Polygon,
    const FVector2D& Point)
{
    const int32 N = Polygon.Num();
    if (N < 3) return false;

    bool  bInside = false;
    int32 j = N - 1;

    for (int32 i = 0; i < N; ++i)
    {
        const FVector2D& Pi = Polygon[i];
        const FVector2D& Pj = Polygon[j];

        if (((Pi.Y > Point.Y) != (Pj.Y > Point.Y)) &&
            (Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X))
        {
            bInside = !bInside;
        }

        j = i;
    }

    return bInside;
}

float ALayeredSplineWaterRegion::DistanceToPolygonEdge2D(const TArray<FVector2D>& Polygon,
    const FVector2D& Point)
{
    const int32 N = Polygon.Num();
    float       MinDistSq = FLT_MAX;
    int32       j = N - 1;

    for (int32 i = 0; i < N; ++i)
    {
        const FVector2D& A = Polygon[j];
        const FVector2D& B = Polygon[i];
        const FVector2D  AB = B - A;
        const float      LenSq = AB.SizeSquared();

        float DistSq;
        if (LenSq < SMALL_NUMBER)
        {
            DistSq = FVector2D::DistSquared(Point, A);
        }
        else
        {
            const float     T = FMath::Clamp(
                FVector2D::DotProduct(Point - A, AB) / LenSq, 0.f, 1.f);
            const FVector2D Closest = A + T * AB;
            DistSq = FVector2D::DistSquared(Point, Closest);
        }

        MinDistSq = FMath::Min(MinDistSq, DistSq);
        j = i;
    }

    return FMath::Sqrt(MinDistSq);
}

FVector2D ALayeredSplineWaterRegion::ComputeCentroid(const TArray<FVector2D>& Pts)
{
    if (Pts.Num() == 0) return FVector2D::ZeroVector;
    FVector2D S = FVector2D::ZeroVector;
    for (const FVector2D& P : Pts) S += P;
    return S / (float)Pts.Num();
}

float ALayeredSplineWaterRegion::AngleFromOrigin(const FVector2D& P, const FVector2D& O)
{
    return FMath::Atan2((P - O).Y, (P - O).X);
}

// ---------------------------------------------------------------------------
//  BuildFillPreviewMesh
// ---------------------------------------------------------------------------
#if WITH_EDITOR
void ALayeredSplineWaterRegion::BuildFillPreviewMesh()
{
    if (!ProceduralMeshFill || PolygonCaches.Num() == 0) return;
    const FLayerPolygonCache& Cache = PolygonCaches[0];
    if (!Cache.bValid) return;

    ProceduralMeshFill->ClearAllMeshSections();

    TArray<FVector>   Verts;
    TArray<int32>     Tris;
    TArray<FVector>   Normals;
    TArray<FVector2D> UVs;
    TArray<FColor>    Colors;
    TArray<FProcMeshTangent> Tangents;

    const float Z = SplineLayers.IsValidIndex(0) ? SplineLayers[0].LayerAltitude : 0.f;
    Verts.Add(FVector(Cache.Centroid.X, Cache.Centroid.Y, Z));
    Normals.Add(FVector::UpVector);
    UVs.Add(FVector2D(0.5f, 0.5f));

    for (const FVector2D& P : Cache.Points)
    {
        Verts.Add(FVector(P.X, P.Y, Z));
        Normals.Add(FVector::UpVector);
        UVs.Add(FVector2D(P.X / 2000.f + 0.5f, P.Y / 2000.f + 0.5f));
    }

    const int32 N = Cache.Points.Num();
    for (int32 i = 1; i <= N; ++i)
    {
        Tris.Add(0); Tris.Add(i); Tris.Add((i % N) + 1);
    }

    ProceduralMeshFill->CreateMeshSection(0, Verts, Tris, Normals,
        UVs, Colors, Tangents, false);
}

// ---------------------------------------------------------------------------
//  DrawLayerConnectionLines
// ---------------------------------------------------------------------------
void ALayeredSplineWaterRegion::DrawLayerConnectionLines() const
{
    UWorld* World = GetWorld();
    if (!World) return;

    // Flush previous persistent lines from this actor so we don't accumulate
    // stale lines when the actor moves or layers change.
    FlushPersistentDebugLines(World);

    const FTransform& Tf = GetActorTransform();

    for (int32 PairIdx = 0; PairIdx < LayerPairings.Num(); ++PairIdx)
    {
        if (!PolygonCaches.IsValidIndex(PairIdx) ||
            !PolygonCaches.IsValidIndex(PairIdx + 1)) continue;

        const FLayerPairing& Pair = LayerPairings[PairIdx];
        const FLayerPolygonCache& Lower = PolygonCaches[PairIdx];
        const FLayerPolygonCache& Upper = PolygonCaches[PairIdx + 1];

        const FLayerPolygonCache& Most = Pair.bLowerIsMost ? Lower : Upper;
        const FLayerPolygonCache& Least = Pair.bLowerIsMost ? Upper : Lower;

        if (!Most.bValid || !Least.bValid) continue;

        for (int32 i = 0; i < Most.Points.Num(); ++i)
        {
            if (!Pair.MostToLeast.IsValidIndex(i)) continue;
            const int32 LeastIdx = Pair.MostToLeast[i];
            if (!Least.Points.IsValidIndex(LeastIdx)) continue;

            DrawDebugLine(World,
                Tf.TransformPosition(FVector(Most.Points[i].X, Most.Points[i].Y, Most.Altitude)),
                Tf.TransformPosition(FVector(Least.Points[LeastIdx].X, Least.Points[LeastIdx].Y, Least.Altitude)),
                FColor(180, 180, 0), /*bPersistentLines=*/true, /*LifeTime=*/-1.f, 0, 1.5f);
        }

        // Draw extra vertices from O1 in a distinct color (orange) so you can
        // see which vertices are duplicates.
        for (const FExtraPairingVertex& Extra : Pair.ExtraVertices)
        {
            if (!Most.Points.IsValidIndex(Extra.InsertAfterMostIndex)) continue;
            if (!Least.Points.IsValidIndex(Extra.TargetLeastIndex)) continue;

            DrawDebugLine(World,
                Tf.TransformPosition(FVector(
                    Most.Points[Extra.InsertAfterMostIndex].X,
                    Most.Points[Extra.InsertAfterMostIndex].Y,
                    Most.Altitude)),
                Tf.TransformPosition(FVector(
                    Least.Points[Extra.TargetLeastIndex].X,
                    Least.Points[Extra.TargetLeastIndex].Y,
                    Least.Altitude)),
                FColor(255, 120, 0), /*bPersistentLines=*/true, /*LifeTime=*/-1.f, 0, 2.f);
        }
    }
}

void ALayeredSplineWaterRegion::PostEditChangeProperty(
    FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    SortAndRebuild();
    RefreshEditorComponents();
    if (bDrawLayerConnections) DrawLayerConnectionLines();
}

void ALayeredSplineWaterRegion::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    if (bFinished)
    {
        SortAndRebuild();
        RefreshEditorComponents();
        if (bDrawLayerConnections) DrawLayerConnectionLines();
    }
}
#endif