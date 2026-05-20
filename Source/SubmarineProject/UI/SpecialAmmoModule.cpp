// Fill out your copyright notice in the Description page of Project Settings.

#include "SpecialAmmoModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

// ---------------------------------------------------------------------------
//  NativeOnInitialized
// ---------------------------------------------------------------------------
void USpecialAmmoModule::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogSpecialAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[SpecialAmmo] NativeOnInitialized  Module='%s'"),
            *Config.ModuleName.ToString());
}

// ---------------------------------------------------------------------------
//  BindToDataSource
// ---------------------------------------------------------------------------
void USpecialAmmoModule::BindToDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj))
    {
        if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogSpecialAmmo) : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[SpecialAmmo] BindToDataSource: invalid DataSource"));
        return;
    }

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogSpecialAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[SpecialAmmo] BindToDataSource: '%s'"), *Obj->GetName());

    DataSource->GetOnAmmoChangedDelegate().AddDynamic(
        this, &USpecialAmmoModule::OnAmmoChanged);
    DataSource->GetOnFireCooldownDelegate().AddDynamic(
        this, &USpecialAmmoModule::OnFireCooldownComplete);

    // Slots have no direct bindings -- parent drives state
    // No PropagateDataSourceToChildren needed (slots ignore SetDataSource)
    RefreshAllSlotStates();
}

// ---------------------------------------------------------------------------
//  UnbindFromDataSource
// ---------------------------------------------------------------------------
void USpecialAmmoModule::UnbindFromDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj)) return;

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogSpecialAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[SpecialAmmo] UnbindFromDataSource: '%s'"), *Obj->GetName());

    DataSource->GetOnAmmoChangedDelegate().RemoveDynamic(
        this, &USpecialAmmoModule::OnAmmoChanged);
    DataSource->GetOnFireCooldownDelegate().RemoveDynamic(
        this, &USpecialAmmoModule::OnFireCooldownComplete);
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
// ---------------------------------------------------------------------------
void USpecialAmmoModule::RefreshVisuals_Implementation()
{
    // RefreshVisuals fires after SetConfig -- Config is valid here.
    if (!bSlotsCreated)
        CreateSlots();

    if (bSlotsCreated && IsValid(DataSource.GetObject()))
        RefreshAllSlotStates();
}

