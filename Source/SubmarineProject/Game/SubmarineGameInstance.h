#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "SubmarineGameInstance.generated.h"

class URuntimeMatchSettings;

UCLASS(Blueprintable)
class SUBMARINEPROJECT_API USubmarineGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:

    UPROPERTY()
    TObjectPtr<URuntimeMatchSettings> RuntimeMatchSettings;

    UFUNCTION(BlueprintCallable)
    URuntimeMatchSettings* GetRuntimeMatchSettings() const { return RuntimeMatchSettings; }

    UFUNCTION(BlueprintCallable)
    void SetRuntimeMatchSettings(URuntimeMatchSettings* NewSettings)
    {
        RuntimeMatchSettings = NewSettings;
    }

    /**
     * If true, the next gameplay level should load in Replay mode.
     * Set by the menu when the player clicks "Watch Replay".
     * Cleared by the GameMode after it reads it in BeginPlay.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Replay")
    bool bStartInReplayMode = false;

    /** The slot to load when starting in replay mode. */
    UPROPERTY(BlueprintReadWrite, Category = "Replay")
    FString ReplaySlotToLoad;
};