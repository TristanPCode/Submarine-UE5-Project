// Fill out your copyright notice in the Description page of Project Settings.

#include "SpawnManagerComponent.h"
#include "RuntimeMatchSettings.h"
#include "HUDGlobalDefaults.h"
#include "SubmarineHUDComponent.h"
#include "HUDTransitionManager.h"
#include "SubmarineHUDSettings.h"
#include "SubmarineSpawnLocator.h"
#include "SubmarineCPUController.h"
#include "Submarine/SubmarinePawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

USpawnManagerComponent::USpawnManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ---------------------------------------------------------------------------
//  ResolveSpawnEntries  (Phase 1 — no spawning, just assignment)
// ---------------------------------------------------------------------------
void USpawnManagerComponent::ResolveSpawnEntries(
    URuntimeMatchSettings* RuntimeSettings)
{
    if (!RuntimeSettings)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[SpawnManager] ResolveSpawnEntries: RuntimeSettings is null."));
        return;
    }

    SpawnEntries.Empty();
    FallbackSpawnCount = 0;

    TArray<ASubmarineSpawnLocator*> Locators = CollectSortedLocators();

    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] ResolveSpawnEntries: found %d locators in level"),
        Locators.Num());

    // -----------------------------------------------------------------------
    //  Build entry list: local players first, then CPUs
    //  This ensures players get priority on preferred spawn points
    // -----------------------------------------------------------------------

    const int32 TotalPlayers = RuntimeSettings->LocalPlayerCount;
    const int32 TotalCPUs = RuntimeSettings->bCPUEnabled ? RuntimeSettings->CPUCount : 0;

    // --- Local player entries ---
    for (int32 i = 0; i < TotalPlayers; ++i)
    {
        FSpawnedSubmarineEntry Entry;
        Entry.EntryGuid = FGuid::NewGuid();
        Entry.bIsLocalPlayer = true;
        Entry.bIsRemotePlayer = false;
        Entry.bIsCPU = false;
        Entry.LocalPlayerIndex = i;
        Entry.TeamIndex = RuntimeSettings->GetPlayerTeamIndex(i);
        Entry.DisplayName = RuntimeSettings->GetPlayerName(i);
        Entry.Level = RuntimeSettings->GetPlayerLevel(i);
        Entry.SubmarineClass = RuntimeSettings->GetPlayerSubmarineClass(i);

        const int32 TeamsInPlay = RuntimeSettings->TeamCount;
        const ESpawnOccupationType OccType = Entry.bIsCPU
            ? ESpawnOccupationType::CPUOnly
            : ESpawnOccupationType::PlayerOnly;

        ASubmarineSpawnLocator* Locator = FindBestLocatorForTeam(
            Locators, Entry.TeamIndex, TeamsInPlay, OccType);
        if (!Locator)
            Locator = FindBestLocatorForTeam(
                Locators, Entry.TeamIndex, TeamsInPlay, ESpawnOccupationType::Any);

        if (Locator)
        {
            Entry.AssignedSpawnLocator = Locator;
            Locator->MarkOccupied();
            UE_LOG(LogTemp, Log,
                TEXT("[SpawnManager] Player %d -> locator '%s' (team=%d)"),
                i, *Locator->GetName(), Entry.TeamIndex);
        }
        else
        {
            Entry.FallbackSpawnTransform = BuildFallbackTransform();
            UE_LOG(LogTemp, Warning,
                TEXT("[SpawnManager] Player %d -> FALLBACK spawn (no locator available)"), i);
        }

        SpawnEntries.Add(Entry);
    }

    // --- CPU entries ---
    for (int32 i = 0; i < TotalCPUs; ++i)
    {
        FSpawnedSubmarineEntry Entry;
        Entry.EntryGuid = FGuid::NewGuid();
        Entry.bIsLocalPlayer = false;
        Entry.bIsRemotePlayer = false;
        Entry.bIsCPU = true;
        Entry.LocalPlayerIndex = -1;
        Entry.TeamIndex = RuntimeSettings->GetCPUTeamIndex(i);
        Entry.DisplayName = RuntimeSettings->GetCPUName(i);
        Entry.Level = RuntimeSettings->GetCPULevel(i);
        Entry.SubmarineClass = RuntimeSettings->GetCPUSubmarineClass(i);

        const int32 TeamsInPlay = RuntimeSettings->TeamCount;
        const ESpawnOccupationType OccType = Entry.bIsCPU
            ? ESpawnOccupationType::CPUOnly
            : ESpawnOccupationType::PlayerOnly;
        
        ASubmarineSpawnLocator* Locator = FindBestLocatorForTeam(
            Locators, Entry.TeamIndex, TeamsInPlay, OccType);
        if (!Locator)
            Locator = FindBestLocatorForTeam(
                Locators, Entry.TeamIndex, TeamsInPlay, ESpawnOccupationType::Any);

        if (Locator)
        {
            Entry.AssignedSpawnLocator = Locator;
            Locator->MarkOccupied();
            UE_LOG(LogTemp, Log,
                TEXT("[SpawnManager] CPU %d -> locator '%s' (team=%d)"),
                i, *Locator->GetName(), Entry.TeamIndex);
        }
        else
        {
            Entry.FallbackSpawnTransform = BuildFallbackTransform();
            UE_LOG(LogTemp, Warning,
                TEXT("[SpawnManager] CPU %d -> FALLBACK spawn (no locator available)"), i);
        }

        SpawnEntries.Add(Entry);
    }

    bEntriesResolved = true;
    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] ResolveSpawnEntries complete: %d entries resolved"),
        SpawnEntries.Num());
}

