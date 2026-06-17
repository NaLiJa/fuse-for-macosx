#!/usr/bin/env swift
//
// format-icon.swift — generate a file-format .icns matching FuseX's existing set.
//
// Usage:  swift fusepb/format-icon.swift <ext> [LABEL]
//   <ext>    output basename, e.g. "udi" -> fusepb/resources/udi.icns
//   LABEL    text drawn on the icon (default: <ext> uppercased)
//
// Method: every existing format icon is fusepb/resources/blank.icns plus an
// uppercase label, so this script reuses blank.icns as the base and stamps the
// matching label onto it.
//
import AppKit

let fontName = "LucidaGrande-Bold"
let inkColor = NSColor(red: 0x62/255.0, green: 0x62/255.0, blue: 0x62/255.0, alpha: 1)

// One entry per .icns layer. `pt` is the label point size and `textTop` the gap
// from the top edge to the top of the text rect. Both are specific to
// LucidaGrande-Bold — recompute them for any other font.
struct Layer { let px: Int; let pt: CGFloat; let textTop: CGFloat }
let layers = [
  Layer(px: 16,  pt: 8.30,  textTop: 7.97),
  Layer(px: 32,  pt: 8.30,  textTop: 20.97),
  Layer(px: 128, pt: 19.37, textTop: 95.27),
]

func die(_ msg: String) -> Never {
  FileHandle.standardError.write(Data(("error: " + msg + "\n").utf8))
  exit(1)
}

func iconutil(_ args: String...) {
  let p = Process()
  p.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
  p.arguments = args
  try? p.run(); p.waitUntilExit()
  if p.terminationStatus != 0 { die("iconutil \(args.joined(separator: " ")) failed") }
}

// Draw `label` onto the base PNG for one layer and write the result. The label is
// centered horizontally; its rect starts `textTop` pixels below the top edge.
func stamp(_ label: String, base basePNG: URL, layer: Layer, to outPNG: URL) {
  let side = CGFloat(layer.px)
  guard let base = NSImage(contentsOf: basePNG) else { die("missing base \(basePNG.lastPathComponent)") }
  guard let out = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: layer.px, pixelsHigh: layer.px,
    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
    colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)
  else { die("cannot allocate \(layer.px)px bitmap") }
  guard let font = NSFont(name: fontName, size: layer.pt) else { die("font \(fontName) unavailable") }

  NSGraphicsContext.saveGraphicsState()
  defer { NSGraphicsContext.restoreGraphicsState() }
  // Top-left origin (flipped) context so textTop measures from the top.
  guard let ctx = NSGraphicsContext(bitmapImageRep: out) else { die("cannot create drawing context") }
  let cg = ctx.cgContext
  NSGraphicsContext.current = NSGraphicsContext(cgContext: cg, flipped: true)
  cg.translateBy(x: 0, y: side); cg.scaleBy(x: 1, y: -1)

  base.draw(in: NSRect(x: 0, y: 0, width: side, height: side))

  let centered = NSMutableParagraphStyle(); centered.alignment = .center
  let attrs: [NSAttributedString.Key: Any] = [
    .font: font,
    .foregroundColor: inkColor,
    .paragraphStyle: centered]
  (label as NSString).draw(in: NSRect(x: 0, y: layer.textTop, width: side, height: side), withAttributes: attrs)

  guard let png = out.representation(using: .png, properties: [:]) else { die("PNG encode failed for \(outPNG.lastPathComponent)") }
  do { try png.write(to: outPNG) } catch { die("write \(outPNG.lastPathComponent): \(error.localizedDescription)") }
}

// --- run ----------------------------------------------------------------------
let argv = CommandLine.arguments
guard argv.count >= 2 else { die("usage: swift format-icon.swift <ext> [LABEL]") }
let ext = argv[1]
let rawLabel = argv.count >= 3 ? argv[2] : ext
let label = (rawLabel.isEmpty ? ext : rawLabel).uppercased()

let fm = FileManager.default
// blank.icns anchors the repo root, so the script works from any CWD.
var root = URL(fileURLWithPath: fm.currentDirectoryPath)
while !fm.fileExists(atPath: root.appendingPathComponent("fusepb/resources/blank.icns").path) {
  let parent = root.deletingLastPathComponent()
  if parent.path == root.path { die("fusepb/resources/blank.icns not found above \(fm.currentDirectoryPath)") }
  root = parent
}
let resources = root.appendingPathComponent("fusepb/resources")

let workDir = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("fmticon-\(ext)")
try? fm.removeItem(at: workDir)
let baseIconset = workDir.appendingPathComponent("base.iconset")
let outputIconset = workDir.appendingPathComponent("\(ext).iconset")
do { try fm.createDirectory(at: outputIconset, withIntermediateDirectories: true) }
catch { die("cannot create \(outputIconset.path): \(error.localizedDescription)") }

iconutil("-c", "iconset", "-o", baseIconset.path, resources.appendingPathComponent("blank.icns").path)
for layer in layers {
  let name = "icon_\(layer.px)x\(layer.px).png"
  stamp(label, base: baseIconset.appendingPathComponent(name), layer: layer, to: outputIconset.appendingPathComponent(name))
}
let outputIcon = resources.appendingPathComponent("\(ext).icns")
iconutil("-c", "icns", "-o", outputIcon.path, outputIconset.path)
try? fm.removeItem(at: workDir)
print("wrote \(outputIcon.path)")
