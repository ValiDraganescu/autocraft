// Pilot audio (GAME-LAYER.md §2.10 "Driving: 3D" and §2.12, chunk E7): how
// the world sounds while the player drives a unit. The port of the 3D stage
// of `Audio.swift` (`start` :183, `hearFrom` :307, `place` :330) and of
// `GameController+Ears.swift` (`ears`, `soundBlocked`), plus the pilot's
// listener (`moveEars`, GameController+Pilot.swift:262) and the spent point
// of a miss (`sightPoint(reach:)` :505). The in-ear sounds themselves
// (footfalls, breath, lock-on, the engine pulling away, the heal loop) are
// C7's `FAcAudioRules`, fed by E2's `AAcPilotPawn::Sounds` (`pilotSounds`).
//
// Swift's stage is an `AVAudioEnvironmentNode`: HRTF (`.HRTFHQ`) at the
// camera, no distance roll-off (the gain is `Listener.gain`), a medium hall
// at −4 dB with a per-source `reverbBlend = min(0.45, 0.06 + 0.4 d/range)`,
// and `obstruction = −14 dB` when a cliff or a building stands between the
// ears and the sound. Apple documents none of these as numbers, so they were
// MEASURED offline from that very node (AVAudioEngine manual rendering, no
// device: unreal/Tools/Audio/avstage_measure.swift, avstage_grid.swift):
//   - HRTF: the level at each ear for a mono source, by azimuth (15° steps)
//     and elevation (−90…90°): `Hrtf()`. Ahead L −0.9 R −0.4 dB; 90° right
//     L −14.0 R 0.0; behind −5.6/−6.3; straight below −12.9/−12.6.
//   - reverbBlend b is an equal-power crossfade: the dry path's power is
//     (1 − b), the reverb's is b (fits to 0.05 dB at b = 0.06…0.45).
//   - The hall's tail at b = 1 holds −13.2 dB of the source's energy and
//     falls 31.5 dB/s (RT60 ≈ 1.9 s).
//   - obstruction −14 dB is a filter on the direct path only (the reverb is
//     untouched): −1.75 dB up to 500 Hz, −2.9 at 2 kHz, −5.5 at 4 kHz,
//     −11.3 at 8 kHz, −18 at 16 kHz: a one-pole low-pass at about 3 kHz
//     after −1.75 dB (UE's source low-pass is one-pole too).
//
// Unreal has no HRTF of its own on the Mac (Resonance Audio is the only
// binaural plugin and renders through its own reverb submix), so by default
// (`ac.PilotHrtf 0`) each world sound is placed the way C7 places the
// top-down ones: at a virtual point one metre round the listener whose
// angle makes UE's equal-power stereo panner give exactly the two ear levels
// Swift's HRTF gives that direction, with the volume carrying their sum. So
// the loudness by direction (front +2.3 dB, behind −3 dB, below −10 dB) and
// the left-right levels are Swift's; the HRTF's timing and colour are not.
// `ac.PilotHrtf 1` plays the sound at its true place with the engine's HRTF
// algorithm instead (a spatialisation plugin set in DefaultEngine.ini, else
// UE's plain panner).
//
// Reverb: one submix, `AcPilotHall` (a `USubmixEffectReverbPreset`, wet only,
// parented to the main submix), fed per voice by `SetSubmixSend` with the
// blend's share. Obstruction: the voice's volume × 0.818 and its low-pass at
// 3 kHz (the hall's send is raised by as much, so the reverb stays).
//
// Checked offline (-deterministicaudio, unreal/Tools/Audio/pilot_probe.py on
// the master recording of `-AcPilotAudioProbe`):
//   - the per-voice gains (dry share × Swift's HRTF level × obstruction):
//     right −1.65, behind −4.79, left −0.98 dB against ahead, as logged to
//     0.01 dB;
//   - the obstruction's filter against Swift's: −1.75/−2.1/−2.0/−1.7/−2.3/
//     −3.3/−6.2/−10.4/−13.1 dB at 63 Hz…12 kHz (Swift −1.75…−15.5);
//   - the pan: NOT audible offline. UE 5.8's non-realtime device reports 2
//     channels but an 8-entry channel array (AudioMixerPlatformNonRealtime.cpp
//     GetOutputDeviceInfo), so every spatialised source renders silent there.
//     The pan is checked as numbers instead: the unit tests put the logged pan
//     through UE's stereo law (FL 270°, FR 90°, equal power,
//     `FMixerDevice::Get3DChannelMap`) and get Swift's ear levels back, and
//     `-AcPilotAudioLog` logs every voice's azimuth, elevation, ear levels and
//     pan each frame;
//   - the hall: NOT measurable offline either: the offline mixer passes the
//     hall's input through without its reverb (decay and wet level made no
//     difference), so `ac.PilotHallDecay`/`ac.PilotHallWet` are estimates.
//
// What the director (C7) calls: `Place(...)` gives an `FAcVoice3D` for a
// world sound while driving (`UAcAudioDirector::Place`), `Apply` sets its
// filter and send on the component (and clears them in the top-down view).
//
// Each frame at the `Other` stage (after E2's pawn): while driving, the
// session's listener moves onto the driven unit, facing its look
// (`moveEars(to: u.position, facing: u.look)`: the reach of every sound is
// from the unit, also in third person), back to the camera's target on
// leaving; and the rules learn where the driven gun's misses land
// (`FAcAudioRules::PilotSpent`).
//
// Command line and console:
//   -AcPilotAudioLog / ac.PilotAudioLog 1   log every placed voice each frame:
//        `pilot audio: <sound> d 6.0 az 90 el -12 L -14.0 R 0.0 dB, blocked 0,
//        blend 0.21, gain 0.91, pan 1.00, lpf 0, send 0.61`
//   -AcPilotAudioProbe   while driving, the game's sounds hushed: one probe a
//        second round the ears at 6 cells (the wind's noise ahead, right,
//        behind, left, ahead blocked; then a hit mark dry, with the hall,
//        blocked), for the offline checks (`UAcPilotAudio::Probe`).
//   ac.PilotHrtf 0|1     see above.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcAudioDirector.h"
#include "SimdMath.h"
#include "Types.h"

