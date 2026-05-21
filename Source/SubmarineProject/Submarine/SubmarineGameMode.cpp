// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineGameMode.h"
#include "SubmarinePawn.h"
#include "SubmarineCollisionComponent.h"
#include "SubmarineSpectatorPawn.h"
#include "TorpedoPawn.h"
#include "ReplayRecorderComponent.h"
#include "ReplayPlaybackComponent.h"
#include "ReplayData.h"
#include "ScreenFadeComponent.h"
#include "CameraBlendSettings.h"
#include "SpectatorTrackerComponent.h"
#include "SpawnManagerComponent.h"
#include "RuntimeMatchSettings.h"
#include "HUDGlobalDefaults.h"
#include "HUDTransitionManager.h"
#include "Load/SubmarineAssetLoader.h"
#include "Load/SubmarineLoadingScreen.h"
#include "Load/LoadingScreenSettings.h"
#include "Billboard/SubmarineInfoBillboardComponent.h"
#include "Billboard/InfoBillboardContextSettings.h"
#include "Billboard/BillboardDisplayComponent.h"
#include "SubmarineGameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

ASubmarineGameMode::ASubmarineGameMode()
{
    // Prevent UE5 from auto-spawning a pawn at PlayerStart actors.
    DefaultPawnClass = nullptr;

    ReplayRecorder = CreateDefaultSubobject<UReplayRecorderComponent>(TEXT("ReplayRecorder"));
    ReplayPlayback = CreateDefaultSubobject<UReplayPlaybackComponent>(TEXT("ReplayPlayback"));
    DeathSequence = CreateDefaultSubobject<UDeathSequenceComponent>(TEXT("DeathSequence"));
    SpawnManager = CreateDefaultSubobject<USpawnManagerComponent>(TEXT("SpawnManager"));
}

// ---------------------------------------------------------------------------
//  GetPlayerFade  (helper -- finds ScreenFadeComponent on a PlayerController)
// ---------------------------------------------------------------------------
static UScreenFadeComponent* GetPlayerFade(APlayerController* PC)
{
    if (!PC) return nullptr;
    return PC->FindComponentByClass<UScreenFadeComponent>();
}

// ---------------------------------------------------------------------------
//  BeginPlay — wire delegates and propagate settings
// ---------------------------------------------------------------------------
void ASubmarineGameMode::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("[GameMode] BeginPlay START"));

    // --- Bind death sequence delegate ---
    if (DeathSequence)
    {
        DeathSequence->OnDeathSequenceComplete.RemoveDynamic(
            this, &ASubmarineGameMode::OnDeathSequenceComplete);
        DeathSequence->OnDeathSequenceComplete.AddDynamic(
            this, &ASubmarineGameMode::OnDeathSequenceComplete);
        UE_LOG(LogTemp, Log, TEXT("[GameMode] DeathSequence delegate bound OK"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] DeathSequence component is NULL!"));
    }

    // Resolve runtime match settings
    USubmarineGameInstance* SGI =
        Cast<USubmarineGameInstance>(GetGameInstance());

    if (SGI && SGI->GetRuntimeMatchSettings())
    {
        ActiveRMS = SGI->GetRuntimeMatchSettings();
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Using RuntimeMatchSettings from GameInstance"));
    }
    else
    {
        ActiveRMS = URuntimeMatchSettings::CreateFromDataAsset(this, DefaultMatchSettings);
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Created RuntimeMatchSettings from DefaultMatchSettings DA"));
    }

    if (!ActiveRMS)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[GameMode] BeginPlay: no RuntimeMatchSettings available. "
                "Assign DefaultMatchSettings in the GameMode Blueprint."));
        return;
    }

    // Check if we should enter replay mode instead of normal gameplay
    if (SGI && SGI->bStartInReplayMode)
    {
        SGI->bStartInReplayMode = false;
        bIsReplayMode = true;
    }

    // bIsReplayMode can also be set directly as a UPROPERTY on this GameMode
    // (useful for testing without a menu -- set it in BP_SubmarineGameMode defaults)
    if (bIsReplayMode)
    {
        EnterReplayMode();
        return;
    }

    // Show loading screen FIRST -- block everything visually
    ShowLoadingScreen();

    // Preload assets
    USubmarineAssetLoader* Loader = SGI
        ? SGI->GetSubsystem<USubmarineAssetLoader>() : nullptr;

    if (Loader)
    {
        // Inject global HUD defaults so ALL contexts are preloaded
        if (HUDGlobalDefaults)
            Loader->AddGlobalHUDDefaults(HUDGlobalDefaults);

        FOnPreloadComplete Callback;
        Callback.BindDynamic(this, &ASubmarineGameMode::OnAssetsPreloaded);
        Loader->PreloadMatchAssets(ActiveRMS, Callback);
        UE_LOG(LogTemp, Log, TEXT("[GameMode] Asset preload started..."));
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[GameMode] No USubmarineAssetLoader found. "
                "Ensure Project Settings -> Maps & Modes -> Game Instance Class "
                "is set to BP_SubmarineGameInstance. Spawning immediately."));
        OnAssetsPreloaded();
    }

    if (ReplayRecorder)
    {
        ReplayRecorder->StartRecording();
        UE_LOG(LogTemp, Log, TEXT("[GameMode] Recording started."));
    }
}

// ---------------------------------------------------------------------------
//  ShowLoadingScreen
// ---------------------------------------------------------------------------
void ASubmarineGameMode::ShowLoadingScreen()
{
    if (!LoadingScreenClass)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] ShowLoadingScreen: no LoadingScreenClass set -- skipping. "
                "Assign BP_LoadingScreen to the GameMode if you want a loading screen."));
        return;
    }

    LoadingScreenStartTime = GetWorld()->GetTimeSeconds();

    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return;

    ActiveLoadingScreen = CreateWidget<USubmarineLoadingScreen>(PC, LoadingScreenClass);
    if (!ActiveLoadingScreen) return;

    ActiveLoadingScreen->ApplySettings(LoadingScreenSettings);
    ActiveLoadingScreen->AddToPlayerScreen(100);

    // Tick progress bar
    GetWorld()->GetTimerManager().SetTimer(
        LoadingProgressTimer,
        this, &ASubmarineGameMode::TickLoadingProgress,
        0.05f, true);

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Loading screen shown"));
}

