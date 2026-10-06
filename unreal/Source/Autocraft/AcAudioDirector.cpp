// See AcAudioDirector.h. Swift: Sources/Autocraft/Audio.swift (AudioDirector),
// SoundBoard.swift, GameController.swift:320-410, GameController+Ears.swift.
#include "AcAudioDirector.h"

#include "AcLog.h"
#include "AcShot.h"
#include "AcNewKinds.h"
#include "AcPilotAudio.h"
#include "AcPoseNewKinds.h"
#include "AcRtsPawn.h"
#include "AcSettings.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"

#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "AudioMixerPlatformNonRealtime.h"
#include "AudioMixerSubmix.h"
#include "RHIGlobals.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundClass.h"

#include "Rules.h"
#include "Simulation.h"

#include <algorithm>
#include "Misc/App.h"

#include <cmath>

static TAutoConsoleVariable<int32> CVarAcAudioFlat(TEXT("ac.AudioFlat"), 0,
	TEXT("1: play every sound unplaced (2D: the gains without the pan). On by itself on the non-realtime mixer; -1: never (offline checks)."));
static TAutoConsoleVariable<int32> CVarAcAudioRenderSelf(TEXT("ac.AudioRenderSelf"), 1,
	TEXT("Offline recording: 1 renders one frame of audio a frame here; 0 leaves it to the non-realtime device."));
static TAutoConsoleVariable<int32> CVarAcAudioSkipSilent(TEXT("ac.AudioSkipSilent"), 1,
	TEXT("1: with no audio device (-nosound) place, pick and start no voices (sounds are still counted for -AcAudioLog); 0: try anyway (A/B)."));
static TAutoConsoleVariable<int32> CVarAcAudioLog(TEXT("ac.AudioLog"), 0,
	TEXT("1: log every second which sounds started (AcAudioDirector)."));

float AcSound::MasterGain()
{
	return Volume * FMath::Clamp(UAcSettings::Get().MasterVolume, 0.f, 1.f);
}

float AcSound::MusicGain()
{
	const UAcSettings& S = UAcSettings::Get();
	return S.bMusic ? MusicVolume * FMath::Clamp(S.MusicVolume, 0.f, 1.f) : 0.f;
}

float AcSound::SfxGain()
{
	return MasterGain() * FMath::Clamp(UAcSettings::Get().SfxVolume, 0.f, 1.f);
}

float AcSound::AmbientGain()
{
	return MasterGain() * FMath::Clamp(UAcSettings::Get().AmbientVolume, 0.f, 1.f);
}

namespace
{
	const ac::Unit* FindUnit(const ac::Simulation& Sim, int64 Id)
	{
		const auto It = Sim.unitIndexByID.find(Id);
		if (It != Sim.unitIndexByID.end() && It->second >= 0 && It->second < (int64)Sim.state.units.size()
			&& Sim.state.units[It->second].id == Id)
		{
			return &Sim.state.units[It->second];
		}
		for (const ac::Unit& U : Sim.state.units)
		{
			if (U.id == Id) return &U;
		}
		return nullptr;
	}

	const ac::Structure* FindStructure(const ac::GameState& State, int64 Id)
	{
		for (const ac::Structure& S : State.structures)
		{
			if (S.id == Id) return &S;
		}
		return nullptr;
	}

	/// A Ranger whose side has the Mini gun (`GameController.minigun`).
	bool Minigun(const ac::Simulation& Sim, const ac::Unit& U)
	{
		return U.kind == ac::UnitKind::ranger && Sim.has(U.owner, ac::Upgrade::minigun);
	}

	/// The loops' keys: what they belong to, times 8, plus which loop.
	enum ELoopSlot : int64 { ToolSlot = 0, MinigunSlot = 1, HumSlot = 2 };
	constexpr int64 HealKey = -1;
	constexpr int64 WindKeys[2] = {-2, -3};

	/// The names of the waves that loop.
	bool IsLoop(FName Name)
	{
		static const FName Loops[] = {"wind", "drill", "weld", "hum", "minigun", "heal"};
		for (const FName& L : Loops)
		{
			if (L == Name) return true;
		}
		return false;
	}

	FString AcTime() { return FDateTime::Now().ToIso8601(); }
}

// MARK: - The rules

FAcAudioRules::FWeapon FAcAudioRules::WeaponOf(std::optional<ac::UnitKind> Kind, bool bAnchored, bool bTower)
{
	if (bTower) return {"missile", AcSound::MissileGain, 5};
	if (!Kind) return {"gun", 0.32f, 9};
	switch (*Kind)
	{
	case ac::UnitKind::peregrine: return {"seekers", AcSound::RocketGain, 6};
	case ac::UnitKind::atlas: return {"atlasshot", AcSound::AtlasShotGain, 3};
	case ac::UnitKind::scorpion: return {"scorpionsting", AcSound::ScorpionStingGain, 3};
	case ac::UnitKind::comet: return {"pistol", 0.3f, 8};
	// `bAnchored` doubles as "the alternate weapon": a Kestrel firing its rail gun at a flier.
	case ac::UnitKind::kestrel: return bAnchored ? FWeapon{"anchorshot", 0.45f, 3} : FWeapon{"rockets", AcSound::RocketGain, 6};
	case ac::UnitKind::hailstorm: return {"flak", AcSound::FlakGain, 6};
	case ac::UnitKind::prospector: return {"weld", 0.3f, 6};
	case ac::UnitKind::firefly: return {"flame", 0.4f, 5};
	case ac::UnitKind::juggernaut: return {"breacher", 0.4f, 6};
	case ac::UnitKind::longbow: return bAnchored ? FWeapon{"anchorshot", 0.55f, 4} : FWeapon{"cannon", AcSound::TankCannonGain, 5};
	default: return {"gun", 0.32f, 9};
	}
}

const char* FAcAudioRules::DeathOf(ac::UnitKind Kind)
{
	switch (AcNewKinds::StandIn(Kind))
	{
	case ac::UnitKind::scorpion: return "scorpiondeath";
	case ac::UnitKind::ranger: return "rangerdeath";
	case ac::UnitKind::comet: return "cometdeath";
	case ac::UnitKind::firefly: return "fireflydeath";
	case ac::UnitKind::juggernaut: return "juggernautdeath";
	case ac::UnitKind::dropship: return "dropshipdeath";
	case ac::UnitKind::longbow: return "longbowdeath";
	case ac::UnitKind::kestrel: return "kestreldeath";
	case ac::UnitKind::hailstorm: return "hailstormdeath";
	default: return "prospectordeath";
	}
}

const char* FAcAudioRules::VoiceOf(ac::UnitKind Kind)
{
	switch (AcNewKinds::StandIn(Kind))
	{
	case ac::UnitKind::scorpion: return "vscorpion";
	case ac::UnitKind::prospector: return "vprospector";
	case ac::UnitKind::ranger: return "vranger";
	case ac::UnitKind::comet: return "vcomet";
	case ac::UnitKind::firefly: return "vfirefly";
	case ac::UnitKind::juggernaut: return "vjuggernaut";
	case ac::UnitKind::dropship: return "vdropship";
	case ac::UnitKind::longbow: return "vlongbow";
	case ac::UnitKind::kestrel: return "vkestrel";
	case ac::UnitKind::hailstorm: return "vhailstorm";
	default: return "vranger";
	}
}

