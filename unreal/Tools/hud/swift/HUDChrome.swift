import AppKit
import SpriteKit

/// The HUD's look: dark navy steel plates with chamfered corners, a steel
/// rim, and a glowing cyan line inset along the edge; bright cyan corner
/// brackets on the screens. Drawn with Core Graphics into textures
/// (gradients and glows SpriteKit's shapes can't do), once per size. The
/// console and the frame round the view are the cab's armour (`Cab`).
enum Chrome {
    static let cyan = NSColor(calibratedRed: 0.35, green: 0.85, blue: 1, alpha: 1)
    static let ice = NSColor(calibratedRed: 0.8, green: 0.95, blue: 1, alpha: 1)
    static let amber = NSColor(calibratedRed: 1, green: 0.7, blue: 0.2, alpha: 1)
    static let glass = NSColor(calibratedRed: 0.01, green: 0.035, blue: 0.07, alpha: 1)
    /// Text on the plates.
    static let text = NSColor(calibratedRed: 0.92, green: 0.97, blue: 1, alpha: 1)
    static let soft = NSColor(calibratedRed: 0.55, green: 0.85, blue: 0.95, alpha: 1)

    /// How far each corner is cut off (0: square).
    struct Cuts: Hashable {
        var tl: CGFloat = 0, tr: CGFloat = 0, br: CGFloat = 0, bl: CGFloat = 0
        static func all(_ c: CGFloat) -> Cuts { Cuts(tl: c, tr: c, br: c, bl: c) }
    }

    /// A rectangle with its corners cut at 45°, inset by `d` (the cuts
    /// shrink with it so the edges stay parallel).
    static func path(_ r: CGRect, _ c: Cuts, inset d: CGFloat = 0) -> CGPath {
        let r = r.insetBy(dx: d, dy: d)
        let k = d * 0.41
        func cut(_ v: CGFloat) -> CGFloat { v > 0 ? max(v - k, 0) : 0 }
        let tl = cut(c.tl), tr = cut(c.tr), br = cut(c.br), bl = cut(c.bl)
        let p = CGMutablePath()
        p.move(to: CGPoint(x: r.minX + bl, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX - br, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.minY + br))
        p.addLine(to: CGPoint(x: r.maxX, y: r.maxY - tr))
        p.addLine(to: CGPoint(x: r.maxX - tr, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX + tl, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX, y: r.maxY - tl))
        p.addLine(to: CGPoint(x: r.minX, y: r.minY + bl))
        p.closeSubpath()
        return p
    }

    private static let space = CGColorSpaceCreateDeviceRGB()
    static func gradient(_ colors: [NSColor], _ at: [CGFloat]) -> CGGradient {
        CGGradient(colorsSpace: space, colors: colors.map(\.cgColor) as CFArray, locations: at)!
    }