#include <functional>
#include <vector>

#include "AcPilotAudio.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundSubmix;
class USubmixEffectReverbPreset;
struct FAcFrame;

namespace ac { class Simulation; struct Structure; }

/// One world sound placed round the ears while driving.
struct FAcVoice3D
{
	/// Where the sound is (UE cm; the driven unit's own sounds 0.4 cells
	/// ahead of the ears, not inside the head).
	FVector At = FVector::ZeroVector;
	/// Where it plays from: `At` itself (`ac.PilotHrtf 1`) or the virtual
	/// point round the listener that pans it as Swift's HRTF would.
	FVector PlayAt = FVector::ZeroVector;
	/// From the ears: cells, and degrees (azimuth + right, elevation + up).
	double Distance = 0;
	double Azimuth = 0;
	double Elevation = 0;
	bool bBlocked = false;
	/// Swift's `reverbBlend`.
	float Blend = 0.f;
	/// Swift's HRTF level at each ear for this direction (linear).
	float Left = 1.f;
	float Right = 1.f;
	/// The volume factor: obstruction × the dry share × the HRTF level.
	float Gain = 1.f;
	/// UE's stereo pan 0 (left) … 1 (right) the virtual point gives.
	float Pan = 0.5f;
	/// The low-pass (Hz; 0: open) and the hall's send level.
	float LowPassHz = 0.f;
	float Send = 0.f;
};