// ---------------------------------------------------------------------------
//  CreateSlots
// ---------------------------------------------------------------------------
void USpecialAmmoModule::CreateSlots()
{
    if (bSlotsCreated) return;

    if (!GridCanvas)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SpecialAmmo] CreateSlots: GridCanvas is null for '%s'. "
                "Ensure Blueprint has a UCanvasPanel named 'GridCanvas'."),
            *Config.ModuleName.ToString());
        return;
    }

    const FVector2D CanvasSize = GridCanvas->GetCachedGeometry().GetLocalSize();
    if (CanvasSize.IsNearlyZero())
        return;  // NativeOnInitialized will retry

    // We need a valid DataSource to know capacity.
    // If DataSource is not yet set, defer to BindToDataSource path via RefreshVisuals.
    // Capacity is needed here, so we read it from DataSource if available,
    // or fall back to SlotTypes array size.
    int32 Capacity = 0;
    if (IsValid(DataSource.GetObject()))
        Capacity = DataSource->GetSpecialAmmoCapacity();
    else
        Capacity = 3;

    if (Capacity <= 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SpecialAmmo] CreateSlots: capacity is 0 for '%s'. "
                "Ensure DataSource is set before NativeOnInitialized, or "
                "populate SlotTypes in the config as a fallback."),
            *Config.ModuleName.ToString());
        return;
    }

    const int32 Columns = FMath::Max(1,
        (int32)Config.GetFloat(HUDConfigKeys::GridColumns, 4.f));

    // Resolve row layout
    TArray<int32> RowLayout = ResolveRowCounts(Capacity, Columns);

    // Compute slot positions
    TArray<FSlotLayoutEntry> Layout = ComputeGridLayout(Capacity, CanvasSize, RowLayout);

    // Determine slot class
    TSubclassOf<UBaseHUDModule> SlotClass = USpecialTorpedoSlotModule::StaticClass();
    if (Config.SlotModuleClass)
        SlotClass = Config.SlotModuleClass;

    // Create and position slots
    for (int32 i = 0; i < Capacity; ++i)
    {
        FHUDModuleConfig SlotCfg = Config;
        SlotCfg.ModuleName = FName(*FString::Printf(TEXT("%s_Slot%02d"),
            *Config.ModuleName.ToString(), i));
        SlotCfg.ModuleClass = nullptr;
        SlotCfg.bEnabled = true;
        SlotCfg.SizeOverride = FVector2D::ZeroVector;
        SlotCfg.PositionOffset = FVector2D::ZeroVector;
        SlotCfg.Anchors = FAnchors(0.f, 0.f, 0.f, 0.f);

        // Use pre-computed absolute values -- CreateChildModule expects normalized,
        // so convert back.  We pass CanvasSize=1,1 and use absolute pos/size directly
        // by treating NormalizedPos as the absolute position. To avoid double-scaling,
        // we create the child manually here (same logic as CreateChildModule but with
        // absolute values directly).

        UBaseHUDModule* ChildBase = CreateWidget<UBaseHUDModule>(
            GetOwningPlayer(), SlotClass);

        if (!ChildBase)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[SpecialAmmo] CreateSlots: CreateWidget returned null for slot %d"), i);
            continue;
        }

        ChildBase->DebugSettings = DebugSettings;

        UCanvasPanelSlot* CanvasSlot = GridCanvas->AddChildToCanvas(ChildBase);
        if (!CanvasSlot)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[SpecialAmmo] CreateSlots: AddChildToCanvas returned null for slot %d"), i);
            continue;
        }

        if (Layout.IsValidIndex(i))
        {
            CanvasSlot->SetPosition(Layout[i].Position);
            CanvasSlot->SetSize(Layout[i].Size);
            CanvasSlot->SetAutoSize(false);
            CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
            CanvasSlot->SetAlignment(FVector2D::ZeroVector);
        }

        ChildBase->SetConfig(SlotCfg);

        USpecialTorpedoSlotModule* ChildCanvaSlot = Cast<USpecialTorpedoSlotModule>(ChildBase);
        if (ChildCanvaSlot)
        {
            // Read torpedo type from the DataSource (torpedo component)
            // so the DA SlotTypes array never needs to be filled manually.
            const bool bHasDS = IsValid(DataSource.GetObject());
            const ETorpedoType Type = IsValid(DataSource.GetObject())
                ? DataSource->GetSpecialTorpedoType()
                : ETorpedoType::Normal;
            ChildCanvaSlot->InitSlot(i, Type);
            SlotModules.Add(ChildCanvaSlot);
            UE_LOG(LogTemp, Log,
                TEXT("[SpecialAmmo] CreateSlots: slot=%d type=%d (from %s)"),
                i, (int32)Type,
                bHasDS ? TEXT("DataSource") : TEXT("Config.SlotTypes fallback"));
        }

        ChildModules.Add(ChildBase);
    }

    bSlotsCreated = true;

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleCreation && DebugSettings->bLogSpecialAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[SpecialAmmo] CreateSlots: created %d slots for '%s'  canvas=(%.0f,%.0f)"),
            Capacity, *Config.ModuleName.ToString(), CanvasSize.X, CanvasSize.Y);
}