// ---------------------------------------------------------------------------
//  TickLoadingProgress
// ---------------------------------------------------------------------------
void ASubmarineGameMode::TickLoadingProgress()
{
    if (!ActiveLoadingScreen) return;

    USubmarineGameInstance* SGI = Cast<USubmarineGameInstance>(GetGameInstance());
    USubmarineAssetLoader* Loader = SGI
        ? SGI->GetSubsystem<USubmarineAssetLoader>() : nullptr;

    const float AssetRatio = Loader ? Loader->GetLoadProgress() : 1.f;

    // Time ratio: how far through MinLoadingScreenTime we are
    float TimeRatio = 1.f;
    if (MinLoadingScreenTime > 0.f)
    {
        const float Elapsed = GetWorld()->GetTimeSeconds() - LoadingScreenStartTime;
        TimeRatio = FMath::Clamp(Elapsed / MinLoadingScreenTime, 0.f, 1.f);
    }

    const float DisplayProgress = FMath::Min(AssetRatio, TimeRatio);
    ActiveLoadingScreen->SetProgress(DisplayProgress);
}

// ---------------------------------------------------------------------------
//  HideLoadingScreen
// ---------------------------------------------------------------------------
void ASubmarineGameMode::HideLoadingScreen()
{
    GetWorld()->GetTimerManager().ClearTimer(LoadingProgressTimer);

    if (ActiveLoadingScreen)
    {
        // Snap camera to black BEFORE removing the loading screen.
        // This ensures the camera is black on the exact same frame the
        // loading screen widget disappears, preventing a 1-frame world flash.
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = Cast<APlayerController>(It->Get());
            if (PC && PC->IsLocalPlayerController())
                PC->ClientSetCameraFade(true, FColor::Black, FVector2D(0.f, 0.f), 0.f, true);
        }
        ActiveLoadingScreen->RemoveFromParent();
        ActiveLoadingScreen = nullptr;
        UE_LOG(LogTemp, Log, TEXT("[GameMode] Loading screen hidden, camera snapped to black"));
    }
}

// ---------------------------------------------------------------------------
//  OnAssetsPreloaded and ExecutePostLoad
// ---------------------------------------------------------------------------

void ASubmarineGameMode::OnAssetsPreloaded()
{
    UE_LOG(LogTemp, Log, TEXT("[GameMode] Assets preloaded. Waiting for minimum screen time..."));

    const float TimeElapsed = GetWorld()->GetTimeSeconds() - LoadingScreenStartTime;
    const float RemainingWait = FMath::Max(0.f, MinLoadingScreenTime - TimeElapsed);

    if (RemainingWait <= 0.f)
    {
        ExecutePostLoad();
        return;
    }

    FTimerHandle MinTimeTimer;
    GetWorld()->GetTimerManager().SetTimer(
        MinTimeTimer,
        this, &ASubmarineGameMode::ExecutePostLoad,
        RemainingWait, false);
}