namespace AcPilotAudio
{
	/// Measured (see the header comment).
	inline constexpr float ObstructedGain = 0.818f;  // −1.75 dB
	inline constexpr float ObstructedLowPassHz = 3000.f;
	/// `place`: closer than this to the ears plays this far ahead (cells).
	inline constexpr double Ahead = 0.4;
	/// `listener?.range ?? 16`, and the window's listener (GameController.swift:186).
	inline constexpr double DefaultRange = 16.0;
	inline constexpr double WindowRange = 24.0;
	/// The hall: decay (s), and its tail's energy at blend 1 (dB of the source's).
	inline constexpr float HallDecay = 1.9f;
	inline constexpr float HallTailDb = -13.15f;
	/// Send levels are clamped to 1 by UE: they go out at this share and the
	/// hall gives it back (+12 dB).
	inline constexpr float SendScale = 0.25f;

	/// `reverbBlend`: 0.06 at the ears to 0.45 at 97.5% of the range.
	AUTOCRAFT_API float ReverbBlend(double DistanceCells, double RangeCells);
	/// Swift's HRTF ear levels (linear) for a direction from the ears.
	AUTOCRAFT_API void Hrtf(double AzimuthDeg, double ElevationDeg, float& OutLeft, float& OutRight);
	/// The voice for a sound at `At` (UE cm) heard by `Ears`. `bTrueHrtf`:
	/// keep the true place (the engine's HRTF does the rest).
	AUTOCRAFT_API FAcVoice3D Place(const FAcEars& Ears, const FVector& At, double RangeCells, bool bBlocked, bool bTrueHrtf = false);
	/// The point round a listener (`Loc`, `Front`, `Right`) that UE's
	/// equal-power stereo panner plays at pan `Pan` (0 left … 1 right), 1 m out.
	AUTOCRAFT_API FVector VirtualPoint(const FVector& Loc, const FVector& Front, const FVector& Right, float Pan);
	/// `GameController.soundBlocked`: ground higher than the line between
	/// the ears `A` and the sound `B` (UE cm), or a building it passes
	/// through below its roof; not the building the sound comes from.
	AUTOCRAFT_API bool SoundBlocked(const FVector& A, const FVector& B, const std::function<double(ac::Vec2)>& Ground,
		const std::vector<ac::Structure>& Structures);
	/// `sightPoint(reach:)`: where a driven gun's shot with nothing to hit is
	/// spent: at the crosshair, or `ReachCm` out toward it from the unit's eye
	/// (the camera's in first person, `ChaseEye` in third).
	AUTOCRAFT_API FVector SightPoint(const FVector& Camera, const FVector& Forward, const TOptional<FVector>& ChaseEye,
		const TOptional<FVector>& Crosshair, double ReachCm);
}

UCLASS()
class AUTOCRAFT_API UAcPilotAudio : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcPilotAudio* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/// The voice of a world sound at `At` (UE cm) round `Ears`, with
	/// `bBlocked` from `soundBlocked`.
	FAcVoice3D Voice(const FAcEars& Ears, const FVector& At, bool bBlocked) const;
	/// The attenuation 3D voices play with (spatialised, no distance model).
	USoundAttenuation* Attenuation() const { return Spatial; }
	/// The voice's filter and hall send on its component; `nullptr`: none
	/// (the top-down view).
	void Apply(UAudioComponent* C, const FAcVoice3D* V) const;
	/// One line per voice and frame (`-AcPilotAudioLog`, `ac.PilotAudioLog 1`).
	void Log(FName Sound, const FAcVoice3D& V, float Volume) const;
	bool Logging() const;
	/// `soundBlocked` against the current state and terrain.
	bool Blocked(const FVector& Ears, const FVector& Sound) const;

private:
	void OnFrame(const FAcFrame& Frame);
	void MakeHall();
	void Probe(const FAcFrame& Frame);

	FDelegateHandle FrameHandle;
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> Spatial;
	UPROPERTY(Transient)
	TObjectPtr<USoundSubmix> Hall;
	UPROPERTY(Transient)
	TObjectPtr<USubmixEffectReverbPreset> HallReverb;
	bool bWasDriving = false;
	bool bLogFlag = false;
	bool bProbe = false;
	double ProbeClock = 0;
	int32 ProbeStep = -1;
	TArray<TPair<TWeakObjectPtr<UAudioComponent>, double>> ProbeStops;
};
