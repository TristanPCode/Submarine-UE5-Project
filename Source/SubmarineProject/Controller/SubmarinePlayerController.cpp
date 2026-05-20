// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarinePlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Submarine/SubmarinePawn.h"
#include "SubmarineViewportClient.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

ASubmarinePlayerController::ASubmarinePlayerController()
{
    // Nothing special in constructor -- components are added via SpawnManager
}

// ---------------------------------------------------------------------------
//  BeginPlay
// ---------------------------------------------------------------------------
void ASubmarinePlayerController::BeginPlay()
{
    Super::BeginPlay();

    // Log A: LocalPlayer identity at creation time
    ULocalPlayer* LP = GetLocalPlayer();
    FPlatformUserId PlatformUser = LP ? LP->GetPlatformUserId() : PLATFORMUSERID_NONE;

    // Log what devices this LocalPlayer already owns at BeginPlay
    TArray<FInputDeviceId> OwnedDevices;
    if (LP)
        IPlatformInputDeviceMapper::Get().GetAllInputDevicesForUser(
            PlatformUser, OwnedDevices);

    UE_LOG(LogTemp, Log,
        TEXT("[SubmarinePC|A] BeginPlay: PC='%s'  IsLocalCtrl=%d  "
            "ControllerId=%d  HasLP=%d  PlatformUserId=%d  "
            "DevicesAlreadyOwned=%d"),
        *GetName(),
        IsLocalController() ? 1 : 0,
        LP ? LP->GetControllerId() : -1,
        LP != nullptr ? 1 : 0,
        PlatformUser.GetInternalId(),
        OwnedDevices.Num());

    for (const FInputDeviceId& Dev : OwnedDevices)
        UE_LOG(LogTemp, Log,
            TEXT("[SubmarinePC|A]   OwnedDevice: DeviceId=%d  State=%d"),
            Dev.GetId(),
            (int32)IPlatformInputDeviceMapper::Get()
            .GetInputDeviceConnectionState(Dev));
}