bool FAcAudioRules::Allow(FName Sound, int32 PerSecond)
{
	TArray<double>& Times = Recent.FindOrAdd(Sound);
	Times.RemoveAll([this](double T) { return !(Now - T < 1.0); });
	if (Times.Num() >= PerSecond) return false;
	Times.Add(Now);
	return true;
}

bool FAcAudioRules::OneShot(FName Sound, ac::Vec2 At, float Gain, EAcSoundPool Pool)
{
	if (Variants(Sound) <= 0) return false;
	const float R = Pool == EAcSoundPool::InEar ? 1.f : Reach(At);
	if (!(R > AcSound::Audible)) return false;
	Cues.push_back({Sound, Gain * R, At, Pool});
	return true;
}

void FAcAudioRules::Later(double Seconds, FName Sound, ac::Vec2 At, float Gain)
{
	Delayed.push_back({Now + Seconds, Sound, At, Gain});
}

void FAcAudioRules::Due()
{
	for (size_t I = 0; I < Delayed.size();)
	{
		if (Delayed[I].At <= Now)
		{
			const FDelayed D = Delayed[I];
			Delayed.erase(Delayed.begin() + I);
			OneShot(D.Sound, D.Where, D.Gain, EAcSoundPool::Combat);
		}
		else
		{
			++I;
		}
	}
}

std::vector<FAcSoundCue> FAcAudioRules::TakeCues()
{
	std::vector<FAcSoundCue> Out;
	Out.swap(Cues);
	return Out;
}

void FAcAudioRules::Reset()
{
	Tools.Reset();
	Miniguns.Reset();
	Hums.Reset();
	HealFade = 0.f;
	bHealing = false;
	Jumping.Reset();
	Anchoring.Reset();
	Burrowing.Reset();
	Locking.Reset();
	bWatched = false;
	Delayed.clear();
	TowerShots.clear();
}

void FAcAudioRules::Deposited(ac::Vec2 At) { OneShot("deposit", At, AcSound::DepositGain, EAcSoundPool::Deposit); }

void FAcAudioRules::Trained(ac::UnitKind Kind, ac::Vec2 At)
{
	if (!(Now - LastVoice > AcSound::VoiceEvery)) return;
	if (OneShot(VoiceOf(Kind), At, AcSound::VoiceGain, EAcSoundPool::Deposit)) LastVoice = Now;
}

void FAcAudioRules::Shot(std::optional<ac::UnitKind> Kind, bool bAnchored, std::optional<ac::Vec2> From, ac::Vec2 At, int64 Victim,
	bool bStructure, bool bMinigun, bool bTower)
{
	if (Victim == LocalPlayer && bStructure && Now - LastAlert > AcSound::AlertEvery
		&& OneShot("alert", At, AcSound::AlertGain, EAcSoundPool::InEar))
	{
		LastAlert = Now;
	}
	if (bMinigun && Kind == ac::UnitKind::ranger) return;
	const FWeapon W = WeaponOf(Kind, bAnchored, bTower);
	if (bTower && From)
	{
		const std::pair<double, double> Key{From->x, From->y};
		const auto It = TowerShots.find(Key);
		const bool bQuiet = Now - (It != TowerShots.end() ? It->second : -10.0) > AcSound::SentinelQuiet;
		TowerShots[Key] = Now;
		if (bQuiet && Allow("sentineltrack", 2))
		{
			OneShot("sentineltrack", *From, AcSound::SentinelTrackGain, EAcSoundPool::Combat);
		}
	}
	if (!Allow(W.Name, W.PerSecond)) return;
	OneShot(W.Name, From ? *From : At, W.Gain, EAcSoundPool::Combat);
	if (Kind == ac::UnitKind::hailstorm && !bTower && Allow("flakhit", 5))
	{
		const double Flight = From ? ac::Rules::flight(ac::UnitKind::hailstorm, false, ac::distance(*From, At)) : 0.0;
		Later(Flight, "flakhit", At, AcSound::FlakHitGain);
	}
	if (Kind == ac::UnitKind::longbow && bAnchored && Allow("anchorhit", 4))
	{
		// When the shell comes down (`Rules.flight`), not as it leaves.
		const double Flight = From ? ac::Rules::flight(ac::UnitKind::longbow, true, ac::distance(*From, At)) : 0.0;
		Later(Flight, "anchorhit", At, AcSound::AnchorHitGain);
	}
}

void FAcAudioRules::Died(ac::UnitKind Kind, ac::Vec2 At)
{
	const FName Sound = DeathOf(Kind);
	if (!Allow(Sound, 3)) return;
	OneShot(Sound, At, AcSound::DeathGain, EAcSoundPool::Combat);
	// Machines blow up.
	if (ac::Leveling::machines.contains(Kind) && Allow("blast", 3))
	{
		OneShot("blast", At, AcSound::BlastGain, EAcSoundPool::Combat);
	}
}

void FAcAudioRules::Destroyed(ac::StructureKind Kind, ac::Vec2 At)
{
	OneShot("boom", At, Kind == ac::StructureKind::habDome ? AcSound::HabDomeBoomGain : AcSound::BoomGain, EAcSoundPool::Combat);
}

void FAcAudioRules::Stomp(ac::Vec2 At)
{
	// The Atlas's foot driven into the ground (Tools/AudioGen/atlas.manifest.json),
	// heard when the foot lands, with the ring and the dust.
	if (Allow("stomp", 3)) Later(AcPoseNew::AtlasStompImpact, "stomp", At, AcSound::StompGain);
}

void FAcAudioRules::Grenade(ac::Vec2 At)
{
	if (Allow("blast", 3)) OneShot("blast", At, AcSound::GrenadeGain, EAcSoundPool::Combat);
}

void FAcAudioRules::Jump(bool bLanding, ac::Vec2 At)
{
	const FName Sound = bLanding ? "jetland" : "jetup";
	if (Allow(Sound, 4)) OneShot(Sound, At, AcSound::JetGain, EAcSoundPool::Combat);
}

void FAcAudioRules::Anchor(bool bUp, ac::Vec2 At)
{
	const FName Sound = bUp ? "anchorset" : "anchorlift";
	if (Allow(Sound, 3)) OneShot(Sound, At, AcSound::AnchorGain, EAcSoundPool::Combat);
}

void FAcAudioRules::Burrow(bool bUp, ac::Vec2 At)
{
	const FName Sound = bUp ? "scorpionbury" : "scorpionunbury";
	if (Allow(Sound, 3)) OneShot(Sound, At, AcSound::ScorpionDigGain, EAcSoundPool::Combat);
}

void FAcAudioRules::ScorpionLock(ac::Vec2 At)
{
	// The rising chirp of the lock-on second (docs/new-units.md, Scorpion looks).
	if (Allow("scorpionlock", 3)) OneShot("scorpionlock", At, AcSound::ScorpionLockGain, EAcSoundPool::Combat);
}

void FAcAudioRules::Footfall(ac::Vec2 At)
{
	if (Allow("atlasstep", 4)) OneShot("atlasstep", At, AcSound::AtlasFootGain, EAcSoundPool::Combat);
}

void FAcAudioRules::HitMark(bool bKill, bool bStructure)
{
	if (bKill && Allow("killmark", 4)) OneShot("killmark", {}, AcSound::KillMarkGain, EAcSoundPool::InEar);
	const FName Sound = bStructure && Variants("hitclank") > 0 ? "hitclank" : "hitmark";
	if (!Allow(Sound, 8)) return;
	OneShot(Sound, {}, Sound == FName("hitclank") ? AcSound::HitClankGain : AcSound::HitMarkGain, EAcSoundPool::InEar);
}