// ---------------------------------------------------------------------------
//  ResolveRowCounts
// ---------------------------------------------------------------------------
TArray<int32> USpecialAmmoModule::ResolveRowCounts(int32 TotalSlots, int32 Columns) const
{
    TArray<int32> Result;

    if (!Config.bUseCustomRowCounts)
    {
        // Auto-fill: uniform rows of Columns
        int32 Remaining = TotalSlots;
        while (Remaining > 0)
        {
            const int32 ThisRow = FMath::Min(Remaining, Columns);
            Result.Add(ThisRow);
            Remaining -= ThisRow;
        }
        return Result;
    }

    // Custom row counts -- start from config definition
    Result = Config.RowCounts;

    // Remove empty or negative entries
    Result.RemoveAll([](int32 N) { return N <= 0; });

    int32 Sum = 0;
    for (int32 N : Result) Sum += N;

    if (Sum > TotalSlots)
    {
        // Overflow: trim from the end of the last rows
        int32 Excess = Sum - TotalSlots;
        for (int32 RowIdx = Result.Num() - 1; RowIdx >= 0 && Excess > 0; --RowIdx)
        {
            const int32 Trim = FMath::Min(Result[RowIdx], Excess);
            Result[RowIdx] -= Trim;
            Excess -= Trim;
            if (Result[RowIdx] == 0)
                Result.RemoveAt(RowIdx);
        }
    }
    else if (Sum < TotalSlots)
    {
        // Underflow: append remaining slots starting from the last row
        int32 ToAdd = TotalSlots - Sum;
        while (ToAdd > 0)
        {
            if (Result.Num() > 0)
            {
                const int32 Space = Columns - Result.Last();
                if (Space > 0)
                {
                    const int32 Fill = FMath::Min(Space, ToAdd);
                    Result.Last() += Fill;
                    ToAdd -= Fill;
                    continue;
                }
            }
            // Last row is full or no rows exist -- add a new row
            const int32 NewRowCount = FMath::Min(ToAdd, Columns);
            Result.Add(NewRowCount);
            ToAdd -= NewRowCount;
        }
    }

    return Result;
}

// ---------------------------------------------------------------------------
//  ComputeSlotSize
// ---------------------------------------------------------------------------
FVector2D USpecialAmmoModule::ComputeSlotSize(
    FVector2D CanvasSize,
    int32     NumRows,
    int32     Columns,
    float     SpacingX,
    float     SpacingY) const
{
    // Compute slot height: divide canvas height evenly among rows
    const float TotalSpacingY = SpacingY * FMath::Max(NumRows - 1, 0);
    const float SlotH = (CanvasSize.Y - TotalSpacingY) / FMath::Max(NumRows, 1);

    // Derive slot width from original texture aspect ratio
    const float TexW = Config.GetFloat(HUDConfigKeys::IconTextureWidth, 64.f);
    const float TexH = Config.GetFloat(HUDConfigKeys::IconTextureHeight, 64.f);
    const float AspectRatio = TexW / FMath::Max(TexH, 1.f);
    float SlotW = SlotH * AspectRatio;

    // If all columns + spacing exceeds canvas width, constrain by width instead
    const float TotalSpacingX = SpacingX * FMath::Max(Columns - 1, 0);
    const float MaxSlotW = (CanvasSize.X - TotalSpacingX) / FMath::Max(Columns, 1);
    if (SlotW > MaxSlotW)
    {
        SlotW = MaxSlotW;
        // Keep aspect ratio: re-derive height from constrained width
        // (slots may be shorter than canvas allows, but won't overflow width)
    }

    return FVector2D(FMath::Max(SlotW, 1.f), FMath::Max(SlotH, 1.f));
}