// ---------------------------------------------------------------------------
//  ExecuteSpawn  (Phase 2)
// ---------------------------------------------------------------------------
void USpawnManagerComponent::ExecuteSpawn(
    URuntimeMatchSettings* RuntimeSettings,
    UHUDGlobalDefaults* GlobalHUDDefaults)
{
    if (!bEntriesResolved)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[SpawnManager] ExecuteSpawn called before ResolveSpawnEntries. Aborting."));
        return;
    }

    if (!RuntimeSettings)
    {
        UE_LOG(LogTemp, Error, TEXT("[SpawnManager] ExecuteSpawn: RuntimeSettings is null."));
        return;
    }

    UWorld* World = GetWorld();
    if (!World) return;

    // -----------------------------------------------------------------------
    //  Configure split screen BEFORE creating the second local player
    // -----------------------------------------------------------------------
    const bool bNeedSplitScreen = RuntimeSettings->bSplitScreenEnabled
        && RuntimeSettings->LocalPlayerCount >= 2;
    ConfigureSplitScreen(bNeedSplitScreen);

    // -----------------------------------------------------------------------
    //  Spawn pawns and controllers
    // -----------------------------------------------------------------------
    for (FSpawnedSubmarineEntry& Entry : SpawnEntries)
    {
        if (!Entry.SubmarineClass)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[SpawnManager] ExecuteSpawn: no SubmarineClass for entry '%s'. Skipping."),
                *Entry.DisplayName);
            continue;
        }

        const FTransform SpawnTF = Entry.GetSpawnTransform();

        // Spawn the pawn (deferred so we can set properties before BeginPlay)
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        ASubmarinePawn* Pawn = World->SpawnActorDeferred<ASubmarinePawn>(
            Entry.SubmarineClass, SpawnTF, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

        if (!Pawn)
        {
            UE_LOG(LogTemp, Error,
                TEXT("[SpawnManager] ExecuteSpawn: SpawnActorDeferred failed for '%s'"),
                *Entry.DisplayName);
            continue;
        }

        Entry.SpawnedPawn = Pawn;

        AController* Controller = nullptr;

        if (Entry.bIsLocalPlayer)
        {
            // Get or create the PlayerController for this local player
            APlayerController* PC = EnsureLocalPlayer(Entry.LocalPlayerIndex);
            if (PC)
            {
                Controller = PC;
                Entry.AssignedController = PC;
            }
            else
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[SpawnManager] ExecuteSpawn: could not get PlayerController "
                        "for LocalPlayerIndex=%d"), Entry.LocalPlayerIndex);
            }
        }
        else if (Entry.bIsCPU)
        {
            // Spawn CPU controller
            ASubmarineCPUController* CPUCtrl = World->SpawnActor<ASubmarineCPUController>(
                ASubmarineCPUController::StaticClass(), SpawnTF);
            if (CPUCtrl)
            {
                Controller = CPUCtrl;
                Entry.AssignedController = CPUCtrl;
            }
        }

        // Finish deferred spawn
        Pawn->FinishSpawning(SpawnTF);

        // Wire the level from the spawn entry to the pawn
        Pawn->SubmarineLevel = Entry.Level;

        // Possess
        if (Controller)
            Controller->Possess(Pawn);

        if (Entry.bIsLocalPlayer)
        {
            const float FOV = bNeedSplitScreen
                ? RuntimeSettings->SplitScreenCameraFOV
                : RuntimeSettings->SoloCameraFOV;
            Pawn->ApplyCameraFOV(FOV);
        }

        UE_LOG(LogTemp, Log,
            TEXT("[SpawnManager] Spawned '%s' (%s) at (%.0f,%.0f,%.0f)"),
            *Entry.DisplayName,
            Entry.bIsCPU ? TEXT("CPU") : TEXT("Player"),
            SpawnTF.GetLocation().X,
            SpawnTF.GetLocation().Y,
            SpawnTF.GetLocation().Z);
    }

    // -----------------------------------------------------------------------
    //  Initialize HUD for local players AFTER all pawns are spawned
    // -----------------------------------------------------------------------
    for (FSpawnedSubmarineEntry& Entry : SpawnEntries)
    {
        if (!Entry.bIsLocalPlayer) continue;

        APlayerController* PC = Cast<APlayerController>(Entry.AssignedController.Get());
        ASubmarinePawn* Pawn = Cast<ASubmarinePawn>(Entry.SpawnedPawn.Get());
        if (!PC || !Pawn) continue;

        InitializeHUDForPlayer(PC, Pawn, RuntimeSettings, GlobalHUDDefaults,
            bNeedSplitScreen, Entry.LocalPlayerIndex);
    }

    OnAllSubmarinesSpawned.Broadcast();
    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] ExecuteSpawn complete. %d submarines in world."),
        SpawnEntries.Num());
}

