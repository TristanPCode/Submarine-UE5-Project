// Fill out your copyright notice in the Description page of Project Settings.

#include "ScreenFadeComponent.h"
#include "GameFramework/PlayerController.h"
#include "Replay/ReplaySettings.h"
#include "SubmarineGameMode.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "TimerManager.h"

UScreenFadeComponent::UScreenFadeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

// ---------------------------------------------------------------------------
//  ShouldLog  --  checks the ReplaySettings CDO for the log toggle
// ---------------------------------------------------------------------------
bool UScreenFadeComponent::ShouldLog() const
{
    // Walk up to the owning GameMode -> its ReplaySettings DA
    // Fallback: use the CDO default (true) so logs are on by default.
    if (AActor* Owner = GetOwner())
    {
        if (UWorld* W = Owner->GetWorld())
        {
            if (AGameModeBase* GM = W->GetAuthGameMode())
            {
                // Dynamic property lookup so we don't need a hard cast to ASubmarineGameMode
                if (FObjectProperty* Prop = FindFProperty<FObjectProperty>(
                    GM->GetClass(), TEXT("ReplaySettings")))
                {
                    if (UReplaySettings* RS = Cast<UReplaySettings>(Prop->GetObjectPropertyValue_InContainer(GM)))
                        return RS->bLogScreenFade;
                }
            }
        }
    }
    return GetDefault<UReplaySettings>()->bLogScreenFade;
}

// ---------------------------------------------------------------------------
//  GetFadeOutTotalDuration
// ---------------------------------------------------------------------------
float UScreenFadeComponent::GetFadeOutTotalDuration(const FScreenFadeSettings& Settings) const
{
    float Total = 0.f;
    if (Settings.bFadeOut && Settings.FadeOutDuration > 0.f)
        Total += Settings.FadeOutDuration;
    if (Settings.bBlackScreenOut && Settings.BlackScreenOutDuration > 0.f)
        Total += Settings.BlackScreenOutDuration;
    return Total;
}

// ---------------------------------------------------------------------------
//  FadeIn
//  Sequence: [BlackScreenIn snap + hold] -> FadeIn (black->clear)
// ---------------------------------------------------------------------------
void UScreenFadeComponent::FadeIn(APlayerController* PC, const FScreenFadeSettings& Settings)
{
    const bool bDoBlackIn = Settings.bBlackScreenIn && Settings.BlackScreenInDuration > 0.f;
    const bool bDoFadeIn = Settings.bFadeIn && Settings.FadeInDuration > 0.f;

    if (!bDoBlackIn && !bDoFadeIn) return;

    if (ShouldLog())
        UE_LOG(LogTemp, Log,
            TEXT("[ScreenFade] FadeIn called  PC='%s'  BlackScreenIn=%d(%.2fs)  FadeIn=%d(%.2fs)  CamMgr=%s"),
            *PC->GetName(),
            bDoBlackIn ? 1 : 0, Settings.BlackScreenInDuration,
            bDoFadeIn ? 1 : 0, Settings.FadeInDuration,
            PC->PlayerCameraManager ? TEXT("valid") : TEXT("NULL"));

    if (bDoBlackIn)
    {
        // Snap to black immediately
        Internal_SnapToBlack(PC, Settings.BlackScreenColor);

        if (bDoFadeIn)
        {
            // After the hold, start the fade
            TWeakObjectPtr<APlayerController> WeakPC(PC);
            FScreenFadeSettings CapturedSettings = Settings;
            GetWorld()->GetTimerManager().SetTimer(
                BlackScreenInTimerHandle,
                FTimerDelegate::CreateLambda([this, WeakPC, CapturedSettings]()
                    {
                        if (WeakPC.IsValid()) {
                            Internal_StartFade(WeakPC.Get(), CapturedSettings.FadeInDuration,/*bFromBlack=*/true, CapturedSettings.FadeColor);
                            ScheduleClearFade(WeakPC.Get(), CapturedSettings.FadeInDuration);
                        }
                    }),
                Settings.BlackScreenInDuration,
                false);
        }
        else
        {
            ScheduleClearFade(PC, Settings.BlackScreenInDuration);
        }
    }
    else
    {
        // Snap to black immediately
        Internal_SnapToBlack(PC, Settings.BlackScreenColor);

        // No black-screen hold: start fade immediately
        Internal_StartFade(PC, Settings.FadeInDuration, /*bFromBlack=*/true, Settings.FadeColor);
    }
}

