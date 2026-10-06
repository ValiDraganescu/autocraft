import AVFoundation
import CoreAudio
import GameCore

/// Sound for the wallpaper: wind, a drill loop on every Prospector while it mines
/// (a welder while it builds), a clunk at each deposit, a report-in line for
/// each new Prospector, the Citadel hum, gunfire, death cries and
/// explosions in a fight, the Oracle when blue's base is under attack,
/// and a music playlist.
///
/// Every sound effect is Autocraft's own (made with ElevenLabs, shipped in
/// the app's `Sounds/sfx`); a sound with no file of its own would come from
/// the audio folder (see `Audio/README` in the app support folder), else a
/// `Synth` placeholder. The music is `MusicPlayer`'s: Autocraft's own
/// tracks (shipped in the app's `Sounds/music`) and any in the audio
/// folder's `music/`, shuffled, else a `Synth` pad. Each source is panned by where it
/// sits across the whole canvas, so the left screen sounds left. While the
/// player drives a unit, world sounds are placed in 3D round the camera
/// instead (HRTF, through an `AVAudioEnvironmentNode`): ahead, behind,
/// above, muffled behind a cliff or a building, and wetter with distance.
/// Sounds "in your ear" (hit marks, footfalls, the Oracle) stay centred.
///
/// The director outlives game sessions: `bind(_:)` swaps the scene sounds
/// and keeps wind and music going.
final class AudioDirector {
    struct Settings {
        var volume: Float = 0.6
        var music: Float = 0.5
        /// Fade to silence while another app plays sound.
        var duck = true
    }

