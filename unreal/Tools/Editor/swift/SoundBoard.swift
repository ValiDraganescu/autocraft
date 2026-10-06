import AVFoundation
import GameCore

/// The model viewer's sounds: a model's sounds as the game plays them (the
/// same files, found the same way, normalised the same way, at the game's
/// gains), one variant after another, and loops that run until stopped.
final class SoundBoard {
    struct Sound {
        /// Unique on the board (the weld is both a loop and a strike).
        let id: String
        /// The game's sound name: `sfx/<name>*.<ext>`.
        let name: String
        let title: String
        /// The game's gain for it.
        let gain: Float
        var loop = false
        /// Heard only while the player drives the unit.
        var driven = false
    }

    /// Every sound the Prospector makes, in the game's gains (`AudioDirector`).
    static let prospector: [Sound] = [
        Sound(id: "vprospector", name: "vprospector", title: "Report in", gain: 0.6),
        Sound(id: "drill", name: "drill", title: "Drill", gain: AudioDirector.drillGain, loop: true),
        Sound(id: "deposit", name: "deposit", title: "Deposit", gain: 0.45),
        Sound(id: "weld", name: "weld", title: "Weld", gain: 0.4, loop: true),
        Sound(id: "strike", name: "weld", title: "Cutter strike", gain: 0.3),
        Sound(id: "prospectordeath", name: "prospectordeath", title: "Death cry", gain: 0.55),
        Sound(id: "blast", name: "blast", title: "Blast", gain: 0.5),
    ] + driven + leveling

    /// Every sound the Ranger makes (its rifle is also the Bastion's).
    static let ranger: [Sound] = [
        Sound(id: "report", name: "vranger", title: "Report in", gain: 0.6),
        Sound(id: "rifle", name: "gun", title: "Rifle", gain: 0.32),
        Sound(id: "minigun", name: "minigun", title: "Mini gun", gain: AudioDirector.minigunGain, loop: true),
        Sound(id: "rangerdeath", name: "rangerdeath", title: "Death cry", gain: 0.55),
    ] + driven + leveling

    /// Every sound the Comet makes (the jet pack fires as it takes off
    /// over a cliff and again as it lands).
    static let comet: [Sound] = [
        Sound(id: "report", name: "vcomet", title: "Report in", gain: 0.6),
        Sound(id: "pistols", name: "pistol", title: "Pistols", gain: 0.3),
        Sound(id: "jetup", name: "jetup", title: "Jet pack", gain: 0.4),
        Sound(id: "jetland", name: "jetland", title: "Landing", gain: 0.4),
        Sound(id: "cometdeath", name: "cometdeath", title: "Death cry", gain: 0.55),
        Sound(id: "pulsemine", name: "blast", title: "Pulse mine", gain: 0.45, driven: true),
    ] + driven + leveling

    /// Every sound the Juggernaut makes (one volley from both launchers per
    /// shot, heard where it stands).
    static let juggernaut: [Sound] = [
        Sound(id: "report", name: "vjuggernaut", title: "Report in", gain: 0.6),
        Sound(id: "grenades", name: "breacher", title: "Grenades", gain: 0.4),
        Sound(id: "juggernautdeath", name: "juggernautdeath", title: "Death cry", gain: 0.55),
    ] + driven + leveling

    /// Every sound the Firefly makes (one burst of flame per attack, heard
    /// where it stands; its driver cries out as it blows up). Driven, its
    /// engine sounds as it pulls away; it has no footfalls or breath.
    static let firefly: [Sound] = [
        Sound(id: "report", name: "vfirefly", title: "Report in", gain: 0.6),
        Sound(id: "flame", name: "flame", title: "Flamethrower", gain: 0.4),
        Sound(id: "fireflydeath", name: "fireflydeath", title: "Death cry", gain: 0.55),
        Sound(id: "blast", name: "blast", title: "Blast", gain: 0.5),
        Sound(id: "fireflymove", name: "fireflymove", title: "Engine", gain: AudioDirector.fireflyEngineGain, driven: true),
    ] + aiming + leveling

    /// Every sound the Dropship makes (its pilot cries out as it is shot
    /// down, and it blows up on the ground). Driven, its engines sound as
    /// it pulls away, the heal beam loops in your ear while it heals, and
    /// the blip marks a friend coming into its reach under the sight; it
    /// has no gun, footfalls or breath.
    static let dropship: [Sound] = [
        Sound(id: "report", name: "vdropship", title: "Report in", gain: 0.6),
        Sound(id: "dropshipdeath", name: "dropshipdeath", title: "Death cry", gain: 0.55),
        Sound(id: "blast", name: "blast", title: "Blast", gain: 0.5),
        Sound(id: "dropshipmove", name: "dropshipmove", title: "Engine", gain: AudioDirector.dropshipEngineGain, driven: true),
        Sound(id: "heal", name: "heal", title: "Heal beam", gain: AudioDirector.healGain, loop: true, driven: true),
        lockOn,
    ] + leveling

