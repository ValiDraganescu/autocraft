// E7 (AcPilotAudio.h): measures the Swift game's 3D stage offline. Run: swiftc -O avstage_measure.swift -o /tmp/m && /tmp/m
// (AVAudioEngine manual rendering: nothing reaches a device). Results as of 2026-10-05 in AcPilotAudio.h.
// Offline measurement of the Swift game's 3D stage (Audio.swift:183-196, place :330):
// AVAudioEnvironmentNode, rolloff 0, mediumHall reverb at -4 dB, HRTFHQ sources,
// obstruction -14 dB, reverbBlend. Manual (offline) rendering: nothing reaches a device.
import AVFoundation
import Foundation

let rate = 44100.0
let mono = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 1)!
let stereo = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2)!

func noise(_ seconds: Double, amp: Float = 0.5) -> AVAudioPCMBuffer {
    let n = AVAudioFrameCount(seconds * rate)
    let b = AVAudioPCMBuffer(pcmFormat: mono, frameCapacity: n)!
    b.frameLength = n
    var s: UInt32 = 12345
    for i in 0..<Int(n) {
        s = s &* 1664525 &+ 1013904223
        b.floatChannelData![0][i] = (Float(s >> 8) / Float(1 << 24) * 2 - 1) * amp
    }
    return b
}

func sine(_ hz: Double, _ seconds: Double, amp: Float = 0.5) -> AVAudioPCMBuffer {
    let n = AVAudioFrameCount(seconds * rate)
    let b = AVAudioPCMBuffer(pcmFormat: mono, frameCapacity: n)!
    b.frameLength = n
    for i in 0..<Int(n) { b.floatChannelData![0][i] = amp * Float(sin(2 * .pi * hz * Double(i) / rate)) }
    return b
}

struct Out { var l: [Float]; var r: [Float] }

/// Render `seconds` of one source at `pos` (listener at the origin facing -Z).
func render(_ buf: AVAudioPCMBuffer, pos: (Float, Float, Float), blend: Float, obstruction: Float, seconds: Double,
            reverb: Bool = true, algorithm: AVAudio3DMixingRenderingAlgorithm = .HRTFHQ) -> Out {
    let e = AVAudioEngine()
    let space = AVAudioEnvironmentNode()
    e.attach(space)
    e.connect(space, to: e.mainMixerNode, format: stereo)
    space.outputType = .auto
    space.distanceAttenuationParameters.distanceAttenuationModel = .inverse
    space.distanceAttenuationParameters.referenceDistance = 1
    space.distanceAttenuationParameters.rolloffFactor = 0
    space.reverbParameters.enable = reverb
    space.reverbParameters.loadFactoryReverbPreset(.mediumHall)
    space.reverbParameters.level = -4
    space.listenerPosition = AVAudio3DPoint(x: 0, y: 0, z: 0)
    space.listenerVectorOrientation = AVAudio3DVectorOrientation(forward: AVAudio3DVector(x: 0, y: 0, z: -1), up: AVAudio3DVector(x: 0, y: 1, z: 0))
    let p = AVAudioPlayerNode()
    e.attach(p)
    e.connect(p, to: space, format: mono)
    try! e.enableManualRenderingMode(.offline, format: stereo, maximumFrameCount: 4096)
    p.renderingAlgorithm = algorithm
    p.position = AVAudio3DPoint(x: pos.0, y: pos.1, z: pos.2)
    p.obstruction = obstruction
    p.reverbBlend = blend
    p.volume = 1
    try! e.start()
    p.scheduleBuffer(buf, at: nil)
    p.play()
    let out = AVAudioPCMBuffer(pcmFormat: e.manualRenderingFormat, frameCapacity: 4096)!
    var o = Out(l: [], r: [])
    let total = Int(seconds * rate)
    while o.l.count < total {
        let n = AVAudioFrameCount(min(4096, total - o.l.count))
        let st = try! e.renderOffline(n, to: out)
        guard st == .success else { break }
        let ch = out.floatChannelData!
        let c2 = Int(out.format.channelCount) > 1
        for i in 0..<Int(out.frameLength) { o.l.append(ch[0][i]); o.r.append(c2 ? ch[1][i] : ch[0][i]) }
    }
    e.stop()
    return o
}

func rms(_ x: ArraySlice<Float>) -> Double {
    guard !x.isEmpty else { return 0 }
    return sqrt(x.reduce(0.0) { $0 + Double($1) * Double($1) } / Double(x.count))
}
func db(_ v: Double) -> Double { 20 * log10(max(v, 1e-9)) }
func win(_ x: [Float], _ a: Double, _ b: Double) -> ArraySlice<Float> { x[Int(a * rate)..<min(x.count, Int(b * rate))] }

let n = noise(1.0)
let ref = rms(win(Array(UnsafeBufferPointer(start: n.floatChannelData![0], count: Int(n.frameLength))), 0.2, 0.9))
print(String(format: "input noise rms %.4f (%.2f dB)", ref, db(ref)))

