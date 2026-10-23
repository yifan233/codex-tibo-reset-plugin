import Cocoa
import QuartzCore

let systemEnglish = !(Locale.preferredLanguages.first ?? "en").hasPrefix("zh")
var english = systemEnglish
func tr(_ zh: String, _ en: String) -> String { english ? en : zh }
func selectLanguage(_ value: String) { english = value == "en" || (value == "auto" && systemEnglish) }
struct CountdownState: Codable {
    var tiboResetAt: String?
    var resetCardExpiresAt: [String]
    var sourceURL: String?
    var language: String = "auto"
    static let empty = CountdownState(tiboResetAt: nil, resetCardExpiresAt: [], sourceURL: nil)
    func validate() throws {
        guard ["auto", "zh", "en"].contains(language) else { throw StateError.invalid("Language must be auto, zh or en") }
        if let value = tiboResetAt, parseDate(value) == nil { throw StateError.invalid(tr("时间必须带时区且日期有效", "Use a valid ISO 8601 date with a time zone")) }
        guard resetCardExpiresAt.count <= 128, resetCardExpiresAt.allSatisfy({ parseDate($0) != nil }) else { throw StateError.invalid(tr("卡期限无效", "Invalid card deadlines")) }
        if let value = sourceURL {
            guard value.utf8.count <= 4096, let u = URLComponents(string: value), u.scheme == "https",
                  ["x.com", "www.x.com", "twitter.com", "www.twitter.com"].contains(u.host?.lowercased() ?? ""),
                  u.user == nil, u.password == nil, u.port == nil,
                  !value.unicodeScalars.contains(where: { $0.value <= 32 || $0.value >= 127 }),
                  !value.contains("\\") else { throw StateError.invalid(tr("原帖必须是 X / Twitter 的 HTTPS 链接", "Source must be an X / Twitter HTTPS URL")) }
        }
    }
}
enum StateError: Error, LocalizedError {
    case invalid(String)
    var errorDescription: String? { if case .invalid(let message) = self { return message }; return nil }
}
func parseDate(_ value: String) -> Date? {
    let pattern = #"^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(?:\.[0-9]+)?(Z|[+-][0-9]{2}:[0-9]{2})$"#
    guard value.utf8.count <= 128, let match = value.range(of: pattern, options: .regularExpression),
          match == value.startIndex..<value.endIndex else { return nil }
    let bytes = Array(value.utf8)
    func number(_ start: Int, _ count: Int) -> Int { Int(String(bytes: bytes[start..<start+count], encoding: .utf8)!)! }
    let year = number(0, 4), month = number(5, 2), day = number(8, 2)
    let leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)
    let days = [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    guard year > 0, (1...12).contains(month), day > 0, day <= days[month-1],
          number(11, 2) < 24, number(14, 2) < 60, number(17, 2) < 60 else { return nil }
    if bytes.last != 90 {
        let hour = number(bytes.count-5, 2), minute = number(bytes.count-2, 2)
        guard hour <= 14, minute < 60, hour < 14 || minute == 0 else { return nil }
    }
    let f = ISO8601DateFormatter()
    if let d = f.date(from: value) { return d }
    f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    return f.date(from: value)
}
let preferences = UserDefaults.standard
let testStateURL = ProcessInfo.processInfo.environment["TIBO_RESET_STATE_DIR"].map { URL(fileURLWithPath: $0).appendingPathComponent("countdown.state") }
func stateData() throws -> Data? {
    if let url = testStateURL {
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        let data = try handle.read(upToCount: 65537) ?? Data()
        guard data.count <= 65536 else { throw StateError.invalid("State is too large") }
        return data
    }
    preferences.synchronize()
    let data = preferences.data(forKey: "countdown")
    guard (data?.count ?? 0) <= 65536 else { throw StateError.invalid("State is too large") }
    return data
}
func decodeState(_ data: Data?) throws -> CountdownState {
    guard let data else { return .empty }
    let state = try PropertyListDecoder().decode(CountdownState.self, from: data)
    try state.validate()
    return state
}
func writeState(_ state: CountdownState) throws {
    try state.validate()
    let encoder = PropertyListEncoder(); encoder.outputFormat = .binary
    let data = try encoder.encode(state)
    if let url = testStateURL {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url, options: .atomic)
    } else {
        preferences.set(data, forKey: "countdown")
        guard preferences.synchronize() else { throw StateError.invalid(tr("保存失败", "Save failed")) }
    }
}
func status(_ state: CountdownState) -> String {
    var rows = ["tibo=" + (state.tiboResetAt ?? "none")]
    rows += state.resetCardExpiresAt.map { "card=" + $0 }
    rows += ["source=" + (state.sourceURL ?? "none"), "language=" + state.language]
    return rows.joined(separator: "\n")
}
func runCommand() {
    let args = Array(CommandLine.arguments.dropFirst())
    guard !args.isEmpty else { return }
    do {
        var state = try (args[0] == "--reset" || args[0] == "--help") ? CountdownState.empty : decodeState(stateData())
        selectLanguage(state.language)
        switch args[0] {
        case "--status":
            guard args.count == 1 else { throw StateError.invalid("Usage: --status") }
        case "--set":
            guard args.count > 1, args.count % 2 == 1 else { throw StateError.invalid("Expected option/value pairs") }
            var seen = Set<String>()
            for i in stride(from: 1, to: args.count, by: 2) {
                let key = args[i], value = args[i+1]
                guard seen.insert(key).inserted else { throw StateError.invalid("Duplicate option") }
                switch key {
                case "--tibo": state.tiboResetAt = value == "none" ? nil : value
                case "--source": state.sourceURL = value == "none" ? nil : value
                case "--cards": state.resetCardExpiresAt = value == "none" ? [] : value.components(separatedBy: ",")
                case "--language": state.language = value
                default: throw StateError.invalid("Unknown option: " + key)
                }
            }
            try state.validate()
            try writeState(state)
            selectLanguage(state.language)
        case "--reset":
            guard args.count == 1 else { throw StateError.invalid("Usage: --reset") }
            state = .empty
            try writeState(state)
        case "--help":
            print("--status | --set [--tibo <ISO8601|none>] [--cards <ISO8601,...|none>] [--source <URL|none>] [--language <zh|en|auto>] | --reset")
            exit(0)
        default: throw StateError.invalid("Use --help for available commands")
        }
        print(status(state)); exit(0)
    } catch {
        FileHandle.standardError.write(Data((tr("设置失败: ", "Update failed: ") + error.localizedDescription + "\n").utf8)); exit(1)
    }
}
runCommand()
func formatted(_ date: Date) -> String {
    let f = DateFormatter(); f.timeZone = .current; f.dateFormat = "MM/dd HH:mm:ss"; return f.string(from: date)
}
func compactRemaining(_ date: Date) -> String {
    let seconds = max(0, Int(ceil(date.timeIntervalSinceNow)))
    if seconds == 0 { return tr("到点", "Due") }
    if seconds >= 86400 { return String(format: "%dd%02dh", seconds/86400, seconds%86400/3600) }
    let minutes = Int(ceil(Double(seconds)/60))
    return String(format: "%02d:%02d", minutes/60, minutes%60)
}
func text(_ string: String, _ frame: NSRect, _ size: CGFloat, _ color: NSColor, mono: Bool = false) -> NSTextField {
    let v = NSTextField(labelWithString: string); v.frame = frame
    v.font = mono ? .monospacedDigitSystemFont(ofSize: size, weight: .semibold) : .systemFont(ofSize: size, weight: .medium)
    v.textColor = color; return v
}
class IslandPanel: NSPanel {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }
}
class IslandView: NSView {
    override var isFlipped: Bool { true }
    override func draw(_ dirtyRect: NSRect) {
        NSColor.black.setFill()
        let radius: CGFloat = bounds.height <= 32 ? 13 : 22
        NSBezierPath(roundedRect: bounds, xRadius: radius, yRadius: radius).fill()
    }
}
class AppDelegate: NSObject, NSApplicationDelegate {
    var window: IslandPanel!
    var notchScreen: NSScreen? { NSScreen.screens.first(where: { $0.safeAreaInsets.top > 0 }) ?? NSScreen.main ?? NSScreen.screens.first }
    var timer: Timer?
    var expanded = false
    var transitioning = false
    var picker: NSDatePicker!
    var editor: NSPanel?
    var tiboLabel: NSTextField!
    var cardLabel: NSTextField!
    var config = CountdownState.empty
    var lastStateData: Data?
    var stateError: String?
    var deadline: Date? { config.tiboResetAt.flatMap(parseDate) }
    var expiries: [Date] { config.resetCardExpiresAt.compactMap(parseDate).sorted() }
    var loadingState = false
    func reloadState() -> Bool {
        guard !loadingState else { return false }
        do {
            let data = try stateData()
            guard data != lastStateData || stateError != nil else { return false }
            let next = try decodeState(data)
            config = next; selectLanguage(next.language); lastStateData = data; stateError = nil
            window?.contentView?.toolTip = nil
            return true
        } catch {
            let message = tr("配置错误，保留上次有效数据：", "Unable to read state; keeping last values: ") + error.localizedDescription
            let changed = stateError != message
            stateError = message
            window?.contentView?.toolTip = stateError
            return changed
        }
    }
    func applicationDidFinishLaunching(_ notification: Notification) {
        window = IslandPanel(contentRect: .zero, styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        window.title = "Reset Island"; window.isOpaque = false; window.backgroundColor = .clear
        window.hasShadow = true; window.level = NSWindow.Level(rawValue: NSWindow.Level.statusBar.rawValue + 1)
        window.hidesOnDeactivate = false; window.isReleasedWhenClosed = false
        window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        NotificationCenter.default.addObserver(self, selector: #selector(screenChanged), name: NSApplication.didChangeScreenParametersNotification, object: nil)
        _ = reloadState()
        render(); window.orderFrontRegardless()
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.update() }
        RunLoop.main.add(timer!, forMode: .common)
    }
    @objc func screenChanged() { render() }
    func position() {
        guard let screen = notchScreen else {
            window.setFrame(NSRect(x: 0, y: 0, width: 296, height: expanded ? 270 : 32), display: true)
            return
        }
        let leftEdge = screen.auxiliaryTopLeftArea?.maxX ?? screen.frame.midX-90
        let rightEdge = screen.auxiliaryTopRightArea?.minX ?? screen.frame.midX+90
        let height: CGFloat = expanded ? 270 : 32
        window.setFrame(NSRect(x: leftEdge-58.5, y: screen.frame.maxY-height, width: rightEdge-leftEdge+117, height: height), display: true)

    }
    func button(_ title: String, frame: NSRect, action: Selector, in view: NSView) {
        let b = NSButton(title: title, target: self, action: action); b.frame = frame
        b.bezelStyle = .rounded; b.font = .systemFont(ofSize: 11); view.addSubview(b)
    }
    func render() {
        position()
        if !expanded { renderWings(); return }
        let view = IslandView(frame: NSRect(origin: .zero, size: window.frame.size)); window.contentView = view
        tiboLabel = text("", NSRect(x: 6, y: 7, width: 50, height: 20), 14, .systemTeal, mono: true); view.addSubview(tiboLabel)
        cardLabel = text("", NSRect(x: view.bounds.width-54.5, y: 7, width: 52, height: 20), 14, .systemOrange, mono: true); view.addSubview(cardLabel)
        let hit = NSButton(frame: NSRect(x: 0, y: 0, width: view.bounds.width, height: 38))
        hit.title = ""; hit.isBordered = false; hit.isTransparent = true; hit.target = self; hit.action = #selector(toggle)
        hit.setAccessibilityLabel(tr("收起倒计时详情", "Collapse countdown details")); view.addSubview(hit)
        let width = view.bounds.width-32
        view.addSubview(text(tr("RESET / 预告与到期", "RESET / Deadlines"), NSRect(x: 16, y: 50, width: width, height: 20), 13, .white))
        view.addSubview(text("Tibo · " + (deadline.map(formatted) ?? tr("未设置", "Unset")), NSRect(x: 16, y: 84, width: width, height: 18), 11, .systemTeal))
        view.addSubview(text(tr("最早卡 · ", "First card · ") + (expiries.first.map(formatted) ?? tr("无卡", "None")), NSRect(x: 16, y: 112, width: width, height: 18), 11, .systemOrange))
        view.addSubview(text(tr("其余卡 · ", "Next card · ") + (expiries.dropFirst().first.map(formatted) ?? tr("无", "None")), NSRect(x: 16, y: 140, width: width, height: 18), 11, .lightGray))
        view.addSubview(text(stateError == nil ? tr("由 Codex 设定", "Set by Codex") : tr("读取失败 · 保留上次数据", "Keeping last valid values"), NSRect(x: 16, y: 192, width: width, height: 16), 10, .gray))
        button(tr("修改时间", "Edit time"), frame: NSRect(x: 12, y: 228, width: 86, height: 26), action: #selector(editTime), in: view)
        button(tr("原帖 ↗", "Source ↗"), frame: NSRect(x: 103, y: 228, width: 78, height: 26), action: #selector(openSource), in: view)
        button(tr("退出", "Quit"), frame: NSRect(x: view.bounds.width-72, y: 228, width: 60, height: 26), action: #selector(quit), in: view)
        let menu = NSMenu(); menu.addItem(withTitle: tr("展开 / 收起", "Expand / Collapse"), action: #selector(toggle), keyEquivalent: "").target = self
        menu.addItem(withTitle: tr("退出", "Quit"), action: #selector(quit), keyEquivalent: "q").target = self; view.menu = menu
        update()
    }
    func renderWings() {
        let view = IslandView(frame: NSRect(origin: .zero, size: window.frame.size))
        window.contentView = view; window.hasShadow = false
        tiboLabel = text("", NSRect(x: 6, y: 7, width: 50, height: 20), 14, .systemTeal, mono: true)
        view.addSubview(tiboLabel)
        let rightX = view.bounds.width-58.5
        cardLabel = text("", NSRect(x: rightX+4, y: 7, width: 52, height: 20), 14, .systemOrange, mono: true)
        view.addSubview(cardLabel)
        let hit = NSButton(frame: view.bounds); hit.title = ""; hit.isBordered = false; hit.isTransparent = true
        hit.target = self; hit.action = #selector(toggle)
        hit.setAccessibilityLabel(tr("Tibo 和卡到期倒计时，点击展开", "Tibo and card countdowns; click to expand"))
        view.addSubview(hit)
        let menu = NSMenu(); menu.addItem(withTitle: tr("展开详情", "Expand details"), action: #selector(toggle), keyEquivalent: "").target = self
        menu.addItem(withTitle: tr("退出", "Quit"), action: #selector(quit), keyEquivalent: "q").target = self; view.menu = menu
        window.orderFrontRegardless(); update()
    }
    func update() {
        if !transitioning && reloadState() {
            loadingState = true; render(); loadingState = false
        }
        tiboLabel?.stringValue = deadline.map(compactRemaining) ?? tr("未设置", "Unset")
        cardLabel?.stringValue = expiries.first.map(compactRemaining) ?? tr("无卡", "None")
        window?.contentView?.toolTip = stateError
    }
    @objc func toggle() {
        guard !transitioning else { return }
        transitioning = true
        let oldFrame = window.frame
        expanded.toggle(); render()
        let target = window.frame
        window.setFrame(oldFrame, display: true)
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.26
            context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            window.animator().setFrame(target, display: true)
        }, completionHandler: { [weak self] in
            self?.transitioning = false
            self?.window.contentView?.needsDisplay = true
        })
    }
    @objc func quit() { NSApp.terminate(nil) }
    @objc func openSource() { if let value = config.sourceURL, let url = URL(string: value) { NSWorkspace.shared.open(url) } }
    @objc func editTime() {
        if let existing = editor { existing.makeKeyAndOrderFront(nil); return }
        let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 360, height: 150), styleMask: [.titled], backing: .buffered, defer: false)
        panel.title = tr("Tibo 截止时间", "Tibo deadline"); panel.level = window.level; panel.isReleasedWhenClosed = false
        picker = NSDatePicker(); picker.frame = NSRect(x: 30, y: 68, width: 300, height: 28)
        picker.datePickerStyle = .textFieldAndStepper; picker.datePickerElements = [.yearMonthDay, .hourMinuteSecond]
        picker.timeZone = .current; picker.dateValue = deadline ?? Date(); panel.contentView!.addSubview(picker)
        button(tr("保存", "Save"), frame: NSRect(x: 250, y: 20, width: 80, height: 28), action: #selector(saveTime), in: panel.contentView!)
        button(tr("取消", "Cancel"), frame: NSRect(x: 160, y: 20, width: 80, height: 28), action: #selector(cancelEdit), in: panel.contentView!)
        editor = panel; panel.center(); panel.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
    }
    @objc func saveTime() {
        do {
            var next = config; next.tiboResetAt = ISO8601DateFormatter().string(from: picker.dateValue)
            try writeState(next); _ = reloadState(); cancelEdit(); render()
        } catch {
            let alert = NSAlert(); alert.messageText = tr("保存失败", "Save failed"); alert.informativeText = error.localizedDescription; alert.runModal()
        }
    }
    @objc func cancelEdit() { editor?.orderOut(nil); editor = nil }
}
let app = NSApplication.shared
let delegate = AppDelegate()
app.setActivationPolicy(.accessory); app.delegate = delegate; app.run()

// 大肥鲸验证完安全