    /// Every sound the Longbow makes: its packed cannon in tank mode;
    /// anchored, the shock cannon's report where it stands and the shell's
    /// burst where it lands (`Rules.flight` later); the mechanism as it
    /// sets up and packs up; its commander's cry as it blows up. Driven,
    /// it has no engine sound, footfalls or breath.
    static let tank: [Sound] = [
        Sound(id: "report", name: "vlongbow", title: "Report in", gain: 0.6),
        Sound(id: "cannon", name: "cannon", title: "Cannons", gain: AudioDirector.tankCannonGain),
        Sound(id: "anchorset", name: "anchorset", title: "Anchor up", gain: AudioDirector.anchorGain),
        Sound(id: "anchorshot", name: "anchorshot", title: "Anchor shot", gain: 0.55),
        Sound(id: "anchorhit", name: "anchorhit", title: "Shell burst", gain: AudioDirector.anchorHitGain),
        Sound(id: "anchorlift", name: "anchorlift", title: "Pack up", gain: AudioDirector.anchorGain),
        Sound(id: "longbowdeath", name: "longbowdeath", title: "Death cry", gain: 0.55),
        Sound(id: "blast", name: "blast", title: "Blast", gain: 0.5),
    ] + aiming + leveling

    /// Every sound the Kestrel makes: a volley from its rocket pods, its
    /// pilot's cry as it is shot down. Driven, its engines as it pulls away.
    static let kestrel: [Sound] = [
        Sound(id: "report", name: "vkestrel", title: "Report in", gain: 0.6),
        Sound(id: "rockets", name: "rockets", title: "Rockets", gain: AudioDirector.rocketGain),
        Sound(id: "kestreldeath", name: "kestreldeath", title: "Death cry", gain: 0.55),
        Sound(id: "blast", name: "blast", title: "Blast", gain: 0.5),
        Sound(id: "kestrelmove", name: "kestrelmove", title: "Engine", gain: AudioDirector.kestrelEngineGain, driven: true),
    ] + aiming + leveling

    /// Every sound the Hailstorm makes: its flak where it stands and the
    /// shell's air burst at the flyer (`Rules.flight` later), its gunner's
    /// cry as it blows up. Driven, its engine as it pulls away.
    static let hailstorm: [Sound] = [
        Sound(id: "report", name: "vhailstorm", title: "Report in", gain: 0.6),
        Sound(id: "flak", name: "flak", title: "Flak", gain: AudioDirector.flakGain),
        Sound(id: "flakhit", name: "flakhit", title: "Air burst", gain: AudioDirector.flakHitGain),
        Sound(id: "hailstormdeath", name: "hailstormdeath", title: "Death cry", gain: 0.55),
        Sound(id: "blast", name: "blast", title: "Blast", gain: 0.5),
        Sound(id: "hailstormmove", name: "hailstormmove", title: "Engine", gain: AudioDirector.hailstormEngineGain, driven: true),
    ] + aiming + leveling

    /// The Sentinel's: the lock-on servo as it opens fire after a quiet
    /// spell, its missiles, and its explosion.
    static let sentinel: [Sound] = [
        Sound(id: "sentineltrack", name: "sentineltrack", title: "Lock on", gain: AudioDirector.sentinelTrackGain),
        Sound(id: "missile", name: "missile", title: "Missiles", gain: AudioDirector.missileGain),
    ] + building

    /// What a unit on foot sounds like while the player drives it.
    static let driven: [Sound] = [
        Sound(id: "step", name: "step", title: "Footfall", gain: 0.22, driven: true),
        Sound(id: "breath", name: "breath", title: "Breath", gain: AudioDirector.breathGain, driven: true),
    ] + aiming

    /// What any driven gun hears: a target coming into reach, its rounds
    /// striking, killing, or spent in the dirt.
    static let aiming: [Sound] = [
        lockOn,
        Sound(id: "hitmark", name: "hitmark", title: "Hit", gain: AudioDirector.hitMarkGain, driven: true),
        Sound(id: "hitclank", name: "hitclank", title: "Hit building", gain: AudioDirector.hitClankGain, driven: true),
        Sound(id: "killmark", name: "killmark", title: "Kill", gain: AudioDirector.killMarkGain, driven: true),
        Sound(id: "dirthit", name: "dirthit", title: "Miss", gain: 0.3, driven: true),
    ]

    /// A target (a patient, for a Dropship) coming into reach under the sight.
    static let lockOn = Sound(id: "lockon", name: "lockon", title: "Lock-on", gain: AudioDirector.lockOnGain, driven: true)

