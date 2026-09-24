import AppKit
import CoreGraphics
import Foundation

private struct Resolution {
    let width: Int
    let height: Int

    var key: String { "\(width)x\(height)" }
    var title: String { "\(width) × \(height)" }
}

private struct CrosshairSettings {
    let style: Int
    let red: Int
    let green: Int
    let blue: Int
    let size: Double
    let thickness: Double
    let gap: Double
    let opacity: Int
    let useOpacity: Bool
    let dot: Bool
    let outline: Bool
    let outlineThickness: Double
    let useWeaponGap: Bool
    let fixedGap: Double
    let legacyScale: Int
    let dynamicSplitDistance: Double
    let dynamicInnerAlpha: Double
    let dynamicOuterAlpha: Double
    let dynamicSplitRatio: Double
    let sniperWidth: Int
    let sniperShowsInaccuracy: Bool
}

private final class CrosshairPreviewView: NSView {
    var settingsProvider: (() -> CrosshairSettings?)?

    override var intrinsicContentSize: NSSize { NSSize(width: 230, height: 230) }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        let background = NSBezierPath(roundedRect: bounds.insetBy(dx: 0.5, dy: 0.5), xRadius: 10, yRadius: 10)
        NSGraphicsContext.saveGraphicsState()
        background.addClip()

        NSColor(calibratedRed: 0.20, green: 0.25, blue: 0.29, alpha: 1).setFill()
        bounds.fill()
        NSColor(calibratedRed: 0.44, green: 0.38, blue: 0.30, alpha: 1).setFill()
        NSRect(x: bounds.minX, y: bounds.minY, width: bounds.width, height: bounds.height * 0.47).fill()
        NSColor(calibratedRed: 0.31, green: 0.34, blue: 0.33, alpha: 1).setFill()
        NSRect(x: bounds.minX + bounds.width * 0.08,
               y: bounds.minY + bounds.height * 0.25,
               width: bounds.width * 0.28,
               height: bounds.height * 0.42).fill()
        NSColor(calibratedRed: 0.59, green: 0.50, blue: 0.38, alpha: 1).setFill()
        NSRect(x: bounds.maxX - bounds.width * 0.31,
               y: bounds.minY + bounds.height * 0.18,
               width: bounds.width * 0.23,
               height: bounds.height * 0.35).fill()

        guard let settings = settingsProvider?() else {
            NSGraphicsContext.restoreGraphicsState()
            drawBorder()
            return
        }

        let previewScale = min(bounds.width, bounds.height) / 180
        let baseAlpha = settings.useOpacity ? CGFloat(settings.opacity) / 100 : 1
        let color = NSColor(srgbRed: CGFloat(settings.red) / 255,
                            green: CGFloat(settings.green) / 255,
                            blue: CGFloat(settings.blue) / 255,
                            alpha: 1)
        let center = NSPoint(x: bounds.midX.rounded(), y: bounds.midY.rounded())
        var length = CGFloat(settings.size) * 2.1 * previewScale
        var thickness = max(0.75, CGFloat(settings.thickness) * 2 * previewScale)
        let selectedGap = settings.style == 1 ? settings.fixedGap : settings.gap
        var gap = max(0, (4 + CGFloat(selectedGap) * 1.5) * previewScale)

        if settings.useWeaponGap {
            gap += 2.5 * previewScale
        }
        if settings.style <= 1 && settings.legacyScale > 0 {
            let legacyMultiplier = min(max(768 / CGFloat(settings.legacyScale), 0.25), 3)
            length *= legacyMultiplier
            thickness *= legacyMultiplier
            gap *= legacyMultiplier
        }

        let maxRadius = min(bounds.width, bounds.height) * 0.39
        gap = min(gap, maxRadius - 1)
        let outlineWidth = max(0.5, CGFloat(settings.outlineThickness) * previewScale)

        if settings.style == 2 && length > 0 {
            let splitDistance = min(CGFloat(settings.dynamicSplitDistance) * previewScale, maxRadius * 0.38)
            length = min(length, max(0, maxRadius - gap - splitDistance))
            let outerLength = length * CGFloat(settings.dynamicSplitRatio)
            let innerLength = max(0, length - outerLength)
            drawArms(center: center,
                     offset: gap,
                     length: innerLength,
                     thickness: thickness,
                     color: color,
                     alpha: baseAlpha * CGFloat(settings.dynamicInnerAlpha),
                     outlined: settings.outline,
                     outlineWidth: outlineWidth)
            drawArms(center: center,
                     offset: gap + innerLength + splitDistance,
                     length: outerLength,
                     thickness: thickness,
                     color: color,
                     alpha: baseAlpha * CGFloat(settings.dynamicOuterAlpha),
                     outlined: settings.outline,
                     outlineWidth: outlineWidth)
        } else if length > 0 {
            length = min(length, max(0, maxRadius - gap))
            drawArms(center: center,
                     offset: gap,
                     length: length,
                     thickness: thickness,
                     color: color,
                     alpha: baseAlpha,
                     outlined: settings.outline,
                     outlineWidth: outlineWidth)
        }

        if settings.dot {
            let dotSize = max(1.5, thickness)
            let dot = NSRect(x: center.x - dotSize / 2,
                             y: center.y - dotSize / 2,
                             width: dotSize,
                             height: dotSize)
            drawSegments([dot], color: color, alpha: baseAlpha,
                         outlined: settings.outline, outlineWidth: outlineWidth)
        }

        let caption = settings.style == 2 ? "MOVEMENT / SPLIT PREVIEW" : "STANDING PREVIEW"
        let attributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 9, weight: .semibold),
            .foregroundColor: NSColor.white.withAlphaComponent(0.62),
        ]
        (caption as NSString).draw(at: NSPoint(x: bounds.minX + 10, y: bounds.maxY - 22),
                                   withAttributes: attributes)

        NSGraphicsContext.restoreGraphicsState()
        drawBorder()
    }

    private func drawArms(center: NSPoint,
                          offset: CGFloat,
                          length: CGFloat,
                          thickness: CGFloat,
                          color: NSColor,
                          alpha: CGFloat,
                          outlined: Bool,
                          outlineWidth: CGFloat) {
        guard length > 0, alpha > 0 else { return }
        let halfThickness = thickness / 2
        let segments = [
            NSRect(x: center.x - offset - length, y: center.y - halfThickness,
                   width: length, height: thickness),
            NSRect(x: center.x + offset, y: center.y - halfThickness,
                   width: length, height: thickness),
            NSRect(x: center.x - halfThickness, y: center.y + offset,
                   width: thickness, height: length),
            NSRect(x: center.x - halfThickness, y: center.y - offset - length,
                   width: thickness, height: length),
        ]
        drawSegments(segments, color: color, alpha: alpha,
                     outlined: outlined, outlineWidth: outlineWidth)
    }

    private func drawSegments(_ segments: [NSRect],
                              color: NSColor,
                              alpha: CGFloat,
                              outlined: Bool,
                              outlineWidth: CGFloat) {
        let clampedAlpha = min(max(alpha, 0), 1)
        if outlined {
            NSColor.black.withAlphaComponent(clampedAlpha).setFill()
            for segment in segments {
                segment.insetBy(dx: -outlineWidth, dy: -outlineWidth).fill()
            }
        }
        color.withAlphaComponent(clampedAlpha).setFill()
        for segment in segments { segment.fill() }
    }

    private func drawBorder() {
        let border = NSBezierPath(roundedRect: bounds.insetBy(dx: 0.5, dy: 0.5), xRadius: 10, yRadius: 10)
        border.lineWidth = 1
        NSColor.separatorColor.setStroke()
        border.stroke()
    }
}