// ---------------------------------------------------------------------------
//  EndPlay
// ---------------------------------------------------------------------------
void ASubmarinePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
//  AssignInputDevice
// ---------------------------------------------------------------------------
void ASubmarinePlayerController::AssignInputDevice(
    EInputDeviceType Device,
    UInputMappingContext* KeyboardIMC,
    UInputMappingContext* GamepadIMC)
{
    AssignedInputDevice = Device;

    UEnhancedInputLocalPlayerSubsystem* InputSub = GetEnhancedInputSubsystem();
    if (!InputSub)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SubmarinePC] AssignInputDevice: "
                "no EnhancedInputLocalPlayerSubsystem for PC='%s'"),
            *GetName());
        return;
    }

    ULocalPlayer* MyLP = GetLocalPlayer();
    const FPlatformUserId MyUserId =
        MyLP ? MyLP->GetPlatformUserId() : PLATFORMUSERID_NONE;

    // Log B: device assignment start
    UE_LOG(LogTemp, Log,
        TEXT("[SubmarinePC|B] AssignInputDevice START: PC='%s'  Device=%d  "
            "LP=%s  PlatformUserId=%d"),
        *GetName(), (int32)Device,
        MyLP ? *MyLP->GetName() : TEXT("NULL"),
        MyUserId.GetInternalId());

    // -----------------------------------------------------------------------
    //  Log C: full device mapper state BEFORE remap
    // -----------------------------------------------------------------------
    if (Device != EInputDeviceType::Keyboard)
    {
        const int32 GamepadSlot = GetGamepadSlot();
        IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();

        // Dump all connected devices and their current owners
        TArray<FInputDeviceId> AllDevices;
        Mapper.GetAllConnectedInputDevices(AllDevices);
        UE_LOG(LogTemp, Log,
            TEXT("[SubmarinePC|C] PRE-REMAP: %d connected devices total. "
                "Targeting slot=%d for PC='%s'"),
            AllDevices.Num(), GamepadSlot, *GetName());

        for (const FInputDeviceId& Dev : AllDevices)
        {
            FPlatformUserId PlatformOwner = Mapper.GetUserForInputDevice(Dev);
            UE_LOG(LogTemp, Log,
                TEXT("[SubmarinePC|C]   Device=%d  CurrentOwner=%d"),
                Dev.GetId(), PlatformOwner.GetInternalId());
        }

        // Resolve our target gamepad slot
        FPlatformUserId CurrentOwner = PLATFORMUSERID_NONE;
        FInputDeviceId  DeviceId = INPUTDEVICEID_NONE;
        Mapper.RemapControllerIdToPlatformUserAndDevice(
            GamepadSlot, CurrentOwner, DeviceId);

        UE_LOG(LogTemp, Log,
            TEXT("[SubmarinePC|C] Resolved slot=%d -> DeviceId=%d  "
                "CurrentOwner=%d  TargetUser=%d"),
            GamepadSlot, DeviceId.GetId(),
            CurrentOwner.GetInternalId(), MyUserId.GetInternalId());

        if (DeviceId == INPUTDEVICEID_NONE)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[SubmarinePC|C] Gamepad slot=%d not found -- "
                    "is it connected? Cannot remap."),
                GamepadSlot);
        }
        else if (CurrentOwner == MyUserId)
        {
            UE_LOG(LogTemp, Log,
                TEXT("[SubmarinePC|C] Gamepad slot=%d already owned by "
                    "correct user -- no remap needed"),
                GamepadSlot);
        }
        else
        {
            // Perform the remap
            const bool bRemapped = Mapper.Internal_ChangeInputDeviceUserMapping(
                DeviceId, MyUserId, CurrentOwner);

            UE_LOG(LogTemp, Log,
                TEXT("[SubmarinePC|C] POST-REMAP: slot=%d  DeviceId=%d  "
                    "%d -> %d  Success=%d"),
                GamepadSlot, DeviceId.GetId(),
                CurrentOwner.GetInternalId(), MyUserId.GetInternalId(),
                bRemapped ? 1 : 0);

            // Verify the remap actually took effect
            FPlatformUserId NewOwner = Mapper.GetUserForInputDevice(DeviceId);
            UE_LOG(LogTemp, Log,
                TEXT("[SubmarinePC|C] VERIFY: DeviceId=%d now owned by UserId=%d "
                    "(expected %d)  Match=%d"),
                DeviceId.GetId(),
                NewOwner.GetInternalId(), MyUserId.GetInternalId(),
                (NewOwner == MyUserId) ? 1 : 0);

            // KEY FIX: UE may re-assign the device back to Player0 on the same
            // frame during LocalPlayer setup. Defer a second remap to ensure
            // our assignment survives the engine's default routing pass.
            if (UWorld* W = GetWorld())
            {
                TWeakObjectPtr<ASubmarinePlayerController> WeakThis(this);
                const FInputDeviceId CapturedDeviceId = DeviceId;
                const FPlatformUserId CapturedTarget = MyUserId;

                W->GetTimerManager().SetTimerForNextTick(
                    [WeakThis, CapturedDeviceId, CapturedTarget]()
                    {
                        if (!WeakThis.IsValid()) return;
                        IPlatformInputDeviceMapper& M =
                            IPlatformInputDeviceMapper::Get();
                        FPlatformUserId StillOwner =
                            M.GetUserForInputDevice(CapturedDeviceId);
                        if (StillOwner != CapturedTarget)
                        {
                            UE_LOG(LogTemp, Warning,
                                TEXT("[SubmarinePC|C] DEFERRED: Device=%d was "
                                    "re-assigned to %d -- forcing back to %d"),
                                CapturedDeviceId.GetId(),
                                StillOwner.GetInternalId(),
                                CapturedTarget.GetInternalId());
                            M.Internal_ChangeInputDeviceUserMapping(
                                CapturedDeviceId, CapturedTarget, StillOwner);
                        }
                        else
                        {
                            UE_LOG(LogTemp, Log,
                                TEXT("[SubmarinePC|C] DEFERRED: Device=%d "
                                    "correctly owned by %d -- stable"),
                                CapturedDeviceId.GetId(),
                                CapturedTarget.GetInternalId());
                        }

                        // Force Enhanced Input to re-discover device ownership
                        if (WeakThis.IsValid())
                        {
                            if (UEnhancedInputLocalPlayerSubsystem* Sub =
                                WeakThis->GetEnhancedInputSubsystem())
                            {
                                Sub->RequestRebuildControlMappings();
                                UE_LOG(LogTemp, Log,
                                    TEXT("[SubmarinePC|C] DEFERRED: "
                                        "RequestRebuildControlMappings called "
                                        "for PC='%s'"),
                                    *WeakThis->GetName());
                            }
                        }
                    });
            }

            // Also force an immediate rebuild in case the deferred tick is
            // too late for the first input frame
            InputSub->RequestRebuildControlMappings();
        }
    }

    // -----------------------------------------------------------------------
    //  Apply IMCs: remove ours if present, then add the correct one
    //  Never ClearAllMappings -- it wipes other players' contexts
    // -----------------------------------------------------------------------
    if (KeyboardIMC && InputSub->HasMappingContext(KeyboardIMC))
        InputSub->RemoveMappingContext(KeyboardIMC);
    if (GamepadIMC && InputSub->HasMappingContext(GamepadIMC))
        InputSub->RemoveMappingContext(GamepadIMC);

    UInputMappingContext* IMCToApply = nullptr;
    switch (Device)
    {
    case EInputDeviceType::Keyboard:
        IMCToApply = KeyboardIMC;
        CachedKeyboardIMC = KeyboardIMC;
        break;
    case EInputDeviceType::Gamepad0:
    case EInputDeviceType::Gamepad1:
    case EInputDeviceType::Gamepad2:
    case EInputDeviceType::Gamepad3:
        IMCToApply = GamepadIMC;
        break;
    }

    if (IMCToApply)
    {
        InputSub->AddMappingContext(IMCToApply, 0);
        UE_LOG(LogTemp, Log,
            TEXT("[SubmarinePC|B] AddMappingContext: PC='%s'  IMC='%s'  "
                "Active=%d"),
            *GetName(), *IMCToApply->GetName(),
            InputSub->HasMappingContext(IMCToApply) ? 1 : 0);
    }
    else
        UE_LOG(LogTemp, Warning,
            TEXT("[SubmarinePC|B] NO IMC for PC='%s'  Device=%d"),
            *GetName(), (int32)Device);

    // -----------------------------------------------------------------------
    //  Keyboard: rebuild viewport client partition
    // -----------------------------------------------------------------------
    if (Device == EInputDeviceType::Keyboard)
    {
        if (UWorld* W = GetWorld())
        {
            if (USubmarineViewportClient* VC =
                Cast<USubmarineViewportClient>(W->GetGameViewport()))
            {
                TMap<int32, UInputMappingContext*> PlayerIMCMap;
                UGameInstance* GI = W->GetGameInstance();
                if (GI)
                {
                    const TArray<ULocalPlayer*>& LPs = GI->GetLocalPlayers();
                    for (int32 i = 0; i < LPs.Num(); ++i)
                    {
                        APlayerController* OtherPC =
                            LPs[i] ? LPs[i]->GetPlayerController(W) : nullptr;
                        ASubmarinePlayerController* SubPC =
                            Cast<ASubmarinePlayerController>(OtherPC);
                        if (!SubPC || !SubPC->CachedKeyboardIMC) continue;
                        if (SubPC->AssignedInputDevice == EInputDeviceType::Keyboard)
                            PlayerIMCMap.Add(i, SubPC->CachedKeyboardIMC);
                    }
                }
                VC->RebuildKeyboardPartition(PlayerIMCMap);
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[SubmarinePC|B] GameViewport is not "
                        "USubmarineViewportClient -- keyboard partition not built. "
                        "Set GameViewportClientClassName in DefaultEngine.ini."));
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Tell pawn which device it uses for sensitivity selection
    // -----------------------------------------------------------------------
    if (ASubmarinePawn* SubPawn = Cast<ASubmarinePawn>(GetPawn()))
    {
        SubPawn->bUsesGamepad = (Device != EInputDeviceType::Keyboard);
        UE_LOG(LogTemp, Log,
            TEXT("[SubmarinePC|B] Set bUsesGamepad=%d on pawn '%s'"),
            SubPawn->bUsesGamepad ? 1 : 0, *SubPawn->GetName());
    }

    // Log D: final summary
    UE_LOG(LogTemp, Log,
        TEXT("[SubmarinePC|D] AssignInputDevice DONE: PC='%s'  Device=%d  "
            "IMC=%s  PlatformUser=%d"),
        *GetName(), (int32)Device,
        IMCToApply ? *IMCToApply->GetName() : TEXT("NULL"),
        MyUserId.GetInternalId());
}