void ASubmarineGameMode::ExecutePostLoad()
{
    HideLoadingScreen();
    if (!SpawnManager || !ActiveRMS) return;
    SpawnManager->ResolveSpawnEntries(ActiveRMS);
    SpawnManager->ExecuteSpawn(ActiveRMS, HUDGlobalDefaults);

    // Initialize billboard data on each submarine pawn.
    // BillboardDisplayComponent on each PC handles all rendering.
    if (HUDGlobalDefaults)
    {
        UInfoBillboardContextSettings* BillboardCtx =
            HUDGlobalDefaults->ResolveBillboard(EHUDContext::Gameplay);

        // Step 1: set identity data on every submarine's billboard component
        for (const FSpawnedSubmarineEntry& Entry : SpawnManager->GetSpawnEntries())
        {
            if (!Entry.SpawnedPawn.IsValid()) continue;
            ASubmarinePawn* Pawn = Entry.SpawnedPawn.Get();

            USubmarineInfoBillboardComponent* Billboard =
                Pawn->FindComponentByClass<USubmarineInfoBillboardComponent>();
            if (!Billboard) continue;

            const FMatchTeamSettings& TeamSettings =
                ActiveRMS->GetTeamSettings(Entry.TeamIndex);

            Billboard->InitializeBillboard(
                EBillboardEntityType::Submarine,
                Entry.TeamIndex,
                Entry.bIsCPU,
                TEXT(""));
            Billboard->EntityTeamName = TeamSettings.TeamName;
            Billboard->EntityDisplayName = Entry.DisplayName;

            UE_LOG(LogTemp, Log,
                TEXT("[BB:Init] Sub='%s'  Team=%d  CPU=%d  DisplayName='%s'"),
                *Pawn->GetName(), Entry.TeamIndex, Entry.bIsCPU ? 1 : 0,
                *Entry.DisplayName);
        }

        // Step 2: give each local PC its own BillboardDisplayComponent context.
        // This is what actually creates and positions the per-player widgets.
        for (const FSpawnedSubmarineEntry& E2 : SpawnManager->GetSpawnEntries())
        {
            if (!E2.bIsLocalPlayer) continue;

            // Find this player's PC
            APlayerController* PC = nullptr;
            if (UGameInstance* GI = GetGameInstance())
            {
                const TArray<ULocalPlayer*>& LPs = GI->GetLocalPlayers();
                if (LPs.IsValidIndex(E2.LocalPlayerIndex))
                    PC = LPs[E2.LocalPlayerIndex]->GetPlayerController(GetWorld());
            }
            if (!PC) continue;

            UBillboardDisplayComponent* DisplayComp =
                PC->FindComponentByClass<UBillboardDisplayComponent>();
            if (!DisplayComp)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[BB:Init] No BillboardDisplayComponent on PC='%s' -- "
                        "add it to BP_PlayerController and set WidgetClass"),
                    *PC->GetName());
                continue;
            }

            // Collect all BBCs from known spawn entries (no world scan needed)
            TArray<USubmarineInfoBillboardComponent*> Sources;
            for (const FSpawnedSubmarineEntry& SE : SpawnManager->GetSpawnEntries())
            {
                if (!SE.SpawnedPawn.IsValid()) continue;
                USubmarineInfoBillboardComponent* BBC =
                    SE.SpawnedPawn->FindComponentByClass<USubmarineInfoBillboardComponent>();
                if (BBC) Sources.Add(BBC);
            }

            DisplayComp->SetBillboardContextWithSources(BillboardCtx, E2.TeamIndex, ActiveRMS, Sources);
            UE_LOG(LogTemp, Log,
                TEXT("[BB:Init] PC='%s'  P%d  Team=%d  Ctx=%s"),
                *PC->GetName(), E2.LocalPlayerIndex, E2.TeamIndex,
                BillboardCtx ? *BillboardCtx->GetName() : TEXT("NULL"));
        }
    }

    // Fade initialiaztion and Fade-In Gameplay
    if (ReplaySettings && ReplaySettings->FadeLibrary.Num() > 0)
    {
        // Step 1 (immediate): init fade library and set HUD to 0 NOW.
        // The HUD widget doesn't exist yet but we pre-zero any existing widget
        // and ensure the fade library is populated before the deferred call.
        bool bFadeGameplayExist = false;
        bool bFadeGamplay = false;
        bool bFadeHUDDarkness = false;
        float HUDFadeInDuration = 0.f;
        float HUDFadeStartTime = 0.f;
        UWorld* W = GetWorld();

        for (FConstPlayerControllerIterator ItFade = GetWorld()->GetPlayerControllerIterator();
            ItFade; ++ItFade)
        {
            APlayerController* PC = Cast<APlayerController>(ItFade->Get());
            if (!PC || !PC->IsLocalPlayerController()) continue;
            if (UHUDTransitionManager* TransMgr = PC->FindComponentByClass<UHUDTransitionManager>())
                TransMgr->SetHUDOpacity(0.f);
            if (UScreenFadeComponent* FC = PC->FindComponentByClass<UScreenFadeComponent>())
                FC->InitFadeLibrary(ReplaySettings->FadeLibrary);

            if (UScreenFadeComponent* Fade = PC->FindComponentByClass<UScreenFadeComponent>())
            {
                FScreenFadeSettings FadeGameplay;
                bFadeGameplayExist = Fade->HasFadeEntry("Gameplay");
                if (bFadeGameplayExist) {
                    FadeGameplay = Fade->GetFadeEntry("Gameplay");
                    HUDFadeInDuration = FadeGameplay.bFadeIn ? FadeGameplay.FadeInDuration : 0.f;
                    HUDFadeStartTime = FadeGameplay.bBlackScreenIn ? FadeGameplay.BlackScreenInDuration : 0.f;
                    bFadeHUDDarkness = FadeGameplay.bFadeInHUDDarkness;
                }
                bFadeGamplay = bFadeGameplayExist && (FadeGameplay.bBlackScreenIn || FadeGameplay.bFadeIn); // Should be the same in all HUD loops
                if (bFadeGamplay)
                {
                    W->GetTimerManager().SetTimerForNextTick([W, Fade, PC]()
                        {
                            Fade->PlayFadeIn(PC, "Gameplay");
                            UE_LOG(LogTemp, Log,
                                TEXT("[GameMode] PlayFadeIn(Gameplay) (one tick delayed) for PC='%s'"),
                                *PC->GetName());
                            if (PC && PC->IsLocalPlayerController()) {
                                if (UHUDTransitionManager* TM = PC->FindComponentByClass<UHUDTransitionManager>())
                                {
                                    TM->SetHUDOpacity(0.f);
                                    UE_LOG(LogTemp, Log,
                                        TEXT("[GameMode] HUD Opacity Reset at 0 for PC='%s'"),
                                        *PC->GetName());
                                }
                            }
                        });
                }
                else
                {
                    // Clear camera snap-to-black from loading screen and show HUD immediately.
                    PC->ClientSetCameraFade(false, FColor::Black, FVector2D(0.f, 1.f), 0.f, false);

                    if (UHUDTransitionManager* TM = PC->FindComponentByClass<UHUDTransitionManager>())
                        TM->SetHUDOpacity(1.f);
                    UE_LOG(LogTemp, Warning,
                        TEXT("[GameMode] No Gameplay fade entry for PC='%s' -- "
                            "HUD at 1.0. Check FadeLibrary key is exactly Gameplay."),
                        *PC->GetName());
                }
            }
        }

        // Step 2 (next tick): HUD widget is now created and in viewport.
        // Re-zero opacity (widget resets to 1 on creation) then start fade-in.
        // One tick is enough -- widget creation and AddToPlayerScreen both happen
        // in the same deferred tick from InitializeHUDForPlayer.
        if (bFadeGamplay) {
            W->GetTimerManager().SetTimerForNextTick([W]()
                {
                    if (!W) return;
                    for (FConstPlayerControllerIterator ItOpacity = W->GetPlayerControllerIterator(); ItOpacity; ++ItOpacity)
                    {
                        APlayerController* PC = Cast<APlayerController>(ItOpacity->Get());
                        if (!PC || !PC->IsLocalPlayerController()) continue;
                        if (UHUDTransitionManager* TM = PC->FindComponentByClass<UHUDTransitionManager>())
                        {
                            TM-> SetHUDOpacity(0.f);
                            UE_LOG(LogTemp, Log,
                                TEXT("[GameMode] HUD Opacity Reset at 0 for PC='%s'"),
                                *PC->GetName());
                        }
                    }
                });
        }
        

        // Step 3 (after black screen in or next tick):
        // Apply FadeIn HUD.
        FTimerHandle HUDFadeTimer;
        if (bFadeGamplay) {
            W->GetTimerManager().SetTimer(HUDFadeTimer, FTimerDelegate::CreateLambda([W, HUDFadeInDuration, bFadeHUDDarkness]()
                {
                    if (!W) return;
                    for (FConstPlayerControllerIterator ItHUDFade = W->GetPlayerControllerIterator(); ItHUDFade; ++ItHUDFade)
                    {
                        APlayerController* PC = Cast<APlayerController>(ItHUDFade->Get());
                        if (!PC || !PC->IsLocalPlayerController()) continue;
                        if (UHUDTransitionManager* TM = PC->FindComponentByClass<UHUDTransitionManager>())
                        {
                            TM->HUDFadeIn(HUDFadeInDuration, bFadeHUDDarkness);
                            UE_LOG(LogTemp, Log,
                                TEXT("[GameMode] HUDFadeIn(%.2fs) for PC='%s'"),
                                HUDFadeInDuration, *PC->GetName());
                        }
                    }
                }),
                FMath::Max(HUDFadeStartTime, 0.01f), false);
        }
    }
}
// ---------------------------------------------------------------------------
//  OnSubmarineDied
// 
// //  Timeline:
//    t=0     : FreezeOnDeath (hide sub, deactivate cameras, unpossess)
//    t=0     : GameplayFade fade-out begins (cosmetic, fades to black)
//    t=0..Delay : Recording continues, explosion VFX captured
//    t=Delay : OnPostDeathRecordingComplete -> extract slice -> BeginDeathSequence
//              DeathSequenceComponent schedules its own DeathReplayFade independently
// ---------------------------------------------------------------------------
void ASubmarineGameMode::OnSubmarineDied(ASubmarinePawn* DeadSubmarine,
    AController* DeadController,
    AActor* Killer)
{
    // -----------------------------------------------------------------------
    //  CRITICAL GUARD: CPU submarines and remote players must NEVER trigger
    //  the death sequence, replay camera, or any HUD/fade transition.
    //  Only local human players go through this flow.
    // -----------------------------------------------------------------------
    APlayerController* DeadPC = Cast<APlayerController>(DeadController);
    if (!DeadPC || !DeadPC->IsLocalPlayerController())
    {
        // CPU or remote player — just destroy the pawn cleanly
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] OnSubmarineDied: '%s' is CPU/remote — "
                "skipping death sequence"),
            DeadSubmarine ? *DeadSubmarine->GetName() : TEXT("NULL"));
        if (DeadSubmarine)
        {
            // Flag as dead before destroying so billboard component
            // hides the widget immediately without waiting for
            // GetOwner() to return null on the next logic tick.
            DeadSubmarine->bDead = true;
            UE_LOG(LogTemp, Log,
                TEXT("[GameMode] CPU death: bDead set on '%s'"),
                *DeadSubmarine->GetName());
            DeadSubmarine->Destroy();
        }
        return;
    }
    // Everything below only runs for human local players:

    UE_LOG(LogTemp, Log, TEXT("[GameMode] OnSubmarineDied called: Sub='%s' Controller='%s' Killer='%s'"),
        DeadSubmarine ? *DeadSubmarine->GetName() : TEXT("NULL"),
        DeadController ? *DeadController->GetName() : TEXT("NULL"),
        Killer ? *Killer->GetName() : TEXT("NULL"));

    if (!DeadSubmarine || !DeadController || !GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] OnSubmarineDied — invalid params, aborting"));
        return;
    }

    // Ensure DeathSequence delegate is bound even if BeginPlay was missed
    if (DeathSequence &&
        !DeathSequence->OnDeathSequenceComplete.IsBound())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[GameMode] OnDeathSequenceComplete was not bound — binding now (BeginPlay may have been skipped)"));
        DeathSequence->OnDeathSequenceComplete.AddDynamic(
            this, &ASubmarineGameMode::OnDeathSequenceComplete);
    }

    // Cache for use after the timer fires
    PendingDeadSub = DeadSubmarine;
    PendingDeadController = DeadController;

    // ------------------------------------------------------------------
    //  Torpedoes destroy themselves during or shortly after the hit frame.
    //  By the time OnPostDeathRecordingComplete fires (DeathPreviewDelay
    //  seconds later), the torpedo actor will be gone. We store everything
    //  we need right now.
    // ------------------------------------------------------------------
    PendingKillerInfo.Clear();

    if (Killer)
    {
        PendingKillerInfo.ActorName = Killer->GetName();
        PendingKillerInfo.ActorGuid = Killer->GetActorInstanceGuid();
        PendingKillerInfo.TorpedoActor = Killer;

        if (ATorpedoPawn* T = Cast<ATorpedoPawn>(Killer))
        {
            PendingKillerInfo.bWasTorpedo = true;
            PendingKillerInfo.KillerSubmarine =
                Cast<ASubmarinePawn>(T->FiringShooter.Get());

            UE_LOG(LogTemp, Log,
                TEXT("[GameMode] Killer=torpedo '%s', firedBy='%s'"),
                *Killer->GetName(),
                PendingKillerInfo.KillerSubmarine.IsValid()
                ? *PendingKillerInfo.KillerSubmarine->GetName()
                : TEXT("unknown"));
        }
        else
        {
            PendingKillerInfo.bWasTorpedo = false;
            PendingKillerInfo.KillerSubmarine = Cast<ASubmarinePawn>(Killer);

            UE_LOG(LogTemp, Log,
                TEXT("[GameMode] Killer='%s' (submarine or environmental)"),
                *Killer->GetName());
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[GameMode] No killer — environmental death"));
    }

    const UReplaySettings* RS = ReplaySettings
        ? ReplaySettings.Get() : GetDefault<UReplaySettings>();

    
    // Freeze the submarine immediately (hides mesh, disables input,
    // unpossesses controller so death cam can take over the view).
    DeadSubmarine->FreezeOnDeath();
    
    // ------------------------------------------------------------------
    //  Step 3: Schedule GameplayFade OUT
    //
    //  The fade should START at (DeathPreviewDelay - FadeOutDuration)
    //  so it FINISHES exactly when the replay begins at DeathPreviewDelay.
    //
    //  If FadeOutDuration >= DeathPreviewDelay, fire immediately (t=0).
    // ------------------------------------------------------------------
    UScreenFadeComponent* PlayerFade = GetPlayerFade(DeadPC);
    TWeakObjectPtr<APlayerController>    WeakPC(DeadPC);

    bool bFadeHUDDarkness = false;

    const float Delay = RS->DeathPreviewDelay;
    FTimerHandle ContextTimer;

    const bool bDeathIsSplit = ActiveRMS && ActiveRMS->bSplitScreenEnabled && ActiveRMS->LocalPlayerCount >= 2;
    const EHUDContext DeathCtx = bDeathIsSplit ? EHUDContext::DeathReplay_Splitscreen : EHUDContext::DeathReplay;

    if (PlayerFade && DeadPC && PlayerFade->HasFadeEntry("Gameplay") && Delay > 0.f)
    {
        const FScreenFadeSettings GameplayFade = PlayerFade->GetFadeEntry("Gameplay");
        FTimerHandle FadeTimer;


        if (GameplayFade.bFadeOut || GameplayFade.bBlackScreenOut)
        {
            FScreenFadeSettings SafeFade = SafeTransitionTransform(GameplayFade, Delay, /*TransitionIn*/false);
            const float FadeTransition = SafeFade.bFadeOut ? SafeFade.FadeOutDuration : 0.f;
            const float BlackScreenTransition = SafeFade.bBlackScreenOut ? SafeFade.BlackScreenOutDuration : 0.f;
            const float TransitionOutDuration = FadeTransition + BlackScreenTransition;
            const float FadeStartTime = Delay - TransitionOutDuration;
            
            bFadeHUDDarkness = SafeFade.bFadeOutHUDDarkness;

            UE_LOG(LogTemp, Log,
                TEXT("[GameMode] GameplayFade OUT (%.2fs) scheduled in %.2fs"),
                GameplayFade.FadeOutDuration, FadeStartTime);

            TWeakObjectPtr<UScreenFadeComponent> WeakFade(PlayerFade);

            if (FadeStartTime <= 0.f)
            {
                PlayerFade->FadeOut(DeadPC, SafeFade);
                if (WeakPC.IsValid()) {
                    UHUDTransitionManager* TransMgr = WeakPC->FindComponentByClass<UHUDTransitionManager>();
                    if (TransMgr)
                    {
                        TransMgr->HUDFadeOut(FadeTransition, bFadeHUDDarkness);
                    }
                }
            }
            else
            {
                GetWorld()->GetTimerManager().SetTimer(FadeTimer,
                    FTimerDelegate::CreateLambda([WeakFade, WeakPC, SafeFade, DeathCtx, FadeTransition, bFadeHUDDarkness]()
                        {
                            if (WeakFade.IsValid() && WeakPC.IsValid()) {
                                UHUDTransitionManager* TransMgr = WeakPC->FindComponentByClass<UHUDTransitionManager>();

                                WeakFade->FadeOut(WeakPC.Get(), SafeFade);
                                if (TransMgr)
                                {
                                    TransMgr->HUDFadeOut(FadeTransition, bFadeHUDDarkness);
                                }
                            }
                        }),
                    FadeStartTime, false);
            }
        }
    }
    if (WeakPC.IsValid()) {
        UE_LOG(LogTemp, Log, TEXT("[GameMode] Death HUD: Tansition to Context set -- instant opacity 0 at %.2fs"), Delay);
        // Capture weak pointers -- safer than capturing 'this' in deferred lambdas
        TWeakObjectPtr<UHUDGlobalDefaults> WeakGlobalDefaults(HUDGlobalDefaults);
        TWeakObjectPtr<URuntimeMatchSettings> WeakActiveRMS(ActiveRMS);
        GetWorld()->GetTimerManager().SetTimer(ContextTimer,
            FTimerDelegate::CreateLambda([WeakPC, DeathCtx, WeakGlobalDefaults, WeakActiveRMS]()
            {
                    if (!WeakPC.IsValid()) return;
                    UHUDTransitionManager* TransMgr =
                        WeakPC->FindComponentByClass<UHUDTransitionManager>();
                    if (TransMgr)
                    {
                        TransMgr->SetHUDOpacity(0.f);
                        TransMgr->TransitionToContext(DeathCtx, nullptr);
                        // HUDFadeIn for the DeathReplay widget.
                        // DeathSequenceComponent handles the CAMERA fade separately.
                        // The HUD widget is now visible but at opacity 0 after
                        // SwapHUDSettings -- we must fade it in independently.
                        UScreenFadeComponent* FadeComp = WeakPC.IsValid()
                            ? WeakPC->FindComponentByClass<UScreenFadeComponent>() : nullptr;
                        float DeathHUDFadeIn = 0.f;  // fallback
                        bool bDeathHUDDarkness = false;
                        if (FadeComp && FadeComp->HasFadeEntry("DeathReplay"))
                        {
                            const FScreenFadeSettings& DS = FadeComp->GetFadeEntry("DeathReplay");
                            DeathHUDFadeIn = DS.bFadeIn ? DS.FadeInDuration : 0.f;
                            bDeathHUDDarkness = DS.bFadeInHUDDarkness;
                        }
                        TransMgr->HUDFadeIn(DeathHUDFadeIn, bDeathHUDDarkness);
                        UE_LOG(LogTemp, Log,
                            TEXT("[GameMode] DeathReplay HUDFadeIn(%.2fs) for PC='%s'"),
                            DeathHUDFadeIn,
                            WeakPC.IsValid() ? *WeakPC->GetName() : TEXT("?"));
                    }
                    // Update billboard context for this HUD context
                    UBillboardDisplayComponent* BillboardDisp =
                        WeakPC->FindComponentByClass<UBillboardDisplayComponent>();
                    if (BillboardDisp)
                    {
                        UInfoBillboardContextSettings* BillCtx = WeakGlobalDefaults.IsValid()
                            ? WeakGlobalDefaults->ResolveBillboard(DeathCtx) : nullptr;
                        BillboardDisp->SetBillboardContext(
                            BillCtx,
                            BillboardDisp->LocalTeamIndex,
                            WeakActiveRMS.Get());
                        UE_LOG(LogTemp, Log,
                            TEXT("[GameMode] Billboard context -> %s for DeathCtx=%d"),
                            BillCtx ? *BillCtx->GetName() : TEXT("NULL (no billboard DA configured)"),
                            (int32)DeathCtx);
                    }
                }), FMath::Max(Delay, 0.01f), false);
    }

    // ------------------------------------------------------------------
    //  Step 4: Post-death recording timer
    // ------------------------------------------------------------------
    if (Delay > 0.f)
    {
        GetWorld()->GetTimerManager().SetTimer(
            PostDeathRecordingTimer,
            this,
            &ASubmarineGameMode::OnPostDeathRecordingComplete,
            Delay,
            false);

        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] PostDeathRecording timer set for %.2fs"), Delay);
    }
    else
    {
        OnPostDeathRecordingComplete();
    }
}