// ---------------------------------------------------------------------------
//  FadeOut
//  Sequence: FadeOut (clear->black) -> [BlackScreenOut hold]
// ---------------------------------------------------------------------------
void UScreenFadeComponent::FadeOut(
    APlayerController* PC,
    const FScreenFadeSettings& Settings)
{
    const bool bDoFadeOut = Settings.bFadeOut && Settings.FadeOutDuration > 0.f;
    const bool bDoBlackOut = Settings.bBlackScreenOut && Settings.BlackScreenOutDuration > 0.f;

    if (!bDoFadeOut && !bDoBlackOut) return;

    if (ShouldLog())
        UE_LOG(LogTemp, Log,
            TEXT("[ScreenFade] FadeOut called  PC='%s'  FadeOut=%d(%.2fs)  BlackScreenOut=%d(%.2fs)  CamMgr=%s"),
            *PC->GetName(),
            bDoFadeOut ? 1 : 0, Settings.FadeOutDuration,
            bDoBlackOut ? 1 : 0, Settings.BlackScreenOutDuration,
            PC->PlayerCameraManager ? TEXT("valid") : TEXT("NULL"));

    if (bDoFadeOut)
    {
        Internal_StartFade(PC, Settings.FadeOutDuration, /*bFromBlack=*/false, Settings.FadeColor);

        // If we also need a black-screen-out hold, schedule a snap after fade completes
        if (bDoBlackOut)
        {
            // After fade-out completes, snap to black and hold,
            // then call Internal_OnFadeOutComplete after the hold.
            TWeakObjectPtr<APlayerController> WeakPC(PC);
            TWeakObjectPtr<UScreenFadeComponent> WeakThis(this);
            FScreenFadeSettings CapturedSettings = Settings;
            GetWorld()->GetTimerManager().SetTimer(
                BlackScreenOutTimerHandle,
                FTimerDelegate::CreateLambda([WeakThis, WeakPC, CapturedSettings]()
                    {
                        if (!WeakPC.IsValid()) return;
                        // Fade is done; snap to solid black to hold
                        WeakThis->Internal_SnapToBlack(WeakPC.Get(), CapturedSettings.FadeColor);

                        if (WeakThis->ShouldLog())
                            UE_LOG(LogTemp, Log,
                                TEXT("[ScreenFade] BlackScreenOut hold started (%.2fs)"),
                                CapturedSettings.BlackScreenOutDuration);

                        // After the hold, chain or clear
                        TWeakObjectPtr<APlayerController> WeakPC2(WeakPC);
                        TWeakObjectPtr<UScreenFadeComponent> WeakThis2(WeakThis);
                        FScreenFadeSettings CapturedSettings2 = CapturedSettings;
                        FTimerHandle HoldTimer;
                        WeakThis->GetWorld()->GetTimerManager().SetTimer(
                            HoldTimer,
                            FTimerDelegate::CreateLambda([WeakThis2, WeakPC2, CapturedSettings2]()
                                {
                                    if (WeakThis2.IsValid() && WeakPC2.IsValid())
                                        WeakThis2->Internal_OnFadeOutComplete(
                                            WeakPC2.Get(), CapturedSettings2);
                                }),
                            CapturedSettings.BlackScreenOutDuration,
                            false);
                    }),
                Settings.FadeOutDuration,
                false);
        }
        else
        {
            // No black-screen-out: chain or clear after the fade-out finishes
            TWeakObjectPtr<APlayerController> WeakPC(PC);
            TWeakObjectPtr<UScreenFadeComponent> WeakThis(this);
            FScreenFadeSettings CapturedSettings = Settings;
            FTimerHandle FadeEndTimer;
            GetWorld()->GetTimerManager().SetTimer(
                FadeEndTimer,
                FTimerDelegate::CreateLambda([WeakThis, WeakPC, CapturedSettings]()
                    {
                        if (WeakThis.IsValid() && WeakPC.IsValid())
                            WeakThis->Internal_OnFadeOutComplete(WeakPC.Get(), CapturedSettings);
                    }),
                Settings.FadeOutDuration,
                false);
        }
    }
    else if (bDoBlackOut)
    {
        // No fade, just snap to black and hold, then chain or clear
        Internal_SnapToBlack(PC, Settings.FadeColor);

        if (ShouldLog())
            UE_LOG(LogTemp, Log,
                TEXT("[ScreenFade] BlackScreenOut snap (no fade), holding %.2fs"),
                Settings.BlackScreenOutDuration);

        TWeakObjectPtr<APlayerController> WeakPC(PC);
        TWeakObjectPtr<UScreenFadeComponent> WeakThis(this);
        FScreenFadeSettings CapturedSettings = Settings;
        FTimerHandle HoldTimer;
        GetWorld()->GetTimerManager().SetTimer(
            HoldTimer,
            FTimerDelegate::CreateLambda([WeakThis, WeakPC, CapturedSettings]()
                {
                    if (WeakThis.IsValid() && WeakPC.IsValid())
                        WeakThis->Internal_OnFadeOutComplete(WeakPC.Get(), CapturedSettings);
                }),
            Settings.BlackScreenOutDuration,
            false);
    }
}

