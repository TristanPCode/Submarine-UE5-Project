#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "SubmarineGameInstance.generated.h"

class URuntimeMatchSettings;
class UWaterRegionDataAsset;
class UOceanDebugSettings;
class UMaterialParameterCollection;

UCLASS(Blueprintable)
class SUBMARINEPROJECT_API USubmarineGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  UGameInstance interface
    // -----------------------------------------------------------------------

    virtual void Init() override;

    // -----------------------------------------------------------------------
    //  Match Settings
    // -----------------------------------------------------------------------

    // No EditAnywhere here intentionally: RuntimeMatchSettings is set at
    // runtime via SetRuntimeMatchSettings(), not assigned in the editor.
    UPROPERTY()
    TObjectPtr<URuntimeMatchSettings> RuntimeMatchSettings;

    UFUNCTION(BlueprintCallable)
    URuntimeMatchSettings* GetRuntimeMatchSettings() const { return RuntimeMatchSettings; }

    UFUNCTION(BlueprintCallable)
    void SetRuntimeMatchSettings(URuntimeMatchSettings* NewSettings)
    {
        RuntimeMatchSettings = NewSettings;
    }

    // -----------------------------------------------------------------------
    //  Replay
    // -----------------------------------------------------------------------

    // If true, the next gameplay level should load in Replay mode.
    // Set by the menu when the player clicks "Watch Replay".
    // Cleared by the GameMode after it reads it in BeginPlay.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    bool bStartInReplayMode = false;

    // The slot to load when starting in replay mode.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FString ReplaySlotToLoad;

    // -----------------------------------------------------------------------
    //  Ocean System
    //  These assets are assigned here in the editor (Class Defaults of
    //  BP_SubmarineGameInstance) and forwarded to UOceanSubsystem in Init().
    //  UGameInstanceSubsystem properties are not directly editable in the
    //  editor, so the GameInstance acts as the configuration entry point.
    // -----------------------------------------------------------------------

    // Fallback region used when no AWaterRegionActor covers a queried position.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean")
    TObjectPtr<UWaterRegionDataAsset> DefaultOceanRegionData;

    // The MPC_Ocean asset. Written to every frame by UOceanSubsystem.
    // Materials and PostProcess read from it. Never read back into gameplay.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean")
    TObjectPtr<UMaterialParameterCollection> OceanMPC;

    // Debug settings DataAsset. Optional - all debug output suppressed if null.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean")
    TObjectPtr<UOceanDebugSettings> OceanDebugSettings;
};