// ---------------------------------------------------------------------------
//  OnPostDeathRecordingComplete
//  Called DeathPreviewDelay seconds after the submarine died.
//  The recording now includes the explosion VFX frame(s).
// ---------------------------------------------------------------------------
void ASubmarineGameMode::OnPostDeathRecordingComplete()
{
    ASubmarinePawn* DeadSub = PendingDeadSub.Get();
    AController* DC = PendingDeadController.Get();

    if (!DC || !GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] OnPostDeathRecordingComplete — controller gone"));
        return;
    }

    APlayerController* DeadPC = Cast<APlayerController>(DC);

    const UReplaySettings* RS = ReplaySettings
        ? ReplaySettings.Get() : GetDefault<UReplaySettings>();

    // ------------------------------------------------------------------
    //  Resolve killer for death cam.
    //  If it was a torpedo: torpedo is gone — use the firing submarine.
    //  If it was a submarine: use that submarine directly.
    //  Environmental: nullptr (StaticBehindDead mode will be used).
    // ------------------------------------------------------------------
    AActor* KillerForDeathSeq = nullptr;

    if (PendingKillerInfo.bWasTorpedo)
    {
        KillerForDeathSeq = PendingKillerInfo.KillerSubmarine.IsValid()
            ? PendingKillerInfo.KillerSubmarine.Get() : nullptr;

        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Torpedo '%s' gone — using firing sub '%s' for death cam"),
            *PendingKillerInfo.ActorName,
            KillerForDeathSeq ? *KillerForDeathSeq->GetName() : TEXT("also gone"));
    }
    else if (PendingKillerInfo.KillerSubmarine.IsValid())
    {
        KillerForDeathSeq = PendingKillerInfo.KillerSubmarine.Get();
    }
    // else: environmental kill, KillerForDeathSeq stays nullptr

    UE_LOG(LogTemp, Log,
        TEXT("[GameMode] KillerForDeathSeq='%s'"),
        KillerForDeathSeq ? *KillerForDeathSeq->GetName() : TEXT("None (environmental)"));

    // -----------------------------------------------------------------------
    //  Extract replay slice
    //
    //  Effective content after death = min(DeathReplayDuration,
    //                                      DeathReplayLeadIn + DeathPreviewDelay)
    //  Slice start = Now - EffectiveContent - DeathPreviewDelay
    //              = Now - min(DeathReplayDuration, DeathReplayLeadIn + DeathPreviewDelay)
    //                    - DeathPreviewDelay
    //
    //  We are now exactly DeathPreviewDelay seconds past the death moment, so:
    //    "Now" in recording time = DeathTime + DeathPreviewDelay
    //    Slice end   = Now (captures the explosion)
    //    Slice start = Now - EffectiveContent - DeathPreviewDelay
    // -----------------------------------------------------------------------
    UReplayData* Slice = nullptr;

    if (ReplayRecorder && RS->DeathReplayDuration > 0.f)
    {
        const float Now = GetWorld()->GetTimeSeconds();
        const float EffectiveContent = RS->GetEffectivePostDeathContent();
        const float SliceStart = Now - EffectiveContent;
        const float SliceEnd = Now;

        Slice = ReplayRecorder->ExtractSlice(SliceStart, SliceEnd);

        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Slice extracted: %.1fs  %d frames  (effective=%.1fs speed=%.1fx)"),
            Slice ? Slice->GetDuration() : 0.f,
            Slice ? Slice->TickFrames.Num() : 0,
            EffectiveContent,
            RS->PlaybackSpeed);
        const float DeathTime = Now - RS->DeathPreviewDelay;
        const float DeathOffsetInSlice = DeathTime - SliceStart;
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Slice: start=%.2f end=%.2f dur=%.1fs frames=%d"),
            SliceStart, SliceEnd,
            Slice ? Slice->GetDuration() : 0.f,
            Slice ? Slice->TickFrames.Num() : 0);
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Death at t=%.2f = %.2fs into slice (should show explosion in last %.2fs)"),
            DeathTime, DeathOffsetInSlice, RS->DeathPreviewDelay);
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] WallClock will be corrected in DeathSeq: slice_dur=%.2f previewDelay=%.2f"),
            Slice ? Slice->GetDuration() : 0.f, RS->DeathPreviewDelay);
    }

    // -----------------------------------------------------------------------
    //  Start death sequence
    //  Pass PlaybackSpeed so ghost movement matches the configured replay speed.
    // -----------------------------------------------------------------------

    // Pre-populate torpedo name BEFORE BeginDeathSequence.
    // By this point the torpedo is already destroyed, so Cast<ATorpedoPawn>(Killer)
    // inside BeginDeathSequence always fails. We set the name here while we still
    // have it cached in PendingKillerInfo.
    if (ReplayPlayback && PendingKillerInfo.bWasTorpedo)
    {
        ReplayPlayback->KillerTorpedoGuid = PendingKillerInfo.ActorGuid;
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Pre-set KillerTorpedoActorName='%s' on ReplayPlayback"),
            *PendingKillerInfo.ActorGuid.ToString());
    }

    // Start death sequence -- pass the dead player's ScreenFadeComponent
    UScreenFadeComponent* PlayerFade = GetPlayerFade(DeadPC);


    if (DeathSequence)
    {
        // DeathSequenceComponent handles DeathReplayFade internally via ScheduleFades.
        // Pass ScreenFade so it can schedule the fade-in/out on the dead PC.
        DeathSequence->BeginDeathSequence(
            DeadSub, DC,
            KillerForDeathSeq,
            Slice,
            (Slice && ReplayPlayback) ? ReplayPlayback.Get() : nullptr,
            RS->PlaybackSpeed,
            PlayerFade);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] No DeathSequence — skipping to spectator"));
        OnDeathSequenceComplete();
    }
}