// ---------------------------------------------------------------------------
//  Internal_OnFadeOutComplete
//
//  Called at the end of every FadeOut path (after fade + any black-screen-out
//  hold). Replaces all the old ScheduleClearFade calls at the end of FadeOut.
//
//  Logic:
//    If bHasNextFade:
//      - If NextFade starts from black (bFadeIn or bBlackScreenIn):
//          -> call FadeIn(NextFade) directly. No ClearFade -- seamless.
//      - If NextFade does NOT start from black:
//          -> call ClearFade first, then FadeIn(NextFade).
//    Else (no chain):
//      -> call ClearFade as before.
// ---------------------------------------------------------------------------
void UScreenFadeComponent::Internal_OnFadeOutComplete(
    APlayerController* PC, const FScreenFadeSettings& Settings)
{
    if (!PC) return;

    if (Settings.NextFadeName == NAME_None)
    {
        if (ShouldLog())
            UE_LOG(LogTemp, Log,
                TEXT("[ScreenFade] FadeOut complete -> ClearFade (no NextFade)  PC='%s'"),
                *PC->GetName());
        ClearFade(PC);
        return;
    }

    const FScreenFadeSettings* Next = FadeLibrary.Find(Settings.NextFadeName);
    if (!Next)
    {
        if (ShouldLog())
            UE_LOG(LogTemp, Warning,
                TEXT("[ScreenFade] FadeOut complete -> NextFadeName='%s' not found in library, ClearFade  PC='%s'"),
                *Settings.NextFadeName.ToString(), *PC->GetName());
        ClearFade(PC);
        return;
    }

    const bool bNextStartsFromBlack =
        (Next->bFadeIn && Next->FadeInDuration > 0.f) ||
        (Next->bBlackScreenIn && Next->BlackScreenInDuration > 0.f);

    if (!bNextStartsFromBlack)
    {
        if (ShouldLog())
            UE_LOG(LogTemp, Log,
                TEXT("[ScreenFade] FadeOut complete -> ClearFade then PlayFadeIn('%s')  PC='%s'"),
                *Settings.NextFadeName.ToString(), *PC->GetName());
        ClearFade(PC);
    }
    else
    {
        if (ShouldLog())
            UE_LOG(LogTemp, Log,
                TEXT("[ScreenFade] FadeOut complete -> PlayFadeIn('%s') seamless  PC='%s'"),
                *Settings.NextFadeName.ToString(), *PC->GetName());
    }

    PlayFadeIn(PC, Settings.NextFadeName);
}