void FAcAudioRules::DirtHit(ac::Vec2 At)
{
	if (Allow("dirthit", 8)) OneShot("dirthit", At, AcSound::DirtHitGain, EAcSoundPool::Combat);
}

void FAcAudioRules::Step()
{
	if (Allow("step", 6)) OneShot("step", {}, AcSound::StepGain, EAcSoundPool::InEar);
}

void FAcAudioRules::Breath() { OneShot("breath", {}, AcSound::BreathGain, EAcSoundPool::InEar); }

void FAcAudioRules::LockOn()
{
	if (Allow("lockon", 3)) OneShot("lockon", {}, AcSound::LockOnGain, EAcSoundPool::InEar);
}

void FAcAudioRules::LevelUp()
{
	if (Allow("levelup", 1)) OneShot("levelup", {}, AcSound::LevelUpGain, EAcSoundPool::InEar);
}

void FAcAudioRules::Picked()
{
	if (Allow("pick", 4)) OneShot("pick", {}, AcSound::PickGain, EAcSoundPool::InEar);
}

void FAcAudioRules::PullAway(ac::UnitKind Kind)
{
	const char* Sound = nullptr;
	float Gain = 0.f;
	switch (Kind)
	{
	case ac::UnitKind::atlas: Sound = "atlasmove"; Gain = AcSound::AtlasEngineGain; break;
	case ac::UnitKind::scorpion: Sound = "scorpionmove"; Gain = AcSound::ScorpionEngineGain; break;
	// The Dart-like shriek (docs/new-units.md, Peregrine sound).
	case ac::UnitKind::peregrine: Sound = "peregrinepass"; Gain = AcSound::KestrelEngineGain; break;
	case ac::UnitKind::firefly: Sound = "fireflymove"; Gain = AcSound::FireflyEngineGain; break;
	case ac::UnitKind::dropship: Sound = "dropshipmove"; Gain = AcSound::DropshipEngineGain; break;
	case ac::UnitKind::kestrel: Sound = "kestrelmove"; Gain = AcSound::KestrelEngineGain; break;
	case ac::UnitKind::hailstorm: Sound = "hailstormmove"; Gain = AcSound::HailstormEngineGain; break;
	default: return;
	}
	if (!(Now - LastEngine > AcSound::EngineEvery)) return;
	if (OneShot(Sound, {}, Gain, EAcSoundPool::InEar)) LastEngine = Now;
}

void FAcAudioRules::Consume(const std::vector<ac::GameEvent>& Events, const ac::Simulation& Sim)
{
	const ac::GameState& State = Sim.state;
	for (const ac::GameEvent& E : Events)
	{
		if (const auto* D = E.as<ac::GameEvent::Deposited>())
		{
			Deposited(D->at);
		}
		else if (const auto* T = E.as<ac::GameEvent::Trained>())
		{
			Trained(T->kind, T->at);
		}
		else if (const auto* S = E.as<ac::GameEvent::Shot>())
		{
			const ac::Unit* Shooter = FindUnit(Sim, S->unit);
			const ac::Structure* Tower = Shooter ? nullptr : FindStructure(State, S->unit);
			std::optional<ac::Vec2> From;
			if (Shooter) From = Shooter->position;
			else if (Tower) From = Tower->position;
			const ac::Unit* Target = FindUnit(Sim, S->target);
			const bool bRail = Shooter && Shooter->kind == ac::UnitKind::kestrel && Target && ac::Rules::stats(Target->kind).air;
			Shot(Shooter ? std::optional<ac::UnitKind>(Shooter->kind) : std::nullopt, bRail || (Shooter && Shooter->anchor.value_or(0) >= 1), From,
				S->at, S->victim, FindStructure(State, S->target) != nullptr, Shooter && Minigun(Sim, *Shooter), Tower != nullptr);
		}
		else if (const auto* M = E.as<ac::GameEvent::Missed>())
		{
			// The driven gun's miss (`GameController.pilotMissed`): Swift spends
			// the round where the sight meets the ground; until the pilot chunk
			// says where that is, where the sim spent it.
			const ac::Unit* Shooter = FindUnit(Sim, M->unit);
			const int64 Driven = Sim.pilot ? Sim.pilot->unit : (PilotUnit ? *PilotUnit : -1);
			DirtHit(M->unit == Driven && PilotSpent ? *PilotSpent : M->at);
			Shot(Shooter ? std::optional<ac::UnitKind>(Shooter->kind) : std::nullopt, Shooter && Shooter->anchor.value_or(0) >= 1,
				Shooter ? std::optional<ac::Vec2>(Shooter->position) : std::nullopt, M->at, -1, false, Shooter && Minigun(Sim, *Shooter));
		}
		else if (const auto* L = E.as<ac::GameEvent::Landed>())
		{
			// `markPilotHit`: the driven unit's round reached its target.
			const int64 Driven = Sim.pilot ? Sim.pilot->unit : (PilotUnit ? *PilotUnit : -1);
			if (L->unit == Driven)
			{
				const ac::Unit* U = FindUnit(Sim, L->target);
				const ac::Structure* B = FindStructure(State, L->target);
				const bool bAlive = U ? U->hp > 0 : B ? B->hp > 0 : false;
				HitMark(!bAlive, B != nullptr);
			}
		}
		else if (const auto* Di = E.as<ac::GameEvent::Died>())
		{
			Died(Di->kind, Di->at);
		}
		else if (const auto* De = E.as<ac::GameEvent::Destroyed>())
		{
			Destroyed(De->kind, De->at);
		}
		else if (E.is<ac::GameEvent::LeveledUp>())
		{
			LevelUp();
		}
		else if (const auto* B = E.as<ac::GameEvent::Blast>())
		{
			Grenade(B->at);
		}
		else if (const auto* St = E.as<ac::GameEvent::Stomp>())
		{
			Stomp(St->at);
		}
	}
}

void FAcAudioRules::Watch(const ac::GameState& State, const ac::Sight* Sight)
{
	// The first look only learns who is in the air and anchored.
	const bool bSpeak = bWatched;
	bWatched = true;
	TSet<int64> Air, Set, Buried, Locks;
	for (const ac::Unit& U : State.units)
	{
		// `let heard = u.owner == team || seen(u.id)`.
		const bool bHeard = bSpeak && (U.owner == LocalPlayer || !Sight || Sight->ids.count(U.id) > 0);
		if (U.jumpFrom)
		{
			Air.Add(U.id);
			if (bHeard && !Jumping.Contains(U.id)) Jump(false, U.position);
		}
		else if (bHeard && Jumping.Contains(U.id))
		{
			Jump(true, U.position);
		}
		if (U.kind == ac::UnitKind::scorpion)
		{
			const bool bDown = U.anchored.value_or(false);
			if (bDown) Buried.Add(U.id);
			if (bHeard && bDown != Burrowing.Contains(U.id)) Burrow(bDown, U.position);
			const bool bLock = U.lockTarget.has_value();
			if (bLock) Locks.Add(U.id);
			if (bHeard && bLock && !Locking.Contains(U.id)) ScorpionLock(U.position);
		}
		if (U.kind == ac::UnitKind::longbow)
		{
			const bool bUp = U.anchored.value_or(false);
			if (bUp) Set.Add(U.id);
			if (bHeard && bUp != Anchoring.Contains(U.id)) Anchor(bUp, U.position);
		}
	}
	Jumping = MoveTemp(Air);
	Anchoring = MoveTemp(Set);
	Burrowing = MoveTemp(Buried);
	Locking = MoveTemp(Locks);
}