    /// A context `size` points big at 2× (y up, in points).
    static func context(_ size: CGSize, scale: CGFloat = 2) -> CGContext {
        let cg = CGContext(data: nil, width: max(1, Int((size.width * scale).rounded(.up))),
                           height: max(1, Int((size.height * scale).rounded(.up))), bitsPerComponent: 8, bytesPerRow: 0,
                           space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        cg.scaleBy(x: scale, y: scale)
        return cg
    }

    /// Navy steel: lighter at the top, faint brushed streaks.
    static func steel(_ cg: CGContext, _ p: CGPath, _ r: CGRect, seed: Int = 1) {
        cg.saveGState()
        cg.addPath(p); cg.clip()
        cg.drawLinearGradient(gradient([NSColor(calibratedRed: 0.14, green: 0.2, blue: 0.3, alpha: 1),
                                        NSColor(calibratedRed: 0.06, green: 0.09, blue: 0.15, alpha: 1),
                                        NSColor(calibratedRed: 0.03, green: 0.04, blue: 0.08, alpha: 1)], [0, 0.45, 1]),
                              start: CGPoint(x: 0, y: r.maxY), end: CGPoint(x: 0, y: r.minY), options: [])
        var g = SplitMix(seed: UInt64(seed))
        for _ in 0..<Int(r.width * r.height / 900) + 4 {
            let y = r.minY + CGFloat(g.unit()) * r.height, x = r.minX + CGFloat(g.unit()) * r.width
            cg.setStrokeColor(NSColor(white: g.unit() < 0.5 ? 1 : 0, alpha: 0.04).cgColor)
            cg.setLineWidth(0.7)
            cg.move(to: CGPoint(x: x, y: y)); cg.addLine(to: CGPoint(x: x + 20 + CGFloat(g.unit()) * 70, y: y))
            cg.strokePath()
        }
        cg.restoreGState()
    }

    /// A glowing line along a path.
    static func glowLine(_ cg: CGContext, _ p: CGPath, _ c: NSColor = cyan, width: CGFloat = 1.3, blur: CGFloat = 6,
                         alpha: CGFloat = 0.95) {
        cg.saveGState()
        cg.setShadow(offset: .zero, blur: blur, color: c.withAlphaComponent(alpha).cgColor)
        cg.addPath(p)
        cg.setStrokeColor(c.withAlphaComponent(alpha).cgColor)
        cg.setLineWidth(width)
        cg.setLineJoin(.miter)
        cg.strokePath()
        // A paler core.
        cg.addPath(p)
        cg.setStrokeColor(ice.withAlphaComponent(alpha * 0.6).cgColor)
        cg.setLineWidth(width * 0.4)
        cg.strokePath()
        cg.restoreGState()
    }

    /// The steel rim: a light edge on top, dark under it.
    static func rim(_ cg: CGContext, _ p: CGPath) {
        cg.addPath(p)
        cg.setStrokeColor(NSColor(calibratedRed: 0.02, green: 0.03, blue: 0.05, alpha: 1).cgColor)
        cg.setLineWidth(3)
        cg.strokePath()
        cg.addPath(p)
        cg.setStrokeColor(NSColor(calibratedRed: 0.5, green: 0.6, blue: 0.72, alpha: 0.8).cgColor)
        cg.setLineWidth(1)
        cg.strokePath()
    }

    /// Bright L-shaped brackets in the corners of `r`.
    static func brackets(_ cg: CGContext, _ r: CGRect, length l: CGFloat = 16, _ c: NSColor = cyan, width: CGFloat = 2.2) {
        let p = CGMutablePath()
        p.move(to: CGPoint(x: r.minX, y: r.maxY - l)); p.addLine(to: CGPoint(x: r.minX, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX + l, y: r.maxY))
        p.move(to: CGPoint(x: r.maxX - l, y: r.maxY)); p.addLine(to: CGPoint(x: r.maxX, y: r.maxY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.maxY - l))
        p.move(to: CGPoint(x: r.maxX, y: r.minY + l)); p.addLine(to: CGPoint(x: r.maxX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX - l, y: r.minY))
        p.move(to: CGPoint(x: r.minX + l, y: r.minY)); p.addLine(to: CGPoint(x: r.minX, y: r.minY))
        p.addLine(to: CGPoint(x: r.minX, y: r.minY + l))
        glowLine(cg, p, c, width: width, blur: 8, alpha: 1)
    }

    /// A row of small lit dashes (status LEDs) from `a`, left to right.
    static func leds(_ cg: CGContext, from a: CGPoint, count: Int, _ c: NSColor = cyan, size: CGSize = CGSize(width: 7, height: 2.5),
                     gap: CGFloat = 4) {
        cg.saveGState()
        cg.setShadow(offset: .zero, blur: 5, color: c.cgColor)
        cg.setFillColor(c.withAlphaComponent(0.9).cgColor)
        for k in 0..<count {
            cg.fill(CGRect(x: a.x + CGFloat(k) * (size.width + gap), y: a.y, width: size.width, height: size.height))
        }
        cg.restoreGState()
    }

    /// A raised housing round a screen: a lighter steel bezel `width`
    /// wide with chamfered corners, a rim, and a dark groove inside.
    static func housing(_ cg: CGContext, _ r: CGRect, width: CGFloat = 12, cut: CGFloat = 16) {
        let outer = r.insetBy(dx: -width, dy: -width)
        let p = path(outer, .all(cut))
        cg.saveGState()
        cg.setShadow(offset: CGSize(width: 0, height: -2), blur: 8, color: NSColor(white: 0, alpha: 0.9).cgColor)
        cg.addPath(p); cg.setFillColor(NSColor(white: 0.06, alpha: 1).cgColor); cg.fillPath()
        cg.restoreGState()
        cg.saveGState()
        cg.addPath(p); cg.clip()
        cg.drawLinearGradient(gradient([NSColor(calibratedRed: 0.24, green: 0.31, blue: 0.42, alpha: 1),
                                        NSColor(calibratedRed: 0.1, green: 0.14, blue: 0.22, alpha: 1),
                                        NSColor(calibratedRed: 0.05, green: 0.07, blue: 0.12, alpha: 1)], [0, 0.5, 1]),
                              start: CGPoint(x: outer.minX, y: outer.maxY), end: CGPoint(x: outer.maxX, y: outer.minY), options: [])
        cg.restoreGState()
        rim(cg, p)
        let groove = path(r.insetBy(dx: -3, dy: -3), .all(max(cut - width * 0.6, 4)))
        cg.addPath(groove)
        cg.setStrokeColor(NSColor(white: 0, alpha: 0.85).cgColor)
        cg.setLineWidth(3)
        cg.strokePath()
    }

    /// A sunk screen of dark glass in `p`: a glow from below, a faint grid,
    /// scan lines, a cyan edge.
    static func screen(_ cg: CGContext, _ p: CGPath, _ r: CGRect, grid: Bool = true, edge: CGFloat = 0.55) {
        cg.saveGState()
        cg.setShadow(offset: CGSize(width: 0, height: -1), blur: 4, color: NSColor(white: 0, alpha: 1).cgColor)
        cg.addPath(p); cg.setFillColor(glass.cgColor); cg.fillPath()
        cg.restoreGState()
        cg.saveGState()
        cg.addPath(p); cg.clip()
        cg.drawRadialGradient(gradient([cyan.withAlphaComponent(0.16), cyan.withAlphaComponent(0)], [0, 1]),
                              startCenter: CGPoint(x: r.midX, y: r.minY), startRadius: 0,
                              endCenter: CGPoint(x: r.midX, y: r.minY), endRadius: max(r.width, r.height) * 0.8, options: [])
        if grid {
            cg.setStrokeColor(cyan.withAlphaComponent(0.05).cgColor)
            cg.setLineWidth(0.6)
            var x = r.minX + 14
            while x < r.maxX { cg.move(to: CGPoint(x: x, y: r.minY)); cg.addLine(to: CGPoint(x: x, y: r.maxY)); x += 14 }
            var y = r.minY + 14
            while y < r.maxY { cg.move(to: CGPoint(x: r.minX, y: y)); cg.addLine(to: CGPoint(x: r.maxX, y: y)); y += 14 }
            cg.strokePath()
        }
        cg.setStrokeColor(NSColor(white: 0, alpha: 0.2).cgColor)
        cg.setLineWidth(1)
        var y = r.minY + 1.5
        while y < r.maxY { cg.move(to: CGPoint(x: r.minX, y: y)); cg.addLine(to: CGPoint(x: r.maxX, y: y)); y += 3 }
        cg.strokePath()
        cg.restoreGState()
        glowLine(cg, p, cyan, width: 1, blur: 4, alpha: edge)
    }

    /// Faint radar rings with ticks, centred at `c` (a hologram's stage).
    static func rings(_ cg: CGContext, at c: CGPoint, radius: CGFloat) {
        cg.saveGState()
        cg.setStrokeColor(cyan.withAlphaComponent(0.22).cgColor)
        cg.setLineWidth(1)
        for f in [1.0, 0.72, 0.42] as [CGFloat] {
            cg.addEllipse(in: CGRect(x: c.x - radius * f, y: c.y - radius * f, width: 2 * radius * f, height: 2 * radius * f))
        }
        cg.strokePath()
        cg.setStrokeColor(cyan.withAlphaComponent(0.35).cgColor)
        for k in 0..<36 {
            let a = CGFloat(k) * .pi / 18, l: CGFloat = k % 9 == 0 ? 8 : 3
            cg.move(to: CGPoint(x: c.x + cos(a) * radius, y: c.y + sin(a) * radius))
            cg.addLine(to: CGPoint(x: c.x + cos(a) * (radius + l), y: c.y + sin(a) * (radius + l)))
        }
        cg.strokePath()
        cg.setStrokeColor(cyan.withAlphaComponent(0.12).cgColor)
        cg.move(to: CGPoint(x: c.x - radius, y: c.y)); cg.addLine(to: CGPoint(x: c.x + radius, y: c.y))
        cg.move(to: CGPoint(x: c.x, y: c.y - radius)); cg.addLine(to: CGPoint(x: c.x, y: c.y + radius))
        cg.strokePath()
        cg.restoreGState()
    }

    /// A plate `size` points big: navy steel inside a steel rim, a glowing
    /// cyan line inset along the edge, lit dashes on its longest edge.
    /// `glass` sinks a dark screen into it, `inset` from the edge.
    static func plate(_ size: CGSize, cuts: Cuts, glass: CGFloat? = nil, leds: Bool = true, seed: Int = 1) -> CGImage {
        let pad: CGFloat = 6 // room for the shadow and the glow
        let cg = context(CGSize(width: size.width + 2 * pad, height: size.height + 2 * pad))
        cg.translateBy(x: pad, y: pad)
        let r = CGRect(origin: .zero, size: size)
        let outer = path(r, cuts, inset: 1)
        cg.saveGState()
        cg.setShadow(offset: CGSize(width: 0, height: -2), blur: 6, color: NSColor(white: 0, alpha: 0.8).cgColor)
        cg.addPath(outer); cg.setFillColor(NSColor(white: 0.05, alpha: 1).cgColor); cg.fillPath()
        cg.restoreGState()
        steel(cg, outer, r, seed: seed)
        if let g = glass {
            let inner = path(r, cuts, inset: g)
            screen(cg, inner, r.insetBy(dx: g, dy: g), grid: false, edge: 0.4)
        }
        rim(cg, outer)
        glowLine(cg, path(r, cuts, inset: 4.5), cyan, width: 1.2, blur: 6, alpha: 0.85)
        if leds, size.width > 120 {
            Chrome.leds(cg, from: CGPoint(x: r.midX - 30, y: 1.2), count: 5, size: CGSize(width: 8, height: 2), gap: 4)
        }
        return cg.makeImage()!
    }

    /// A sprite of `plate`, placed with its plate (not its padding) at `r`.
    static func plateNode(_ r: CGRect, cuts: Cuts, glass: CGFloat? = nil, leds: Bool = true, seed: Int = 1) -> SKSpriteNode {
        let pad: CGFloat = 6
        let n = SKSpriteNode(texture: SKTexture(cgImage: plate(r.size, cuts: cuts, glass: glass, leds: leds, seed: seed)))
        n.size = CGSize(width: r.width + 2 * pad, height: r.height + 2 * pad)
        n.anchorPoint = .zero
        n.position = CGPoint(x: r.minX - pad, y: r.minY - pad)
        return n
    }

    /// A command card button's face: navy glass with a sheen on top.
    static func buttonFace(_ side: CGFloat, enabled: Bool) -> CGImage {
        let cg = context(CGSize(width: side, height: side))
        let r = CGRect(x: 0, y: 0, width: side, height: side).insetBy(dx: 1, dy: 1)
        let p = CGPath(roundedRect: r, cornerWidth: 6, cornerHeight: 6, transform: nil)
        cg.saveGState()
        cg.addPath(p); cg.clip()
        let top = enabled ? NSColor(calibratedRed: 0.1, green: 0.22, blue: 0.38, alpha: 1)
            : NSColor(calibratedRed: 0.07, green: 0.08, blue: 0.1, alpha: 1)
        let bottom = enabled ? NSColor(calibratedRed: 0.02, green: 0.06, blue: 0.13, alpha: 1)
            : NSColor(calibratedRed: 0.03, green: 0.03, blue: 0.04, alpha: 1)
        cg.drawLinearGradient(gradient([top, bottom], [0, 1]), start: CGPoint(x: 0, y: r.maxY), end: CGPoint(x: 0, y: r.minY),
                              options: [])
        if enabled {
            cg.drawRadialGradient(gradient([cyan.withAlphaComponent(0.25), cyan.withAlphaComponent(0)], [0, 1]),
                                  startCenter: CGPoint(x: r.midX, y: r.midY), startRadius: 0,
                                  endCenter: CGPoint(x: r.midX, y: r.midY), endRadius: side * 0.6, options: [])
        }
        // The sheen over the top half.
        cg.addPath(CGPath(roundedRect: CGRect(x: r.minX + 3, y: r.midY + 2, width: r.width - 6, height: r.height / 2 - 5),
                          cornerWidth: 4, cornerHeight: 4, transform: nil))
        cg.setFillColor(NSColor(white: 1, alpha: enabled ? 0.07 : 0.03).cgColor)
        cg.fillPath()
        cg.restoreGState()
        return cg.makeImage()!
    }

    /// Scan lines for a hologram, `size` points.
    static func scanlines(_ size: CGSize) -> CGImage {
        let cg = context(size, scale: 1)
        cg.setFillColor(NSColor(calibratedRed: 0, green: 0.02, blue: 0.05, alpha: 0.45).cgColor)
        var y: CGFloat = 0
        while y < size.height { cg.fill(CGRect(x: 0, y: y, width: size.width, height: 1)); y += 3 }
        return cg.makeImage()!
    }
}

/// A tiny seeded generator, so a plate looks the same every time it is
/// drawn.
struct SplitMix {
    var state: UInt64
    init(seed: UInt64) { state = seed &+ 0x9E37_79B9_7F4A_7C15 }
    mutating func next() -> UInt64 {
        state &+= 0x9E37_79B9_7F4A_7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
        z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
        return z ^ (z >> 31)
    }
    mutating func unit() -> Double { Double(next() >> 11) / Double(1 << 53) }
}