// ---------------------------------------------------------------------------
//  FadeOutThenCall
// ---------------------------------------------------------------------------
void UScreenFadeComponent::FadeOutThenCall(
    APlayerController* PC,
    const FScreenFadeSettings& Settings,
    FSimpleDelegate OnComplete)
{
    if (!PC)
    {
        OnComplete.ExecuteIfBound();
        return;
    }

    // No fade configured
    if (!Settings.bFadeOut || Settings.FadeOutDuration <= 0.f)
    {
        OnComplete.ExecuteIfBound();
        return;
    }

    // Launch fade normally
    FadeOut(PC, Settings);

    // Remove previous pending timer for this controller
    TWeakObjectPtr<APlayerController> WeakPC(PC);

    if (FTimerHandle* Existing = PendingFadeTimers.Find(WeakPC))
    {
        GetWorld()->GetTimerManager().ClearTimer(*Existing);
        PendingFadeTimers.Remove(WeakPC);
    }

    // Schedule callback at fade completion
    if (OnComplete.IsBound())
    {
        FTimerHandle Handle;

        GetWorld()->GetTimerManager().SetTimer(
            Handle,
            FTimerDelegate::CreateLambda(
                [this, WeakPC, OnComplete]()
                {
                    OnComplete.ExecuteIfBound();
                    PendingFadeTimers.Remove(WeakPC);
                }),
            Settings.FadeOutDuration,
            false
        );

        PendingFadeTimers.Add(WeakPC, Handle);
    }
}

// ---------------------------------------------------------------------------
//  ClearFade
// ---------------------------------------------------------------------------
void UScreenFadeComponent::ClearFade(APlayerController* PC)
{
    if (!PC || !PC->PlayerCameraManager) return;

    auto CamMgr = PC->PlayerCameraManager;
    CamMgr->StopCameraFade();
    CamMgr->bEnableFading = false;
}

void UScreenFadeComponent::ScheduleClearFade(APlayerController* PC, float Delay)
{
    if (!PC) return;

    TWeakObjectPtr<UScreenFadeComponent> WeakThis(this);
    TWeakObjectPtr<APlayerController> WeakPC(PC);

    FTimerHandle TempHandle;

    GetWorld()->GetTimerManager().SetTimer(
        TempHandle,
        FTimerDelegate::CreateLambda([WeakThis, WeakPC]()
            {
                if (WeakThis.IsValid() && WeakPC.IsValid())
                {
                    WeakThis->ClearFade(WeakPC.Get());
                }
            }),
        Delay,
        false
    );
}

// ---------------------------------------------------------------------------
//  TickComponent -- logs fade alpha every 0.25s while a fade is active
// ---------------------------------------------------------------------------
void UScreenFadeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bFadeMonitorActive || !MonitoredPC.IsValid()) return;

    APlayerController* PC = MonitoredPC.Get();
    if (!PC || !PC->PlayerCameraManager)
    {
        bFadeMonitorActive = false;
        return;
    }

    // Log alpha every frame while a fade is in progress
    APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
    FadeMonitorTimer += DeltaTime;

    if (ShouldLog() && FadeMonitorTimer >= 0.05f)   // ~20 Hz -- enough to see the curve
    {
        FadeMonitorTimer = 0.f;
        AActor* VT = PC->GetViewTarget();
        if (ShouldLog()) {
            UE_LOG(LogTemp, Log,
                TEXT("[ScreenFade] ALPHA  PC='%s'  ViewTarget='%s'  bEnableFading=%d  FadeAmount=%.4f"),
                *PC->GetName(),
                VT ? *VT->GetName() : TEXT("NULL"),
                CamMgr->bEnableFading ? 1 : 0,
                CamMgr->FadeAmount);
        }

        // Stop monitoring when fade settles.
        // FadeAmount=0 = screen CLEAR (fade-in complete).
        // FadeAmount=1 = screen BLACK (fade-out complete or snap-to-black).
        if (!CamMgr->bEnableFading || CamMgr->FadeAmount <= 0.001f || CamMgr->FadeAmount >= 0.999f)
        {
            const bool bIsBlack = CamMgr->FadeAmount >= 0.999f;
            if (ShouldLog()) {
                UE_LOG(LogTemp, Log,
                    TEXT("[ScreenFade] Fade settled  alpha=%.4f  screen=%s -- monitoring stopped"),
                    CamMgr->FadeAmount,
                    bIsBlack ? TEXT("BLACK") : TEXT("CLEAR"));
            }
            bFadeMonitorActive = false;
        }
    }
}