// ---------------------------------------------------------------------------
//  OnDeathSequenceComplete  (called when delay + death cam are done)
// 
// //  At this point:
//  - DeathSequenceComponent has already called StopPlayback (ghosts destroyed)
//  - DeathSequenceComponent has already run its fade-out (if configured)
//  - Screen may be black from the DeathReplayFade fade-out
//
//  We spawn the spectator and do a SpectatorFade fade-in.
//  We do NOT do another fade-out here — that would double-fade.
// ---------------------------------------------------------------------------
void ASubmarineGameMode::OnDeathSequenceComplete()
{
    UE_LOG(LogTemp, Log, TEXT("[GameMode] OnDeathSequenceComplete fired"));

    ASubmarinePawn* DeadSub = PendingDeadSub.Get();
    AController* DC = PendingDeadController.Get();

    if (!DC || !GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] OnDeathSequenceComplete — controller invalid, skipping spectator spawn"));
        return;
    }

    if (!SpectatorPawnClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] SpectatorPawnClass not set!"));
        return;
    }

    // Save full match replay on player death if enabled
    if (ReplayRecorder)
    {
        const UReplaySettings* RS = ReplaySettings
            ? ReplaySettings.Get() : GetDefault<UReplaySettings>();
        if (RS && RS->bSaveFullReplayOnDeath)
        {
            const FString Label = FString::Printf(
                TEXT("Death_%s_%s"),
                *DeadSub->GetName(),
                *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
            const bool bSaved = ReplayRecorder->SaveFullMatchReplay(Label);
            UE_LOG(LogTemp, Log,
                TEXT("[GameMode] Full match replay save on death: %s"),
                bSaved ? TEXT("OK") : TEXT("FAILED"));
        }
    }

    const UReplaySettings* RS = ReplaySettings
        ? ReplaySettings.Get() : GetDefault<UReplaySettings>();

    APlayerController* PC = Cast<APlayerController>(DC);

    // Spawn spectator immediately — no extra fade-out here.
    // The DeathReplayFade already faded to black at the end of the replay.
    // SpectatorFade will fade-in from that black state.
    TArray<ASubmarinePawn*> AllSubs;
    for (TActorIterator<ASubmarinePawn> It(GetWorld()); It; ++It)
        if (IsValid(*It) && *It != DeadSub)
            AllSubs.Add(*It);

    const FTransform SpawnTransform = DeadSub
        ? DeadSub->GetActorTransform() : FTransform::Identity;

    FActorSpawnParameters Params;
    Params.Owner = DC;

    ASubmarineSpectatorPawn* Spectator = GetWorld()->SpawnActor<ASubmarineSpectatorPawn>(
        SpectatorPawnClass, SpawnTransform, Params);

    if (!Spectator)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] Failed to spawn SpectatorPawn!"));
        return;
    }

    DC->Possess(Spectator);
    Spectator->InitSpectator(AllSubs, true);
    Spectator->bUsesGamepad = DeadSub->bUsesGamepad;
    if (CameraBlendSettings) Spectator->BlendSettings = CameraBlendSettings;

    // Auto-select a submarine to spectate
    USpectatorTrackerComponent* Tracker =
        PC->FindComponentByClass<USpectatorTrackerComponent>();
    if (Tracker)
        Tracker->AutoSelectTarget();

    // Transition HUD to Spectator context
    UHUDTransitionManager* TransMgr = PC
        ? PC->FindComponentByClass<UHUDTransitionManager>() : nullptr;
    if (TransMgr)
    {
        // Defer one tick so the new widget has geometry before SetHUDOpacity(1.f)
        TWeakObjectPtr<UHUDTransitionManager> WeakTrans(TransMgr);
        TWeakObjectPtr<APlayerController> WeakPC(PC);
        TWeakObjectPtr<UHUDGlobalDefaults> WeakDefaults(HUDGlobalDefaults);
        TWeakObjectPtr<URuntimeMatchSettings> WeakRMS(ActiveRMS);
        const bool bSpectIsSplit = ActiveRMS && ActiveRMS->bSplitScreenEnabled && ActiveRMS->LocalPlayerCount >= 2;

        // Capture AllSubs for the lambda — avoids world scan racing
        TArray<TWeakObjectPtr<ASubmarinePawn>> WeakAllSubs;
        for (ASubmarinePawn* S : AllSubs) WeakAllSubs.Add(S);

        GetWorld()->GetTimerManager().SetTimerForNextTick([WeakTrans, WeakPC, WeakDefaults, WeakRMS, bSpectIsSplit, WeakAllSubs]()
            {
                if (WeakTrans.IsValid())
                {
                    const EHUDContext SpectCtx = bSpectIsSplit
                        ? EHUDContext::Spectator_Splitscreen
                        : EHUDContext::Spectator;
                    WeakTrans->TransitionToContext(SpectCtx, nullptr);
                    // Update billboard context for spectator
                    UBillboardDisplayComponent* BillboardDisp =
                        WeakPC.IsValid()
                        ? WeakPC->FindComponentByClass<UBillboardDisplayComponent>() : nullptr;
                    UE_LOG(LogTemp, Log,
                        TEXT("[GameMode|BBCtx] Spectator lambda fired: PC='%s'  "
                            "SpectCtx=%d  BillboardDisp=%s  WeakDefaults=%s"),
                        WeakPC.IsValid() ? *WeakPC->GetName() : TEXT("?"),
                        (int32)SpectCtx,
                        BillboardDisp ? TEXT("found") : TEXT("NOT FOUND on PC"),
                        WeakDefaults.IsValid() ? TEXT("valid") : TEXT("INVALID"));
                    if (BillboardDisp)
                    {
                        // Resolve BBCs from the known-alive subs list (no world scan)
                        TArray<USubmarineInfoBillboardComponent*> Sources;
                        for (const TWeakObjectPtr<ASubmarinePawn>& WS : WeakAllSubs)
                        {
                            if (!WS.IsValid()) continue;
                            USubmarineInfoBillboardComponent* BBC =
                                WS->FindComponentByClass<USubmarineInfoBillboardComponent>();
                            if (BBC) Sources.Add(BBC);
                        }

                        UInfoBillboardContextSettings* BillCtx = WeakDefaults.IsValid()
                            ? WeakDefaults->ResolveBillboard(SpectCtx) : nullptr;
                        UE_LOG(LogTemp, Log,
                            TEXT("[GameMode|BBCtx] ResolveBillboard(SpectCtx=%d) -> %s"),
                            (int32)SpectCtx,
                            BillCtx ? *BillCtx->GetName() : TEXT("NULL -- assign DA in HUDGlobalDefaults"));
                        BillboardDisp->SetBillboardContext(BillCtx,
                            BillboardDisp->LocalTeamIndex, WeakRMS.Get());
                    }
                }
            });
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] HUD Spectator transition queued (next tick) for PC='%s'"),
            PC ? *PC->GetName() : TEXT("None"));
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[GameMode] No HUDTransitionManager on PC='%s' -- HUD context unchanged"),
            PC ? *PC->GetName() : TEXT("None"));
    }

     //-- Spectator fade-in --------------------------------------------------

    UScreenFadeComponent* PlayerFade = GetPlayerFade(PC);
    if (PlayerFade && PlayerFade->HasFadeEntry("Spectator"))
    {
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            if (DC)
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[GameMode] SpectatorFade IN for PC='%s'"), *PC->GetName());
                PlayerFade->PlayFadeIn(PC, "Spectator");
                const FScreenFadeSettings SpectatorFade = PlayerFade->GetFadeEntry("Spectator");
                float FadeTransition = SpectatorFade.bFadeIn ? SpectatorFade.FadeInDuration : 0.f;
                float HUDFadeStartTime = SpectatorFade.bBlackScreenIn ? SpectatorFade.BlackScreenInDuration : 0.f;
                bool bFadeHUDDarkness = SpectatorFade.bFadeInHUDDarkness;
                if (TransMgr)
                {
                    FTimerHandle HUDFadeTimer;
                    UWorld* W = GetWorld();
                        W->GetTimerManager().SetTimer(HUDFadeTimer, FTimerDelegate::CreateLambda([TransMgr, FadeTransition, bFadeHUDDarkness, PC]()
                            {
                                TransMgr->HUDFadeIn(FadeTransition, bFadeHUDDarkness);
                                UE_LOG(LogTemp, Log,
                                    TEXT("[GameMode] SpectatorFade IN for HUD for PC='%s'"), *PC->GetName());
                            }),
                            FMath::Max(HUDFadeStartTime, 0.01f), false);
                }
            }
        }
    }

    if (IsValid(DeadSub)) DeadSub->Destroy();

    PendingDeadSub = nullptr;
    PendingDeadController = nullptr;

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Spectator spawned, SpectatorFade-in triggered"));
}