void FAcAudioRules::Loops(const ac::GameState& Shown, double Dt, std::vector<FAcLoopWant>& Out)
{
	const float D = static_cast<float>(Dt);
	auto Fade = [D](float& Gain, float Target, float In, float OutTime)
	{
		const float Rate = D / (Target > Gain ? In : OutTime);
		Gain += FMath::Clamp(Target - Gain, -Rate, Rate);
	};
	// The wind: two layers, the second half a loop on, panned apart.
	for (int32 K = 0; K < 2; ++K)
	{
		FAcLoopWant W;
		W.Key = WindKeys[K];
		W.Name = "wind";
		W.Variant = FMath::Max(0, FMath::Min(K, Variants("wind") - 1));
		W.Volume = AcSound::WindGain;
		W.bAmbient = true;
		W.Pan = K == 0 ? -AcSound::WindPan : AcSound::WindPan;
		W.StartFraction = (K == 1 && Variants("wind") < 2) ? 0.5f : 0.f;
		Out.push_back(W);
	}
	// Each Prospector's tool: the drill while it mines, the welder while it builds.
	TSet<int64> Prospectors;
	for (const ac::Unit& U : Shown.units)
	{
		if (U.kind != ac::UnitKind::prospector) continue;
		Prospectors.Add(U.id);
		const FName Want = U.task == ac::Unit::Task::building || U.task == ac::Unit::Task::repairing ? FName("weld") : FName("drill");
		FTool* T = Tools.Find(U.id);
		if (!T)
		{
			const int32 N = Variants(Want);
			if (N <= 0) continue;
			T = &Tools.Add(U.id, FTool{Want, static_cast<int32>(U.id % N), 0.f});
		}
		// Swap the loop once the old one has faded out.
		if (T->Sound != Want && T->Gain < 0.01f && Variants(Want) > 0)
		{
			T->Sound = Want;
			T->Variant = std::uniform_int_distribution<int32>(0, Variants(Want) - 1)(Random);
		}
		const float Target = U.working() && T->Sound == Want ? 1.f : 0.f;
		// ~0.15 s attack, ~0.4 s release.
		Fade(T->Gain, Target, 0.15f, 0.4f);
		FAcLoopWant W;
		W.Key = U.id * 8 + ToolSlot;
		W.Name = T->Sound;
		W.Variant = T->Variant;
		W.Volume = T->Gain * (T->Sound == FName("weld") ? AcSound::WeldLoopGain : AcSound::DrillGain) * Reach(U.position);
		W.At = U.position;
		Out.push_back(W);
	}
	for (auto It = Tools.CreateIterator(); It; ++It)
	{
		if (!Prospectors.Contains(It.Key())) It.RemoveCurrent();
	}
	// The mini guns: one roar a Ranger from its burst's first round to its
	// last; the nearest `MinigunsAtOnce` of those firing.
	const double Beat = ac::UnitStats::minigun.cooldown + 0.06;
	std::vector<std::pair<const ac::Unit*, float>> Near;
	for (const ac::Unit& U : Shown.units)
	{
		if (U.kind != ac::UnitKind::ranger || !(Shown.time - U.firedAt.value_or(-10) < Beat)) continue;
		if (!(U.owner >= 0 && U.owner < (int64)Shown.players.size())) continue;
		const auto& Ups = Shown.players[U.owner].upgrades;
		if (!Ups || !Ups->count(ac::Upgrade::minigun)) continue;
		const float R = Reach(U.position);
		if (R > AcSound::Audible) Near.push_back({&U, R});
	}
	std::stable_sort(Near.begin(), Near.end(), [](const auto& A, const auto& B) { return A.second > B.second; });
	TSet<int64> On;
	for (size_t I = 0; I < Near.size() && I < (size_t)AcSound::MinigunsAtOnce; ++I) On.Add(Near[I].first->id);
	const int32 Guns = Variants("minigun");
	for (const int64 Id : On)
	{
		if (!Miniguns.Contains(Id) && Guns > 0) Miniguns.Add(Id, FTool{"minigun", static_cast<int32>(Id % Guns), 0.f});
	}
	for (auto It = Miniguns.CreateIterator(); It; ++It)
	{
		// In at once with the first round, out in 0.08 s after the last.
		const float Target = On.Contains(It.Key()) ? 1.f : 0.f;
		Fade(It.Value().Gain, Target, 0.03f, 0.08f);
		const ac::Unit* U = nullptr;
		for (const ac::Unit& X : Shown.units)
		{
			if (X.id == It.Key()) { U = &X; break; }
		}
		if (!(It.Value().Gain > 0 || Target > 0) || !U)
		{
			It.RemoveCurrent();
			continue;
		}
		FAcLoopWant W;
		W.Key = It.Key() * 8 + MinigunSlot;
		W.Name = "minigun";
		W.Variant = It.Value().Variant;
		W.Volume = It.Value().Gain * AcSound::MinigunGain * Reach(U->position);
		W.At = U->position;
		Out.push_back(W);
	}
	// The Citadels' hum, once built.
	TSet<int64> Citadels;
	for (const ac::Structure& S : Shown.structures)
	{
		if (S.kind != ac::StructureKind::citadel) continue;
		Citadels.Add(S.id);
		if (S.complete()) Hums.Add(S.id);
		if (!Hums.Contains(S.id) || Variants("hum") <= 0) continue;
		FAcLoopWant W;
		W.Key = S.id * 8 + HumSlot;
		W.Name = "hum";
		W.Volume = AcSound::HumGain * Reach(S.position);
		W.bAmbient = true;
		W.At = S.position;
		W.Lift = 1.5f;
		Out.push_back(W);
	}
	for (auto It = Hums.CreateIterator(); It; ++It)
	{
		if (!Citadels.Contains(*It)) It.RemoveCurrent();
	}
	// The driven Dropship's heal beam, in your ear.
	Fade(HealFade, bHealing ? 1.f : 0.f, 0.15f, 0.4f);
	if ((bHealing || HealFade > 0.f) && Variants("heal") > 0)
	{
		FAcLoopWant W;
		W.Key = HealKey;
		W.Name = "heal";
		W.Volume = HealFade * AcSound::HealGain;
		W.bInEar = true;
		Out.push_back(W);
	}
}

// MARK: - The director

UAcAudioDirector* UAcAudioDirector::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAcAudioDirector>() : nullptr;
}

bool UAcAudioDirector::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAcAudioDirector::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UAcSimSubsystem>();
	bLog = FParse::Param(FCommandLine::Get(), TEXT("AcAudioLog"));
	Brain.Reach = [this](ac::Vec2 P) { return Reach(P); };
	Brain.Variants = [this](FName N)
	{
		const FAcSoundVariants* V = Bank.Find(N);
		return V ? V->Waves.Num() : 0;
	};
	double Quit = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcAudioQuitAt="), Quit)) QuitAt = Quit;
	FString RecordPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcAudioRecord="), RecordPath))
	{
		FRecording R;
		R.Path = FPaths::ConvertRelativePathToFull(RecordPath);
		FParse::Value(FCommandLine::Get(), TEXT("AcAudioRecordAt="), R.At);
		FParse::Value(FCommandLine::Get(), TEXT("AcAudioRecordSeconds="), R.Seconds);
		Recording = R;
	}
}