// ---------------------------------------------------------------------------
//  Internal_SnapToBlack  --  instant alpha=1 (no animation)
// ---------------------------------------------------------------------------
void UScreenFadeComponent::Internal_SnapToBlack(APlayerController* PC, const FLinearColor& Color)
{
    if (!PC || !PC->PlayerCameraManager) return;

    APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
    CamMgr->bEnableFading = true;

    CamMgr->StopCameraFade();

    CamMgr->StartCameraFade(
        1.f,   // from
        1.f,   // to
        0.001f,   // duration instant
        Color,
        true,
        true   // hold black
    );
}

// ---------------------------------------------------------------------------
//  Internal_StartFade
//  Calls APlayerCameraManager::StartCameraFade which is UE5 built-in.
//
//  StartCameraFade(Duration, FromAlpha, ToAlpha, Color, bShouldFadeAudio, bHoldWhenFinished)
//    FromAlpha=1 ToAlpha=0 -> fade in  (starts black, ends clear)
//    FromAlpha=0 ToAlpha=1 -> fade out (starts clear, ends black)
// ---------------------------------------------------------------------------
void UScreenFadeComponent::Internal_StartFade(APlayerController* PC,
    float Duration,
    bool bFromBlack,
    const FLinearColor& Color)
{
    if (!PC)
    {
        if (ShouldLog()) {
            UE_LOG(LogTemp, Warning, TEXT("[ScreenFade] Internal_StartFade — null PC"));
        }
        return;
    }

    if (!PC->PlayerCameraManager)
    {
        if (ShouldLog()) {
            UE_LOG(LogTemp, Warning,
                TEXT("[ScreenFade] Internal_StartFade — no PlayerCameraManager on PC='%s'"),
                *PC->GetName());
        }
        return;
    }

    APlayerCameraManager* CamMgr = PC->PlayerCameraManager;

    // Stop any currently held fade before starting a new one.
    // Without this, a held fade (bHoldWhenFinished=true) will resist
    // being overridden by the next StartCameraFade call in UE5.
    CamMgr->StopCameraFade();

    // Force fading enabled
    CamMgr->bEnableFading = true;

    // If there is no view target (happens after UnPossess), use the camera manager itself
    if (!PC->GetViewTarget())
    {
        if (ShouldLog())
            UE_LOG(LogTemp, Log,
                TEXT("[ScreenFade] No view target on PC='%s' -- setting CameraManager as view target"),
                *PC->GetName());
        PC->SetViewTarget(CamMgr);
    }

    // FadeAmount=1 = FULLY BLACK overlay, FadeAmount=0 = NO overlay (clear screen).
    // FadeIn  (bFromBlack=true):  From=1(black) -> To=0(clear).  Screen clears from black.
    // FadeOut (bFromBlack=false): From=0(clear) -> To=1(black).  Screen darkens to black.
    const float FromAlpha = bFromBlack ? 1.f : 0.f;
    const float ToAlpha = bFromBlack ? 0.f : 1.f;

    if (ShouldLog())
    {
        AActor* VT = PC->GetViewTarget();
        UE_LOG(LogTemp, Log,
            TEXT("[ScreenFade] StartCameraFade  PC='%s'  ViewTarget='%s'  Duration=%.2f  From=%.1f->To=%.1f  bFromBlack=%d  bEnablingFading=%d  CurrentAlpha=%.4f"),
            *PC->GetName(),
            VT ? *VT->GetName() : TEXT("NULL"),
            Duration, FromAlpha, ToAlpha, bFromBlack ? 1 : 0,
            CamMgr->bEnableFading ? 1 : 0,
            CamMgr->FadeAmount);
    }

    CamMgr->StartCameraFade(FromAlpha, ToAlpha, Duration, Color, true, true);

    // Start per-frame alpha monitoring
    bFadeMonitorActive = true;
    MonitoredPC = PC;
    FadeMonitorTimer = 0.f;

    if (ShouldLog()) {
        UE_LOG(LogTemp, Log,
            TEXT("[ScreenFade] Fade started -- monitoring alpha every ~50ms until settled"));
    }
}

