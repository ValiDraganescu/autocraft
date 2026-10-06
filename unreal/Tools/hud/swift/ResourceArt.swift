import AppKit
import SpriteKit

/// The two resources as the HUD and the cockpit overlay draw them: a dark
/// nodule of Stardust Ore with an opal glint, and a silver droplet of
/// Metallic Hydrogen. Drawn once with Core Graphics into textures.
enum ResourceArt {
    /// Pixels on a side of each texture: sharp up to about 48 points at 2x.
    static let pixels = 96

    static let oreTexture = SKTexture(cgImage: ore(pixels))
    static let hydrogenTexture = SKTexture(cgImage: hydrogen(pixels))

    /// The ore nodule, `side` points across.
    static func oreSprite(side: CGFloat) -> SKSpriteNode {
        SKSpriteNode(texture: oreTexture, size: CGSize(width: side, height: side))
    }

    /// The MH droplet, `side` points across.
    static func hydrogenSprite(side: CGFloat) -> SKSpriteNode {
        SKSpriteNode(texture: hydrogenTexture, size: CGSize(width: side, height: side))
    }

    private static let space = CGColorSpace(name: CGColorSpace.sRGB)!

    private static func rgb(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> CGColor {
        CGColor(colorSpace: space, components: [r, g, b, a])!
    }

    /// Teal, violet and gold: the opal's play of colour.
    private static func opal(_ alpha: CGFloat = 1) -> CGGradient {
        CGGradient(colorsSpace: space,
                   colors: [rgb(0.1, 0.95, 0.8, alpha), rgb(0.62, 0.32, 1, alpha), rgb(1, 0.74, 0.2, alpha),
                            rgb(0.1, 0.95, 0.8, alpha)] as CFArray,
                   locations: [0, 0.4, 0.75, 1])!
    }

    /// A rough, rounded nodule of near-black rock. Opal patches catch the
    /// light on it, an opal rim picks it out of a dark panel, and a white
    /// glint sparkles at its shoulder.
    static func ore(_ n: Int) -> CGImage {
        let c = MaterialLibrary.context(n, n)
        let s = CGFloat(n)
        c.scaleBy(x: s, y: s)
        // The outline: a lumpy round, flatter underneath.
        let radii: [CGFloat] = [0.37, 0.33, 0.39, 0.35, 0.3, 0.33, 0.36, 0.3, 0.35]
        let pts = radii.enumerated().map { k, r -> CGPoint in
            let a = CGFloat(k) / CGFloat(radii.count) * 2 * .pi + 0.3
            return CGPoint(x: 0.5 + cos(a) * r, y: 0.47 + sin(a) * r * (sin(a) < 0 ? 0.8 : 0.95))
        }
        let path = CGMutablePath()
        func mid(_ a: CGPoint, _ b: CGPoint) -> CGPoint { CGPoint(x: (a.x + b.x) / 2, y: (a.y + b.y) / 2) }
        path.move(to: mid(pts[pts.count - 1], pts[0]))
        for k in pts.indices { path.addQuadCurve(to: mid(pts[k], pts[(k + 1) % pts.count]), control: pts[k]) }
        path.closeSubpath()

        // Body: warm near-black, lighter toward the upper left.
        c.saveGState()
        c.addPath(path); c.clip()
        let body = CGGradient(colorsSpace: space, colors: [rgb(0.3, 0.26, 0.24), rgb(0.07, 0.06, 0.055)] as CFArray,
                              locations: [0, 1])!
        c.drawRadialGradient(body, startCenter: CGPoint(x: 0.36, y: 0.66), startRadius: 0,
                             endCenter: CGPoint(x: 0.5, y: 0.47), endRadius: 0.45, options: [.drawsAfterEndLocation])
        func poly(_ p: [(CGFloat, CGFloat)]) {
            c.move(to: CGPoint(x: p[0].0, y: p[0].1))
            for q in p.dropFirst() { c.addLine(to: CGPoint(x: q.0, y: q.1)) }
            c.closePath()
        }
        // Facets: a lit plane on top, darker planes below and to the right.
        c.setFillColor(rgb(0.5, 0.45, 0.4, 0.28))
        poly([(0.2, 0.62), (0.5, 0.86), (0.78, 0.7), (0.5, 0.52)]); c.fillPath()
        c.setFillColor(rgb(0, 0, 0, 0.4))
        poly([(0.5, 0.52), (0.78, 0.7), (0.95, 0.4), (0.85, 0.05), (0.55, 0.15)]); c.fillPath()
        c.setFillColor(rgb(0, 0, 0, 0.25))
        poly([(0.05, 0.45), (0.2, 0.62), (0.5, 0.52), (0.55, 0.15), (0.2, 0.05)]); c.fillPath()
        // Opal flashes on the faces: angular chips of teal, violet and gold.
        let chips: [([(CGFloat, CGFloat)], CGPoint, CGPoint)] = [
            ([(0.24, 0.63), (0.4, 0.76), (0.5, 0.64), (0.36, 0.55)], CGPoint(x: 0.24, y: 0.76), CGPoint(x: 0.5, y: 0.55)),
            ([(0.52, 0.6), (0.6, 0.68), (0.66, 0.6), (0.58, 0.54)], CGPoint(x: 0.66, y: 0.68), CGPoint(x: 0.52, y: 0.54)),
            ([(0.62, 0.36), (0.74, 0.46), (0.82, 0.32), (0.7, 0.24)], CGPoint(x: 0.82, y: 0.46), CGPoint(x: 0.62, y: 0.24)),
            ([(0.2, 0.36), (0.3, 0.42), (0.34, 0.32), (0.25, 0.28)], CGPoint(x: 0.2, y: 0.42), CGPoint(x: 0.34, y: 0.28)),
        ]
        for (chip, from, to) in chips {
            c.saveGState()
            poly(chip); c.clip()
            c.drawLinearGradient(opal(0.95), start: from, end: to, options: [])
            c.restoreGState()
        }
        c.restoreGState()

        // Rim: a thin warm-grey edge all round, opal along the lit upper left.
        c.saveGState()
        c.addPath(path)
        c.setStrokeColor(rgb(0.42, 0.38, 0.35))
        c.setLineWidth(0.025)
        c.strokePath()
        c.addPath(path)
        c.setLineWidth(0.035)
        c.replacePathWithStrokedPath()
        c.clip()
        poly([(0, 1), (0, 0.25), (0.75, 1)]); c.clip()
        c.drawLinearGradient(opal(), start: CGPoint(x: 0.05, y: 0.3), end: CGPoint(x: 0.7, y: 0.95), options: [])
        c.restoreGState()

        // A four-point glint at the shoulder.
        c.saveGState()
        c.setShadow(offset: .zero, blur: s * 0.06, color: rgb(1, 1, 1, 0.9))
        c.setFillColor(rgb(1, 1, 0.95))
        let g = CGPoint(x: 0.7, y: 0.76), long: CGFloat = 0.11, thin: CGFloat = 0.022
        c.move(to: CGPoint(x: g.x, y: g.y + long))
        c.addLine(to: CGPoint(x: g.x + thin, y: g.y + thin)); c.addLine(to: CGPoint(x: g.x + long, y: g.y))
        c.addLine(to: CGPoint(x: g.x + thin, y: g.y - thin)); c.addLine(to: CGPoint(x: g.x, y: g.y - long))
        c.addLine(to: CGPoint(x: g.x - thin, y: g.y - thin)); c.addLine(to: CGPoint(x: g.x - long, y: g.y))
        c.addLine(to: CGPoint(x: g.x - thin, y: g.y + thin)); c.closePath()
        c.fillPath()
        c.restoreGState()
        return c.makeImage()!
    }

    /// A droplet of liquid silver with a cold blue-white glow round it: a
    /// mirror finish, bright above, a dark reflected band low across it.
    static func hydrogen(_ n: Int) -> CGImage {
        let c = MaterialLibrary.context(n, n)
        let s = CGFloat(n)
        c.scaleBy(x: s, y: s)
        let centre = CGPoint(x: 0.5, y: 0.37), r: CGFloat = 0.27
        let tip = CGPoint(x: 0.5, y: 0.93)
        let path = CGMutablePath()
        path.move(to: tip)
        path.addCurve(to: CGPoint(x: centre.x + r, y: centre.y), control1: CGPoint(x: 0.56, y: 0.8),
                      control2: CGPoint(x: centre.x + r, y: centre.y + 0.2))
        path.addArc(center: centre, radius: r, startAngle: 0, endAngle: .pi, clockwise: true)
        path.addCurve(to: tip, control1: CGPoint(x: centre.x - r, y: centre.y + 0.2), control2: CGPoint(x: 0.44, y: 0.8))
        path.closeSubpath()

        // Cold glow behind it.
        c.saveGState()
        c.setShadow(offset: .zero, blur: s * 0.1, color: rgb(0.6, 0.8, 1, 0.95))
        c.addPath(path)
        c.setFillColor(rgb(0.7, 0.78, 0.88))
        c.fillPath()
        c.restoreGState()

        // Mirror body: silver-white above, steel below.
        c.saveGState()
        c.addPath(path); c.clip()
        let body = CGGradient(colorsSpace: space,
                              colors: [rgb(0.98, 0.99, 1), rgb(0.78, 0.83, 0.9), rgb(0.42, 0.47, 0.56), rgb(0.82, 0.88, 0.96)] as CFArray,
                              locations: [0, 0.45, 0.72, 1])!
        c.drawLinearGradient(body, start: CGPoint(x: 0.3, y: 0.95), end: CGPoint(x: 0.62, y: 0.08), options: [])
        // The dark band of a reflected horizon, curving with the drop.
        c.setFillColor(rgb(0.16, 0.2, 0.28, 0.75))
        c.addEllipse(in: CGRect(x: 0.14, y: 0.2, width: 0.72, height: 0.16))
        c.fillPath()
        c.setFillColor(rgb(0.7, 0.85, 1, 0.55))
        c.addEllipse(in: CGRect(x: 0.2, y: 0.12, width: 0.6, height: 0.09))
        c.fillPath()
        // Highlight up on the shoulder.
        c.setFillColor(rgb(1, 1, 1, 0.95))
        c.addEllipse(in: CGRect(x: 0.33, y: 0.44, width: 0.1, height: 0.17))
        c.fillPath()
        c.restoreGState()

        // A thin cold rim.
        c.addPath(path)
        c.setStrokeColor(rgb(0.86, 0.94, 1))
        c.setLineWidth(0.025)
        c.strokePath()
        return c.makeImage()!
    }
}
