// `AAcDaylight`: day and night on the game's clock (GAME-LAYER.md §2.8,
// chunk B9), the port of `Daylight` (Sources/Autocraft/Daylight.swift) with
// Unreal's richer sky:
//
// - The clock. A day is `ac.DayLength` (600) seconds of game time; a game
//   starts at 08:00; the sun is up 05:30-18:30. `ac.Hour 21` (or
//   `-AcHour=21` on the command line) holds the clock at an hour, for shots.
// - The sun and the moon: two directional lights on one arc (the moon is
//   opposite the sun), rising bottom left of the top-down view, highest top
//   left at noon (`normalize(-60, 110, -55)` in SceneKit axes), setting top
//   right. The Swift intensities (sun 1900, moon 650) and colours (orange
//   to (1, 0.93, 0.82) by 32°; the moon (0.5, 0.64, 1)), scaled to lux for
//   the fixed exposure below. They light the ground only; two more
//   directional lights along the same rays (`SkySun`, `SkyMoon`, lighting
//   no geometry) are the SkyAtmosphere's lights 0 and 1, so the sky turns
//   orange at dusk and dark blue under the moon while the ground keeps the
//   Swift colours (the atmosphere would redden a low sun twice). Shadows
//   are virtual shadow maps (stable under a moving light, so the Swift
//   two-key trick is not needed), fading out near the horizon as in Swift.
// - A cool fill from the opposite side (no shadows), Swift's `tone`.
// - SkyLight with real-time capture of the atmosphere, clouds and stars,
//   scaled by Swift's ambient tone; Lumen GI carries it.
// - ExponentialHeightFog with volumetric fog, in the Swift haze colour.
// - VolumetricCloud (the engine's simple cloud material): cloud shadows
//   drift over the map; `ac.Daylight.Clouds 0` turns them off.
// - A sky dome (`M_AcSky`, made by Tools/Editor/make_sky_material.py): the
//   atmosphere, the sun and moon discs and the Swift star field at night.
// - Exposure: manual, EV 0 (an unbound post-process component), so the
//   night stays dark instead of being brightened by auto exposure. A camera
//   that sets its own exposure overrides it.
// - The terrain's `Haze` parameter (the border fading out), from the Swift
//   top-down haze tone.
//
// `GetDark()` is how dark it is for lamps (C6): 0 with the sun up, 1 soon
// after sunset. `GetSkyTones()` gives the Swift first-person sky tones for
// the pilot's fog veil (B10, E2).
//
// Usage: `AAcDaylight::SpawnFor(World)` once per game world (AAcGameMode).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AcDaylight.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class FAcGasGiant;
class FAcSkyFx;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class UVolumetricCloudComponent;

/// The Swift first-person sky (`Daylight.Sky`), colours sRGB as Swift
/// writes them; `Sun` toward the sun in Unreal axes.
struct FAcSkyTones
{
	FLinearColor Top, High, Horizon, Haze, Glow, Cloud, Shade;
	FVector Sun = FVector::UpVector;
	/// How bright the stars are (0-1).
	double Stars = 0;
	/// How high the haze rises (degrees) and how far the eye sees (cells).
	double Rise = 12, Reach = 110;
};

UCLASS()
class AUTOCRAFT_API AAcDaylight : public AActor
{
	GENERATED_BODY()

public:
	/// Hours: a game starts in the morning; the sun is up from sunrise to sunset.
	static constexpr double StartHour = 8.0;
	static constexpr double Sunrise = 5.5;
	static constexpr double Sunset = 18.5;

	/// One day and night, seconds of game time (`ac.DayLength`, 600).
	static double DayLength();
	/// The held hour (`ac.Hour`, `-AcHour=`), or a negative number.
	static double HeldHour();
	/// The hour (0-24) `Time` seconds into a game.
	static double HourAt(double Time);
	/// Toward the sun at `Hour`, a unit vector in SceneKit axes (Y up),
	/// below the ground at night (`Daylight.sun(hour:)`).
	static FVector SunSceneKit(double Hour);
	/// The same in Unreal axes (Z up).
	static FVector SunToward(double Hour);

	AAcDaylight();

	/// The daylight of the world (spawned on first ask).
	static AAcDaylight* SpawnFor(UWorld* World);
	static AAcDaylight* Find(const UWorld* World);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/// Light the world for `Time` seconds of game time.
	void Update(double Time);

	double GetHour() const { return Hour; }
	/// 0 with the sun up, 1 from soon after sunset; lamps come on as the sun
	/// sinks below 10°.
	double GetDark() const { return Dark; }
	/// The sun's elevation, degrees (negative at night).
	double GetSunElevation() const { return Elevation; }
	/// The top-down haze (background, fog, the terrain border), sRGB.
	FLinearColor GetHaze() const { return Haze; }
	/// The Swift first-person sky tones at the current hour.
	const FAcSkyTones& GetSkyTones() const { return Tones; }
	/// Swift's haze rising from the horizon (`sky_color`'s `rise`), as the
	/// sky dome (M_AcSky) and the pilot's veil (M_AcPilotVeil) take it: the
	/// haze tone in linear light (rgb) and how high it rises, degrees (a).
	FLinearColor GetHazeRise() const;

private:
	void RemoveStandIns();
	/// -AcSkyView[=YAW]: look at the sky from the ground (see the .cpp).
	void StageSkyView();
	UPROPERTY(Transient)
	TObjectPtr<AActor> SkyCamera;
	int32 SkyViewFrames = 0;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> Moon;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> Fill;
	/// The sun and the moon as the atmosphere sees them (lights 0 and 1):
	/// they light the sky, the fog and the clouds, not the ground.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> SkySun;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> SkyMoon;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Dome;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPostProcessComponent> Exposure;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DomeMaterial;

	double Hour = StartHour;
	double Dark = 0;
	double Elevation = 0;
	FLinearColor Haze;
	FAcSkyTones Tones;
	/// What the terrain's haze was last set to (set again only on change).
	FLinearColor TerrainHaze = FLinearColor(-1, -1, -1, -1);
	bool bSunShadows = true;
	int32 StandInChecks = 0;
	/// The gas giant in the sky (AcGasGiant.h); its look is in M_AcSky.
	TSharedPtr<FAcGasGiant> GasGiant;
	/// Ships, the comet and the meteor shower (AcSkyFx.h).
	TSharedPtr<FAcSkyFx> SkyFx;
};