void UAcAudioDirector::Deinitialize()
{
	if (UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr)
	{
		Sim->RemoveFrameListener(EAcFrameStage::Audio, FrameHandle);
		Sim->OnGameStarted.Remove(StartedHandle);
	}
	if (bLog || CVarAcAudioLog.GetValueOnGameThread())
	{
		TArray<FString> Parts;
		TArray<FName> Names;
		Totals.GetKeys(Names);
		Names.Sort(FNameLexicalLess());
		for (const FName& N : Names) Parts.Add(FString::Printf(TEXT("%s %d"), *N.ToString(), Totals[N]));
		UE_LOG(LogAutocraft, Log, TEXT("audio: total over %.0f s: %s"), Brain.Clock(), *FString::Join(Parts, TEXT(", ")));
	}
	Super::Deinitialize();
}

void UAcAudioDirector::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	LoadBank();
	MakeSettings();
	UAcSimSubsystem* Sim = InWorld.GetSubsystem<UAcSimSubsystem>();
	if (!Sim) return;
	FrameHandle = Sim->AddFrameListener(EAcFrameStage::Audio, FAcFrameEvent::FDelegate::CreateUObject(this, &UAcAudioDirector::OnFrame));
	StartedHandle = Sim->OnGameStarted.AddUObject(this, &UAcAudioDirector::OnGameStarted);
	if (FParse::Param(FCommandLine::Get(), TEXT("AcHearAll")) && Sim->IsRunning()) SetHearWholeMap(true);
	if (FAudioDevice* Device = InWorld.GetAudioDeviceRaw())
	{
		// The quality level caps the voices at 32; the device has room for
		// `AudioMaxChannels` (DefaultEngine.ini, 96).
		const int32 Voices = FMath::Max(Device->GetMaxChannels(), FMath::Min(Device->GetMaxSources(), 96));
		if (Voices > Device->GetMaxChannels()) Device->SetMaxChannels(Voices);
		UE_LOG(LogAutocraft, Log, TEXT("audio: %d sounds in the bank, %d voices, %s"), Bank.Num(), Voices,
			Device->IsNonRealtime() ? TEXT("non-realtime (offline render)") : TEXT("realtime"));
	}
}

void UAcAudioDirector::OnGameStarted(UAcSimSubsystem& Sim)
{
	// A new session's scene: drop the old loops (`AudioDirector.bind`).
	for (auto& Pair : LoopVoices)
	{
		if (Pair.Value) Pair.Value->Stop();
	}
	LoopVoices.Reset();
	LoopSounds.Reset();
	Loops3D.Reset();
	Brain.Reset();
	Brain.LocalPlayer = Sim.LocalPlayer();
	if (FParse::Param(FCommandLine::Get(), TEXT("AcHearAll"))) SetHearWholeMap(true);
}

void UAcAudioDirector::LoadBank()
{
	Bank.Reset();
	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Audio/Sounds.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogAutocraft, Warning, TEXT("audio: no sound table at %s (run Tools/Editor/import_sounds.py)"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return;
	const TSharedPtr<FJsonObject>* Sounds = nullptr;
	if (!Root->TryGetObjectField(TEXT("sounds"), Sounds)) return;
	int32 Missing = 0;
	for (const auto& Pair : (*Sounds)->Values)
	{
		const TSharedPtr<FJsonObject> Sound = Pair.Value->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Variants = nullptr;
		if (!Sound || !Sound->TryGetArrayField(TEXT("variants"), Variants)) continue;
		const FName Name(*FString(Pair.Key));
		FAcSoundVariants& Entry = Bank.FindOrAdd(Name);
		for (const TSharedPtr<FJsonValue>& V : *Variants)
		{
			const FString Asset = V->AsObject()->GetStringField(TEXT("asset"));
			USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Asset);
			if (!Wave)
			{
				++Missing;
				continue;
			}
			// The loops loop; a one-shot of a looping wave (the weld's
			// strike) is stopped after one pass (`StopAt`).
			if (IsLoop(Name)) Wave->bLooping = true;
			// Keep every effect in memory, as Swift does (`loadBank`): a
			// first shot must not wait for its chunk (and an offline render
			// runs far ahead of the disk).
			Wave->OverrideLoadingBehavior(ESoundWaveLoadingBehavior::RetainOnLoad);
			Entry.Waves.Add(Wave);
		}
	}
	{
		TArray<FString> Parts;
		for (const auto& P : Bank) Parts.Add(FString::Printf(TEXT("%s %d"), *P.Key.ToString(), P.Value.Waves.Num()));
		UE_LOG(LogAutocraft, Verbose, TEXT("audio: bank %s"), *FString::Join(Parts, TEXT(", ")));
	}
	if (const FAcSoundVariants* Wind = Bank.Find("wind"); Wind && Wind->Waves.Num())
	{
		const USoundWave* W = Wind->Waves[0];
		UE_LOG(LogAutocraft, Log, TEXT("audio: bank wind %s: %.2f s, %d ch, %d Hz, volume %.2f, class %s, streaming %d, loading %d"), *W->GetPathName(), W->Duration,
			W->NumChannels, (int32)W->GetSampleRateForCurrentPlatform(), W->Volume, W->SoundClassObject ? *W->SoundClassObject->GetName() : TEXT("none"),
			W->IsStreaming() ? 1 : 0, (int32)W->GetLoadingBehavior());
	}
	BankLoadedAt = FPlatformTime::Seconds();
	if (Missing > 0) UE_LOG(LogAutocraft, Warning, TEXT("audio: %d sound waves missing (run Tools/Editor/import_sounds.py)"), Missing);
}

void UAcAudioDirector::MakeSettings()
{
	// Placed round the listener, no distance model of its own (the gain
	// comes from `Reach`), panned by azimuth (equal power).
	Panned = NewObject<USoundAttenuation>(this);
	FSoundAttenuationSettings& A = Panned->Attenuation;
	A.bAttenuate = false;
	A.bSpatialize = true;
	A.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_Default;
	A.NonSpatializedRadiusStart = 0.f;
	A.NonSpatializedRadiusEnd = 0.f;
	A.bAttenuateWithLPF = false;
	A.bEnableOcclusion = false;
	A.bEnableReverbSend = false;
	A.bEnablePriorityAttenuation = false;
	A.bEnableListenerFocus = false;
	auto Pool = [this](int32 Count)
	{
		USoundConcurrency* C = NewObject<USoundConcurrency>(this);
		C->Concurrency.MaxCount = Count;
		C->Concurrency.bLimitToOwner = false;
		C->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
		return C;
	};
	DepositPool = Pool(AcSound::DepositVoices);
	CombatPool = Pool(AcSound::CombatVoices);
	InEarPool = Pool(AcSound::InEarVoices);
}

void UAcAudioDirector::SetMuted(bool bInMuted) { bMuted = bInMuted; }

bool UAcAudioDirector::HearsWholeMap() const
{
	const UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr;
	if (!Sim || !Sim->IsRunning()) return false;
	const std::optional<ac::Listener>& L = Sim->Session().listener;
	return L && !L->local;
}

