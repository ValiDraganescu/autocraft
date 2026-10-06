// Bakes the HUD's art from the Swift game's own drawing code into PNGs for
// Unreal's Slate (GAME-LAYER.md §2.4, chunk D1). Compiled together with
// Sources/Autocraft/HUDChrome.swift and ResourceArt.swift by
// `unreal/Tools/hud/bake_hud.sh`, so the plates are the Swift pixels, not a
// re-drawing. Writes unreal/Content-src/hud/T_*.png; the editor script
// unreal/Tools/Editor/import_hud.py turns them into textures.
//
// Plates are drawn at the size the Swift HUD uses, at 2x (`Chrome.context`),
// padded by 6 points for the shadow and glow (`Chrome.plateNode`). Slate
// draws them as box brushes (9-slice) whose margins are the padding plus the
// corner cuts (`FAcHudStyle`), so a plate stretched to another width keeps
// its corners.
//
// To add art for a later chunk: add a line to `plates` or `images` below.
import AppKit
import ImageIO
import UniformTypeIdentifiers

/// `ResourceArt` draws through this (it lives in Materials.swift in the game).
enum MaterialLibrary {
    static func context(_ w: Int, _ h: Int) -> CGContext {
        CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    }
}

let out = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "unreal/Content-src/hud")
try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)

func write(_ image: CGImage, _ name: String) {
    let url = out.appendingPathComponent("\(name).png")
    guard let d = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
        fatalError("cannot write \(url.path)")
    }
    CGImageDestinationAddImage(d, image, nil)
    guard CGImageDestinationFinalize(d) else { fatalError("cannot write \(url.path)") }
    print("\(name).png \(image.width)x\(image.height)")
}

/// name, size in points, cuts, LEDs, seed: as the Swift HUD calls `Chrome.plate`.
let plates: [(String, CGSize, Chrome.Cuts, Bool, Int)] = [
    // HUD.buildBar: the resource bar (drawn at exactly this size).
    ("T_PlateBar", CGSize(width: 340, height: 42), Chrome.Cuts(tl: 4, tr: 4, br: 14, bl: 14), true, 11),
    // HUD.buildScoreboard: the scoreboard (stretched to its width).
    ("T_PlateScore", CGSize(width: 220, height: 42), Chrome.Cuts(tl: 4, tr: 4, br: 14, bl: 14), false, 12),
    // HUD.applyLevelUp: the level-up banner (E9; stretched).
    ("T_PlateLevel", CGSize(width: 360, height: 82), Chrome.Cuts(tl: 14, tr: 14, br: 4, bl: 4), false, 23),
]
for (name, size, cuts, leds, seed) in plates {
    write(Chrome.plate(size, cuts: cuts, leds: leds, seed: seed), name)
}

// ResourceArt: the ore nodule and the MH droplet, at the game's 96 px.
write(ResourceArt.ore(ResourceArt.pixels), "T_IconOre")
write(ResourceArt.hydrogen(ResourceArt.pixels), "T_IconHydrogen")

/// HUD.supplyIcon: a supply house, 24 points across, at 4x.
func supply() -> CGImage {
    let scale: CGFloat = 4, side: CGFloat = 24
    let c = MaterialLibrary.context(Int(side * scale), Int(side * scale))
    c.scaleBy(x: scale, y: scale)
    c.translateBy(x: side / 2, y: side / 2)
    let p = CGMutablePath()
    p.move(to: CGPoint(x: -9, y: -8)); p.addLine(to: CGPoint(x: 9, y: -8))
    p.addLine(to: CGPoint(x: 9, y: 2)); p.addLine(to: CGPoint(x: 0, y: 9))
    p.addLine(to: CGPoint(x: -9, y: 2)); p.closeSubpath()
    c.addPath(p)
    c.setFillColor(NSColor(calibratedRed: 0.85, green: 0.85, blue: 0.8, alpha: 1).cgColor)
    c.fillPath()
    c.addPath(p)
    c.setStrokeColor(NSColor(white: 1, alpha: 1).cgColor)
    c.setLineWidth(1)
    c.strokePath()
    c.setFillColor(NSColor(white: 0.25, alpha: 1).cgColor)
    c.fill(CGRect(x: -3, y: -8, width: 6, height: 8))
    return c.makeImage()!
}
write(supply(), "T_IconSupply")