// ---------------------------------------------------------------------------
//  ResetSpawnLocators
// ---------------------------------------------------------------------------
void USpawnManagerComponent::ResetSpawnLocators()
{
    for (TActorIterator<ASubmarineSpawnLocator> It(GetWorld()); It; ++It)
        It->ResetOccupation();

    bEntriesResolved = false;
    SpawnEntries.Empty();
    FallbackSpawnCount = 0;
}

// ---------------------------------------------------------------------------
//  FindEntryForPawn
// ---------------------------------------------------------------------------
const FSpawnedSubmarineEntry* USpawnManagerComponent::FindEntryForPawn(
    ASubmarinePawn* Pawn) const
{
    for (const FSpawnedSubmarineEntry& E : SpawnEntries)
        if (E.SpawnedPawn.Get() == Pawn) return &E;
    return nullptr;
}

// ---------------------------------------------------------------------------
//  FindEntryForController
// ---------------------------------------------------------------------------
const FSpawnedSubmarineEntry* USpawnManagerComponent::FindEntryForController(
    AController* Controller) const
{
    for (const FSpawnedSubmarineEntry& E : SpawnEntries)
        if (E.AssignedController.Get() == Controller) return &E;
    return nullptr;
}

// ---------------------------------------------------------------------------
//  CollectSortedLocators
// ---------------------------------------------------------------------------
TArray<ASubmarineSpawnLocator*> USpawnManagerComponent::CollectSortedLocators() const
{
    TArray<ASubmarineSpawnLocator*> Result;
    for (TActorIterator<ASubmarineSpawnLocator> It(GetWorld()); It; ++It)
        if (IsValid(*It) && !It->bOccupied)
            Result.Add(*It);

    Result.Sort([](const ASubmarineSpawnLocator& A, const ASubmarineSpawnLocator& B)
        {
            return A.SpawnPriority < B.SpawnPriority;
        });

    return Result;
}

