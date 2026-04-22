#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "SubmarineSpectatorPawn.generated.h"

class UCameraComponent;
class ASubmarinePawn;
class ATorpedoPawn;
class USubmarineTorpedoComponent;
class UCameraBlendSettings;

/**
 * ASubmarineSpectatorPawn
 *
 * Possessed by the player controller when their submarine dies.
 * Owns a free-floating camera that blends toward the target submarine/torpedo.
 *
 * Navigation:
 *   Left / Right  — cycle through submarines
 *   Up   / Down   — cycle through [Submarine, Torpedo1, Torpedo2, ...] for
 *                   the currently watched submarine
 *
 * The spectator never modifies the cameras of observed submarines/torpedoes.
 * It copies their world position/rotation and lerps toward them independently.
 *
 * Blueprint setup:
 *   1. Call InitSpectator() after possessing this pawn, passing the full list
 *      of all submarines in the match (or use RegisterSubmarine() per-sub).
 *   2. Bind Left/Right/Up/Down input actions to the provided UFUNCTION callbacks.
 */
UCLASS()
class SUBMARINEPROJECT_API ASubmarineSpectatorPawn : public ASpectatorPawn
{
    GENERATED_BODY()

public:
    ASubmarineSpectatorPawn();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;


    // -------------------------------------------------------------------------
    //  Initialisation API (call from GameMode or PlayerController on death)
    // -------------------------------------------------------------------------

    /**
     * Seed the spectator with the initial list of submarines.
     * Dead or invalid entries are filtered automatically each tick.
     * Call this immediately after possessing the spectator pawn.
     */
    UFUNCTION(BlueprintCallable, Category = "Spectator")
    void InitSpectator(const TArray<ASubmarinePawn*>& AllSubmarines,
        bool bStartOnFirstLiveSubmarine = true);

    /**
     * Add a submarine to the tracked list at runtime
     * (e.g. if submarines can spawn mid-match).
     */
    UFUNCTION(BlueprintCallable, Category = "Spectator")
    void RegisterSubmarine(ASubmarinePawn* Submarine);

    // -------------------------------------------------------------------------
    //  Key cooldown settings — subject switch (Left/Right/Up/Down)
    // -------------------------------------------------------------------------

    /**
     * How long a key must be held (seconds) before "hold mode" activates.
     * Before this threshold each press fires exactly one switch.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spectator|Input")
    float SwitchHoldThreshold = 0.25f;

    /**
     * While held past SwitchHoldThreshold, how many seconds between
     * automatic switches. Lower = faster cycling while held.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spectator|Input")
    float SwitchHoldCooldown = 0.25f;

    // -------------------------------------------------------------------------
    //  Key cooldown settings — camera mode toggle (POV <-> 3rd person)
    // -------------------------------------------------------------------------

    /**
     * How long the camera toggle key must be held before auto-repeat activates.
     * Set high (e.g. 99) to disable auto-repeat entirely for this key.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spectator|Input")
    float CameraToggleHoldThreshold = 0.4f;

    /** Auto-repeat interval for camera toggle while held (seconds). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spectator|Input")
    float CameraToggleHoldCooldown = 0.3f;

    // -------------------------------------------------------------------------
    //  Camera blend settings (assigned from GameMode)
    // -------------------------------------------------------------------------

    /**
     * Assign the game-wide UCameraBlendSettings DataAsset here.
     * Usually set by GameMode right after spawning this pawn.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spectator|Camera")
    TObjectPtr<UCameraBlendSettings> BlendSettings;

    FVector  SubjectSwitchBlendStartLoc = FVector::ZeroVector;
    FRotator SubjectSwitchBlendStartRot = FRotator::ZeroRotator;

    // -------------------------------------------------------------------------
    //  Input callbacks — subject switch (Pressed/Released handle tap+hold)
    // -------------------------------------------------------------------------

    /**
     * Call on PRESSED (Started) for the Left spectator key.
     * Fires an immediate switch + begins hold tracking.
     */
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorLeftPressed();
    /** Call on RELEASED for the Left spectator key. */
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorLeftReleased();

    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorRightPressed();
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorRightReleased();

    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorUpPressed();
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorUpReleased();

    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorDownPressed();
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorDownReleased();

    // Single-fire versions (also called by hold system)
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorLeft();
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorRight();
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorUp();
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorDown();

    // -------------------------------------------------------------------------
    //  Input — camera mode toggle (also uses hold+cooldown system)
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorCameraTogglePressed();
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorCameraToggleReleased();

    // -------------------------------------------------------------------------
    //  Input — mouse / zoom (call every frame from your input binding)
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorMouseX(float AxisValue);
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorMouseY(float AxisValue);
    UFUNCTION(BlueprintCallable, Category = "Spectator|Input")
    void SpectatorScrollZoom(float AxisValue);

    // -------------------------------------------------------------------------
    //  State (read-only from Blueprint)
    // -------------------------------------------------------------------------

    /** Currently watched submarine (may be null if all are dead) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spectator")
    TObjectPtr<ASubmarinePawn> CurrentSubTarget;

    /**
     * Currently watched torpedo (null = watching the submarine itself).
     * Only valid while the torpedo is alive; reverts to submarine on death.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spectator")
    TObjectPtr<ATorpedoPawn> CurrentTorpedoTarget;

    /** True when watching a torpedo rather than a submarine */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spectator")
    bool bWatchingTorpedo = false;