@main
final class LauncherApp: NSObject, NSApplicationDelegate {
    private let defaults = UserDefaults.standard
    private let mapPopup = NSPopUpButton()
    private let botCountPopup = NSPopUpButton()
    private let difficultyPopup = NSPopUpButton()
    private let resolutionPopup = NSPopUpButton()
    private let fullscreenCheckbox = NSButton(checkboxWithTitle: "Fullscreen", target: nil, action: nil)
    private let graphicsButton = NSButton(title: "Graphics Settings…", target: nil, action: nil)
    private let crosshairButton = NSButton(title: "Crosshair Settings…", target: nil, action: nil)
    private let qualityPopup = NSPopUpButton()
    // Custom is derived from the saved individual settings, which remain authoritative.
    private let qualityPresets = [
        [0, 0, 0, 0, 0], // Low
        [1, 1, 0, 1, 1], // Medium
        [2, 3, 2, 2, 1], // High
        [3, 5, 2, 3, 1], // Very High
    ]
    private let texturePopup = NSPopUpButton()
    private let filteringPopup = NSPopUpButton()
    private let antialiasingPopup = NSPopUpButton()
    private let shadowsPopup = NSPopUpButton()
    private let shadersPopup = NSPopUpButton()
    private let vsyncCheckbox = NSButton(checkboxWithTitle: "Limit frames to display refresh", target: nil, action: nil)
    private let crosshairStylePopup = NSPopUpButton()
    private let crosshairColorWell = NSColorWell()
    private let crosshairSizeField = NSTextField()
    private let crosshairThicknessField = NSTextField()
    private let crosshairGapField = NSTextField()
    private let crosshairOpacityField = NSTextField()
    private let crosshairUseOpacityCheckbox = NSButton(checkboxWithTitle: "Use custom opacity", target: nil, action: nil)
    private let crosshairDotCheckbox = NSButton(checkboxWithTitle: "Show center dot", target: nil, action: nil)
    private let crosshairOutlineCheckbox = NSButton(checkboxWithTitle: "Draw black outline", target: nil, action: nil)
    private let crosshairOutlineThicknessField = NSTextField()
    private let crosshairWeaponGapCheckbox = NSButton(checkboxWithTitle: "Adjust gap for equipped weapon", target: nil, action: nil)
    private let crosshairFixedGapField = NSTextField()
    private let crosshairLegacyScaleField = NSTextField()
    private let crosshairDynamicSplitDistanceField = NSTextField()
    private let crosshairDynamicInnerAlphaField = NSTextField()
    private let crosshairDynamicOuterAlphaField = NSTextField()
    private let crosshairDynamicSplitRatioField = NSTextField()
    private let crosshairSniperWidthField = NSTextField()
    private let crosshairSniperInaccuracyCheckbox = NSButton(checkboxWithTitle: "Include standing inaccuracy in sniper blur", target: nil, action: nil)
    private let crosshairPreview = CrosshairPreviewView()
    private var crosshairSteppers: [ObjectIdentifier: NSStepper] = [:]
    private var crosshairFieldsByStepper: [ObjectIdentifier: NSTextField] = [:]
    private let launchButton = NSButton(title: "Launch Game", target: nil, action: nil)
    private let statusLabel = NSTextField(labelWithString: "Choose settings, then launch.")
    private var resolutions: [Resolution] = []
    private var window: NSWindow!
    private var graphicsWindow: NSPanel?
    private var crosshairWindow: NSPanel?
    private var gameProcess: Process?

    private var gameRoot: URL {
        Bundle.main.bundleURL.deletingLastPathComponent().standardizedFileURL
    }