// ---------------------------------------------------------------------------
//  FindBestLocatorForTeam
//
//  Resolution order:
//    Pass 1: locators whose GroupIndex is in the team's allowed group list
//            (resolved from UTeamSpawnSettings based on TeamsInPlay)
//            Tried in group-priority order, then SpawnPriority within each group.
//    Pass 2: locators with bAnyTeam = true
//            Tried in SpawnPriority order.
//    Returns nullptr if nothing found (triggers fallback spawn).
// ---------------------------------------------------------------------------
ASubmarineSpawnLocator* USpawnManagerComponent::FindBestLocatorForTeam(
    TArray<ASubmarineSpawnLocator*>& Locators,
    int32 TeamIndex,
    int32 TeamsInPlay,
    ESpawnOccupationType OccupationType) const
{
    // -----------------------------------------------------------------------
    //  Pass 1: Group-matched locators, iterated by priority tier
    // -----------------------------------------------------------------------
    const FTeamLocatorGroupMapping* Mapping = TeamSpawnSettings
        ? TeamSpawnSettings->FindMapping(TeamIndex, TeamsInPlay) : nullptr;

    TArray<int32> GroupList;
    TArray<int32> TierList;

    if (Mapping)
    {
        GroupList = Mapping->LocatorGroups;
        TierList.SetNumZeroed(GroupList.Num());
        for (int32 i = 0; i < GroupList.Num(); ++i)
            TierList[i] = Mapping->GetGroupPriority(i);
    }
    else
    {
        // No mapping -- home group only, tier 0
        GroupList.Add(TeamIndex);
        TierList.Add(0);
    }

    // Find the minimum tier value present
    int32 MinTier = INT_MAX;
    for (int32 T : TierList)
        MinTier = FMath::Min(MinTier, T);

    // Iterate tier by tier from lowest (highest priority) upward
    TSet<int32> VisitedTiers;
    for (int32 CurrentTier = MinTier; ; )
    {
        if (VisitedTiers.Contains(CurrentTier)) break;
        VisitedTiers.Add(CurrentTier);

        // Collect all groups in this tier
        TArray<int32> TierGroups;
        for (int32 i = 0; i < GroupList.Num(); ++i)
            if (TierList[i] == CurrentTier)
                TierGroups.Add(GroupList[i]);

        if (TierGroups.Num() > 0)
        {
            // Collect all valid candidates within this tier's groups
            // Locators array is already sorted by SpawnPriority ascending
            ASubmarineSpawnLocator* Found = PickFromGroups(
                Locators, TierGroups, OccupationType);
            if (Found) return Found;
        }

        // Advance to next tier
        int32 NextTier = INT_MAX;
        for (int32 T : TierList)
            if (T > CurrentTier)
                NextTier = FMath::Min(NextTier, T);
        if (NextTier == INT_MAX) break;
        CurrentTier = NextTier;
    }

    // -----------------------------------------------------------------------
    //  Pass 2: bAnyTeam locators
    // -----------------------------------------------------------------------
    TArray<ASubmarineSpawnLocator*> AnyTeamCandidates;
    for (ASubmarineSpawnLocator* L : Locators)
    {
        if (IsValid(L) && !L->bOccupied && L->bAnyTeam && L->CanAccept(OccupationType))
            AnyTeamCandidates.Add(L);
    }

    if (AnyTeamCandidates.Num() == 0) return nullptr;

    if (!bRandomizeEqualPriority) return AnyTeamCandidates[0];

    // Random among lowest SpawnPriority tier in any-team candidates
    const int32 LowestPriority = AnyTeamCandidates[0]->SpawnPriority;  // sorted
    TArray<ASubmarineSpawnLocator*> Tied;
    for (ASubmarineSpawnLocator* L : AnyTeamCandidates)
        if (L->SpawnPriority == LowestPriority) Tied.Add(L);
    return Tied[FMath::RandRange(0, Tied.Num() - 1)];
}