// ---------------------------------------------------------------------------
//  Replay Mode
// ---------------------------------------------------------------------------
void ASubmarineGameMode::EnterReplayMode()
{
    UE_LOG(LogTemp, Log, TEXT("[GameMode] EnterReplayMode: showing loading screen..."));

    // Show loading screen first -- same as normal gameplay flow
    ShowLoadingScreen();

    // LoadGameFromSlot is synchronous, so we wait one tick before loading
    // so the loading screen has time to render at least one frame
    GetWorld()->GetTimerManager().SetTimerForNextTick(
        [this]()
        {
            ExecuteReplayLoad();
        });
}

void ASubmarineGameMode::ExecuteReplayLoad()
{
    USubmarineGameInstance* SGI = Cast<USubmarineGameInstance>(GetGameInstance());

    const bool bLoaded = ReplayRecorder->LoadReplay();
    if (!bLoaded || !ReplayRecorder->LoadedReplay)
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] EnterReplayMode: failed to load replay"));
        HideLoadingScreen();
        return;
    }

    UReplayData* FullReplay = ReplayRecorder->LoadedReplay;

    // Respect minimum loading screen time (same as normal flow)
    const float TimeElapsed = GetWorld()->GetTimeSeconds() - LoadingScreenStartTime;
    const float RemainingWait = FMath::Max(0.f, MinLoadingScreenTime - TimeElapsed);

    if (RemainingWait > 0.f)
    {
        FTimerHandle MinTimeTimer;
        GetWorld()->GetTimerManager().SetTimer(MinTimeTimer,
            [this, FullReplay]() { FinishEnterReplayMode(FullReplay); },
            RemainingWait, false);
    }
    else
    {
        FinishEnterReplayMode(FullReplay);
    }
}

