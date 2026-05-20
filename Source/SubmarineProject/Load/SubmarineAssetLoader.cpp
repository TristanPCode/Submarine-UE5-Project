// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineAssetLoader.h"
#include "Match/RuntimeMatchSettings.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/HUDGlobalDefaults.h"
#include "HUDContextTypes.h"
#include "HUD/MainHUDWidget.h"
#include "HUD/BaseHUDModule.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Algo/Unique.h"

// ---------------------------------------------------------------------------
//  PreloadMatchAssets
// ---------------------------------------------------------------------------
void USubmarineAssetLoader::PreloadMatchAssets(
    URuntimeMatchSettings* Settings,
    FOnPreloadComplete OnComplete)
{
    // Cancel any previous in-progress load
    CancelLoad();

    bAssetsReady = false;
    CompletionDelegate = OnComplete;

    if (!Settings)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[AssetLoader] PreloadMatchAssets: Settings is null. "
                "Completing immediately."));
        bAssetsReady = true;
        OnComplete.ExecuteIfBound();
        return;
    }

    TArray<FSoftObjectPath> Paths = CollectPathsFromSettings(Settings);

    if (Paths.IsEmpty())
    {
        UE_LOG(LogTemp, Log,
            TEXT("[AssetLoader] PreloadMatchAssets: no soft paths to load. "
                "Completing immediately."));
        bAssetsReady = true;
        OnComplete.ExecuteIfBound();
        return;
    }

    // Deduplicate
    Paths.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B)
        {
            return A.ToString() < B.ToString();
        });
    Paths.SetNum(Algo::Unique(Paths));

    UE_LOG(LogTemp, Log,
        TEXT("[AssetLoader] PreloadMatchAssets: loading %d assets..."),
        Paths.Num());

    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    LoadHandle = Streamable.RequestAsyncLoad(
        Paths,
        FStreamableDelegate::CreateUObject(
            this, &USubmarineAssetLoader::OnLoadComplete),
        FStreamableManager::AsyncLoadHighPriority);
}

// ---------------------------------------------------------------------------
//  GetLoadProgress
// ---------------------------------------------------------------------------
float USubmarineAssetLoader::GetLoadProgress() const
{
    if (bAssetsReady || !LoadHandle.IsValid()) return 1.f;
    return LoadHandle->GetProgress();
}

// ---------------------------------------------------------------------------
//  CancelLoad
// ---------------------------------------------------------------------------
void USubmarineAssetLoader::CancelLoad()
{
    if (LoadHandle.IsValid())
    {
        LoadHandle->CancelHandle();
        LoadHandle.Reset();
    }
    bAssetsReady = false;
}

// ---------------------------------------------------------------------------
//  AddGlobalHUDDefaults
// ---------------------------------------------------------------------------
void USubmarineAssetLoader::AddGlobalHUDDefaults(UHUDGlobalDefaults* GlobalDefaults)
{
    if (!GlobalDefaults) return;
    PendingGlobalDefaults = GlobalDefaults;
}

// ---------------------------------------------------------------------------
//  OnLoadComplete
// ---------------------------------------------------------------------------
void USubmarineAssetLoader::OnLoadComplete()
{
    bAssetsReady = true;
    UE_LOG(LogTemp, Log, TEXT("[AssetLoader] All match assets loaded."));
    CompletionDelegate.ExecuteIfBound();
}

// ---------------------------------------------------------------------------
//  CollectPathsFromSettings
// ---------------------------------------------------------------------------
TArray<FSoftObjectPath> USubmarineAssetLoader::CollectPathsFromSettings(
    URuntimeMatchSettings* Settings)
{
    TArray<FSoftObjectPath> Paths;
    if (!Settings) return Paths;

    // --- Submarine Blueprint classes ---
    auto AddClass = [&](TSubclassOf<ASubmarinePawn> C)
        {
            if (C) Paths.Add(FSoftObjectPath(C));
        };

    for (auto& C : Settings->PlayerSubmarineClasses) AddClass(C);
    for (auto& C : Settings->CPUSubmarineClasses)    AddClass(C);
    AddClass(Settings->DefaultSubmarineClass);

    // GlobalDefaults HUD paths
    if (PendingGlobalDefaults)
    {
        for (auto& Pair : PendingGlobalDefaults->DefaultsPerContext)
            if (Pair.Value) CollectHUDPaths(Pair.Value.Get(), Paths);
        PendingGlobalDefaults = nullptr;  // consumed
    }

    // Note: GlobalDefaults HUD paths are collected at GameMode level
    // and injected here when this loader is invoked from GameMode.
    // If you have access to UHUDGlobalDefaults here, iterate its map too.

    return Paths;
}

// ---------------------------------------------------------------------------
//  CollectHUDPaths
// ---------------------------------------------------------------------------
void USubmarineAssetLoader::CollectHUDPaths(
    const USubmarineHUDSettings* HUDSettings,
    TArray<FSoftObjectPath>& OutPaths) const
{
    if (!HUDSettings) return;

    // Widget class
    if (HUDSettings->WidgetClass)
        OutPaths.Add(FSoftObjectPath(HUDSettings->WidgetClass));

    // Per-module assets
    for (const FHUDModuleConfig& Module : HUDSettings->Modules)
    {
        if (!Module.bEnabled) continue;

        // Widget classes
        if (Module.ModuleClass)
            OutPaths.Add(FSoftObjectPath(Module.ModuleClass));
        if (Module.SlotModuleClass)
            OutPaths.Add(FSoftObjectPath(Module.SlotModuleClass));
        if (Module.CounterModuleClass)
            OutPaths.Add(FSoftObjectPath(Module.CounterModuleClass));

        // Textures
        for (const auto& Pair : Module.Textures)
            if (Pair.Value) OutPaths.Add(FSoftObjectPath(Pair.Value.Get()));

        // Materials
        for (const auto& Pair : Module.Materials)
            if (Pair.Value) OutPaths.Add(FSoftObjectPath(Pair.Value.Get()));

        // Icon textures (special ammo)
        for (const auto& Pair : Module.IconPerType)
        {
            if (Pair.Value.ReadyIcon)
                OutPaths.Add(FSoftObjectPath(Pair.Value.ReadyIcon.Get()));
            if (Pair.Value.CooldownIcon)
                OutPaths.Add(FSoftObjectPath(Pair.Value.CooldownIcon.Get()));
        }
    }
}