// ---------------------------------------------------------------------------
//  BuildFallbackTransform
// ---------------------------------------------------------------------------
FTransform USpawnManagerComponent::BuildFallbackTransform()
{
    const FVector Offset = FVector(FallbackSpawnStep * (FallbackSpawnCount + 1), 0.f, 0.f);
    ++FallbackSpawnCount;
    return FTransform(FRotator::ZeroRotator, FallbackSpawnOrigin + Offset);
}

// ---------------------------------------------------------------------------
//  ConfigureSplitScreen
// ---------------------------------------------------------------------------
void USpawnManagerComponent::ConfigureSplitScreen(bool bEnable)
{
    UGameViewportClient* Viewport = GetWorld()
        ? GetWorld()->GetGameViewport() : nullptr;
    if (!Viewport) return;

    if (bEnable)
    {
        // Force vertical (left/right) split — ESplitScreenType::TwoPlayer_Vertical
        // UE5 stores split info in SplitscreenInfo array indexed by ESplitScreenType
        // We set the active type directly:
        Viewport->SetForceDisableSplitscreen(false);

        // The vertical split layout is configured via project viewport settings.
        // At runtime we ensure it's not force-disabled.
        UE_LOG(LogTemp, Log,
            TEXT("[SpawnManager] Split screen enabled (vertical layout)"));
    }
    else
    {
        Viewport->SetForceDisableSplitscreen(true);
        UE_LOG(LogTemp, Log, TEXT("[SpawnManager] Split screen disabled"));
    }
}

// ---------------------------------------------------------------------------
//  EnsureLocalPlayer
// ---------------------------------------------------------------------------
APlayerController* USpawnManagerComponent::EnsureLocalPlayer(
    int32 LocalPlayerIndex)
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    UGameInstance* GI = World->GetGameInstance();
    if (!GI) return nullptr;

    // Player 0 always exists
    if (LocalPlayerIndex == 0)
    {
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = Cast<APlayerController>(It->Get());
            if (PC && PC->GetLocalPlayer()
                && PC->GetLocalPlayer()->GetControllerId() == 0)
                return PC;
        }
        // Fallback: first player controller
        return World->GetFirstPlayerController();
    }

    // Player 1+ — may need to create
    // First check if it already exists
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = Cast<APlayerController>(It->Get());
        if (PC && PC->GetLocalPlayer()
            && PC->GetLocalPlayer()->GetControllerId() == LocalPlayerIndex)
            return PC;
    }

    // Create the local player
    FString Error;
    ULocalPlayer* NewPlayer = GI->CreateLocalPlayer(LocalPlayerIndex, Error, true);
    if (!NewPlayer)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[SpawnManager] EnsureLocalPlayer: CreateLocalPlayer(%d) failed: %s"),
            LocalPlayerIndex, *Error);
        return nullptr;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] Created LocalPlayer index=%d"), LocalPlayerIndex);

    // Return the new PlayerController
    return Cast<APlayerController>(NewPlayer->GetPlayerController(World));
}