// ---------------------------------------------------------------------------
//  ReapplyInputMappings
// ---------------------------------------------------------------------------
void ASubmarinePlayerController::ReapplyInputMappings(
    UInputMappingContext* KeyboardIMC,
    UInputMappingContext* GamepadIMC)
{
    AssignInputDevice(AssignedInputDevice, KeyboardIMC, GamepadIMC);
}

// ---------------------------------------------------------------------------
//  IsInputDeviceAllowed  (kept for reference, no longer used for filtering)
// ---------------------------------------------------------------------------
bool ASubmarinePlayerController::IsInputDeviceAllowed(
    const FKey& Key, int32 ControllerId) const
{
    const bool bIsGamepadKey = Key.IsGamepadKey();
    switch (AssignedInputDevice)
    {
    case EInputDeviceType::Keyboard:    return !bIsGamepadKey;
    case EInputDeviceType::Gamepad0:    return bIsGamepadKey && (ControllerId == 0);
    case EInputDeviceType::Gamepad1:    return bIsGamepadKey && (ControllerId == 1);
    case EInputDeviceType::Gamepad2:    return bIsGamepadKey && (ControllerId == 2);
    case EInputDeviceType::Gamepad3:    return bIsGamepadKey && (ControllerId == 3);
    default:                            return true;
    }
}

// ---------------------------------------------------------------------------
//  GetGamepadSlot
// ---------------------------------------------------------------------------
int32 ASubmarinePlayerController::GetGamepadSlot() const
{
    switch (AssignedInputDevice)
    {
    case EInputDeviceType::Gamepad0: return 0;
    case EInputDeviceType::Gamepad1: return 1;
    case EInputDeviceType::Gamepad2: return 2;
    case EInputDeviceType::Gamepad3: return 3;
    default:                         return -1;  // keyboard
    }
}

// ---------------------------------------------------------------------------
//  GetEnhancedInputSubsystem
// ---------------------------------------------------------------------------
UEnhancedInputLocalPlayerSubsystem* ASubmarinePlayerController::GetEnhancedInputSubsystem() const
{
    ULocalPlayer* LP = GetLocalPlayer();
    if (!LP) return nullptr;
    return LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
}