#pragma once

#include "CoreMinimal.h"
#include "HUDContextTypes.generated.h"

/**
 * EHUDContext
 *
 * Presentation context for the HUD system.
 * Deliberately decoupled from gameplay state.
 *
 * The HUD system is independently switchable:
 *   - Gameplay state can change without forcing a HUD context change
 *   - HUD context can change without modifying gameplay state
 *   - Multiple valid combinations exist (e.g. Spectator with Debug HUD)
 *
 * Context -> HUD DataAsset resolution order:
 *   1. RuntimeMatchSettings->HUDContextOverrides[Context]
 *   2. UHUDGlobalDefaults->DefaultsPerContext[Context]
 *   3. Error — no HUD loaded for this context
 */
UENUM(BlueprintType)
enum class EHUDContext : uint8
{
    /** No HUD — used during loading, before match starts. */
    None,

    /** Standard single-player gameplay. */
    Gameplay,

    /** Split-screen two-player gameplay (reduced screen space). */
    Gameplay_Splitscreen,

    /** Watching another player after death. */
    Spectator,

    /** Split-screen two-player spectator (reduced screen space). */
    Spectator_Splitscreen,

    /** Full replay playback mode. */
    Replay,

    /** Death replay — short clip showing the killing blow. */
    DeathReplay,

    /** Split-screen two-player death replay (reduced screen space). */
    DeathReplay_Splitscreen,

    /** Main menu or lobby (future). */
    MainMenu,

    /** Cutscene / cinematic — HUD typically hidden or minimal. */
    Cinematic
};