    /** True when the spectator camera is in 3rd person orbit mode */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spectator")
    bool bSpectatorThirdPerson = false;

private:
    // -------------------------------------------------------------------------
    //  Camera
    // -------------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UCameraComponent> SpectatorCamera;

    // -------------------------------------------------------------------------
    //  Submarine / torpedo list management
    // -------------------------------------------------------------------------

    /** All tracked submarines (includes dead ones — filtered in GetLiveSubmarines) */
    UPROPERTY()
    TArray<TObjectPtr<ASubmarinePawn>> TrackedSubmarines;

    /** Returns only live (valid, not destroyed) submarines */
    TArray<ASubmarinePawn*> GetLiveSubmarines() const;

    /**
     * Returns the "view list" for the current submarine:
     * [Submarine, Torpedo0, Torpedo1, ...]
     * Index 0 is always the submarine itself.
     * Only live torpedoes from its TorpedoComponent are included.
     */
    TArray<UObject*> BuildViewList(ASubmarinePawn* Sub) const;

    /** Index into GetLiveSubmarines() */
    int32 SubmarineListIndex = 0;

    /**
     * Index into BuildViewList(CurrentSubTarget).
     * 0 = submarine, 1+ = torpedoes.
     */
    int32 ViewListIndex = 0;

    // -------------------------------------------------------------------------
    //  Camera blend state
    // -------------------------------------------------------------------------

    /** Current interpolated camera position/rotation */
    FVector  CurrentCamLocation = FVector::ZeroVector;
    FRotator CurrentCamRotation = FRotator::ZeroRotator;
    bool     bCameraInitialised = false;

    FVector FallbackLocation = FVector::ZeroVector;

    // -- Camera mode transition blend (POV <-> 3rd person) ------------------
    bool     bCameraModeBlending = false;
    float    CameraModeBlendTimer = 0.f;
    float    CameraModeBlendDuration = 0.3f;
    FVector  CamModeBlendStartLoc = FVector::ZeroVector;
    FRotator CamModeBlendStartRot = FRotator::ZeroRotator;

    // -- Subject switch blend -----------------------------------------------
    // Only active immediately after a subject switch (Left/Right/Up/Down).
    // Does NOT bleed into orbit movement or camera mode transitions.
    bool  bSubjectSwitchBlending = false;
    float SubjectSwitchBlendTimer = 0.f;
    float SubjectSwitchBlendDuration = 0.5f; // seconds; shorter = snappier

    // -------------------------------------------------------------------------
    //  3rd person orbit state (spectator's own, never touches target cameras)
    // -------------------------------------------------------------------------

    float SpectatorOrbitYaw = 180.f;
    float SpectatorOrbitPitch = 20.f;
    float SpectatorOrbitRadius = 1200.f;

    // Desired orbit values (actual values lerp toward these when blend is on)
    float DesiredOrbitYaw = 180.f;
    float DesiredOrbitPitch = 20.f;
    float DesiredOrbitRadius = 1200.f;

    // Sensitivities (taken from the current submarine DA when available)
    float GetOrbitYawSensitivity()   const;
    float GetOrbitPitchSensitivity() const;
    float GetOrbitScrollSpeed()      const;
    float GetOrbitMinPitch()         const;
    float GetOrbitMaxPitch()         const;
    float GetOrbitMinRadius()        const;
    float GetOrbitMaxRadius()        const;

    // -------------------------------------------------------------------------
    //  Hold-to-repeat input state  (Left/Right/Up/Down)
    // -------------------------------------------------------------------------

    struct FHoldInput
    {
        bool  bHeld = false;
        float HoldTimer = 0.f; // time since key was pressed
        float HoldCooldownTimer = 0.f; // time since last auto-fire while held
    };

    FHoldInput HoldLeft;
    FHoldInput HoldRight;
    FHoldInput HoldUp;
    FHoldInput HoldDown;
    FHoldInput HoldCameraToggle;

    void TickHoldInput(FHoldInput& Input, float DeltaTime, float HoldThreshold, float HoldCooldown, TFunction<void()> OnFire);

    // -------------------------------------------------------------------------
    //  Helpers
    // -------------------------------------------------------------------------

    /** Performs the actual POV <-> 3rd person switch + starts mode blend. */
    void DoToggleCameraMode();

    /** Resolves the camera world transform of the current target */
    void GetTargetCameraTransform(FVector& OutLocation, FRotator& OutRotation) const;

    void GetThirdPersonTransform(FVector& OutLocation, FRotator& OutRotation) const;

    /** Applies SubmarineListIndex + ViewListIndex, validates, falls back to empty cam */
    void ApplyCurrentTarget();

    /** Called each tick — checks if CurrentTorpedoTarget died and reverts to submarine */
    void ValidateTorpedoTarget();

    /** Returns resolved blend speed for subject-switch, honouring BlendSettings. */
    bool  IsSubjectSwitchBlendEnabled() const;
    float GetSubjectSwitchBlendSpeed()  const;
    bool  IsCameraModeBlendEnabled()    const;
    float GetCameraModeBlendSpeed()     const;
    bool  IsOrbitMoveBlendEnabled()     const;
    float GetOrbitMoveBlendSpeed()      const;

    const class USubmarineCharacteristics* GetSubStats(ASubmarinePawn* Sub) const;
};