void ASubmarineGameMode::FinishEnterReplayMode(UReplayData* FullReplay)
{
    HideLoadingScreen();

    if (ReplayPlayback)
        ReplayPlayback->BeginPlayback(FullReplay,
            GetWorld()->GetFirstPlayerController(),
            FullReplay->RecordStartTime, 1.f);

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
        It; ++It)
    {
        APlayerController* PC = Cast<APlayerController>(It->Get());
        if (!PC || !PC->IsLocalPlayerController()) continue;

        UHUDTransitionManager* TransMgr = PC->FindComponentByClass<UHUDTransitionManager>();
        if (TransMgr)
        {
            TransMgr->SetRuntimeSettings(ActiveRMS, HUDGlobalDefaults);
            TransMgr->TransitionToContext(EHUDContext::Replay, nullptr);
        }

        USpectatorTrackerComponent* Tracker =
            PC->FindComponentByClass<USpectatorTrackerComponent>();
        if (Tracker)
            Tracker->AutoSelectTarget();
    }

    UE_LOG(LogTemp, Log, TEXT("[GameMode] EnterReplayMode: replay started"));
}

// ---------------------------------------------------------------------------
//  Replay forwarding
// ---------------------------------------------------------------------------
void ASubmarineGameMode::StartRecording()
{
    if (ReplayRecorder) ReplayRecorder->StartRecording();
}

void ASubmarineGameMode::StopRecording()
{
    if (!ReplayRecorder) return;

    ReplayRecorder->StopRecording();

    // bAutoSaveOnStop: save the replay buffer to disk when recording stops.
    // File appears at: <ProjectDir>/Saved/SaveGames/<ReplaySaveSlot>.sav
    const UReplaySettings* S = ReplaySettings
        ? ReplaySettings.Get()
        : GetDefault<UReplaySettings>();

    if (S && S->bAutoSaveOnStop)
    {
        const bool bOk = ReplayRecorder->SaveReplay();
        UE_LOG(LogTemp, Log,
            TEXT("[GameMode] Auto-save on stop: %s"), bOk ? TEXT("OK") : TEXT("FAILED"));
    }
}

bool ASubmarineGameMode::SaveReplay(const FString& Label)
{
    return ReplayRecorder ? ReplayRecorder->SaveReplay(Label) : false;
}

bool ASubmarineGameMode::LoadReplay()
{
    return ReplayRecorder ? ReplayRecorder->LoadReplay() : false;
}

bool ASubmarineGameMode::IsRecording() const
{
    return ReplayRecorder ? ReplayRecorder->IsRecording() : false;
}