// ---------------------------------------------------------------------------
//  InitializeHUDForPlayer
// ---------------------------------------------------------------------------
void USpawnManagerComponent::InitializeHUDForPlayer(
    APlayerController* PC,
    ASubmarinePawn* TrackedPawn,
    URuntimeMatchSettings* RuntimeSettings,
    UHUDGlobalDefaults* GlobalHUDDefaults,
    bool bSplitScreen, int32 LocalPlayerIndex)
{
    if (!PC) return;

    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] InitializeHUDForPlayer START: PC='%s'  "
            "LocalPlayerIndex=%d  TrackedPawn='%s'  SplitScreen=%d"),
        *PC->GetName(), LocalPlayerIndex,
        TrackedPawn ? *TrackedPawn->GetName() : TEXT("NULL"),
        bSplitScreen ? 1 : 0);

    // -----------------------------------------------------------------------
    //  Input device assignment -- ALWAYS runs, regardless of HUD path
    // -----------------------------------------------------------------------
    ASubmarinePlayerController* SubPC = Cast<ASubmarinePlayerController>(PC);
    if (SubPC && RuntimeSettings)
    {
        EInputDeviceType DeviceType = EInputDeviceType::Keyboard;
        for (const FPlayerInputMapping& Mapping : RuntimeSettings->InputMappings)
        {
            if (Mapping.LocalPlayerIndex == LocalPlayerIndex)
            {
                DeviceType = Mapping.DeviceType;
                break;
            }
        }

        // Defer one tick -- LocalPlayer may not be fully bound at spawn time
        TWeakObjectPtr<ASubmarinePlayerController> WeakSubPC(SubPC);
        EInputDeviceType CapturedDevice = DeviceType;
        UInputMappingContext* CapturedKeyIMC = KeyboardMappingContext;
        UInputMappingContext* CapturedPadIMC = GamepadMappingContext;
        GetWorld()->GetTimerManager().SetTimerForNextTick(
            [WeakSubPC, CapturedDevice, CapturedKeyIMC, CapturedPadIMC]()
            {
                if (!WeakSubPC.IsValid()) return;
                WeakSubPC->AssignInputDevice(CapturedDevice, CapturedKeyIMC, CapturedPadIMC);
                UE_LOG(LogTemp, Log,
                    TEXT("[SpawnManager] Input device assigned (deferred): "
                        "PC='%s'  Device=%d  KeyIMC=%s  PadIMC=%s"),
                    *WeakSubPC->GetName(), (int32)CapturedDevice,
                    CapturedKeyIMC ? *CapturedKeyIMC->GetName() : TEXT("NULL"),
                    CapturedPadIMC ? *CapturedPadIMC->GetName() : TEXT("NULL"));
            });
    }
    else if (!SubPC)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SpawnManager] PC='%s' is NOT ASubmarinePlayerController -- "
                "device assignment skipped. Reparent BP_PlayerController."),
            *PC->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SpawnManager] LocalPlayerIndex=%d has no matching InputMapping entry "
                "in RuntimeMatchSettings->InputMappings -- defaulting to Keyboard."),
            LocalPlayerIndex);
    }

    // -----------------------------------------------------------------------
    // Ensure SubmarineHUDComponent exists on this PlayerController
    // -----------------------------------------------------------------------
    USubmarineHUDComponent* HUDComp =
        PC->FindComponentByClass<USubmarineHUDComponent>();

    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] InitializeHUDForPlayer components: PC='%s'  "
            "LocalPlayerIndex=%d  HUDComp=%s  TrackedPawn=%s"),
        *PC->GetName(), LocalPlayerIndex,
        HUDComp ? TEXT("OK") : TEXT("MISSING -- check BP_PlayerController"),
        TrackedPawn ? *TrackedPawn->GetName() : TEXT("NULL"));

    if (!HUDComp)
    {
        // SpawnManagerComponent should not add components to PlayerController
        // at runtime — this is a setup error. Log it.
        UE_LOG(LogTemp, Warning,
            TEXT("[SpawnManager] InitializeHUDForPlayer: PC '%s' has no "
                "USubmarineHUDComponent. Add it to your BP_PlayerController."),
            *PC->GetName());
        return;
    }

    // Ensure HUDTransitionManager exists
    // WeakTrans declared here so both the fallback path and full path can capture it.
    UHUDTransitionManager* TransMgr =
        PC->FindComponentByClass<UHUDTransitionManager>();
    TWeakObjectPtr<UHUDTransitionManager> WeakTrans(TransMgr);

    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] InitializeHUDForPlayer: PC='%s'  LocalPlayerIndex=%d  "
            "TransMgr=%s  RuntimeSettings=%s  GlobalDefaults=%s"),
        *PC->GetName(), LocalPlayerIndex,
        TransMgr ? TEXT("OK") : TEXT("MISSING -- using fallback path"),
        RuntimeSettings ? TEXT("OK") : TEXT("NULL"),
        GlobalHUDDefaults ? TEXT("OK") : TEXT("NULL"));

    if (!TransMgr)
    {
        // HUDTransitionManager not added to BP_PlayerController yet.
        // Fall back: wait one frame for HUDComponent::BeginPlay to complete,
        // then call SetTrackedSubmarine directly. This is the safe interim path.
        UE_LOG(LogTemp, Warning,
            TEXT("[SpawnManager] InitializeHUDForPlayer: PC '%s' has no "
                "UHUDTransitionManager. Add it to BP_PlayerController for full "
                "HUD context system. Using direct SetTrackedSubmarine fallback."),
            *PC->GetName());

        TWeakObjectPtr<USubmarineHUDComponent> WeakHUD(HUDComp);
        TWeakObjectPtr<ASubmarinePawn>         WeakPawn(TrackedPawn);
        TWeakObjectPtr<URuntimeMatchSettings>  WeakRMS(RuntimeSettings);
        TWeakObjectPtr<UHUDGlobalDefaults>     WeakGlobals(GlobalHUDDefaults);
        bool bSplit = bSplitScreen;


        const EHUDContext Context = bSplit
            ? EHUDContext::Gameplay_Splitscreen
            : EHUDContext::Gameplay;

        UE_LOG(LogTemp, Log,
            TEXT("[SpawnManager] Fallback HUD path: queuing deferred tick for "
                "PC='%s'  LocalPlayerIndex=%d  Context=%d"),
            *PC->GetName(), LocalPlayerIndex, (int32)Context);

        int32 CapturedIndex = LocalPlayerIndex;

        // One-frame delay: HUDComponent::BeginPlay fires in the same frame as
        // ExecuteSpawn. We need to wait for it to complete so the widget exists.
        GetWorld()->GetTimerManager().SetTimerForNextTick(
            [WeakHUD, WeakPawn, WeakRMS, WeakGlobals, bSplit, Context, CapturedIndex]()
            {
                if (!WeakHUD.IsValid() || !WeakPawn.IsValid()) return;

                if (WeakRMS.IsValid() && WeakGlobals.IsValid())
                {
                    USubmarineHUDSettings* Settings =
                        WeakRMS->ResolveHUDSettings(Context, WeakGlobals.Get());

                    UE_LOG(LogTemp, Log,
                        TEXT("[SpawnManager] Fallback HUD ResolveHUDSettings: "
                            "LocalPlayerIndex=%d  Context=%d  Settings=%s"),
                        CapturedIndex, (int32)Context,
                        Settings ? *Settings->GetName() : TEXT("NULL -- check HUDContextOverrides/GlobalDefaults"));

                    if (Settings)
                        WeakHUD->SwapHUDSettings(Settings);
                }
                else
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[SpawnManager] Fallback HUD: RMS=%s  Globals=%s -- "
                            "cannot resolve HUD settings for LocalPlayerIndex=%d"),
                        WeakRMS.IsValid() ? TEXT("valid") : TEXT("GONE"),
                        WeakGlobals.IsValid() ? TEXT("valid") : TEXT("GONE"),
                        CapturedIndex);
                }

                WeakHUD->SetTrackedSubmarine(WeakPawn.Get());

                UE_LOG(LogTemp, Log,
                    TEXT("[SpawnManager] Fallback HUD init complete: "
                        "LocalPlayerIndex=%d  tracked='%s'"),
                    CapturedIndex, *WeakPawn->GetName());
            });
        return;
    }

    // -----------------------------------------------------------------------
    // Full path: HUDTransitionManager present
    // -----------------------------------------------------------------------
    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] Full HUD path: PC='%s'  LocalPlayerIndex=%d  "
            "TransMgr=OK  RuntimeSettings=%s  GlobalDefaults=%s"),
        *PC->GetName(), LocalPlayerIndex,
        RuntimeSettings ? TEXT("OK") : TEXT("NULL"),
        GlobalHUDDefaults ? TEXT("OK") : TEXT("NULL"));

    // Full path: HUDTransitionManager present
    TransMgr->SetRuntimeSettings(RuntimeSettings, GlobalHUDDefaults);

    // Transition to gameplay context
    const EHUDContext Context = bSplitScreen
        ? EHUDContext::Gameplay_Splitscreen
        : EHUDContext::Gameplay;

    // One-frame delay here too -- same reason: wait for BeginPlay to complete
    TWeakObjectPtr<ASubmarinePawn>        WeakPawn(TrackedPawn);
    EHUDContext Ctx = Context;

    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] Full HUD path: queuing deferred tick for "
            "PC='%s'  LocalPlayerIndex=%d  Context=%d"),
        *PC->GetName(), LocalPlayerIndex, (int32)Ctx);


    GetWorld()->GetTimerManager().SetTimerForNextTick(
        [WeakTrans, WeakPawn, Ctx]()
        {
            if (WeakTrans.IsValid() && WeakPawn.IsValid())
                WeakTrans->TransitionToContext(Ctx, WeakPawn.Get());
        });

    UE_LOG(LogTemp, Log,
        TEXT("[SpawnManager] HUD init queued for PC='%s'  Context=%s  Tracked='%s'"),
        *PC->GetName(),
        bSplitScreen ? TEXT("Gameplay_Splitscreen") : TEXT("Gameplay"),
        *TrackedPawn->GetName());
}