    static func main() {
        let app = NSApplication.shared
        let delegate = LauncherApp()
        app.delegate = delegate
        app.setActivationPolicy(.regular)
        app.run()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        buildMenus()
        restoreSettings()
        buildWindow()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        loadMaps()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    private func buildMenus() {
        for count in 0...20 {
            botCountPopup.addItem(withTitle: count == 0 ? "0 (solo)" : "\(count)")
        }
        difficultyPopup.addItems(withTitles: ["Easy", "Normal", "Hard", "Expert"])
        qualityPopup.addItems(withTitles: ["Custom", "Low", "Medium", "High", "Very High"])
        texturePopup.addItems(withTitles: ["Low", "Medium", "High", "Very High"])
        filteringPopup.addItems(withTitles: ["Bilinear", "Trilinear", "Anisotropic 2×", "Anisotropic 4×", "Anisotropic 8×", "Anisotropic 16×"])
        antialiasingPopup.addItems(withTitles: ["Off", "2× MSAA", "4× MSAA"])
        shadowsPopup.addItems(withTitles: ["Off", "Low", "Medium", "High"])
        shadersPopup.addItems(withTitles: ["Low", "High"])
        crosshairStylePopup.addItems(withTitles: [
            "Default",
            "Default Static",
            "Accurate Split",
            "Accurate Dynamic",
            "Classic Static",
            "Classic Dynamic",
        ])
        configureNumericField(crosshairSizeField, minimum: 0, maximum: 100, step: 0.5, decimalPlaces: 2)
        configureNumericField(crosshairThicknessField, minimum: 0, maximum: 20, step: 0.1, decimalPlaces: 2)
        configureNumericField(crosshairGapField, minimum: -100, maximum: 100, step: 0.5, decimalPlaces: 2)
        configureNumericField(crosshairOpacityField, minimum: 0, maximum: 100, step: 1, decimalPlaces: 0)
        configureNumericField(crosshairOutlineThicknessField, minimum: 0.1, maximum: 3, step: 0.1, decimalPlaces: 2)
        configureNumericField(crosshairFixedGapField, minimum: -100, maximum: 100, step: 0.5, decimalPlaces: 2)
        configureNumericField(crosshairLegacyScaleField, minimum: 0, maximum: 5000, step: 1, decimalPlaces: 0)
        configureNumericField(crosshairDynamicSplitDistanceField, minimum: 0, maximum: 100, step: 0.5, decimalPlaces: 2)
        configureNumericField(crosshairDynamicInnerAlphaField, minimum: 0, maximum: 1, step: 0.05, decimalPlaces: 2)
        configureNumericField(crosshairDynamicOuterAlphaField, minimum: 0.3, maximum: 1, step: 0.05, decimalPlaces: 2)
        configureNumericField(crosshairDynamicSplitRatioField, minimum: 0, maximum: 1, step: 0.05, decimalPlaces: 2)
        configureNumericField(crosshairSniperWidthField, minimum: 1, maximum: 20, step: 1, decimalPlaces: 0)

        let displayWidth = Int(CGDisplayPixelsWide(CGMainDisplayID()))
        let displayHeight = Int(CGDisplayPixelsHigh(CGMainDisplayID()))
        let presets = [
            Resolution(width: 1024, height: 768),
            Resolution(width: 1280, height: 720),
            Resolution(width: 1280, height: 800),
            Resolution(width: 1440, height: 900),
            Resolution(width: 1600, height: 900),
            Resolution(width: 1920, height: 1080),
            Resolution(width: 2560, height: 1440),
        ]
        resolutions = presets.filter { $0.width <= displayWidth && $0.height <= displayHeight }
        let current = Resolution(width: displayWidth, height: displayHeight)
        if !resolutions.contains(where: { $0.key == current.key }) {
            resolutions.append(current)
        }
        if resolutions.isEmpty {
            resolutions = [current]
        }
        resolutionPopup.addItems(withTitles: resolutions.map(\.title))
    }

    private func loadMaps() {
        let mapsDirectory = gameRoot.appendingPathComponent("csgo/maps", isDirectory: true)
        let preferredMap = defaults.string(forKey: "map") ?? "de_dust2"
        launchButton.isEnabled = false
        statusLabel.stringValue = "Finding installed maps…"

        // The installer records the available maps inside the signed bundle.
        // Reading the sibling maps directory through File Provider can otherwise
        // block an app's main thread indefinitely on recent macOS versions.
        if let manifestURL = Bundle.main.url(forResource: "maps", withExtension: "txt"),
           let manifest = try? String(contentsOf: manifestURL, encoding: .utf8) {
            let maps = manifest.split(whereSeparator: \.isNewline).map(String.init).sorted()
            if !maps.isEmpty {
                mapPopup.addItems(withTitles: maps)
                mapPopup.selectItem(withTitle: preferredMap)
                if mapPopup.selectedItem == nil {
                    mapPopup.selectItem(at: 0)
                }
                launchButton.isEnabled = true
                statusLabel.stringValue = "Choose settings, then launch."
                return
            }
        }

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            do {
                let mapFiles = try FileManager.default.contentsOfDirectory(atPath: mapsDirectory.path)
                let maps = mapFiles.filter {
                    URL(fileURLWithPath: $0).pathExtension.lowercased() == "bsp"
                }.map {
                    URL(fileURLWithPath: $0).deletingPathExtension().lastPathComponent
                }.sorted()

                DispatchQueue.main.async {
                    guard let self = self else { return }
                    self.mapPopup.removeAllItems()
                    self.mapPopup.addItems(withTitles: maps)
                    self.mapPopup.selectItem(withTitle: preferredMap)
                    if self.mapPopup.selectedItem == nil {
                        self.mapPopup.selectItem(at: 0)
                    }
                    self.launchButton.isEnabled = !maps.isEmpty
                    self.statusLabel.stringValue = maps.isEmpty
                        ? "No .bsp maps found in \(mapsDirectory.path)."
                        : "Choose settings, then launch."
                }
            } catch {
                DispatchQueue.main.async {
                    guard let self = self else { return }
                    self.launchButton.isEnabled = false
                    self.statusLabel.stringValue = "Could not read \(mapsDirectory.path): \(error.localizedDescription)"
                }
            }
        }
    }

    private func restoreSettings() {
        let botCount = defaults.object(forKey: "botCount") == nil ? 8 : defaults.integer(forKey: "botCount")
        botCountPopup.selectItem(at: min(max(botCount, 0), 20))
        let difficulty = defaults.object(forKey: "difficulty") == nil ? 1 : defaults.integer(forKey: "difficulty")
        difficultyPopup.selectItem(at: min(max(difficulty, 0), 3))

        let resolutionKey = defaults.string(forKey: "resolution") ?? "1280x720"
        let resolutionIndex = resolutions.firstIndex(where: { $0.key == resolutionKey }) ?? 0
        resolutionPopup.selectItem(at: resolutionIndex)
        fullscreenCheckbox.state = defaults.bool(forKey: "fullscreen") ? .on : .off
        texturePopup.selectItem(at: storedInteger("textureDetail", defaultValue: 2, range: 0...3))
        filteringPopup.selectItem(at: storedInteger("textureFiltering", defaultValue: 1, range: 0...5))
        antialiasingPopup.selectItem(at: storedInteger("antialiasing", defaultValue: 0, range: 0...2))
        shadowsPopup.selectItem(at: storedInteger("shadows", defaultValue: 1, range: 0...3))
        shadersPopup.selectItem(at: storedInteger("shaderDetail", defaultValue: 1, range: 0...1))
        vsyncCheckbox.state = defaults.bool(forKey: "vsync") ? .on : .off
        crosshairStylePopup.selectItem(at: storedInteger("crosshairStyle", defaultValue: 4, range: 0...5))
        let red = storedInteger("red", defaultValue: 50, range: 0...255)
        let green = storedInteger("green", defaultValue: 250, range: 0...255)
        let blue = storedInteger("blue", defaultValue: 50, range: 0...255)
        crosshairColorWell.color = NSColor(srgbRed: CGFloat(red) / 255,
                                           green: CGFloat(green) / 255,
                                           blue: CGFloat(blue) / 255,
                                           alpha: 1)
        setNumericField(crosshairSizeField, value: storedDouble("size", defaultValue: 5, range: 0...100))
        setNumericField(crosshairThicknessField, value: storedDouble("thickness", defaultValue: 0.5, range: 0...20))
        setNumericField(crosshairGapField, value: storedDouble("gap", defaultValue: 1, range: -100...100))
        setNumericField(crosshairOpacityField, value: storedDouble("opacity", defaultValue: 78, range: 0...100))
        crosshairUseOpacityCheckbox.state = storedBool("crosshairUseOpacity", defaultValue: true) ? .on : .off
        crosshairDotCheckbox.state = storedBool("dot", defaultValue: false) ? .on : .off
        crosshairOutlineCheckbox.state = storedBool("outline", defaultValue: true) ? .on : .off
        setNumericField(crosshairOutlineThicknessField, value: storedDouble("crosshairOutlineThickness", defaultValue: 1, range: 0.1...3))
        crosshairWeaponGapCheckbox.state = storedBool("crosshairWeaponGap", defaultValue: false) ? .on : .off
        setNumericField(crosshairFixedGapField, value: storedDouble("crosshairFixedGap", defaultValue: 3, range: -100...100))
        setNumericField(crosshairLegacyScaleField, value: storedDouble("crosshairLegacyScale", defaultValue: 0, range: 0...5000))
        setNumericField(crosshairDynamicSplitDistanceField, value: storedDouble("crosshairDynamicSplitDistance", defaultValue: 7, range: 0...100))
        setNumericField(crosshairDynamicInnerAlphaField, value: storedDouble("crosshairDynamicInnerAlpha", defaultValue: 1, range: 0...1))
        setNumericField(crosshairDynamicOuterAlphaField, value: storedDouble("crosshairDynamicOuterAlpha", defaultValue: 0.5, range: 0.3...1))
        setNumericField(crosshairDynamicSplitRatioField, value: storedDouble("crosshairDynamicSplitRatio", defaultValue: 0.35, range: 0...1))
        setNumericField(crosshairSniperWidthField, value: storedDouble("crosshairSniperWidth", defaultValue: 1, range: 1...20))
        crosshairSniperInaccuracyCheckbox.state = storedBool("crosshairSniperInaccuracy", defaultValue: false) ? .on : .off
        syncGraphicsQuality()
    }