// ===========================================================================
//  NAMED FADE LIBRARY API  (Phase 7)
// ===========================================================================

// ---------------------------------------------------------------------------
//  InitFadeLibrary  -- merge entries from source into runtime library
// ---------------------------------------------------------------------------
void UScreenFadeComponent::InitFadeLibrary(const TMap<FName, FScreenFadeSettings>& Library)
{
    for (const TPair<FName, FScreenFadeSettings>& Pair : Library)
        FadeLibrary.Add(Pair.Key, Pair.Value);

    if (ShouldLog())
        UE_LOG(LogTemp, Log,
            TEXT("[ScreenFade] FadeLibrary initialised/merged -- %d entries total"),
            FadeLibrary.Num());
}

// ---------------------------------------------------------------------------
//  SetFadeEntry
// ---------------------------------------------------------------------------
void UScreenFadeComponent::SetFadeEntry(FName Key, const FScreenFadeSettings& Settings)
{
    FadeLibrary.Add(Key, Settings);

    if (ShouldLog())
        UE_LOG(LogTemp, Log,
            TEXT("[ScreenFade] FadeLibrary entry set: '%s'"), *Key.ToString());
}

// ---------------------------------------------------------------------------
//  SetNextFade  -- change the chain target of an existing entry at runtime
// ---------------------------------------------------------------------------
void UScreenFadeComponent::SetNextFade(FName FadeKey, FName NextFadeKey)
{
    FScreenFadeSettings* Entry = FadeLibrary.Find(FadeKey);
    if (!Entry)
    {
        if (ShouldLog())
            UE_LOG(LogTemp, Warning,
                TEXT("[ScreenFade] SetNextFade: key '%s' not found in library"),
                *FadeKey.ToString());
        return;
    }

    Entry->NextFadeName = NextFadeKey;

    if (ShouldLog())
        UE_LOG(LogTemp, Log,
            TEXT("[ScreenFade] SetNextFade: '%s' -> '%s'"),
            *FadeKey.ToString(), *NextFadeKey.ToString());
}

// ---------------------------------------------------------------------------
//  PlayFadeIn  -- trigger the fade-in portion of a named entry
// ---------------------------------------------------------------------------
void UScreenFadeComponent::PlayFadeIn(APlayerController* PC, FName FadeKey)
{
    if (!PC) return;

    const FScreenFadeSettings* Entry = FadeLibrary.Find(FadeKey);
    if (!Entry)
    {
        if (ShouldLog())
            UE_LOG(LogTemp, Warning,
                TEXT("[ScreenFade] PlayFadeIn: key '%s' not found in library"),
                *FadeKey.ToString());
        return;
    }

    if (ShouldLog())
        UE_LOG(LogTemp, Log,
            TEXT("[ScreenFade] PlayFadeIn('%s')  PC='%s'"),
            *FadeKey.ToString(), *PC->GetName());

    FadeIn(PC, *Entry);
}