print("\n# 1. HRTFHQ, blend 0, reverb on: L/R rms (dB re input) by azimuth at 5 cells (0 = ahead, +90 = right)")
for az in [0.0, 30, 60, 90, 120, 150, 180, -90, -45] {
    let a = az * .pi / 180
    let o = render(n, pos: (Float(5 * sin(a)), 0, Float(-5 * cos(a))), blend: 0, obstruction: 0, seconds: 1.0)
    let l = rms(win(o.l, 0.2, 0.9)), r = rms(win(o.r, 0.2, 0.9))
    print(String(format: "az %5.0f  L %6.2f  R %6.2f  L-R %6.2f  sum(pow) %6.2f", az, db(l / ref), db(r / ref), db(l / r), 10 * log10((l * l + r * r) / (ref * ref))))
}
print("\n# 1b. elevation: above (0,5,0), below (0,-5,0), ahead-up 45")
for (name, p) in [("up", (Float(0), Float(5), Float(0))), ("down", (0, -5, 0)), ("ahead+45up", (0, 3.54, -3.54))] {
    let o = render(n, pos: p, blend: 0, obstruction: 0, seconds: 1.0)
    let l = rms(win(o.l, 0.2, 0.9)), r = rms(win(o.r, 0.2, 0.9))
    print(String(format: "%@  L %6.2f  R %6.2f", name, db(l / ref), db(r / ref)))
}
print("\n# 1c. equalPowerPanning (top-down path) for comparison")
for az in [0.0, 45, 90] {
    let a = az * .pi / 180
    let o = render(n, pos: (Float(sin(a)), 0, Float(-cos(a))), blend: 0, obstruction: 0, seconds: 1.0, algorithm: .equalPowerPanning)
    let l = rms(win(o.l, 0.2, 0.9)), r = rms(win(o.r, 0.2, 0.9))
    print(String(format: "az %5.0f  L %6.2f  R %6.2f", az, db(l / ref), db(r / ref)))
}

print("\n# 2. reverbBlend: 50 ms noise burst ahead; direct window 0-60 ms, tail 150 ms-2.5 s (energy dB re burst)")
let burst = noise(0.05)
let bref = rms(win(Array(UnsafeBufferPointer(start: burst.floatChannelData![0], count: Int(burst.frameLength))), 0, 0.05))
for b: Float in [0, 0.06, 0.15, 0.25, 0.35, 0.45, 1.0] {
    let o = render(burst, pos: (0, 0, -5), blend: b, obstruction: 0, seconds: 3.0)
    func energy(_ x: ArraySlice<Float>) -> Double { x.reduce(0.0) { $0 + Double($1) * Double($1) } }
    let d = energy(win(o.l, 0, 0.06)) + energy(win(o.r, 0, 0.06))
    let t = energy(win(o.l, 0.15, 3.0)) + energy(win(o.r, 0.15, 3.0))
    let e0 = 2 * energy(win(Array(UnsafeBufferPointer(start: burst.floatChannelData![0], count: Int(burst.frameLength))), 0, 0.05))
    // Decay: energy in 100 ms slices of the tail.
    var slices: [String] = []
    for k in stride(from: 0.1, to: 2.5, by: 0.2) {
        let s = energy(win(o.l, k, k + 0.1)) + energy(win(o.r, k, k + 0.1))
        slices.append(String(format: "%.0f", 10 * log10(max(s / e0, 1e-12))))
    }
    print(String(format: "blend %.2f  direct %6.2f dB  tail %6.2f dB  | tail slices from 0.1 s every 0.2 s: %@", b, 10 * log10(d / e0), 10 * log10(max(t / e0, 1e-12)), slices.joined(separator: " ")))
}
_ = bref

print("\n# 3. obstruction -14 vs 0: sine response (dB, obstructed - clear), ahead, blend 0")
for hz in [63.0, 125, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000] {
    let s = sine(hz, 1.0)
    let c = render(s, pos: (0, 0, -5), blend: 0, obstruction: 0, seconds: 1.0)
    let o = render(s, pos: (0, 0, -5), blend: 0, obstruction: -14, seconds: 1.0)
    let cl = rms(win(c.l, 0.3, 0.9)) + rms(win(c.r, 0.3, 0.9)), ol = rms(win(o.l, 0.3, 0.9)) + rms(win(o.r, 0.3, 0.9))
    print(String(format: "%6.0f Hz  %6.2f dB", hz, db(ol / cl)))
}
print("\n# 3b. obstruction -14 with blend 0.45: tail (reverb path) obstructed vs clear")
do {
    func energy(_ x: ArraySlice<Float>) -> Double { x.reduce(0.0) { $0 + Double($1) * Double($1) } }
    let c = render(burst, pos: (0, 0, -5), blend: 0.45, obstruction: 0, seconds: 3.0)
    let o = render(burst, pos: (0, 0, -5), blend: 0.45, obstruction: -14, seconds: 3.0)
    let ct = energy(win(c.l, 0.15, 3.0)) + energy(win(c.r, 0.15, 3.0)), ot = energy(win(o.l, 0.15, 3.0)) + energy(win(o.r, 0.15, 3.0))
    let cd = energy(win(c.l, 0, 0.06)) + energy(win(c.r, 0, 0.06)), od = energy(win(o.l, 0, 0.06)) + energy(win(o.r, 0, 0.06))
    print(String(format: "direct %.2f dB, tail %.2f dB", 10 * log10(od / cd), 10 * log10(ot / ct)))
    // Broadband noise through the obstruction.
    let cn = render(n, pos: (0, 0, -5), blend: 0, obstruction: 0, seconds: 1.0)
    let on = render(n, pos: (0, 0, -5), blend: 0, obstruction: -14, seconds: 1.0)
    print(String(format: "white noise broadband: %.2f dB", db((rms(win(on.l, 0.2, 0.9)) + rms(win(on.r, 0.2, 0.9))) / (rms(win(cn.l, 0.2, 0.9)) + rms(win(cn.r, 0.2, 0.9))))))
}