// ---------------------------------------------------------------------------
//  ComputeGridLayout
// ---------------------------------------------------------------------------
TArray<FSlotLayoutEntry> USpecialAmmoModule::ComputeGridLayout(
    int32                    TotalSlots,
    FVector2D                CanvasSize,
    const TArray<int32>& RowCounts) const
{
    TArray<FSlotLayoutEntry> Result;
    Result.Reserve(TotalSlots);

    if (RowCounts.IsEmpty() || TotalSlots == 0) return Result;

    const int32 Columns = FMath::Max(1,
        (int32)Config.GetFloat(HUDConfigKeys::GridColumns, 4.f));

    // Convert spacing from original-texture-space pixels to screen pixels.
    // We use IconTextureHeight as the reference dimension for Y,
    // IconTextureWidth for X, consistent with the rest of the system.
    const float TexW = Config.GetFloat(HUDConfigKeys::IconTextureWidth, 64.f);
    const float TexH = Config.GetFloat(HUDConfigKeys::IconTextureHeight, 64.f);

    const int32 NumRows = RowCounts.Num();
    const FVector2D SlotSize = ComputeSlotSize(CanvasSize, NumRows, Columns, 0.f, 0.f);

    // Re-compute spacing in screen pixels proportional to slot size
    const float SpacingX_screen =
        (Config.GetFloat(HUDConfigKeys::SlotSpacingX, 4.f) / FMath::Max(TexW, 1.f))
        * SlotSize.X;
    const float SpacingY_screen =
        (Config.GetFloat(HUDConfigKeys::SlotSpacingY, 4.f) / FMath::Max(TexH, 1.f))
        * SlotSize.Y;

    const ESlotRowAlignment Alignment = (ESlotRowAlignment)(int32)
        Config.GetFloat(HUDConfigKeys::GridRowAlignment, 0.f);

    int32 GlobalSlotIdx = 0;
    float CurrentY = 0.f;

    for (int32 RowIdx = 0; RowIdx < RowCounts.Num() && GlobalSlotIdx < TotalSlots; ++RowIdx)
    {
        const int32 SlotsInRow = FMath::Max(RowCounts[RowIdx], 0);

        // Row width
        const float RowWidth = SlotsInRow * SlotSize.X
            + (SlotsInRow - 1) * SpacingX_screen;

        // Starting X based on alignment
        float StartX = 0.f;
        switch (Alignment)
        {
        case ESlotRowAlignment::Left:
            StartX = 0.f;
            break;
        case ESlotRowAlignment::Right:
            StartX = CanvasSize.X - RowWidth;
            break;
        case ESlotRowAlignment::Center:
            StartX = (CanvasSize.X - RowWidth) * 0.5f;
            break;
        }

        for (int32 ColIdx = 0; ColIdx < SlotsInRow && GlobalSlotIdx < TotalSlots; ++ColIdx)
        {
            FSlotLayoutEntry Entry;
            Entry.SlotIndex = GlobalSlotIdx;
            Entry.Size = SlotSize;
            Entry.Position = FVector2D(
                StartX + ColIdx * (SlotSize.X + SpacingX_screen),
                CurrentY);

            Result.Add(Entry);
            ++GlobalSlotIdx;
        }

        CurrentY += SlotSize.Y + SpacingY_screen;
    }

    return Result;
}

// ---------------------------------------------------------------------------
//  RefreshAllSlotStates
// ---------------------------------------------------------------------------
void USpecialAmmoModule::RefreshAllSlotStates()
{
    if (!IsValid(DataSource.GetObject())) return;

    const int32  ReadyCount = DataSource->GetSpecialAmmoCount();
    const bool   bOnCooldown = DataSource->GetFireCooldownRatio() < 1.f;

    for (int32 i = 0; i < SlotModules.Num(); ++i)
    {
        if (!IsValid(SlotModules[i])) continue;

        ESpecialSlotState State;
        if (i < ReadyCount)
            State = bOnCooldown ? ESpecialSlotState::Cooldown : ESpecialSlotState::Ready;
        else
            State = ESpecialSlotState::Used;

        SlotModules[i]->SetSlotState(State);
    }
}

// ---------------------------------------------------------------------------
//  Delegate callbacks
// ---------------------------------------------------------------------------
void USpecialAmmoModule::OnAmmoChanged(int32 NormalCount, int32 SpecialCount)
{
    RefreshAllSlotStates();
}

void USpecialAmmoModule::OnFireCooldownComplete()
{
    RefreshAllSlotStates();
}