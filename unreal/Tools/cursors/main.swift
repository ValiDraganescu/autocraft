// Bakes the game's four mouse pointers (Sources/Autocraft/GameCursor.swift,
// compiled in beside this file, so they are the Swift drawing itself) for
// Unreal's hardware cursors (chunk D4, AcPointer.cpp): one multi-resolution
// TIFF per kind (32 points: 1x and 2x reps), which macOS's NSCursor picks
// from by screen, plus PNGs to look at.
//   sh unreal/Tools/cursors/bake_cursors.sh
// Writes unreal/Content/UI/Cursors/Cursor_<kind>.tiff (read at runtime by
// UGameViewportClient::SetHardwareCursor) and unreal/Content-src/cursors/.
import AppKit

let args = CommandLine.arguments
let tiffDir = URL(fileURLWithPath: args.count > 1 ? args[1] : "unreal/Content/UI/Cursors")
let pngDir = URL(fileURLWithPath: args.count > 2 ? args[2] : "unreal/Content-src/cursors")
for d in [tiffDir, pngDir] { try? FileManager.default.createDirectory(at: d, withIntermediateDirectories: true) }

func rep(_ image: NSImage, scale: Int) -> NSBitmapImageRep {
    let px = 32 * scale
    let r = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8, samplesPerPixel: 4,
                             hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    r.size = NSSize(width: 32, height: 32)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: r)
    image.draw(in: NSRect(x: 0, y: 0, width: 32, height: 32))
    NSGraphicsContext.restoreGraphicsState()
    return r
}

for (name, kind) in [("normal", GameCursor.Kind.normal), ("select", .select), ("enemy", .enemy), ("drive", .drive)] {
    let cursor = GameCursor.cursor(kind)
    let reps = [rep(cursor.image, scale: 1), rep(cursor.image, scale: 2)]
    let out = NSImage(size: NSSize(width: 32, height: 32))
    reps.forEach(out.addRepresentation)
    guard let tiff = out.tiffRepresentation else { fatalError("no tiff for \(name)") }
    try! tiff.write(to: tiffDir.appendingPathComponent("Cursor_\(name).tiff"))
    try! reps[1].representation(using: .png, properties: [:])!.write(to: pngDir.appendingPathComponent("Cursor_\(name)@2x.png"))
    print("Cursor_\(name): hot spot \(cursor.hotSpot)")
}