void UAcAudioDirector::SetHearWholeMap(bool bOn)
{
	UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr;
	if (!Sim || !Sim->IsRunning()) return;
	std::optional<ac::Listener>& L = Sim->Session().listener;
	if (!L)
	{
		L = ac::Listener{};
		L->range = 24;
	}
	L->local = !bOn;
}

double UAcAudioDirector::GroundHeight(ac::Vec2 P) const
{
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	return Terrain ? Terrain->FieldHeight(P) : 0.0;
}

float UAcAudioDirector::Reach(ac::Vec2 P) const
{
	const UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr;
	if (!Sim || !Sim->IsRunning()) return 1.f;
	const std::optional<ac::Listener>& L = Sim->Session().listener;
	if (L && !L->local) return 1.f;
	// The top-down window hears what it shows (`GameController.screenGain`).
	if (!Ears)
	{
		if (const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this))
		{
			if (const ac::FreeView* View = Pawn->View())
			{
				const ac::Vec2 C = View->canvas(ac::Vec3(P.x, GroundHeight(P), P.y));
				return static_cast<float>(ac::Listener::viewGain(C, View->size, View->cover));
			}
		}
	}
	return L ? static_cast<float>(L->gain(P)) : 1.f;
}

float UAcAudioDirector::Pan(ac::Vec2 P) const
{
	if (const AAcRtsPawn* Pawn = AAcRtsPawn::Get(this))
	{
		if (const ac::FreeView* View = Pawn->View())
		{
			const ac::Vec2 C = View->canvas(ac::Vec3(P.x, GroundHeight(P), P.y));
			const double X = C.x / FMath::Max(View->size.x, 1.0);
			return static_cast<float>(FMath::Clamp(X * 2 - 1, -1.0, 1.0)) * 0.85f;
		}
	}
	return 0.f;
}

FVector UAcAudioDirector::Place(ac::Vec2 P, float Lift, TOptional<float> FixedPan, TOptional<FAcVoice3D>& OutVoice) const
{
	OutVoice.Reset();
	const UAcPilotAudio* Pilot = UAcPilotAudio::Get(this);
	if (Ears && !FixedPan && Pilot)
	{
		// While driving (E7, `Audio.place`): round the ears as Swift's HRTF
		// has it, muffled behind a cliff or a building, wetter the farther.
		const FVector At = AcSpace::ToWorld(P, GroundHeight(P) + Lift);
		OutVoice = Pilot->Voice(*Ears, At, Blocked ? Blocked(Ears->At, At) : Pilot->Blocked(Ears->At, At));
		return OutVoice->PlayAt;
	}
	// Top-down: on a unit circle round the listener at the angle of its
	// screen pan (Swift: listener at the origin facing −Z, x = sin a, z = −cos a).
	FVector Loc = FVector::ZeroVector, Front = FVector::ForwardVector, Right = FVector::RightVector;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->GetAudioListenerPosition(Loc, Front, Right);
		}
	}
	const float A = (FixedPan ? *FixedPan : Pan(P)) * UE_HALF_PI;
	return Loc + (Right * FMath::Sin(A) + Front * FMath::Cos(A)) * 100.0;
}

bool UAcAudioDirector::PlaceFlat() const
{
	// The non-realtime mixer (-deterministicaudio, the offline render)
	// renders spatialized sources silent in UE 5.8, so there every sound
	// plays unplaced: the gains and the mix are right, the pan is not.
	const int32 Flat = CVarAcAudioFlat.GetValueOnGameThread();
	if (Flat < 0) return false;
	if (Flat > 0) return true;
	const FAudioDevice* Device = GetWorld() ? GetWorld()->GetAudioDeviceRaw() : nullptr;
	return Device && Device->IsNonRealtime();
}

USoundWave* UAcAudioDirector::Pick(FName Name, int32 Variant)
{
	const FAcSoundVariants* V = Bank.Find(Name);
	if (!V || V->Waves.Num() == 0) return nullptr;
	const int32 I = Variant >= 0 ? Variant % V->Waves.Num() : FMath::RandRange(0, V->Waves.Num() - 1);
	return V->Waves[I];
}

void UAcAudioDirector::Play(const FAcSoundCue& Cue)
{
	ThisSecond.FindOrAdd(Cue.Name)++;
	Totals.FindOrAdd(Cue.Name)++;
	if (!HasDevice()) return;
	USoundWave* Wave = Pick(Cue.Name);
	UWorld* World = GetWorld();
	if (!Wave || !World || bMuted) return;
	UAudioComponent* C = nullptr;
	if (Cue.Pool == EAcSoundPool::InEar || PlaceFlat())
	{
		// Offline (flat) a pilot's world sound keeps its 3D gain, filter and
		// hall send, without the pan.
		TOptional<FAcVoice3D> Voice;
		if (Cue.Pool != EAcSoundPool::InEar) Place(Cue.At, 0.7f, {}, Voice);
		C = UGameplayStatics::SpawnSound2D(World, Wave, Cue.Volume * (Voice ? Voice->Gain : 1.f) * AcSound::SfxGain(), 1.f, 0.f,
			Cue.Pool == EAcSoundPool::InEar ? InEarPool.Get() : Cue.Pool == EAcSoundPool::Combat ? CombatPool.Get() : DepositPool.Get());
		if (Voice)
		{
			if (const UAcPilotAudio* Pilot = UAcPilotAudio::Get(this))
			{
				Pilot->Apply(C, Voice.GetPtrOrNull());
				Pilot->Log(Cue.Name, *Voice, Cue.Volume * Voice->Gain * AcSound::SfxGain());
			}
		}
	}
	else
	{
		TOptional<FAcVoice3D> Voice;
		const FVector At = Place(Cue.At, 0.7f, {}, Voice);
		const UAcPilotAudio* Pilot = Voice ? UAcPilotAudio::Get(this) : nullptr;
		const float Volume = Cue.Volume * (Voice ? Voice->Gain : 1.f) * AcSound::SfxGain();
		C = UGameplayStatics::SpawnSoundAtLocation(World, Wave, At, FRotator::ZeroRotator, Volume, 1.f, 0.f,
			Pilot ? Pilot->Attenuation() : Panned.Get(), Cue.Pool == EAcSoundPool::Combat ? CombatPool.Get() : DepositPool.Get());
		if (Pilot)
		{
			Pilot->Apply(C, Voice.GetPtrOrNull());
			Pilot->Log(Cue.Name, *Voice, Volume);
		}
	}
	if (C && Wave->bLooping) StopAt.Add({C, Brain.Clock() + Wave->Duration});
}

bool UAcAudioDirector::HasDevice() const
{
	// -nosound (every headless run): no device, so no voice could start; each
	// try cost a placement, a pick and a failed spawn per loop per frame.
	if (!CVarAcAudioSkipSilent.GetValueOnGameThread()) return true;
	const UWorld* World = GetWorld();
	return World && World->GetAudioDeviceRaw() != nullptr;
}

