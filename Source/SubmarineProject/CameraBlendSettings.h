#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CameraBlendSettings.generated.h"

/**
 * UCameraBlendSettings
 *
 * Game-wide DataAsset controlling camera interpolation behaviour.
 * Assign one instance to ASubmarineGameMode::CameraBlendSettings.
 *
 * Three independent blend scenarios:
 *   1. SubjectSwitch  — spectator changes which submarine/torpedo it watches
 *   2. CameraMode     — switching between POV / Periscope / 3rd person
 *   3. OrbitMove      — mouse orbit movement in 3rd person
 *
 * A "blend all" master override is also available.
 */
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UCameraBlendSettings : public UDataAsset
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Master override
    // -----------------------------------------------------------------------

    /**
     * When true, ALL blend scenarios use bBlendAll / BlendAllSpeed.
     * Individual scenario settings are ignored.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend|Master")
    bool bBlendAll = false;

    /** Lerp speed used for every scenario when bBlendAll is true. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend|Master",
        meta = (ClampMin = "0.1", EditCondition = "bBlendAll"))
    float BlendAllSpeed = 6.f;

    // -----------------------------------------------------------------------
    //  Scenario 1 — Subject switch (spectator cycles submarines / torpedoes)
    // -----------------------------------------------------------------------

    /** Enable smooth blend when the spectator switches its watched target. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend|SubjectSwitch")
    bool bBlendSubjectSwitch = true;

    /** Lerp speed for subject-switch blends. Higher = snappier. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend|SubjectSwitch",
        meta = (ClampMin = "0.1", EditCondition = "bBlendSubjectSwitch"))
    float SubjectSwitchBlendSpeed = 5.f;

    // -----------------------------------------------------------------------
    //  Scenario 2 — Camera mode change (POV <-> Periscope <-> 3rd person)
    // -----------------------------------------------------------------------

    /** Enable smooth blend when switching camera modes on a submarine. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend|CameraMode")
    bool bBlendCameraMode = true;

    /** Lerp speed for camera-mode-switch blends. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend|CameraMode",
        meta = (ClampMin = "0.1", EditCondition = "bBlendCameraMode"))
    float CameraModeBlendSpeed = 8.f;

    // -----------------------------------------------------------------------
    //  Scenario 3 — 3rd person orbit movement (mouse drag / zoom)
    // -----------------------------------------------------------------------

    /**
     * Enable smooth lag when orbiting in 3rd person.
     * When false the camera snaps instantly to the orbit position.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend|OrbitMove")
    bool bBlendOrbitMove = true;

    /** Lerp speed for orbit-move lag. Lower = more cinematic lag. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend|OrbitMove",
        meta = (ClampMin = "0.1", EditCondition = "bBlendOrbitMove"))
    float OrbitMoveBlendSpeed = 12.f;

    // -----------------------------------------------------------------------
    //  Helpers (callable from C++ and Blueprint)
    // -----------------------------------------------------------------------

    /** Returns the effective blend speed for subject-switch, respecting master override. */
    UFUNCTION(BlueprintPure, Category = "Camera|Blend")
    float GetSubjectSwitchSpeed() const
    {
        return bBlendAll ? BlendAllSpeed : SubjectSwitchBlendSpeed;
    }

    /** Returns whether subject-switch blending is active. */
    UFUNCTION(BlueprintPure, Category = "Camera|Blend")
    bool IsSubjectSwitchBlendEnabled() const
    {
        return bBlendAll ? true : bBlendSubjectSwitch;
    }

    /** Returns the effective blend speed for camera-mode changes. */
    UFUNCTION(BlueprintPure, Category = "Camera|Blend")
    float GetCameraModeSpeed() const
    {
        return bBlendAll ? BlendAllSpeed : CameraModeBlendSpeed;
    }

    /** Returns whether camera-mode blending is active. */
    UFUNCTION(BlueprintPure, Category = "Camera|Blend")
    bool IsCameraModeBlendEnabled() const
    {
        return bBlendAll ? true : bBlendCameraMode;
    }

    /** Returns the effective blend speed for orbit movement. */
    UFUNCTION(BlueprintPure, Category = "Camera|Blend")
    float GetOrbitMoveSpeed() const
    {
        return bBlendAll ? BlendAllSpeed : OrbitMoveBlendSpeed;
    }

    /** Returns whether orbit-move blending is active. */
    UFUNCTION(BlueprintPure, Category = "Camera|Blend")
    bool IsOrbitMoveBlendEnabled() const
    {
        return bBlendAll ? true : bBlendOrbitMove;
    }
};