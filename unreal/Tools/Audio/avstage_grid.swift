// E7 (AcPilotAudio.h): Swift's .HRTFHQ ear levels by azimuth and elevation, offline, as the table in AcPilotAudio.cpp.
// Run: swiftc -O avstage_grid.swift -o /tmp/g && /tmp/g
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
let els: [Double] = [-90, -60, -30, -15, 0, 15, 30, 60, 90]
print("// HRTFHQ levels (dB re a mono source), [elevation][azimuth]: azimuth 0..345 step 15 (+ = right), elevations", els)
for el in els {
    var ls: [String] = [], rs: [String] = []
    for k in 0..<24 {
        let az = Double(k) * 15 * .pi / 180, e = el * .pi / 180
        let o = render(n, pos: (Float(5 * sin(az) * cos(e)), Float(5 * sin(e)), Float(-5 * cos(az) * cos(e))), blend: 0, obstruction: 0, seconds: 1.0, reverb: false)
        ls.append(String(format: "%.2ff", db(rms(win(o.l, 0.2, 0.9)) / ref)))
        rs.append(String(format: "%.2ff", db(rms(win(o.r, 0.2, 0.9)) / ref)))
    }
    print("L el \(el): {" + ls.joined(separator: ", ") + "}")
    print("R el \(el): {" + rs.joined(separator: ", ") + "}")
}