void UAcAudioDirector::SyncLoops(const std::vector<FAcLoopWant>& Wants)
{
	if (!HasDevice())
	{
		// Only the counts the log line reads.
		AudibleLoops = 0;
		LoopCount.Reset();
		for (const FAcLoopWant& W : Wants)
		{
			if (!(W.Volume > AcSound::Audible)) continue;
			++AudibleLoops;
			LoopCount.FindOrAdd(W.Name)++;
		}
		for (auto& V : LoopVoices) if (V.Value) V.Value->Stop();
		LoopVoices.Reset();
		LoopSounds.Reset();
		Loops3D.Reset();
		return;
	}
	UWorld* World = GetWorld();
	const UAcPilotAudio* Pilot = UAcPilotAudio::Get(this);
	TSet<int64> Seen;
	AudibleLoops = 0;
	LoopCount.Reset();
	// The game held (home screen, pause menu, a dialog): the world's loops
	// (drills, welders, guns, wind, hum) fall silent; only the music plays.
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	const bool bHeld = Sim && Sim->IsPaused();
	for (const FAcLoopWant& W : Wants)
	{
		Seen.Add(W.Key);
		const float Volume = bMuted || bHeld ? 0.f : W.Volume * (W.bAmbient ? AcSound::AmbientGain() : AcSound::SfxGain());
		// Where it plays (placed only while heard, as Swift's `place`), and
		// while driving its 3D voice (E7: gain, filter, hall send).
		const bool bFlat = W.bInEar || PlaceFlat();
		const bool bHeard = W.Volume > AcSound::Audible;
		TOptional<FAcVoice3D> Voice;
		FVector At = FVector::ZeroVector;
		if (!W.bInEar && bHeard) At = Place(W.At, W.Lift, W.Pan, Voice);
		const float Level = Volume * (Voice ? Voice->Gain : 1.f);
		if (W.Volume > AcSound::Audible)
		{
			++AudibleLoops;
			LoopCount.FindOrAdd(W.Name)++;
		}
		TObjectPtr<UAudioComponent>* Have = LoopVoices.Find(W.Key);
		const TPair<FName, int32>* Sound = LoopSounds.Find(W.Key);
		const bool bSame = Have && *Have && Sound && Sound->Key == W.Name && Sound->Value == W.Variant;
		if (!bSame)
		{
			if (Have && *Have) (*Have)->Stop();
			LoopVoices.Remove(W.Key);
			LoopSounds.Remove(W.Key);
			// Started only once it is heard (silent Prospectors keep no voice).
			if (!(W.Volume > AcSound::Audible) || !World) continue;
			USoundWave* Wave = Pick(W.Name, W.Variant);
			if (!Wave) continue;
			const float Start = W.StartFraction * Wave->Duration;
			UAudioComponent* C = nullptr;
			if (bFlat)
			{
				C = UGameplayStatics::SpawnSound2D(World, Wave, Level, 1.f, Start, nullptr, false, false);
			}
			else
			{
				C = UGameplayStatics::SpawnSoundAtLocation(World, Wave, At, FRotator::ZeroRotator, Level, 1.f, Start,
					Voice && Pilot ? Pilot->Attenuation() : Panned.Get(), nullptr, false);
			}
			if (!C)
			{
				UE_LOG(LogAutocraft, Warning, TEXT("audio: loop %s did not start"), *W.Name.ToString());
				continue;
			}
			UE_LOG(LogAutocraft, Verbose, TEXT("audio: loop %s started, volume %.3f"), *W.Name.ToString(), Level);
			LoopVoices.Add(W.Key, C);
			LoopSounds.Add(W.Key, {W.Name, W.Variant});
			Loops3D.Remove(W.Key);
			if (Voice && Pilot)
			{
				Pilot->Apply(C, Voice.GetPtrOrNull());
				Pilot->Log(W.Name, *Voice, Level);
				Loops3D.Add(W.Key);
			}
			continue;
		}
		UAudioComponent* C = *Have;
		if (bHeard && !W.bInEar)
		{
			if (!bFlat) C->SetWorldLocation(At);
			// Into or out of the drive: the 3D voice's attenuation, filter
			// and send on, or off again (a silent loop keeps what it had).
			const bool bWas3D = Loops3D.Contains(W.Key);
			if (Pilot && (Voice.IsSet() || bWas3D))
			{
				if (!bFlat && Voice.IsSet() != bWas3D) C->AdjustAttenuation((Voice ? Pilot->Attenuation() : Panned.Get())->Attenuation);
				Pilot->Apply(C, Voice.GetPtrOrNull());
				if (Voice) Pilot->Log(W.Name, *Voice, Level);
			}
			if (Voice) Loops3D.Add(W.Key);
			else Loops3D.Remove(W.Key);
		}
		C->SetVolumeMultiplier(FMath::Max(bHeard ? Level : Volume, 0.f));
	}
	for (auto It = LoopVoices.CreateIterator(); It; ++It)
	{
		if (Seen.Contains(It.Key())) continue;
		// A dead Prospector's loop (a destroyed Citadel's hum) would play on
		// at its last volume wherever the camera went: drop it.
		if (It.Value()) It.Value()->Stop();
		LoopSounds.Remove(It.Key());
		Loops3D.Remove(It.Key());
		It.RemoveCurrent();
	}
}

void UAcAudioDirector::OnFrame(const FAcFrame& Frame)
{
	UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr;
	if (!Sim || !Sim->IsRunning() || !Frame.State) return;
	Brain.LocalPlayer = Frame.LocalPlayer;
	// A fixed step (-UseFixedTimeStep -FPS=60, the offline checks) is 1/60
	// in float (`1 / FixedFPS`, 0.0166666675): summed, the clock runs ahead
	// of Swift's, which adds 1.0 / 60 in double, and the limiter's
	// `clock - t < 1` let a sound through exactly 60 frames after another
	// where Swift held it back. Snap it to the rate meant.
	double Dt = Frame.RealDelta;
	if (FApp::UseFixedTimeStep() && FApp::GetFixedDeltaTime() > 0)
	{
		const double Fps = FMath::RoundToDouble(1.0 / FApp::GetFixedDeltaTime());
		if (Fps > 0 && FMath::IsNearlyEqual(Dt, 1.0 / Fps, 1e-6)) Dt = 1.0 / Fps;
	}
	Brain.Advance(Dt);
	// Headless (-nullrhi) the viewport never draws, so nothing sets the
	// ears and every placed sound is silent: put them at the camera here.
	if (GUsingNullRHI)
	{
		if (FAudioDevice* Device = GetWorld()->GetAudioDeviceRaw())
		{
			if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				FVector Loc, Front, Right;
				PC->GetAudioListenerPosition(Loc, Front, Right);
				const FVector Up = FVector::CrossProduct(Front, Right).GetSafeNormal();
				const FTransform T(FMatrix(Front, Right, Up.IsNearlyZero() ? FVector::UpVector : Up, Loc));
				Device->SetListener(GetWorld(), 0, T, static_cast<float>(Frame.RealDelta));
			}
		}
	}
	const ac::GameState& Shown = Sim->Shown();
	Brain.Consume(Frame.Events, Sim->Simulation());
	{
		const ac::Simulation& Truth = Sim->Simulation();
		const int64 Team = Frame.LocalPlayer;
		Brain.Watch(Truth.state, Team >= 0 && Team < static_cast<int64>(Truth.sights.size()) ? &Truth.sights[Team] : nullptr);
	}
	Brain.Due();
	for (const FAcSoundCue& Cue : Brain.TakeCues()) Play(Cue);
	std::vector<FAcLoopWant> Wants;
	Brain.Loops(Shown, Frame.RealDelta, Wants);
	SyncLoops(Wants);
	for (int32 I = StopAt.Num() - 1; I >= 0; --I)
	{
		if (StopAt[I].Value > Brain.Clock()) continue;
		if (UAudioComponent* C = StopAt[I].Key.Get()) C->Stop();
		StopAt.RemoveAtSwap(I);
	}
	LogSecond(Frame.Time);
	TickRecording(Frame);
	if (QuitAt && Brain.Clock() >= *QuitAt)
	{
		QuitAt.Reset();
		FPlatformMisc::RequestExit(false, TEXT("AcAudioQuitAt"));
	}
}