    /// Any driven kind's leveling (docs/leveling.md): the level-up fanfare
    /// and the latch of a pick kept.
    static let leveling: [Sound] = [
        Sound(id: "levelup", name: "levelup", title: "Level up", gain: AudioDirector.levelUpGain, driven: true),
        Sound(id: "pick", name: "pick", title: "Pick", gain: AudioDirector.pickGain, driven: true),
    ]

    /// The Citadel's: its hum (looping while it stands), the
    /// Oracle's warning when blue's base is hit, the wind bed the whole
    /// map hears (one of the game's two layers), and its explosion.
    static let citadel: [Sound] = [
        Sound(id: "hum", name: "hum", title: "Hum", gain: AudioDirector.humGain, loop: true),
        Sound(id: "alert", name: "alert", title: "Base under attack", gain: 0.7),
        Sound(id: "wind", name: "wind", title: "Wind", gain: AudioDirector.windGain, loop: true),
    ] + building

    /// Any other building's: it goes up in flames (a Hab Dome's blast is smaller).
    static let building = [Sound(id: "boom", name: "boom", title: "Explosion", gain: AudioDirector.boomGain)]
    static let habDome = [Sound(id: "boom", name: "boom", title: "Explosion", gain: AudioDirector.habDomeBoomGain)]

    /// The models with a sound bar, by the viewer's model name.
    static let byModel: [String: [Sound]] = ["prospector": prospector, "ranger": ranger, "comet": comet, "juggernaut": juggernaut,
                                             "firefly": firefly, "dropship": dropship, "longbow": tank,
                                             "kestrel": kestrel, "hailstorm": hailstorm, "sentinel": sentinel,
                                             "citadel": citadel, "habdome": habDome, "garrison": building, "bastion": building,
                                             "derrick": building, "foundry": building, "spacedock": building,
                                             "lab": building, "garrisonlab": building]

    private let engine = AVAudioEngine()
    private var shots: [AVAudioPlayerNode] = []
    private var nextShot = 0
    private var loops: [String: AVAudioPlayerNode] = [:]
    private let shelves = AudioDirector.soundShelves()
    private var cache: [String: [(url: URL, buffer: AVAudioPCMBuffer)]] = [:]
    private var turn: [String: Int] = [:]
    /// What just started: "ready_2.wav, own, 2 of 4".
    var played: ((String) -> Void)?

    init() {
        for _ in 0..<6 {
            let p = AVAudioPlayerNode()
            engine.attach(p)
            engine.connect(p, to: engine.mainMixerNode, format: Synth.mono)
            shots.append(p)
        }
        do { try engine.start() } catch { Log.write("viewer audio: \(error)") }
    }

    /// The files of `s` and whether they are Autocraft's own.
    func files(_ s: Sound) -> (files: [URL], own: Bool) { AudioDirector.soundFiles(s.name, in: shelves) }

    private func variants(_ s: Sound) -> [(url: URL, buffer: AVAudioPCMBuffer)] {
        if let v = cache[s.name] { return v }
        let v = files(s).files.compactMap { url in
            AudioDirector.loadMono(url).map { b -> (url: URL, buffer: AVAudioPCMBuffer) in Synth.normalize(b); return (url, b) }
        }
        cache[s.name] = v
        return v
    }

    /// The next variant of `s`, in turn.
    private func next(_ s: Sound) -> (url: URL, buffer: AVAudioPCMBuffer)? {
        let v = variants(s)
        guard !v.isEmpty else { return nil }
        let k = turn[s.id, default: 0] % v.count
        turn[s.id] = k + 1
        let own = files(s).own
        played?("\(s.title): \(v[k].url.lastPathComponent) · \(own ? "Autocraft's own" : "Audio folder") · \(k + 1) of \(v.count)")
        return v[k]
    }

    /// Play `s` once (the next variant).
    func play(_ s: Sound) {
        guard engine.isRunning, let v = next(s) else { return }
        let p = shots[nextShot % shots.count]
        nextShot += 1
        p.stop()
        p.volume = s.gain
        p.scheduleBuffer(v.buffer, at: nil)
        p.play()
    }

    /// Start or stop loop `s` (the next variant each time it starts).
    func loop(_ s: Sound, on: Bool) {
        if !on {
            loops.removeValue(forKey: s.id).map { $0.stop(); engine.detach($0) }
            return
        }
        guard engine.isRunning, loops[s.id] == nil, let v = next(s) else { return }
        let p = AVAudioPlayerNode()
        engine.attach(p)
        engine.connect(p, to: engine.mainMixerNode, format: Synth.mono)
        p.volume = s.gain
        p.scheduleBuffer(v.buffer, at: nil, options: .loops)
        p.play()
        loops[s.id] = p
    }

    func looping(_ id: String) -> Bool { loops[id] != nil }

    func stopAll() {
        for (_, p) in loops { p.stop(); engine.detach(p) }
        loops = [:]
        shots.forEach { $0.stop() }
    }
}