// ---------------------------------------------------------------------------
//  GetSpawnTransform
// ---------------------------------------------------------------------------

FTransform FSpawnedSubmarineEntry::GetSpawnTransform() const
{
    if (AssignedSpawnLocator.IsValid())
        return AssignedSpawnLocator->GetSpawnTransform();
    return FallbackSpawnTransform;
}

// ---------------------------------------------------------------------------
//  PickFromGroups helper
// ---------------------------------------------------------------------------
ASubmarineSpawnLocator* USpawnManagerComponent::PickFromGroups(
    TArray<ASubmarineSpawnLocator*>& Locators,
    const TArray<int32>& Groups,
    ESpawnOccupationType OccupationType) const
{
    // Collect all valid candidates across the given groups
    // Locators is pre-sorted by SpawnPriority ascending
    TArray<ASubmarineSpawnLocator*> Candidates;
    for (ASubmarineSpawnLocator* L : Locators)
    {
        if (!IsValid(L) || L->bOccupied || L->bAnyTeam) continue;
        if (!Groups.Contains(L->GroupIndex)) continue;
        if (!L->CanAccept(OccupationType)) continue;
        Candidates.Add(L);
    }

    if (Candidates.IsEmpty()) return nullptr;

    if (!bRandomizeEqualPriority) return Candidates[0];

    // Pick randomly among all candidates sharing the lowest SpawnPriority
    const int32 LowestPriority = Candidates[0]->SpawnPriority;
    TArray<ASubmarineSpawnLocator*> Tied;
    for (ASubmarineSpawnLocator* L : Candidates)
        if (L->SpawnPriority == LowestPriority) Tied.Add(L);

    return Tied[FMath::RandRange(0, Tied.Num() - 1)];
}