void UAcAudioDirector::LogSecond(const double GameTime)
{
	// Seconds of game time (`state.time`, 1/60 steps summed in double, the
	// same numbers as Swift's sim), not the director's clock: that sums the
	// engine's float frame time (1/60 as a float is 0.0166666675), runs
	// ahead of the sim and put the frame that ends each second, at k
	// exactly, in second k where Swift's harness had k - 1.
	// Counted from the whole second the run started in (a saved game starts
	// at its own time).
	if (!FirstSecond) FirstSecond = static_cast<int64>(FMath::FloorToDouble(GameTime));
	const int64 Second = static_cast<int64>(FMath::FloorToDouble(GameTime)) - *FirstSecond;
	if (Second == LoggedSecond) return;
	if (bLog || CVarAcAudioLog.GetValueOnGameThread())
	{
		auto Line = [](const TMap<FName, int32>& Counts)
		{
			TArray<FName> Names;
			Counts.GetKeys(Names);
			Names.Sort(FNameLexicalLess());
			TArray<FString> Parts;
			for (const FName& N : Names) Parts.Add(FString::Printf(TEXT("%s %d"), *N.ToString(), Counts[N]));
			return FString::Join(Parts, TEXT(", "));
		};
		UE_LOG(LogAutocraft, Log, TEXT("audio: %llds %s | loops %s"), LoggedSecond, ThisSecond.Num() ? *Line(ThisSecond) : TEXT("-"),
			LoopCount.Num() ? *Line(LoopCount) : TEXT("-"));
	}
	ThisSecond.Reset();
	LoggedSecond = Second;
}

// MARK: - Recording the mix

namespace
{
	bool WriteWav(const FString& Path, const Audio::FAlignedFloatBuffer& Samples, int32 Channels, int32 Rate)
	{
		TArray<uint8> Out;
		const int32 Count = Samples.Num();
		const uint32 DataBytes = Count * 2;
		auto U32 = [&Out](uint32 V) { Out.Append(reinterpret_cast<const uint8*>(&V), 4); };
		auto U16 = [&Out](uint16 V) { Out.Append(reinterpret_cast<const uint8*>(&V), 2); };
		Out.Append(reinterpret_cast<const uint8*>("RIFF"), 4);
		U32(36 + DataBytes);
		Out.Append(reinterpret_cast<const uint8*>("WAVEfmt "), 8);
		U32(16);
		U16(1);
		U16(static_cast<uint16>(Channels));
		U32(Rate);
		U32(Rate * Channels * 2);
		U16(static_cast<uint16>(Channels * 2));
		U16(16);
		Out.Append(reinterpret_cast<const uint8*>("data"), 4);
		U32(DataBytes);
		Out.Reserve(Out.Num() + DataBytes);
		for (int32 I = 0; I < Count; ++I)
		{
			U16(static_cast<uint16>(static_cast<int16>(FMath::Clamp(Samples[I], -1.f, 1.f) * 32767.f)));
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveArrayToFile(Out, *Path);
	}
}

void UAcAudioDirector::TickRecording(const FAcFrame& Frame)
{
	if (!Recording || Recording->bDone) return;
	FAudioDevice* Device = GetWorld() ? GetWorld()->GetAudioDeviceRaw() : nullptr;
	if (!Device) return;
	Audio::FMixerDevice* Mixer = static_cast<Audio::FMixerDevice*>(Device);
	FRecording& R = *Recording;
	Audio::IAudioMixerPlatformInterface* Platform = Mixer->GetAudioMixerPlatform();
	Audio::FMixerPlatformNonRealtime* Offline =
		Platform && Platform->IsNonRealtime() ? static_cast<Audio::FMixerPlatformNonRealtime*>(Platform) : nullptr;
	if (Offline && !R.bNonRealtime)
	{
		// Render the audio here, one frame of it a frame, not on the
		// device's own guess of the tick.
		R.bNonRealtime = true;
		if (CVarAcAudioRenderSelf.GetValueOnGameThread())
		{
			if (IConsoleVariable* Every = IConsoleManager::Get().FindConsoleVariable(TEXT("au.nrt.RenderEveryTick"))) Every->Set(0);
		}
		UE_LOG(LogAutocraft, Log, TEXT("audio: recording offline (non-realtime mixer)"));
	}
	else if (!Offline && !R.bStarted && Brain.Clock() == Frame.RealDelta)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("audio: recording in real time (add -deterministicaudio for an offline render)"));
	}
	TSharedPtr<Audio::FMixerSubmix, ESPMode::ThreadSafe> Master = Mixer->GetMasterSubmix().Pin();
	if (!Master) return;
	// Offline the game runs far faster than the disk: give the stream
	// cache a few real seconds to bring the bank in before recording.
	if (!R.bStarted && Brain.Clock() >= R.At && FPlatformTime::Seconds() - BankLoadedAt < 3.0)
	{
		if (Offline && CVarAcAudioRenderSelf.GetValueOnGameThread()) Offline->RenderAudio(FApp::GetDeltaTime());
		FPlatformProcess::Sleep(0.01f);
		return;
	}
	if (!R.bStarted && Brain.Clock() >= R.At)
	{
		R.bStarted = true;
		Master->OnStartRecordingOutput(static_cast<float>(R.Seconds));
		UE_LOG(LogAutocraft, Log, TEXT("audio: recording %.0f s from %.2f s (world %.4f s)"), R.Seconds, Brain.Clock(), GetWorld()->GetTimeSeconds());
	}
	if (Offline && CVarAcAudioRenderSelf.GetValueOnGameThread()) Offline->RenderAudio(FApp::GetDeltaTime());
	if (R.bStarted) R.Rendered += Offline ? FApp::GetDeltaTime() : Frame.RealDelta;
	if (R.bStarted && R.Rendered >= R.Seconds)
	{
		R.bDone = true;
		float Channels = 0.f, Rate = 0.f;
		Audio::FAlignedFloatBuffer& Samples = Master->OnStopRecordingOutput(Channels, Rate);
		float Peak = 0.f;
		double Sum = 0.0;
		for (const float S : Samples)
		{
			Peak = FMath::Max(Peak, FMath::Abs(S));
			Sum += double(S) * S;
		}
		const double Rms = Samples.Num() ? FMath::Sqrt(Sum / Samples.Num()) : 0.0;
		const bool bOk = WriteWav(R.Path, Samples, FMath::RoundToInt(Channels), FMath::RoundToInt(Rate));
		UE_LOG(LogAutocraft, Log, TEXT("audio: recorded %.2f s (%d ch, %.0f Hz, peak %.3f, rms %.1f dBFS) to %s%s"),
			Samples.Num() / FMath::Max(Channels * Rate, 1.f), FMath::RoundToInt(Channels), Rate, Peak,
			20.0 * FMath::LogX(10.0, FMath::Max(Rms, 1e-9)), *R.Path, bOk ? TEXT("") : TEXT(" (FAILED to write)"));
		if (!UAcShotSubsystem::IsShotRun()) FPlatformMisc::RequestExit(false, TEXT("AcAudioRecord"));
	}
}
