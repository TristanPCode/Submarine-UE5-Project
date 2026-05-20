// Fill out your copyright notice in the Description page of Project Settings.

#include "SpecialTorpedoSlotModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

// ---------------------------------------------------------------------------
//  InitSlot
// ---------------------------------------------------------------------------
void USpecialTorpedoSlotModule::InitSlot(int32 InSlotIndex, ETorpedoType InSlotType)
{
    SlotIndex = InSlotIndex;
    SlotType = InSlotType;

    if (!CreateMID())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SpecialSlot] InitSlot: MID creation failed for slot %d (module '%s'). "
                "Ensure MatSpecialSlot is set in FHUDModuleConfig."),
            SlotIndex, *Config.ModuleName.ToString());
        return;
    }

    // Push optional glow mask (same for all states)
    if (UTexture2D* GlowMask = Config.GetTexture(HUDConfigKeys::GlowMaskTex))
        SlotMID->SetTextureParameterValue(TEXT("GlowMask"), GlowMask);

    SlotMID->SetScalarParameterValue(TEXT("GlowPulseSpeed"),
        Config.GetFloat(HUDConfigKeys::GlowPulseSpeed, 1.5f));

    // Start fully transparent until parent calls SetSlotState
    SlotMID->SetScalarParameterValue(TEXT("State"), 0.f);
    SlotMID->SetScalarParameterValue(TEXT("GlowIntensity"), 0.f);
}

// ---------------------------------------------------------------------------
//  SetSlotState
// ---------------------------------------------------------------------------
void USpecialTorpedoSlotModule::SetSlotState(ESpecialSlotState NewState)
{
    if (!SlotMID) return;

    CurrentState = NewState;

    // Select texture based on state
    const bool bShowReadyTex = (CurrentState == ESpecialSlotState::Ready);
    UTexture2D* Tex = Config.GetIconForType(SlotType, bShowReadyTex);
    if (Tex)
    {
        SlotMID->SetTextureParameterValue(TEXT("MainTexture"), Tex);
        UE_LOG(LogTemp, Log,
            TEXT("[SpecialSlot] SetSlotState: slot=%d type=%d state=%s tex=%s"),
            SlotIndex, (int32)SlotType,
            bShowReadyTex ? TEXT("Ready") : TEXT("Cooldown"),
            *Tex->GetName());
    }
    else
    {
        // IconPerType map in DA does not have an entry for this ETorpedoType.
        // Add the type key to IconPerType in your HUD DataAsset.
        UE_LOG(LogTemp, Warning,
            TEXT("[SpecialSlot] SetSlotState: no %s icon for type=%d slot=%d -- "
                "check IconPerType map in DA has key for this ETorpedoType"),
            bShowReadyTex ? TEXT("Ready") : TEXT("Cooldown"),
            (int32)SlotType, SlotIndex);
    }

    // State encoding matches M_SpecialSlot material branches:
    //   0 = Used     -> dimmed (multiply by 0.3 in material)
    //   1 = Ready    -> full color + glow
    //   2 = Cooldown -> blue tint in material
    const float StateValue =
        (CurrentState == ESpecialSlotState::Ready) ? 1.f :
        (CurrentState == ESpecialSlotState::Cooldown) ? 2.f :
        0.f;

    SlotMID->SetScalarParameterValue(TEXT("State"), StateValue);

    // Kill glow when not ready
    if (CurrentState != ESpecialSlotState::Ready)
        SlotMID->SetScalarParameterValue(TEXT("GlowIntensity"), 0.f);

    SetContinuousTickEnabled(CurrentState == ESpecialSlotState::Ready);
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
// ---------------------------------------------------------------------------
void USpecialTorpedoSlotModule::RefreshVisuals_Implementation()
{
    // Re-create MID if not yet done (Config just became valid)
    if (!SlotMID)
        CreateMID();

    // Re-push texture -- Config may have changed
    if (SlotMID)
    {
        if (UTexture2D* GlowMask = Config.GetTexture(HUDConfigKeys::GlowMaskTex))
            SlotMID->SetTextureParameterValue(TEXT("GlowMask"), GlowMask);
        SlotMID->SetScalarParameterValue(TEXT("GlowPulseSpeed"),
            Config.GetFloat(HUDConfigKeys::GlowPulseSpeed, 1.5f));
    }

    ApplyStateToMaterial();
}

// ---------------------------------------------------------------------------
//  NativeTick  (only active when Ready)
// ---------------------------------------------------------------------------
void USpecialTorpedoSlotModule::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (!SlotMID || CurrentState != ESpecialSlotState::Ready) return;

    GlowTimer += InDeltaTime;
    const float PulseSpeed = Config.GetFloat(HUDConfigKeys::GlowPulseSpeed, 1.5f);
    const float Glow = FMath::Sin(GlowTimer * PulseSpeed) * 0.5f + 0.5f;
    SlotMID->SetScalarParameterValue(TEXT("GlowIntensity"), Glow);
}

// ---------------------------------------------------------------------------
//  ApplyStateToMaterial
// ---------------------------------------------------------------------------
void USpecialTorpedoSlotModule::ApplyStateToMaterial()
{
    if (!SlotMID) return;

    // State encoding: 0=Used, 1=Ready, 2=Cooldown
    const float StateValue =
        (CurrentState == ESpecialSlotState::Ready) ? 1.f :
        (CurrentState == ESpecialSlotState::Cooldown) ? 2.f : 0.f;

    SlotMID->SetScalarParameterValue(TEXT("State"), StateValue);

    // Zero glow when not ready
    if (CurrentState != ESpecialSlotState::Ready)
        SlotMID->SetScalarParameterValue(TEXT("GlowIntensity"), 0.f);
}

// ---------------------------------------------------------------------------
//  CreateMID
// ---------------------------------------------------------------------------
bool USpecialTorpedoSlotModule::CreateMID()
{
    UMaterialInterface* Base = Config.GetMaterial(HUDConfigKeys::MatSpecialSlot);
    if (!Base) return false;

    SlotMID = UMaterialInstanceDynamic::Create(Base, this);
    if (!SlotMID) return false;

    if (SlotImage)
        SlotImage->SetBrushFromMaterial(SlotMID);
    else
        UE_LOG(LogTemp, Warning,
            TEXT("[SpecialSlot] CreateMID: SlotImage is null for slot %d. "
                "Ensure Blueprint has a UImage named 'SlotImage'."),
            SlotIndex);

    return true;
}