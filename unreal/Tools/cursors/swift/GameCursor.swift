import AppKit

/// The game's own mouse pointer: an angular green
/// arrow with a dark edge and a soft glow. `select` is the same arrow lit
/// brighter with a ring, shown over one of the player's buildings or units;
/// `enemy` is red, over the other side's; `drive` is cyan with a cockpit
/// reticle, over a unit the player can take the controls of.
enum GameCursor {
    enum Kind { case normal, select, enemy, drive }

    static let normal = make(lit: false)
    static let select = make(lit: true)
    static let enemy = make(lit: true, tint: NSColor(calibratedRed: 1, green: 0.35, blue: 0.3, alpha: 1))
    static let drive = make(lit: true, tint: NSColor(calibratedRed: 0.35, green: 0.9, blue: 1, alpha: 1), reticle: true)

    static func cursor(_ k: Kind) -> NSCursor {
        switch k {
        case .normal: normal
        case .select: select
        case .enemy: enemy
        case .drive: drive
        }
    }

    private static func make(lit: Bool, tint: NSColor? = nil, reticle: Bool = false) -> NSCursor {
        let size = NSSize(width: 32, height: 32)
        let image = NSImage(size: size, flipped: true) { _ in
            guard let cg = NSGraphicsContext.current?.cgContext else { return false }
            // The arrow, tip at (3, 2), in a y-down frame.
            let arrow = CGMutablePath()
            arrow.move(to: CGPoint(x: 3, y: 2))
            arrow.addLine(to: CGPoint(x: 21, y: 16))
            arrow.addLine(to: CGPoint(x: 13.5, y: 17.5))
            arrow.addLine(to: CGPoint(x: 18, y: 26))
            arrow.addLine(to: CGPoint(x: 14.5, y: 28))
            arrow.addLine(to: CGPoint(x: 10, y: 19.5))
            arrow.addLine(to: CGPoint(x: 4.5, y: 24))
            arrow.closeSubpath()
            let green = tint ?? (lit ? NSColor(calibratedRed: 0.55, green: 1, blue: 0.6, alpha: 1)
                : NSColor(calibratedRed: 0.3, green: 0.85, blue: 0.4, alpha: 1))
            // Glow.
            cg.saveGState()
            cg.setShadow(offset: .zero, blur: lit ? 6 : 4, color: green.withAlphaComponent(lit ? 0.9 : 0.6).cgColor)
            cg.addPath(arrow)
            cg.setFillColor(NSColor(calibratedRed: 0.02, green: 0.12, blue: 0.05, alpha: 1).cgColor)
            cg.fillPath()
            cg.restoreGState()
            // Body: a bright bevelled face over the dark edge.
            let face = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(),
                                  colors: [green.cgColor,
                                           (green.blended(withFraction: 0.55, of: .black) ?? green).cgColor] as CFArray,
                                  locations: [0, 1])!
            cg.saveGState()
            cg.addPath(arrow)
            cg.clip()
            cg.drawLinearGradient(face, start: CGPoint(x: 3, y: 2), end: CGPoint(x: 16, y: 28), options: [])
            // A bright spine down the arrow.
            cg.setStrokeColor(NSColor(white: 1, alpha: lit ? 0.75 : 0.5).cgColor)
            cg.setLineWidth(1)
            cg.move(to: CGPoint(x: 4.5, y: 4.5))
            cg.addLine(to: CGPoint(x: 11.5, y: 17))
            cg.strokePath()
            cg.restoreGState()
            cg.addPath(arrow)
            cg.setStrokeColor(NSColor(calibratedRed: 0.02, green: 0.1, blue: 0.04, alpha: 1).cgColor)
            cg.setLineWidth(1.2)
            cg.strokePath()
            if reticle {
                // A cockpit sight by the tip: four corner brackets round a dot.
                cg.setStrokeColor(green.cgColor)
                cg.setLineWidth(1.6)
                let r = CGRect(x: 19, y: 2, width: 11, height: 11), k: CGFloat = 3.5
                for (x, y, dx, dy) in [(r.minX, r.minY, k, k), (r.maxX, r.minY, -k, k),
                                       (r.minX, r.maxY, k, -k), (r.maxX, r.maxY, -k, -k)] {
                    cg.move(to: CGPoint(x: x + dx, y: y))
                    cg.addLine(to: CGPoint(x: x, y: y))
                    cg.addLine(to: CGPoint(x: x, y: y + dy))
                }
                cg.strokePath()
                cg.setFillColor(green.cgColor)
                cg.fillEllipse(in: CGRect(x: r.midX - 1.5, y: r.midY - 1.5, width: 3, height: 3))
            } else if lit {
                // A small selection ring by the tip.
                cg.setStrokeColor(green.cgColor)
                cg.setLineWidth(1.6)
                cg.strokeEllipse(in: CGRect(x: 19, y: 3, width: 10, height: 6))
            }
            return true
        }
        return NSCursor(image: image, hotSpot: NSPoint(x: 3, y: 2))
    }
}
