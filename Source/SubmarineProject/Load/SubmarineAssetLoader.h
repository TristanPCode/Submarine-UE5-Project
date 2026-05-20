#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/StreamableManager.h"
#include "SubmarineAssetLoader.generated.h"

class URuntimeMatchSettings;
class USubmarineHUDSettings;

DECLARE_DYNAMIC_DELEGATE(FOnPreloadComplete);

/**
 * USubmarineAssetLoader
 *
 * GameInstanceSubsystem — created automatically, persists through level loads.
 *
 * Preloads all assets referenced by a match configuration BEFORE gameplay
 * starts. This eliminates first-frame lag spikes from:
 *   - Submarine Blueprint class loading
 *   - HUD module widget Blueprint loading
 *   - Texture streaming
 *   - Material instance creation
 *   - Niagara system loading
 *
 * Usage:
 *   USubmarineAssetLoader* Loader =
 *       GameInstance->GetSubsystem<USubmarineAssetLoader>();
 *   Loader->PreloadMatchAssets(RuntimeSettings,
 *       FOnPreloadComplete::CreateUObject(this, &AMyGameMode::OnLoadingDone));
 *
 * Progress polling:
 *   Loader->GetLoadProgress()  -> 0..1 for a progress bar
 *   Loader->AreAssetsReady()   -> true when complete
 */
UCLASS()
class SUBMARINEPROJECT_API USubmarineAssetLoader : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Main API
    // -----------------------------------------------------------------------

    /**
     * Begin async preload of all assets referenced by the match settings.
     * OnComplete fires when ALL assets are loaded and ready.
     * Safe to call multiple times — cancels and restarts if called again.
     *
     * @param Settings     The resolved runtime match settings.
     * @param OnComplete   Delegate fired when loading is done.
     */
    UFUNCTION(BlueprintCallable, Category = "AssetLoader")
    void PreloadMatchAssets(URuntimeMatchSettings* Settings,
        FOnPreloadComplete OnComplete);

    /**
     * Load progress as 0..1.
     * Returns 1.0 when not loading or when complete.
     */
    UFUNCTION(BlueprintPure, Category = "AssetLoader")
    float GetLoadProgress() const;

    /**
     * True when all requested assets have finished loading.
     */
    UFUNCTION(BlueprintPure, Category = "AssetLoader")
    bool AreAssetsReady() const { return bAssetsReady; }

    /**
     * Cancel any in-progress load.
     */
    UFUNCTION(BlueprintCallable, Category = "AssetLoader")
    void CancelLoad();

    // Called from GameMode::BeginPlay to also preload all global HUD defaults,
    // not just the per-match overrides.
    UFUNCTION(BlueprintCallable, Category = "AssetLoader")
    void AddGlobalHUDDefaults(UHUDGlobalDefaults* GlobalDefaults);

private:

    bool bAssetsReady = false;
    FOnPreloadComplete CompletionDelegate;
    TSharedPtr<FStreamableHandle> LoadHandle;

    /**
     * Collect all soft paths that need to be loaded for the given settings.
     * Walks: submarine classes, HUD DAs, textures, materials, widget blueprints,
     * torpedo characteristics, Niagara systems.
     */
    TArray<FSoftObjectPath> CollectPathsFromSettings(
        URuntimeMatchSettings* Settings);

    /**
     * Collect all soft paths from a HUD settings asset
     * (textures, materials, widget blueprint classes).
     */
    void CollectHUDPaths(const USubmarineHUDSettings* HUDSettings,
        TArray<FSoftObjectPath>& OutPaths) const;

    /** Called by FStreamableManager when loading completes. */
    void OnLoadComplete();

    // Held between AddGlobalHUDDefaults() and PreloadMatchAssets()
    UPROPERTY()
    TObjectPtr<UHUDGlobalDefaults> PendingGlobalDefaults;
};