    static var directory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Autocraft/Audio", isDirectory: true)
    }

    /// The Prospector's drill loop. The drills are dense, so they play low.
    static let drillGain: Float = 0.25
    /// A driven Firefly's engine as it pulls away.
    static let fireflyEngineGain: Float = 0.22
    /// A driven Dropship's engines as it pulls away.
    static let dropshipEngineGain: Float = 0.24
    /// A driven Kestrel's and Hailstorm's engines (their loudest 0.4 s
    /// about -7 and -9 dB at full peak, beside the Dropship's -7.5 dB).
    static let kestrelEngineGain: Float = 0.22
    static let hailstormEngineGain: Float = 0.26
    /// The new kinds' weapons, set by their loudest 0.4 s to land near the
    /// rifle's and the tank cannon's level (about -18 dB): the Kestrel's
    /// rockets (-10.6 dB at full peak), the Hailstorm's flak (-18 dB) and
    /// its air bursts (-19 dB), the Sentinel's missiles (-9 dB) and its
    /// lock-on servo (-14 dB), which sits under the launch.
    static let rocketGain: Float = 0.45
    static let flakGain: Float = 0.85
    static let flakHitGain: Float = 0.7
    static let missileGain: Float = 0.35
    static let sentinelTrackGain: Float = 0.3
    /// The driven Dropship's heal beam loop, in your ear.
    static let healGain: Float = 0.35
    /// The Longbow's cannon shots, its shell's burst, and its anchor
    /// mechanism are quiet takes (in their loudest 0.4 s), so they play loud.
    static let tankCannonGain: Float = 0.7
    /// The mini gun's roar (a loop for the whole burst; the user's takes,
    /// ~-9 dB RMS at full peak, so about a rifle burst's level at 0.3).
    static let minigunGain: Float = 0.3
    /// Mini gun roars heard at once: the nearest of those firing.
    static let minigunsAtOnce = 6
    static let anchorHitGain: Float = 0.8
    static let anchorGain: Float = 0.75
    /// The Citadel's hum loop: a dense take, so it plays low.
    static let humGain: Float = 0.11
    /// Each of the wind bed's two layers. The bed (-23 dBFS RMS) never
    /// stops, so it sits under the game: both layers together at about
    /// -36 dB, below a drill heard up close (-31 dB). It was 0.55 (-25 dB),
    /// louder than the drills.
    static let windGain: Float = 0.16
    /// A building's explosion (a Hab Dome's is smaller): quiet takes (in their
    /// loudest 0.4 s), so the first plays at full volume.
    static let boomGain: Float = 1
    static let habDomeBoomGain: Float = 0.7
    /// The driven unit's sounds in your ear, set by their loudest 0.4 s: the
    /// hit, the clank on a building, the lock-on and the breath are quiet
    /// takes and play louder; the kill ping is a loud one and plays lower.
    static let hitMarkGain: Float = 0.5
    static let hitClankGain: Float = 0.55
    static let lockOnGain: Float = 0.28
    static let breathGain: Float = 0.5
    static let killMarkGain: Float = 0.34
    /// The driven kind's level-up fanfare and a pick kept, in your ear:
    /// over the hit marks, under the Oracle.
    static let levelUpGain: Float = 0.5
    static let pickGain: Float = 0.35
    static let musicExtensions: Set<String> = ["ogg", "mp3", "m4a", "aac", "wav", "aif", "aiff", "caf", "flac"]

    let settings: Settings
    /// The running engine; nil while it (re)starts on `queue`. Starting an
    /// engine on a Bluetooth output blocks for seconds (3 s measured on
    /// AirPods Max), so that never happens on the main thread.
    private var engine: AVAudioEngine?
    private let queue = DispatchQueue(label: "autocraft.audio")
    private var pendingRestart: DispatchWorkItem?
    private weak var game: GameController?

    private var bank: [String: [AVAudioPCMBuffer]] = [:]
    private var sources: [String: String] = [:]
    private var ambience: [AVAudioPlayerNode] = []
    /// Where world sounds are placed: in 3D round the ears while driving,
    /// left to right across the view otherwise (`place`).
    private var space: AVAudioEnvironmentNode?
    /// The ears this frame (nil: the top-down view).
    private var ears: Ears?
    /// Players for sounds in your ear (hit marks, footfalls, the Oracle),
    /// centred, straight into the mix.
    private var inEar: [AVAudioPlayerNode] = []
    private var nextEar = 0
    /// Each Prospector's tool loop: the drill while mining, the welder while building.
    private var tools: [Int: Tool] = [:]
    private struct Tool { var node: AVAudioPlayerNode; var gain: Float; var sound: String }
    /// Hum per built Citadel, by structure id.
    private var hums: [Int: (node: AVAudioPlayerNode, at: Vec2)] = [:]
    /// One-shot players (deposits, voice lines), used round-robin.
    private var deposits: [AVAudioPlayerNode] = []
    private var nextDeposit = 0
    private var lastVoice = -10.0
    /// Players for the fight (shots, deaths, blasts), so a firefight never
    /// cuts off a deposit or a voice line.
    private var combat: [AVAudioPlayerNode] = []
    private var nextCombat = 0
    /// Recent one-shots per sound, to cap how many play per second.
    private var recent: [String: [Double]] = [:]
    private var lastAlert = -100.0
    /// The driven Dropship's heal beam loop (made on first use), whether it
    /// heals now, and when a driven vehicle's engine last sounded.
    private var healLoop: Tool?
    private var healing = false
    /// Each mini gun Ranger's roar while it fires, by unit id.
    private var miniguns: [Int: Tool] = [:]
    private var lastEngine = -10.0
    /// When each tower (by where it stands) last fired: a Sentinel that
    /// opens fire after a quiet spell swings its head and locks on.
    private var towerShots: [Vec2: Double] = [:]

    /// The music's player node, and the player that drives it: the
    /// playlist, the dashboard's controls, the media keys, the tracking.
    private var musicNode = AVAudioPlayerNode()
    let music: MusicPlayer
    /// Rendered once (0.7 s); only when there is no music on disk.
    private lazy var pad = Synth.pad()

    /// Silenced from the menu bar; everything keeps running.
    var muted = false
    private var duck: Float = 1
    private var othersPlaying = false
    private var lastPoll = 0.0
    private var clock = 0.0
    private var configObserver: NSObjectProtocol?

    init(settings: Settings) {
        self.settings = settings
        music = MusicPlayer(volume: settings.music)
        try? FileManager.default.createDirectory(at: Self.directory.appendingPathComponent("music"),
                                                 withIntermediateDirectories: true)
        try? FileManager.default.createDirectory(at: Self.directory.appendingPathComponent("sfx"),
                                                 withIntermediateDirectories: true)
        writeReadme()
        loadBank()
        start()
    }

    deinit {
        if let configObserver { NotificationCenter.default.removeObserver(configObserver) }
    }

    // MARK: - Lifecycle

    /// Build and start an engine on `queue`, then hand it to the main thread.
    private func start() {
        let wind = bank["wind"] ?? []
        let started = CACurrentMediaTime()
        queue.async { [weak self] in
            let e = AVAudioEngine()
            e.mainMixerNode.outputVolume = 0
            // The 3D stage: stereo out (HRTF for headphones or speakers),
            // no distance fall-off of its own (`Listener.gain` does that), a
            // light hall for the distance cue.
            let space = AVAudioEnvironmentNode()
            e.attach(space)
            let rate = e.outputNode.outputFormat(forBus: 0).sampleRate
            e.connect(space, to: e.mainMixerNode, format: AVAudioFormat(standardFormatWithSampleRate: rate > 0 ? rate : Synth.rate, channels: 2))
            space.outputType = .auto
            space.distanceAttenuationParameters.distanceAttenuationModel = .inverse
            space.distanceAttenuationParameters.referenceDistance = 1
            space.distanceAttenuationParameters.rolloffFactor = 0
            space.reverbParameters.enable = true
            space.reverbParameters.loadFactoryReverbPreset(.mediumHall)
            space.reverbParameters.level = -4
            func attach(_ format: AVAudioFormat, spatial: Bool = false) -> AVAudioPlayerNode {
                let p = AVAudioPlayerNode()
                e.attach(p)
                e.connect(p, to: spatial ? space : e.mainMixerNode, format: format)
                return p
            }
            // Two wind layers, offset and spread for width.
            let ambience = zip(wind, [-0.6, 0.6] as [Float]).map { buf, pan in
                let p = attach(buf.format)
                p.pan = pan
                p.volume = Self.windGain
                p.scheduleBuffer(buf, at: nil, options: .loops)
                return p
            }
            let deposits = (0..<6).map { _ in attach(Synth.mono, spatial: true) }
            let combat = (0..<12).map { _ in attach(Synth.mono, spatial: true) }
            let inEar = (0..<4).map { _ in attach(Synth.mono) }
            let music = AVAudioPlayerNode()
            e.attach(music)
            do { try e.start() } catch {
                Log.write("audio: engine failed to start: \(error)")
                return
            }
            ambience.forEach { $0.play() }
            let ms = (CACurrentMediaTime() - started) * 1000
            DispatchQueue.main.async {
                self?.install(e, space: space, ambience: ambience, deposits: deposits, combat: combat, inEar: inEar,
                              music: music, startMS: ms)
            }
        }
    }

    private func install(_ e: AVAudioEngine, space: AVAudioEnvironmentNode, ambience: [AVAudioPlayerNode],
                         deposits: [AVAudioPlayerNode], combat: [AVAudioPlayerNode], inEar: [AVAudioPlayerNode],
                         music: AVAudioPlayerNode, startMS: Double) {
        engine = e
        self.space = space
        self.inEar = inEar
        self.ambience = ambience
        self.deposits = deposits
        self.combat = combat
        musicNode = music
        duck = 0
        startMusic()
        if let game { Log.during("audio bind") { bind(game) } }
        if let configObserver { NotificationCenter.default.removeObserver(configObserver) }
        // The output device changed (headphones, display audio): the engine
        // stops itself; start over on the new device.
        configObserver = NotificationCenter.default.addObserver(
            forName: .AVAudioEngineConfigurationChange, object: e, queue: .main) { [weak self] _ in
            self?.restart()
        }
        let output = e.outputNode.outputFormat(forBus: 0)
        Log.write(String(format: "audio: started in %.0f ms on %.0f Hz %d ch | %@ | music: %@", startMS,
                         output.sampleRate, output.channelCount,
                         sources.sorted { $0.key < $1.key }.map { "\($0.key) \($0.value)" }.joined(separator: ", "),
                         self.music.isEmpty ? "placeholder pad"
                             : "\(self.music.counts.own) own + \(self.music.counts.folder) folder tracks"))
    }

    /// macOS sends a burst of change notifications while a device switches
    /// (three in 3 s moving to AirPods); restart once it settles.
    private func restart() {
        pendingRestart?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            Log.write("audio: output changed, restarting")
            self.music.detach()
            let old = self.engine
            self.engine = nil
            self.space = nil
            self.inEar = []
            self.nextEar = 0
            self.tools = [:]
            self.hums = [:]
            self.healLoop = nil
            self.queue.async { old?.stop() }
            self.start()
        }
        pendingRestart = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.6, execute: work)
    }

    func suspend() {
        let e = engine
        queue.async { e?.pause() }
    }

    func resume() {
        let e = engine
        queue.async { try? e?.start() }
    }

    func stop() {
        music.detach()
        engine?.stop()
    }

    /// A new player: into the 3D stage (`spatial`), or straight into the mix.
    private func attach(_ format: AVAudioFormat, spatial: Bool = false) -> AVAudioPlayerNode? {
        guard let engine else { return nil }
        let p = AVAudioPlayerNode()
        engine.attach(p)
        if spatial, let space { engine.connect(p, to: space, format: format) }
        else { engine.connect(p, to: engine.mainMixerNode, format: format) }
        return p
    }

    // MARK: - Placing sounds

    /// The ears for this frame: the stage's listener at the camera while
    /// driving, else at the origin facing −Z (for `place`'s left-right
    /// stand-in).
    private func hearFrom(_ e: Ears?) {
        ears = e
        guard let space else { return }
        if let e {
            space.listenerPosition = AVAudio3DPoint(x: e.at.x, y: e.at.y, z: e.at.z)
            space.listenerVectorOrientation = AVAudio3DVectorOrientation(
                forward: AVAudio3DVector(x: e.forward.x, y: e.forward.y, z: e.forward.z),
                up: AVAudio3DVector(x: e.up.x, y: e.up.y, z: e.up.z))
        } else {
            space.listenerPosition = AVAudio3DPoint(x: 0, y: 0, z: 0)
            space.listenerVectorOrientation = AVAudio3DVectorOrientation(
                forward: AVAudio3DVector(x: 0, y: 0, z: -1), up: AVAudio3DVector(x: 0, y: 1, z: 0))
        }
    }

    /// Put a world sound at ground point `ground` (`lift` over it). While
    /// driving: at its place round the ears, HRTF, muffled when a cliff or
    /// a building is in the way, wetter the farther off. Otherwise: on a
    /// unit circle round the origin at the angle of its old stereo pan, so
    /// the top-down view sounds as it did.
    private func place(_ p: AVAudioPlayerNode, _ ground: Vec2, lift: Double = 0.7) {
        guard let game else { return }
        if let e = ears {
            var at = game.soundPoint(ground, lift: lift)
            // The driven unit's own sounds: just ahead, not inside the head.
            if simd_distance(at, e.at) < 0.4 { at = e.at + e.forward * 0.4 }
            if p.renderingAlgorithm != .HRTFHQ { p.renderingAlgorithm = .HRTFHQ }
            p.position = AVAudio3DPoint(x: at.x, y: at.y, z: at.z)
            p.obstruction = game.soundBlocked(from: e.at, to: at) ? -14 : 0
            let range = Float(game.listener?.range ?? 16)
            p.reverbBlend = min(0.45, 0.06 + 0.4 * simd_distance(at, e.at) / max(range, 1))
        } else {
            if p.renderingAlgorithm != .equalPowerPanning { p.renderingAlgorithm = .equalPowerPanning }
            let a = pan(ground) * .pi / 2
            p.position = AVAudio3DPoint(x: sin(a), y: 0, z: -cos(a))
            p.obstruction = 0
            p.reverbBlend = 0
        }
    }

    // MARK: - Session

    /// A new session's scene: drop the old session's loops; `update` makes
    /// loops for Prospectors and Citadels as they appear.
    func bind(_ game: GameController) {
        self.game = game
        music.game = game
        for (_, t) in tools { t.node.stop(); engine?.detach(t.node) }
        for (_, t) in miniguns { t.node.stop(); engine?.detach(t.node) }
        miniguns = [:]
        for (_, h) in hums { h.node.stop(); engine?.detach(h.node) }
        if let h = healLoop { h.node.stop(); engine?.detach(h.node) }
        tools = [:]
        hums = [:]
        healLoop = nil
        healing = false
    }

    /// Follow the state: tools fade with work, pans follow the Prospectors, new
    /// units and buildings get their loops, and everything ducks under other
    /// apps' sound.
    func update(state: GameState, dt: Double) {
        guard let engine, engine.isRunning else { return }
        clock += dt
        hearFrom(game?.ears())
        for u in state.units where u.kind == .prospector {
            let want = u.task == .building || u.task == .repairing ? "weld" : "drill"
            guard var t = tools[u.id] ?? makeTool(u, want) else { continue }
            // Swap the loop once the old one has faded out.
            if t.sound != want, t.gain < 0.01, let buf = bank[want]?.randomElement() {
                t.node.stop()
                t.node.scheduleBuffer(buf, at: nil, options: .loops)
                t.node.play()
                t.sound = want
            }
            let target: Float = u.working && t.sound == want ? 1 : 0
            // ~0.15 s attack, ~0.4 s release.
            let rate = Float(dt) / (target > t.gain ? 0.15 : 0.4)
            t.gain += max(-rate, min(rate, target - t.gain))
            t.node.volume = t.gain * (t.sound == "weld" ? 0.4 : Self.drillGain) * reach(u.position)
            if t.node.volume > 0.001 { place(t.node, u.position) }
            tools[u.id] = t
        }
        // A dead Prospector's loop (or a destroyed Citadel's hum) would play
        // on at its last volume wherever the camera went: drop them.
        let prospectors = Set(state.units.lazy.filter { $0.kind == .prospector }.map(\.id))
        for (id, t) in tools where !prospectors.contains(id) {
            t.node.stop()
            engine.detach(t.node)
            tools[id] = nil
        }
        updateMiniguns(state, dt)
        let centers = Set(state.structures.lazy.filter { $0.kind == .citadel }.map(\.id))
        for (id, h) in hums where !centers.contains(id) {
            h.node.stop()
            engine.detach(h.node)
            hums[id] = nil
        }
        if let buf = bank["hum"]?.first {
            for s in state.structures where s.kind == .citadel && s.complete && hums[s.id] == nil {
                guard let p = attach(buf.format, spatial: true) else { break }
                p.volume = 0
                p.scheduleBuffer(buf, at: nil, options: .loops)
                p.play()
                hums[s.id] = (p, s.position)
            }
        }
        for (_, h) in hums {
            h.node.volume = Self.humGain * reach(h.at)
            if h.node.volume > 0.001 { place(h.node, h.at, lift: 1.5) }
        }
        if healing, healLoop == nil, let buf = bank["heal"]?.first, let p = attach(buf.format) {
            p.volume = 0
            p.scheduleBuffer(buf, at: nil, options: .loops)
            p.play()
            healLoop = Tool(node: p, gain: 0, sound: "heal")
        }
        if var h = healLoop {
            // In your ear, fading in and out like the Prospector's tools.
            let target: Float = healing ? 1 : 0
            let rate = Float(dt) / (target > h.gain ? 0.15 : 0.4)
            h.gain += max(-rate, min(rate, target - h.gain))
            h.node.volume = h.gain * Self.healGain
            healLoop = h
        }

        if settings.duck, clock - lastPoll > 1.5 {
            lastPoll = clock
            othersPlaying = Self.otherProcessPlaying()
        }
        let target: Float = settings.duck && othersPlaying ? 0 : 1
        let rate = Float(dt) / (target > duck ? 2.5 : 0.6)
        duck += max(-rate, min(rate, target - duck))
        engine.mainMixerNode.outputVolume = muted ? 0 : settings.volume * duck
        music.update(dt: dt, audible: !muted && duck > 0.5 && settings.music > 0)
    }

    /// The Mini gun: one seamless roar on each Ranger from its burst's
    /// first round to its last (it is firing while its last round is under
    /// a beat old), and nothing in its pauses. The nearest
    /// `minigunsAtOnce` of those firing are heard.
    private func updateMiniguns(_ state: GameState, _ dt: Double) {
        let beat = UnitStats.minigun.cooldown + 0.06
        let firing = state.units.filter { u in
            u.kind == .ranger && state.time - (u.firedAt ?? -10) < beat
                && state.players.indices.contains(u.owner) && state.players[u.owner].upgrades?.contains(.minigun) == true
        }
        let near = firing.map { ($0, reach($0.position)) }.filter { $0.1 > 0.001 }.sorted { $0.1 > $1.1 }
        let on = Set(near.prefix(Self.minigunsAtOnce).map(\.0.id))
        for id in on where miniguns[id] == nil {
            guard let u = state.unit(id), let t = makeTool(u, "minigun") else { break }
            miniguns[id] = t
        }
        for (id, var t) in miniguns {
            // In at once with the first round, out in 0.08 s after the last.
            let target: Float = on.contains(id) ? 1 : 0
            let rate = Float(dt) / (target > t.gain ? 0.03 : 0.08)
            t.gain += max(-rate, min(rate, target - t.gain))
            guard t.gain > 0 || target > 0, let u = state.unit(id) else {
                t.node.stop(); engine?.detach(t.node); miniguns[id] = nil; continue
            }
            t.node.volume = t.gain * Self.minigunGain * reach(u.position)
            if t.node.volume > 0.001 { place(t.node, u.position) }
            miniguns[id] = t
        }
    }

    private func makeTool(_ u: GameCore.Unit, _ sound: String) -> Tool? {
        let variants = bank[sound] ?? []
        guard !variants.isEmpty else { return nil }
        let buf = variants[u.id % variants.count]
        guard let p = attach(buf.format, spatial: true) else { return nil }
        p.volume = 0
        p.scheduleBuffer(buf, at: nil, options: .loops)
        p.play()
        return Tool(node: p, gain: 0, sound: sound)
    }

    /// A load was dropped at `at` (ground position).
    func deposited(at: Vec2) { oneShot("deposit", at: at, gain: 0.45) }

    /// A new unit reports in ("Prospector good to go, sir", "You want a piece of
    /// me, boy?"); at most one line every 3 s.
    func trained(_ kind: GameCore.Unit.Kind, at: Vec2) {
        guard clock - lastVoice > 3 else { return }
        let voice: [GameCore.Unit.Kind: String] = [.prospector: "vprospector", .ranger: "vranger", .comet: "vcomet", .firefly: "vfirefly",
                                                   .juggernaut: "vjuggernaut", .dropship: "vdropship", .longbow: "vlongbow",
                                                   .kestrel: "vkestrel", .hailstorm: "vhailstorm"]
        if oneShot(voice[kind] ?? "vranger", at: at, gain: 0.6) { lastVoice = clock }
    }

    /// A Ranger fires (coil rifle). When the round hits blue's base the
    /// Oracle warns, at most once every 30 s.
    /// Each shooter has its own sound: the Ranger's rifle, the Comet's
    /// pistols, the Firefly's flame, the Juggernaut's grenades, the tank's
    /// cannons (and the anchored tank's shell and its impact).
    /// The report sounds where the shooter stands (`from`), an anchored
    /// shell's impact where it lands (`at`).
    /// `minigun`: a Ranger's mini gun, whose roar plays for the whole burst
    /// (`updateMiniguns`), not per round. `tower`: a Sentinel's missiles,
    /// with its lock-on servo when it opens fire after 3 s of quiet.
    /// The Kestrel fires its rockets; the Hailstorm its flak, whose shell
    /// bursts in the air at the flyer (`Rules.flight` later).
    func shot(by kind: GameCore.Unit.Kind?, anchored: Bool, from: Vec2?, at: Vec2, hitting victim: Int, structure: Bool,
              minigun: Bool = false, tower: Bool = false) {
        if victim == 0, structure, clock - lastAlert > 30, oneShot("alert", at: at, gain: 0.7, spatial: false) {
            lastAlert = clock
        }
        if minigun, kind == .ranger { return }
        let (sound, gain, rate): (String, Float, Int) = switch kind {
        case _ where tower: ("missile", Self.missileGain, 5)
        case .comet: ("pistol", 0.3, 8)
        case .kestrel: ("rockets", Self.rocketGain, 6)
        case .hailstorm: ("flak", Self.flakGain, 6)
        case .prospector: ("weld", 0.3, 6)
        case .firefly: ("flame", 0.4, 5)
        case .juggernaut: ("breacher", 0.4, 6)
        case .longbow: anchored ? ("anchorshot", 0.55, 4) : ("cannon", Self.tankCannonGain, 5)
        default: ("gun", 0.32, 9)
        }
        if tower, let from {
            let quiet = clock - (towerShots[from] ?? -10) > 3
            towerShots[from] = clock
            if quiet, allow("sentineltrack", perSecond: 2) {
                oneShot("sentineltrack", at: from, gain: Self.sentinelTrackGain, pool: true)
            }
        }
        guard allow(sound, perSecond: rate) else { return }
        oneShot(sound, at: from ?? at, gain: gain, pool: true)
        if kind == .hailstorm, !tower, allow("flakhit", perSecond: 5) {
            let flight = from.map { Rules.flight(.hailstorm, anchored: false, distance: simd_distance($0, at)) } ?? 0
            DispatchQueue.main.asyncAfter(deadline: .now() + flight) { [weak self] in
                _ = self?.oneShot("flakhit", at: at, gain: Self.flakHitGain, pool: true)
            }
        }
        if kind == .longbow, anchored, allow("anchorhit", perSecond: 4) {
            // When the shell comes down (`Rules.flight`), not as it leaves.
            let flight = from.map { Rules.flight(.longbow, anchored: true, distance: simd_distance($0, at)) } ?? 0
            DispatchQueue.main.asyncAfter(deadline: .now() + flight) { [weak self] in
                _ = self?.oneShot("anchorhit", at: at, gain: Self.anchorHitGain, pool: true)
            }
        }
    }

    /// The driven gun's round landed, in your ear: a round striking home
    /// (a clank on a building's hull), or the kill ping.
    func hitMark(kill: Bool, structure: Bool) {
        if kill, allow("killmark", perSecond: 4) { oneShot("killmark", at: .zero, gain: Self.killMarkGain, spatial: false) }
        let sound = structure && bank["hitclank"]?.isEmpty == false ? "hitclank" : "hitmark"
        guard allow(sound, perSecond: 8) else { return }
        oneShot(sound, at: .zero, gain: sound == "hitclank" ? Self.hitClankGain : Self.hitMarkGain, spatial: false)
    }

    /// The driven gun's round spent in the dirt where the sight meets it.
    func dirtHit(at: Vec2) {
        guard allow("dirthit", perSecond: 8) else { return }
        oneShot("dirthit", at: at, gain: 0.3, pool: true)
    }

    /// The driven unit's footfall.
    func step() {
        guard allow("step", perSecond: 6) else { return }
        oneShot("step", at: .zero, gain: 0.22, spatial: false)
    }

    /// The driven unit is badly hurt: a laboured breath.
    func breath() {
        oneShot("breath", at: .zero, gain: Self.breathGain, spatial: false)
    }

    /// An enemy came into the driven gun's range under its sight.
    func lockOn() {
        guard allow("lockon", perSecond: 3) else { return }
        oneShot("lockon", at: .zero, gain: Self.lockOnGain, spatial: false)
    }

    /// The driven kind went up a level: a short fanfare in your ear.
    func levelUp() {
        guard allow("levelup", perSecond: 1) else { return }
        oneShot("levelup", at: .zero, gain: Self.levelUpGain, spatial: false)
    }

    /// A leveling pick was kept: a latch and a beep.
    func picked() {
        guard allow("pick", perSecond: 4) else { return }
        oneShot("pick", at: .zero, gain: Self.pickGain, spatial: false)
    }

    /// The driven Dropship's heal beam is on (it loops while it is).
    func heal(on: Bool) { healing = on }

    /// A driven vehicle pulls away: its engine (the Firefly's, the
    /// Dropship's, the Kestrel's, the Hailstorm's), at most once every 2 s.
    /// Kinds on foot, and the Longbow, make no sound here.
    func pullAway(_ kind: GameCore.Unit.Kind) {
        let engines: [GameCore.Unit.Kind: (sound: String, gain: Float)] = [.firefly: ("fireflymove", Self.fireflyEngineGain),
                                                                           .dropship: ("dropshipmove", Self.dropshipEngineGain),
                                                                           .kestrel: ("kestrelmove", Self.kestrelEngineGain),
                                                                           .hailstorm: ("hailstormmove", Self.hailstormEngineGain)]
        guard let motor = engines[kind], clock - lastEngine > 2 else { return }
        if oneShot(motor.sound, at: .zero, gain: motor.gain, spatial: false) { lastEngine = clock }
    }

    /// A Comet's jetpack fires for a cliff jump, and again as it lands.
    func jump(landing: Bool, at: Vec2) {
        let sound = landing ? "jetland" : "jetup"
        guard allow(sound, perSecond: 4) else { return }
        oneShot(sound, at: at, gain: 0.4, pool: true)
    }

    /// A Longbow sets up or packs up.
    func anchor(up: Bool, at: Vec2) {
        let sound = up ? "anchorset" : "anchorlift"
        guard allow(sound, perSecond: 3) else { return }
        oneShot(sound, at: at, gain: Self.anchorGain, pool: true)
    }

    /// A unit is killed: the Ranger's cry, or the Prospector's and a small blast.
    func died(_ kind: GameCore.Unit.Kind, at: Vec2) {
        let sounds: [GameCore.Unit.Kind: String] = [.ranger: "rangerdeath", .comet: "cometdeath", .firefly: "fireflydeath", .juggernaut: "juggernautdeath",
                                                    .dropship: "dropshipdeath", .longbow: "longbowdeath",
                                                    .kestrel: "kestreldeath", .hailstorm: "hailstormdeath"]
        let sound = sounds[kind] ?? "prospectordeath"
        guard allow(sound, perSecond: 3) else { return }
        oneShot(sound, at: at, gain: 0.55, pool: true)
        // Machines blow up.
        if Leveling.machines.contains(kind), allow("blast", perSecond: 3) {
            oneShot("blast", at: at, gain: 0.5, pool: true)
        }
    }

    /// A driven unit's grenade goes off (Pulse mine).
    func grenade(at: Vec2) {
        guard allow("blast", perSecond: 3) else { return }
        oneShot("blast", at: at, gain: 0.45, pool: true)
    }

    /// A building goes up in flames.
    func destroyed(_ kind: Structure.Kind, at: Vec2) {
        oneShot("boom", at: at, gain: kind == .habDome ? Self.habDomeBoomGain : Self.boomGain, pool: true)
    }

    /// True if fewer than `perSecond` of this sound started in the last second.
    private func allow(_ sound: String, perSecond: Int) -> Bool {
        var times = (recent[sound] ?? []).filter { clock - $0 < 1 }
        guard times.count < perSecond else { recent[sound] = times; return false }
        times.append(clock)
        recent[sound] = times
        return true
    }

    /// `pool`: play on the combat players. `spatial` false: centred and not
    /// faded by distance (the Oracle speaks in your ear).
    @discardableResult
    private func oneShot(_ sound: String, at: Vec2, gain: Float, pool: Bool = false, spatial: Bool = true) -> Bool {
        let players = !spatial ? inEar : pool ? combat : deposits
        guard engine?.isRunning == true, let variants = bank[sound], !variants.isEmpty, !players.isEmpty else { return false }
        let r = spatial ? reach(at) : 1
        guard r > 0.001 else { return false }
        let p: AVAudioPlayerNode
        if !spatial {
            p = inEar[nextEar]
            nextEar = (nextEar + 1) % inEar.count
        } else if pool {
            p = combat[nextCombat]
            nextCombat = (nextCombat + 1) % combat.count
        } else {
            p = deposits[nextDeposit]
            nextDeposit = (nextDeposit + 1) % deposits.count
        }
        p.stop()
        if spatial { place(p, at) }
        p.volume = gain * r
        p.scheduleBuffer(variants.randomElement()!, at: nil)
        p.play()
        return true
    }

    /// How much of a sound at `ground` reaches "my location" (1 when the
    /// whole map is heard); in the top-down window, what the screen shows.
    private func reach(_ ground: Vec2) -> Float {
        guard let l = game?.listener, l.local else { return 1 }
        return Float(game?.screenGain(ground) ?? l.gain(at: ground))
    }

    /// Around "my location" on the wallpaper, pan by the offset from it;
    /// otherwise (the window too) the left edge of the view is hard-ish
    /// left and the right edge hard-ish right.
    private func pan(_ ground: Vec2) -> Float {
        guard let game else { return 0 }
        if game.free == nil, let l = game.listener, l.local { return Float(l.pan(at: ground)) * 0.85 }
        let c = game.canvasPoint(SIMD3(ground.x, game.world.groundY(ground), ground.y))
        let x = c.x / max(game.canvasWidth, 1)
        return Float(min(max(x * 2 - 1, -1), 1)) * 0.85
    }

    // MARK: - Music

    private func startMusic() {
        guard let engine else { return }
        guard music.isEmpty else { return music.attach(engine, node: musicNode) }
        let pad = self.pad
        engine.connect(musicNode, to: engine.mainMixerNode, format: pad.format)
        musicNode.scheduleBuffer(pad, at: nil, options: .loops)
        musicNode.volume = settings.music * 0.6
        musicNode.play()
    }

    /// The audio files in `dir` (none if it is missing), sorted by name.
    static func audioFiles(in dir: URL?) -> [URL] {
        guard let dir else { return [] }
        return ((try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? [])
            .filter { musicExtensions.contains($0.pathExtension.lowercased()) }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
    }

    // MARK: - Sound bank

    /// Each sound is every `sfx/<name>*.<ext>` file (variants play at
    /// random): Autocraft's own (made with ElevenLabs, shipped in the app's
    /// `Sounds`) where it has them, else the ones in the Audio folder, else
    /// the synth placeholder.
    private func loadBank() {
        let shelves = Self.soundShelves()
        let placeholders: [String: () -> [AVAudioPCMBuffer]] = [
            "wind": { [Synth.wind(seed: 1), Synth.wind(seed: 2)] },
            "drill": { (0..<3).map { Synth.drill(seed: UInt64($0)) } },
            "deposit": { (0..<3).map { Synth.deposit(seed: UInt64($0)) } },
            "hum": { [Synth.hum()] },
            // The welder falls back to the drill; the voice has no placeholder.
            "weld": { (0..<3).map { Synth.drill(seed: UInt64(10 + $0)) } },
            "vprospector": { [] },
            "vranger": { [] },
            "gun": { (0..<3).map { Synth.gun(seed: UInt64($0)) } },
            "minigun": { [] },
            "boom": { (0..<2).map { Synth.boom(seed: UInt64($0)) } },
            "blast": { (0..<2).map { Synth.boom(seed: UInt64(10 + $0), length: 0.9) } },
            "rangerdeath": { [] },
            "prospectordeath": { [] },
            "alert": { [] },
            // The later units: weapons fall back to the rifle or a short
            // blast; voices and the rest have no placeholder.
            "pistol": { (0..<3).map { Synth.gun(seed: UInt64(20 + $0)) } },
            "flame": { (0..<2).map { Synth.boom(seed: UInt64(30 + $0), length: 0.6) } },
            "breacher": { (0..<2).map { Synth.boom(seed: UInt64(40 + $0), length: 0.5) } },
            "cannon": { (0..<2).map { Synth.boom(seed: UInt64(50 + $0), length: 0.7) } },
            "anchorshot": { (0..<2).map { Synth.boom(seed: UInt64(60 + $0), length: 1.2) } },
            "hitmark": { [Synth.hitMark()] }, "killmark": { [Synth.killMark()] },
            "hitclank": { [] }, "dirthit": { [] }, "step": { [] }, "breath": { [] }, "lockon": { [] },
            "levelup": { [] }, "pick": { [] },
            "anchorhit": { [] }, "anchorset": { [] }, "anchorlift": { [] },
            "jetup": { [] }, "jetland": { [] }, "heal": { [] }, "fireflymove": { [] }, "dropshipmove": { [] },
            "vcomet": { [] }, "vfirefly": { [] }, "vjuggernaut": { [] }, "vdropship": { [] }, "vlongbow": { [] },
            "cometdeath": { [] }, "fireflydeath": { [] }, "juggernautdeath": { [] }, "dropshipdeath": { [] }, "longbowdeath": { [] },
            "rockets": { (0..<2).map { Synth.boom(seed: UInt64(70 + $0), length: 0.5) } },
            "flak": { (0..<2).map { Synth.boom(seed: UInt64(80 + $0), length: 0.6) } },
            "missile": { (0..<2).map { Synth.boom(seed: UInt64(90 + $0), length: 0.5) } },
            "flakhit": { [] }, "sentineltrack": { [] }, "kestrelmove": { [] }, "hailstormmove": { [] },
            "vkestrel": { [] }, "vhailstorm": { [] }, "kestreldeath": { [] }, "hailstormdeath": { [] },
        ]
        for (name, synth) in placeholders {
            let (mine, ours) = Self.soundFiles(name, in: shelves)
            // Same peak as the placeholders, so the mix gains fit both.
            let loaded = mine.compactMap(Self.loadMono)
            loaded.forEach { Synth.normalize($0) }
            if loaded.isEmpty {
                bank[name] = synth()
                sources[name] = bank[name]!.isEmpty ? "none" : "synth"
            } else {
                // Wind wants two layers; offset the second copy by half.
                bank[name] = name == "wind" && loaded.count == 1 ? [loaded[0], Self.rotated(loaded[0])] : loaded
                sources[name] = "\(loaded.count) \(ours ? "own " : "")file\(loaded.count == 1 ? "" : "s")"
            }
        }
    }

    /// Every sound file on hand: Autocraft's own (the app's `Sounds/sfx`)
    /// and the Audio folder's `sfx`, each sorted by name.
    static func soundShelves() -> (own: [URL], folder: [URL]) {
        (audioFiles(in: Bundle.module.resourceURL?.appendingPathComponent("Sounds/sfx")),
         audioFiles(in: directory.appendingPathComponent("sfx")))
    }

    /// The files of sound `name` (its variants): Autocraft's own where it
    /// has any (`own` true), else the Audio folder's.
    static func soundFiles(_ name: String, in shelves: (own: [URL], folder: [URL])) -> (files: [URL], own: Bool) {
        let ours = shelves.own.filter { $0.lastPathComponent.lowercased().hasPrefix(name) }
        if !ours.isEmpty { return (ours, true) }
        return (shelves.folder.filter { $0.lastPathComponent.lowercased().hasPrefix(name) }, false)
    }

    /// Read any audio file as mono 44.1 kHz float.
    static func loadMono(_ url: URL) -> AVAudioPCMBuffer? {
        guard let file = try? AVAudioFile(forReading: url),
              let input = AVAudioPCMBuffer(pcmFormat: file.processingFormat,
                                           frameCapacity: AVAudioFrameCount(file.length)),
              (try? file.read(into: input)) != nil,
              let converter = AVAudioConverter(from: file.processingFormat, to: Synth.mono) else {
            Log.write("audio: cannot read \(url.lastPathComponent)")
            return nil
        }
        converter.downmix = true
        let ratio = Synth.rate / file.processingFormat.sampleRate
        guard let out = AVAudioPCMBuffer(pcmFormat: Synth.mono,
                                         frameCapacity: AVAudioFrameCount(Double(input.frameLength) * ratio) + 1024)
        else { return nil }
        var fed = false
        var error: NSError?
        converter.convert(to: out, error: &error) { _, status in
            if fed { status.pointee = .endOfStream; return nil }
            fed = true
            status.pointee = .haveData
            return input
        }
        if let error { Log.write("audio: \(url.lastPathComponent): \(error)"); return nil }
        return out
    }

    static func rotated(_ b: AVAudioPCMBuffer) -> AVAudioPCMBuffer {
        let n = Int(b.frameLength)
        let out = AVAudioPCMBuffer(pcmFormat: b.format, frameCapacity: b.frameLength)!
        out.frameLength = b.frameLength
        let src = b.floatChannelData![0], dst = out.floatChannelData![0]
        for i in 0..<n { dst[i] = src[(i + n / 2) % n] }
        return out
    }

    // MARK: - Other apps

    /// Whether any other process is sending sound to an output right now
    /// (CoreAudio's per-process objects, macOS 14+).
    static func otherProcessPlaying() -> Bool {
        let system = AudioObjectID(kAudioObjectSystemObject)
        var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyProcessObjectList,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(system, &addr, 0, nil, &size) == noErr, size > 0 else { return false }
        var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
        guard AudioObjectGetPropertyData(system, &addr, 0, nil, &size, &ids) == noErr else { return false }
        let me = getpid()
        for id in ids {
            var pid: pid_t = 0
            var running: UInt32 = 0
            var s = UInt32(MemoryLayout<pid_t>.size)
            addr.mSelector = kAudioProcessPropertyPID
            guard AudioObjectGetPropertyData(id, &addr, 0, nil, &s, &pid) == noErr, pid != me else { continue }
            s = UInt32(MemoryLayout<UInt32>.size)
            addr.mSelector = kAudioProcessPropertyIsRunningOutput
            if AudioObjectGetPropertyData(id, &addr, 0, nil, &s, &running) == noErr, running != 0 { return true }
        }
        return false
    }

    private func writeReadme() {
        let url = Self.directory.appendingPathComponent("README.txt")
        guard (try? String(contentsOf: url, encoding: .utf8))?.contains("Autocraft's own soundtrack") != true else { return }
        let text = """
        Autocraft sounds. Restart Autocraft after changing files here.

        music/            your own tracks: every audio file here plays too, shuffled in with
                          Autocraft's own soundtrack, with a pause between tracks
        sfx/              used only for a sound Autocraft has no file of its own for; it ships
                          its own for every sound below, so today only music/ is used
        sfx/wind*.*       ambient wind bed (loops)
        sfx/drill*.*      Prospector drilling (loops while a Prospector mines; several files = variants)
        sfx/deposit*.*    ore returned to the Citadel (one-shot, variants)
        sfx/hum*.*        Citadel idle hum (loops)
        sfx/weld*.*       Prospector welding while it builds (loops, variants)
        sfx/vprospector*.*  a new Prospector reports in (one-shot voice, variants)
        sfx/vranger*.*    a new Ranger reports in (one-shot voice, variants)
        sfx/gun*.*        a Ranger's rifle burst (one-shot, variants)
        sfx/minigun*.*    a Ranger's mini gun roar (loops for the whole burst)
        sfx/rangerdeath*.*, prospectordeath*.*  a Ranger or a Prospector dies (one-shot voice, variants)
        sfx/blast*.*      small explosion when a Prospector dies (one-shot, variants)
        sfx/boom*.*       a building explodes (one-shot, variants)
        sfx/alert*.*      the Oracle warns that the base is under attack (blue's buildings only, at most every 30 s)
        sfx/pistol*.*     a Comet's pistols         sfx/jetup*.*, jetland*.*  a Comet's cliff jump
        sfx/flame*.*      a Firefly's flame         sfx/breacher*.*  a Juggernaut's grenades
        sfx/cannon*.*     a Longbow's cannons       sfx/anchorshot*.*, anchorhit*.*  an anchored Longbow's shell
        sfx/anchorset*.*, anchorlift*.*  a Longbow anchors and packs up
        sfx/heal*.*       a Dropship's heal beam (loops)
        sfx/rockets*.*    a Kestrel's rockets       sfx/flak*.*, flakhit*.*  a Hailstorm's flak and its air burst
        sfx/missile*.*, sentineltrack*.*  a Sentinel's missiles and its lock-on servo
        sfx/fireflymove*.*, dropshipmove*.*, kestrelmove*.*, hailstormmove*.*  a driven vehicle's engine as it pulls away
        sfx/vcomet*.*, vfirefly*.*, vjuggernaut*.*, vdropship*.*, vlongbow*.*, vkestrel*.*, vhailstorm*.*  a new unit reports in
        sfx/cometdeath*.*, fireflydeath*.*, juggernautdeath*.*, dropshipdeath*.*, longbowdeath*.*,
            kestreldeath*.*, hailstormdeath*.*  the unit dies
        sfx/hitmark*.*, hitclank*.*, killmark*.*  the driven unit's shot hits a unit, a building, or kills (in your ear)
        sfx/dirthit*.*    the driven unit's shot misses into the ground
        sfx/step*.*, breath*.*, lockon*.*  the driven unit's footfalls, its breath when badly hurt, an enemy coming into range
        sfx/levelup*.*, pick*.*  the driven kind goes up a level, a leveling pick is kept (in your ear)

        Formats: ogg, mp3, m4a, aac, wav, aiff, caf, flac. Anything missing uses a built-in placeholder.

        """
        try? text.write(to: url, atomically: true, encoding: .utf8)
    }
}
