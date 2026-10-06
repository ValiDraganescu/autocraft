// The music deck's button icons (chunk D9, GAME-LAYER.md §2.11): the SF
// Symbols `MusicDeck.symbol` draws (36 pt, bold, at 2x, filled white), as
// unreal/Content-src/icons/sym.<symbol>.png. Rerunnable:
//   swift unreal/Tools/hud/bake_symbols.swift
// then import: py "<repo>/unreal/Tools/Editor/import_icons.py"
// (→ /Game/UI/Icons/T_Icon_sym_<symbol with _ for .>, `FAcIcons::Brush("sym.<symbol>")`).
import AppKit

let names = ["hand.thumbsdown", "hand.thumbsdown.fill", "hand.thumbsup", "hand.thumbsup.fill",
             "backward.end.fill", "forward.end.fill", "play.fill", "pause.fill"]
let here = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent()
let out = here.appendingPathComponent("../../Content-src/icons").standardized
try FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
for name in names {
    guard let image = NSImage(systemSymbolName: name, accessibilityDescription: nil)?
        .withSymbolConfiguration(NSImage.SymbolConfiguration(pointSize: 36, weight: .bold)) else {
        print("no symbol \(name)"); exit(1)
    }
    let scale: CGFloat = 2
    let w = Int((image.size.width * scale).rounded(.up)), h = Int((image.size.height * scale).rounded(.up))
    let cg = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                       space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    let full = CGRect(x: 0, y: 0, width: w, height: h)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(cgContext: cg, flipped: false)
    image.draw(in: full)
    NSGraphicsContext.restoreGraphicsState()
    cg.setBlendMode(.sourceIn)
    cg.setFillColor(NSColor.white.cgColor)
    cg.fill(full)
    let rep = NSBitmapImageRep(cgImage: cg.makeImage()!)
    let url = out.appendingPathComponent("sym.\(name).png")
    try rep.representation(using: .png, properties: [:])!.write(to: url)
    print("\(url.lastPathComponent) \(w)x\(h)")
}