// ---------------------------------------------------------------------------
//  PlayFadeOut  -- trigger the fade-out portion of a named entry
// ---------------------------------------------------------------------------
void UScreenFadeComponent::PlayFadeOut(APlayerController* PC, FName FadeKey)
{
    if (!PC) return;

    const FScreenFadeSettings* Entry = FadeLibrary.Find(FadeKey);
    if (!Entry)
    {
        if (ShouldLog())
            UE_LOG(LogTemp, Warning,
                TEXT("[ScreenFade] PlayFadeOut: key '%s' not found in library"),
                *FadeKey.ToString());
        return;
    }

    if (ShouldLog())
        UE_LOG(LogTemp, Log,
            TEXT("[ScreenFade] PlayFadeOut('%s')  PC='%s'  NextFade='%s'"),
            *FadeKey.ToString(), *PC->GetName(), *Entry->NextFadeName.ToString());

    FadeOut(PC, *Entry);
}

// ---------------------------------------------------------------------------
//  GetFadeEntry
// ---------------------------------------------------------------------------
FScreenFadeSettings UScreenFadeComponent::GetFadeEntry(FName Key) const
{
    const FScreenFadeSettings* Found = FadeLibrary.Find(Key);
    return Found ? *Found : FScreenFadeSettings{};
}

// ---------------------------------------------------------------------------
//  HasFadeEntry
// ---------------------------------------------------------------------------
bool UScreenFadeComponent::HasFadeEntry(FName Key) const
{
    return FadeLibrary.Contains(Key);
}


// ---------------------------------------------------------------------------
//  Safe Transition Transform FScreenSettings
// ---------------------------------------------------------------------------

FScreenFadeSettings SafeTransitionTransform(FScreenFadeSettings pFade, float MaxDuration, bool TransitionIn) {

    FScreenFadeSettings FadeRet = pFade;

    float TransitionDuration = 0.f;
    float FadeDurationCap = 0.f;
    float BlackScreenDurationCap = 0.f;
    bool bBlackSreenCap = false;

    // Gathering values
    if (TransitionIn) {

        bBlackSreenCap = pFade.bBlackScreenIn;

        if (pFade.bFadeIn && pFade.FadeInDuration > 0.f) {
            FadeDurationCap = pFade.FadeOutDuration;
        }
        if (bBlackSreenCap && pFade.BlackScreenInDuration > 0.f) {
            BlackScreenDurationCap = pFade.BlackScreenInDuration;
        }
    }
    else {
        bBlackSreenCap = pFade.bBlackScreenOut;

        if (pFade.bFadeOut && pFade.FadeOutDuration > 0.f) {
            FadeDurationCap = pFade.FadeOutDuration;
        }
        if (bBlackSreenCap && pFade.BlackScreenOutDuration > 0.f) {
            BlackScreenDurationCap = pFade.BlackScreenOutDuration;
        }
    }

    // If the fade itself is too long, we shorten it and skip the BlackScreen
    if (FadeDurationCap >= MaxDuration) {
        TransitionDuration = MaxDuration;
        FadeDurationCap = MaxDuration;
        bBlackSreenCap = false;
    }
    else {
        // BlackScreen is possible, if activated
        TransitionDuration = FadeDurationCap + BlackScreenDurationCap;

        // If Transition is too long but not the fade, it means we have BlackScreen, we can shorten it 
        if (TransitionDuration > MaxDuration) {
            BlackScreenDurationCap = MaxDuration - FadeDurationCap; // Certainly > 0.f
            //TransitionDuration = FadeDurationCap + BlackScreenDurationCap;
        }
    }

    // Readjusting the Fade struct
    if (TransitionIn) {
        FadeRet.bBlackScreenIn = bBlackSreenCap;
        FadeRet.BlackScreenInDuration = BlackScreenDurationCap;
        FadeRet.FadeInDuration = FadeDurationCap;
    }
    else {
        FadeRet.bBlackScreenOut = bBlackSreenCap;
        FadeRet.BlackScreenOutDuration = BlackScreenDurationCap;
        FadeRet.FadeOutDuration = FadeDurationCap;
    }

    return FadeRet;
}