    private func storedInteger(_ key: String, defaultValue: Int, range: ClosedRange<Int>) -> Int {
        guard defaults.object(forKey: key) != nil else { return defaultValue }
        return min(max(defaults.integer(forKey: key), range.lowerBound), range.upperBound)
    }

    private func storedDouble(_ key: String, defaultValue: Double, range: ClosedRange<Double>) -> Double {
        guard defaults.object(forKey: key) != nil else { return defaultValue }
        return min(max(defaults.double(forKey: key), range.lowerBound), range.upperBound)
    }

    private func storedBool(_ key: String, defaultValue: Bool) -> Bool {
        guard defaults.object(forKey: key) != nil else { return defaultValue }
        return defaults.bool(forKey: key)
    }

    private func buildWindow() {
        for control in [mapPopup, botCountPopup, difficultyPopup, resolutionPopup, qualityPopup] {
            control.widthAnchor.constraint(equalToConstant: 359).isActive = true
        }
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 550, height: 470),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "CS:GO Mac Launcher"
        window.center()

        let content = NSView()
        window.contentView = content
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 24),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -24),
            stack.topAnchor.constraint(equalTo: content.topAnchor, constant: 20),
        ])

        stack.addArrangedSubview(sectionLabel("Match"))
        stack.addArrangedSubview(row("Map", control: mapPopup))
        stack.addArrangedSubview(row("Bots", control: botCountPopup))
        botCountPopup.toolTip = "The launcher reserves enough player slots for exactly this many bots plus you."
        stack.addArrangedSubview(row("Bot difficulty", control: difficultyPopup))
        stack.addArrangedSubview(row("Resolution", control: resolutionPopup))
        stack.addArrangedSubview(row("Display", control: fullscreenCheckbox))
        qualityPopup.target = self
        qualityPopup.action = #selector(graphicsQualityChanged)
        stack.addArrangedSubview(row("Quality", control: qualityPopup))
        graphicsButton.target = self
        graphicsButton.action = #selector(showGraphicsSettings)
        stack.addArrangedSubview(row("Graphics", control: graphicsButton))
        crosshairButton.target = self
        crosshairButton.action = #selector(showCrosshairSettings)
        stack.addArrangedSubview(row("Crosshair", control: crosshairButton))

        launchButton.bezelStyle = .rounded
        launchButton.keyEquivalent = "\r"
        launchButton.target = self
        launchButton.action = #selector(launchGame)
        stack.addArrangedSubview(launchButton)
        statusLabel.textColor = .secondaryLabelColor
        stack.addArrangedSubview(statusLabel)

        for control in [mapPopup, botCountPopup, difficultyPopup, resolutionPopup, fullscreenCheckbox] {
            control.target = self
            control.action = #selector(settingsChanged)
        }
    }

    private func sectionLabel(_ title: String) -> NSTextField {
        let label = NSTextField(labelWithString: title)
        label.font = NSFont.boldSystemFont(ofSize: 15)
        return label
    }

    private func row(_ title: String, control: NSView) -> NSStackView {
        let label = NSTextField(labelWithString: title)
        label.alignment = .right
        label.widthAnchor.constraint(equalToConstant: 105).isActive = true
        let row = NSStackView(views: [label, control])
        row.orientation = .horizontal
        row.alignment = .centerY
        row.spacing = 14
        row.heightAnchor.constraint(greaterThanOrEqualToConstant: 28).isActive = true
        return row
    }

    private func crosshairRow(_ title: String, control: NSView) -> NSStackView {
        let label = NSTextField(labelWithString: title)
        label.alignment = .right
        label.widthAnchor.constraint(equalToConstant: 220).isActive = true
        let row = NSStackView(views: [label, control])
        row.orientation = .horizontal
        row.alignment = .centerY
        row.spacing = 14
        row.heightAnchor.constraint(greaterThanOrEqualToConstant: 28).isActive = true
        return row
    }

    private func configureNumericField(_ field: NSTextField,
                                       minimum: Double,
                                       maximum: Double,
                                       step: Double,
                                       decimalPlaces: Int) {
        let formatter = NumberFormatter()
        formatter.numberStyle = .decimal
        formatter.minimum = NSNumber(value: minimum)
        formatter.maximum = NSNumber(value: maximum)
        formatter.allowsFloats = decimalPlaces > 0
        formatter.minimumFractionDigits = 0
        formatter.maximumFractionDigits = decimalPlaces
        field.formatter = formatter
        field.alignment = .right
        field.widthAnchor.constraint(equalToConstant: 110).isActive = true
        field.target = self
        field.action = #selector(crosshairSettingsChanged)

        let stepper = NSStepper()
        stepper.minValue = minimum
        stepper.maxValue = maximum
        stepper.increment = step
        stepper.valueWraps = false
        stepper.autorepeat = true
        stepper.toolTip = "Use the arrows to adjust by \(commandNumber(step))."
        stepper.target = self
        stepper.action = #selector(crosshairStepperChanged)
        crosshairSteppers[ObjectIdentifier(field)] = stepper
        crosshairFieldsByStepper[ObjectIdentifier(stepper)] = field
    }

    private func numericControl(for field: NSTextField) -> NSView {
        guard let stepper = crosshairSteppers[ObjectIdentifier(field)] else { return field }
        let control = NSStackView(views: [field, stepper])
        control.orientation = .horizontal
        control.alignment = .centerY
        control.spacing = 4
        return control
    }

    private func setNumericField(_ field: NSTextField, value: Double) {
        field.stringValue = commandNumber(value)
        crosshairSteppers[ObjectIdentifier(field)]?.doubleValue = value
    }

    private func numericValue(_ field: NSTextField,
                              defaultValue: Double,
                              range: ClosedRange<Double>,
                              decimalPlaces: Int = 2) -> Double {
        var value = field.stringValue.isEmpty ? defaultValue : field.doubleValue
        if !value.isFinite { value = defaultValue }
        value = min(max(value, range.lowerBound), range.upperBound)
        let precision = pow(10, Double(decimalPlaces))
        value = (value * precision).rounded() / precision
        setNumericField(field, value: value)
        return value
    }

    private func commandNumber(_ value: Double) -> String {
        var text = String(format: "%.2f", locale: Locale(identifier: "en_US_POSIX"), value)
        while text.contains(".") && text.last == "0" { text.removeLast() }
        if text.last == "." { text.removeLast() }
        return text
    }

    @objc private func crosshairStepperChanged(_ sender: NSStepper) {
        guard let field = crosshairFieldsByStepper[ObjectIdentifier(sender)] else { return }
        setNumericField(field, value: sender.doubleValue)
        saveSettings()
        crosshairPreview.needsDisplay = true
    }

    @objc private func showCrosshairSettings() {
        if let crosshairWindow {
            crosshairPreview.needsDisplay = true
            crosshairWindow.makeKeyAndOrderFront(nil)
            return
        }

        let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 900, height: 520),
                            styleMask: [.titled, .closable, .utilityWindow], backing: .buffered, defer: false)
        panel.title = "Crosshair Settings"
        panel.isFloatingPanel = false
        panel.hidesOnDeactivate = false
        panel.isReleasedWhenClosed = false
        panel.center()

        let content = NSView()
        panel.contentView = content
        let tabView = NSTabView()
        tabView.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(tabView)

        let previewTitle = NSTextField(labelWithString: "Live Preview")
        previewTitle.font = NSFont.boldSystemFont(ofSize: 14)
        previewTitle.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(previewTitle)
        crosshairPreview.settingsProvider = { [weak self] in self?.currentCrosshairSettings() }
        crosshairPreview.toolTip = "Approximate crosshair appearance at the selected settings."
        crosshairPreview.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(crosshairPreview)
        let previewNote = NSTextField(wrappingLabelWithString:
            "The preview reacts live. Exact spacing can vary slightly in-game with resolution, movement, and the equipped weapon. Sniper settings apply while scoped.")
        previewNote.textColor = .secondaryLabelColor
        previewNote.alignment = .center
        previewNote.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(previewNote)

        let basicView = NSView()
        let basicStack = NSStackView()
        basicStack.orientation = .vertical
        basicStack.alignment = .leading
        basicStack.spacing = 7
        basicStack.translatesAutoresizingMaskIntoConstraints = false
        basicView.addSubview(basicStack)
        NSLayoutConstraint.activate([
            basicStack.leadingAnchor.constraint(equalTo: basicView.leadingAnchor, constant: 14),
            basicStack.trailingAnchor.constraint(lessThanOrEqualTo: basicView.trailingAnchor, constant: -14),
            basicStack.topAnchor.constraint(equalTo: basicView.topAnchor, constant: 14),
        ])

        crosshairStylePopup.widthAnchor.constraint(equalToConstant: 320).isActive = true
        crosshairStylePopup.target = self
        crosshairStylePopup.action = #selector(crosshairSettingsChanged)
        crosshairColorWell.target = self
        crosshairColorWell.action = #selector(crosshairSettingsChanged)
        crosshairColorWell.widthAnchor.constraint(equalToConstant: 80).isActive = true
        basicStack.addArrangedSubview(crosshairRow("Style", control: crosshairStylePopup))
        basicStack.addArrangedSubview(crosshairRow("Color", control: crosshairColorWell))
        basicStack.addArrangedSubview(crosshairRow("Length", control: numericControl(for: crosshairSizeField)))
        basicStack.addArrangedSubview(crosshairRow("Thickness", control: numericControl(for: crosshairThicknessField)))
        basicStack.addArrangedSubview(crosshairRow("Gap", control: numericControl(for: crosshairGapField)))
        basicStack.addArrangedSubview(crosshairRow("Opacity (%)", control: numericControl(for: crosshairOpacityField)))
        basicStack.addArrangedSubview(crosshairRow("Opacity", control: crosshairUseOpacityCheckbox))
        basicStack.addArrangedSubview(crosshairRow("Dot", control: crosshairDotCheckbox))
        basicStack.addArrangedSubview(crosshairRow("Outline", control: crosshairOutlineCheckbox))
        basicStack.addArrangedSubview(crosshairRow("Outline thickness", control: numericControl(for: crosshairOutlineThicknessField)))
        basicStack.addArrangedSubview(crosshairRow("Weapon-specific gap", control: crosshairWeaponGapCheckbox))

        let advancedView = NSView()
        let advancedStack = NSStackView()
        advancedStack.orientation = .vertical
        advancedStack.alignment = .leading
        advancedStack.spacing = 7
        advancedStack.translatesAutoresizingMaskIntoConstraints = false
        advancedView.addSubview(advancedStack)
        NSLayoutConstraint.activate([
            advancedStack.leadingAnchor.constraint(equalTo: advancedView.leadingAnchor, constant: 14),
            advancedStack.trailingAnchor.constraint(lessThanOrEqualTo: advancedView.trailingAnchor, constant: -14),
            advancedStack.topAnchor.constraint(equalTo: advancedView.topAnchor, constant: 14),
        ])
        advancedStack.addArrangedSubview(crosshairRow("Default-style fixed gap", control: numericControl(for: crosshairFixedGapField)))
        advancedStack.addArrangedSubview(crosshairRow("Legacy scale (deprecated)", control: numericControl(for: crosshairLegacyScaleField)))
        advancedStack.addArrangedSubview(crosshairRow("Split distance (style 2)", control: numericControl(for: crosshairDynamicSplitDistanceField)))
        advancedStack.addArrangedSubview(crosshairRow("Inner alpha (style 2)", control: numericControl(for: crosshairDynamicInnerAlphaField)))
        advancedStack.addArrangedSubview(crosshairRow("Outer alpha (style 2)", control: numericControl(for: crosshairDynamicOuterAlphaField)))
        advancedStack.addArrangedSubview(crosshairRow("Inner/outer ratio (style 2)", control: numericControl(for: crosshairDynamicSplitRatioField)))
        advancedStack.addArrangedSubview(crosshairRow("Sniper line width", control: numericControl(for: crosshairSniperWidthField)))
        advancedStack.addArrangedSubview(crosshairRow("Sniper blur", control: crosshairSniperInaccuracyCheckbox))

        let basicTab = NSTabViewItem(identifier: "basic")
        basicTab.label = "Basic"
        basicTab.view = basicView
        tabView.addTabViewItem(basicTab)
        let advancedTab = NSTabViewItem(identifier: "advanced")
        advancedTab.label = "Advanced"
        advancedTab.view = advancedView
        tabView.addTabViewItem(advancedTab)

        let allCrosshairControls: [NSControl] = [
            crosshairSizeField, crosshairThicknessField, crosshairGapField, crosshairOpacityField,
            crosshairUseOpacityCheckbox, crosshairDotCheckbox, crosshairOutlineCheckbox,
            crosshairOutlineThicknessField, crosshairWeaponGapCheckbox, crosshairFixedGapField,
            crosshairLegacyScaleField, crosshairDynamicSplitDistanceField,
            crosshairDynamicInnerAlphaField, crosshairDynamicOuterAlphaField,
            crosshairDynamicSplitRatioField, crosshairSniperWidthField,
            crosshairSniperInaccuracyCheckbox,
        ]
        for control in allCrosshairControls {
            control.target = self
            control.action = #selector(crosshairSettingsChanged)
        }

        let resetButton = NSButton(title: "Reset to Game Defaults", target: self, action: #selector(resetCrosshairSettings))
        resetButton.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(resetButton)
        let note = NSTextField(labelWithString: "Changes apply the next time you launch the game.")
        note.textColor = .secondaryLabelColor
        note.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(note)
        NSLayoutConstraint.activate([
            tabView.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 18),
            tabView.trailingAnchor.constraint(equalTo: crosshairPreview.leadingAnchor, constant: -22),
            tabView.topAnchor.constraint(equalTo: content.topAnchor, constant: 14),
            tabView.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -52),
            crosshairPreview.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -22),
            crosshairPreview.topAnchor.constraint(equalTo: content.topAnchor, constant: 62),
            crosshairPreview.widthAnchor.constraint(equalToConstant: 230),
            crosshairPreview.heightAnchor.constraint(equalToConstant: 230),
            previewTitle.centerXAnchor.constraint(equalTo: crosshairPreview.centerXAnchor),
            previewTitle.bottomAnchor.constraint(equalTo: crosshairPreview.topAnchor, constant: -10),
            previewNote.leadingAnchor.constraint(equalTo: crosshairPreview.leadingAnchor),
            previewNote.trailingAnchor.constraint(equalTo: crosshairPreview.trailingAnchor),
            previewNote.topAnchor.constraint(equalTo: crosshairPreview.bottomAnchor, constant: 12),
            resetButton.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 22),
            resetButton.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -14),
            note.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -22),
            note.centerYAnchor.constraint(equalTo: resetButton.centerYAnchor),
        ])

        crosshairWindow = panel
        crosshairPreview.needsDisplay = true
        panel.makeKeyAndOrderFront(nil)
    }

    @objc private func crosshairSettingsChanged() {
        saveSettings()
        crosshairPreview.needsDisplay = true
    }

    @objc private func resetCrosshairSettings() {
        crosshairStylePopup.selectItem(at: 2)
        crosshairColorWell.color = NSColor(srgbRed: 50.0 / 255.0,
                                           green: 250.0 / 255.0,
                                           blue: 50.0 / 255.0,
                                           alpha: 1)
        setNumericField(crosshairSizeField, value: 5)
        setNumericField(crosshairThicknessField, value: 0.5)
        setNumericField(crosshairGapField, value: 1)
        setNumericField(crosshairOpacityField, value: 78)
        crosshairUseOpacityCheckbox.state = .on
        crosshairDotCheckbox.state = .off
        crosshairOutlineCheckbox.state = .on
        setNumericField(crosshairOutlineThicknessField, value: 1)
        crosshairWeaponGapCheckbox.state = .off
        setNumericField(crosshairFixedGapField, value: 3)
        setNumericField(crosshairLegacyScaleField, value: 0)
        setNumericField(crosshairDynamicSplitDistanceField, value: 7)
        setNumericField(crosshairDynamicInnerAlphaField, value: 1)
        setNumericField(crosshairDynamicOuterAlphaField, value: 0.5)
        setNumericField(crosshairDynamicSplitRatioField, value: 0.35)
        setNumericField(crosshairSniperWidthField, value: 1)
        crosshairSniperInaccuracyCheckbox.state = .off
        saveSettings()
        crosshairPreview.needsDisplay = true
    }

    @objc private func showGraphicsSettings() {
        if let graphicsWindow {
            graphicsWindow.makeKeyAndOrderFront(nil)
            return
        }

        let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 480, height: 320),
                            styleMask: [.titled, .closable, .utilityWindow], backing: .buffered, defer: false)
        panel.title = "Graphics Settings"
        panel.isFloatingPanel = false
        panel.hidesOnDeactivate = false
        panel.isReleasedWhenClosed = false
        panel.center()
        let content = NSView()
        panel.contentView = content
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 22),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -22),
            stack.topAnchor.constraint(equalTo: content.topAnchor, constant: 20),
        ])

        for control in [texturePopup, filteringPopup, antialiasingPopup, shadowsPopup, shadersPopup] {
            control.widthAnchor.constraint(equalToConstant: 280).isActive = true
            control.target = self
            control.action = #selector(settingsChanged)
        }
        stack.addArrangedSubview(row("Textures", control: texturePopup))
        stack.addArrangedSubview(row("Filtering", control: filteringPopup))
        stack.addArrangedSubview(row("Anti-aliasing", control: antialiasingPopup))
        stack.addArrangedSubview(row("Shadows", control: shadowsPopup))
        stack.addArrangedSubview(row("Shaders", control: shadersPopup))
        vsyncCheckbox.target = self
        vsyncCheckbox.action = #selector(settingsChanged)
        stack.addArrangedSubview(row("VSync", control: vsyncCheckbox))
        let note = NSTextField(labelWithString: "Changes apply the next time you launch the game.")
        note.textColor = .secondaryLabelColor
        stack.addArrangedSubview(note)
        graphicsWindow = panel
        panel.makeKeyAndOrderFront(nil)
    }

    @objc private func graphicsQualityChanged() {
        let index = qualityPopup.indexOfSelectedItem - 1
        guard qualityPresets.indices.contains(index) else { return }
        let preset = qualityPresets[index]
        texturePopup.selectItem(at: preset[0])
        filteringPopup.selectItem(at: preset[1])
        antialiasingPopup.selectItem(at: preset[2])
        shadowsPopup.selectItem(at: preset[3])
        shadersPopup.selectItem(at: preset[4])
        saveSettings()
    }

    private func syncGraphicsQuality() {
        let current = [texturePopup, filteringPopup, antialiasingPopup, shadowsPopup, shadersPopup]
            .map(\.indexOfSelectedItem)
        let match = qualityPresets.firstIndex(of: current)
        qualityPopup.selectItem(at: match.map { $0 + 1 } ?? 0)
    }

    @objc private func settingsChanged() {
        syncGraphicsQuality()
        saveSettings()
    }

    private func currentCrosshairSettings() -> CrosshairSettings {
        let color = crosshairColorWell.color.usingColorSpace(.sRGB)
            ?? NSColor(srgbRed: 50.0 / 255.0, green: 250.0 / 255.0, blue: 50.0 / 255.0, alpha: 1)
        let red = min(max(Int((color.redComponent * 255).rounded()), 0), 255)
        let green = min(max(Int((color.greenComponent * 255).rounded()), 0), 255)
        let blue = min(max(Int((color.blueComponent * 255).rounded()), 0), 255)
        return CrosshairSettings(
            style: min(max(crosshairStylePopup.indexOfSelectedItem, 0), 5),
            red: red,
            green: green,
            blue: blue,
            size: numericValue(crosshairSizeField, defaultValue: 5, range: 0...100),
            thickness: numericValue(crosshairThicknessField, defaultValue: 0.5, range: 0...20),
            gap: numericValue(crosshairGapField, defaultValue: 1, range: -100...100),
            opacity: Int(numericValue(crosshairOpacityField, defaultValue: 78, range: 0...100, decimalPlaces: 0)),
            useOpacity: crosshairUseOpacityCheckbox.state == .on,
            dot: crosshairDotCheckbox.state == .on,
            outline: crosshairOutlineCheckbox.state == .on,
            outlineThickness: numericValue(crosshairOutlineThicknessField, defaultValue: 1, range: 0.1...3),
            useWeaponGap: crosshairWeaponGapCheckbox.state == .on,
            fixedGap: numericValue(crosshairFixedGapField, defaultValue: 3, range: -100...100),
            legacyScale: Int(numericValue(crosshairLegacyScaleField, defaultValue: 0, range: 0...5000, decimalPlaces: 0)),
            dynamicSplitDistance: numericValue(crosshairDynamicSplitDistanceField, defaultValue: 7, range: 0...100),
            dynamicInnerAlpha: numericValue(crosshairDynamicInnerAlphaField, defaultValue: 1, range: 0...1),
            dynamicOuterAlpha: numericValue(crosshairDynamicOuterAlphaField, defaultValue: 0.5, range: 0.3...1),
            dynamicSplitRatio: numericValue(crosshairDynamicSplitRatioField, defaultValue: 0.35, range: 0...1),
            sniperWidth: Int(numericValue(crosshairSniperWidthField, defaultValue: 1, range: 1...20, decimalPlaces: 0)),
            sniperShowsInaccuracy: crosshairSniperInaccuracyCheckbox.state == .on
        )
    }

    private func saveSettings() {
        let crosshair = currentCrosshairSettings()
        defaults.set(mapPopup.titleOfSelectedItem, forKey: "map")
        defaults.set(botCountPopup.indexOfSelectedItem, forKey: "botCount")
        defaults.set(difficultyPopup.indexOfSelectedItem, forKey: "difficulty")
        defaults.set(resolutions[resolutionPopup.indexOfSelectedItem].key, forKey: "resolution")
        defaults.set(fullscreenCheckbox.state == .on, forKey: "fullscreen")
        defaults.set(texturePopup.indexOfSelectedItem, forKey: "textureDetail")
        defaults.set(filteringPopup.indexOfSelectedItem, forKey: "textureFiltering")
        defaults.set(antialiasingPopup.indexOfSelectedItem, forKey: "antialiasing")
        defaults.set(shadowsPopup.indexOfSelectedItem, forKey: "shadows")
        defaults.set(shadersPopup.indexOfSelectedItem, forKey: "shaderDetail")
        defaults.set(vsyncCheckbox.state == .on, forKey: "vsync")
        defaults.set(crosshair.style, forKey: "crosshairStyle")
        defaults.set(crosshair.red, forKey: "red")
        defaults.set(crosshair.green, forKey: "green")
        defaults.set(crosshair.blue, forKey: "blue")
        defaults.set(crosshair.size, forKey: "size")
        defaults.set(crosshair.thickness, forKey: "thickness")
        defaults.set(crosshair.gap, forKey: "gap")
        defaults.set(crosshair.opacity, forKey: "opacity")
        defaults.set(crosshair.useOpacity, forKey: "crosshairUseOpacity")
        defaults.set(crosshair.dot, forKey: "dot")
        defaults.set(crosshair.outline, forKey: "outline")
        defaults.set(crosshair.outlineThickness, forKey: "crosshairOutlineThickness")
        defaults.set(crosshair.useWeaponGap, forKey: "crosshairWeaponGap")
        defaults.set(crosshair.fixedGap, forKey: "crosshairFixedGap")
        defaults.set(crosshair.legacyScale, forKey: "crosshairLegacyScale")
        defaults.set(crosshair.dynamicSplitDistance, forKey: "crosshairDynamicSplitDistance")
        defaults.set(crosshair.dynamicInnerAlpha, forKey: "crosshairDynamicInnerAlpha")
        defaults.set(crosshair.dynamicOuterAlpha, forKey: "crosshairDynamicOuterAlpha")
        defaults.set(crosshair.dynamicSplitRatio, forKey: "crosshairDynamicSplitRatio")
        defaults.set(crosshair.sniperWidth, forKey: "crosshairSniperWidth")
        defaults.set(crosshair.sniperShowsInaccuracy, forKey: "crosshairSniperInaccuracy")
    }

    @objc private func launchGame() {
        guard gameProcess == nil else { return }
        guard let map = mapPopup.titleOfSelectedItem,
              FileManager.default.fileExists(atPath: gameRoot.appendingPathComponent("csgo/maps/\(map).bsp").path) else {
            showError("Select an installed map.")
            return
        }
        let game = gameRoot.appendingPathComponent("csgo_osx64")
        guard FileManager.default.isExecutableFile(atPath: game.path) else {
            showError("The game executable is missing from \(gameRoot.path).")
            return
        }

        saveSettings()
        let requestedBots = botCountPopup.indexOfSelectedItem
        let requestedParticipants = requestedBots + 1
        // CS:GO splits the mode's slot count evenly between T and CT using integer division.
        // Round up to an even count so an odd total (for example, 20 bots + one human)
        // does not silently lose one usable team slot.
        let maxPlayers = max(2, requestedParticipants + requestedParticipants % 2)
        let crosshair = currentCrosshairSettings()
        let crosshairAlpha = Int((Double(crosshair.opacity) * 255 / 100).rounded())
        let texturePicmip = [2, 1, 0, -1][texturePopup.indexOfSelectedItem]
        let filtering = filteringPopup.indexOfSelectedItem
        let anisotropy = [1, 1, 2, 4, 8, 16][filtering]
        let antialiasing = [0, 2, 4][antialiasingPopup.indexOfSelectedItem]
        let shadows = shadowsPopup.indexOfSelectedItem
        let vsync = vsyncCheckbox.state == .on ? 1 : 0
        let config = """
        // Generated by CS:GO Mac Launcher. Recreated each time the game starts.
        bot_quota_mode normal
        bot_quota \(requestedBots)
        bot_difficulty \(difficultyPopup.indexOfSelectedItem)
        bot_auto_vacate 0
        bot_join_delay 0
        bot_join_after_player 0
        bot_join_in_warmup 1
        sv_auto_adjust_bot_difficulty 0
        mp_autoteambalance 0
        mp_limitteams 0
        mp_forcecamera 0
        mp_force_assign_teams 1
        mp_humanteam CT
        mp_do_warmup_period 0
        mp_do_warmup_offine 0
        mp_warmup_pausetimer 0
        mp_freezetime 0
        mp_round_restart_delay 0
        mp_respawn_immunitytime 0
        mp_spawnprotectiontime 0
        mp_use_respawn_waves 0
        mp_respawn_on_death_t 1
        mp_respawn_on_death_ct 1
        cl_crosshairstyle \(crosshair.style)
        cl_crosshaircolor 5
        cl_crosshaircolor_r \(crosshair.red)
        cl_crosshaircolor_g \(crosshair.green)
        cl_crosshaircolor_b \(crosshair.blue)
        cl_crosshairsize \(commandNumber(crosshair.size))
        cl_crosshairthickness \(commandNumber(crosshair.thickness))
        cl_crosshairgap \(commandNumber(crosshair.gap))
        cl_crosshairalpha \(crosshairAlpha)
        cl_crosshairusealpha \(crosshair.useOpacity ? 1 : 0)
        cl_crosshairdot \(crosshair.dot ? 1 : 0)
        cl_crosshair_drawoutline \(crosshair.outline ? 1 : 0)
        cl_crosshair_outlinethickness \(commandNumber(crosshair.outlineThickness))
        cl_crosshairgap_useweaponvalue \(crosshair.useWeaponGap ? 1 : 0)
        cl_fixedcrosshairgap \(commandNumber(crosshair.fixedGap))
        cl_crosshairscale \(crosshair.legacyScale)
        cl_crosshair_dynamic_splitdist \(commandNumber(crosshair.dynamicSplitDistance))
        cl_crosshair_dynamic_splitalpha_innermod \(commandNumber(crosshair.dynamicInnerAlpha))
        cl_crosshair_dynamic_splitalpha_outermod \(commandNumber(crosshair.dynamicOuterAlpha))
        cl_crosshair_dynamic_maxdist_splitratio \(commandNumber(crosshair.dynamicSplitRatio))
        cl_crosshair_sniper_width \(crosshair.sniperWidth)
        cl_crosshair_sniper_show_normal_inaccuracy \(crosshair.sniperShowsInaccuracy ? 1 : 0)
        mat_picmip \(texturePicmip)
        mat_trilinear \(filtering == 1 ? 1 : 0)
        mat_forceaniso \(anisotropy)
        mat_antialias \(antialiasing)
        mat_aaquality 0
        r_shadows \(shadows == 0 ? 0 : 1)
        r_shadowrendertotexture \(shadows >= 2 ? 1 : 0)
        r_flashlightdepthtexture \(shadows >= 3 ? 1 : 0)
        mat_reducefillrate \(shadersPopup.indexOfSelectedItem == 0 ? 1 : 0)
        mat_vsync \(vsync)

        """
        let configURL = gameRoot.appendingPathComponent("csgo/cfg/mac_launcher.cfg")

        do {
            try config.write(to: configURL, atomically: true, encoding: .utf8)
            let logURL = gameRoot.appendingPathComponent("launcher-game.log")
            try Data().write(to: logURL, options: .atomic)
            let logHandle = try FileHandle(forWritingTo: logURL)
            let resolution = resolutions[resolutionPopup.indexOfSelectedItem]
            let process = Process()
            process.executableURL = URL(fileURLWithPath: "/usr/bin/arch")
            process.currentDirectoryURL = gameRoot
            process.standardOutput = logHandle
            process.standardError = logHandle
            process.arguments = ["-arm64", game.path, "-insecure", "-novid",
                                 fullscreenCheckbox.state == .on ? "-fullscreen" : "-windowed",
                                 "-w", "\(resolution.width)", "-h", "\(resolution.height)",
                                 "-mat_antialias", "\(antialiasing)", "-mat_aaquality", "0",
                                 "-mat_vsync", "\(vsync)",
                                 "-maxplayers_override", "\(maxPlayers)",
                                 "+exec", "mac_launcher.cfg", "+map", map,
                                 "+exec", "mac_launcher.cfg"]
            process.terminationHandler = { [weak self] finished in
                try? logHandle.close()
                DispatchQueue.main.async {
                    self?.gameProcess = nil
                    self?.launchButton.isEnabled = true
                    if finished.terminationReason == .uncaughtSignal {
                        self?.statusLabel.stringValue = "Game crashed (signal \(finished.terminationStatus)); see DiagnosticReports."
                    } else if finished.terminationStatus == 0 {
                        self?.statusLabel.stringValue = "Game closed."
                    } else {
                        self?.statusLabel.stringValue = "Game exited (status \(finished.terminationStatus)); see launcher-game.log."
                    }
                    NSApp.unhide(nil)
                    NSApp.activate(ignoringOtherApps: true)
                }
            }
            try process.run()
            gameProcess = process
            launchButton.isEnabled = false
            statusLabel.stringValue = "Game running on \(map) with \(requestedBots) bot\(requestedBots == 1 ? "" : "s")."
            graphicsWindow?.orderOut(nil)
            crosshairWindow?.orderOut(nil)
            NSApp.hide(nil)
        } catch {
            showError("Could not launch the game: \(error.localizedDescription)")
        }
    }

    private func showError(_ message: String) {
        let alert = NSAlert()
        alert.messageText = "CS:GO Launcher"
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.runModal()
    }
}
