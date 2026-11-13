import Foundation
import SwiftUI
#if os(iOS)
import UIKit
#else
import AppKit
#endif

@MainActor
final class LibraryStore: ObservableObject {
    @Published private(set) var installs: [Install] = []
    @Published private(set) var status: [String: InstallStatus] = [:]
    @Published private(set) var probes: [String: ProbeInfo] = [:]
    @Published var playing: String?
    @Published var lastError: String?
    @Published var crashedInstall: String?

    #if os(macOS)
    @Published var sources: [String] = []
    var player: Process?
    #endif

    private let root: URL
    private var index: URL { root.appendingPathComponent("library.json") }
    private var ignoredURL: URL { root.appendingPathComponent("ignored.json") }
    private var ignoredIDs: Set<String> = []
    #if os(macOS)
    var sourcesURL: URL { root.appendingPathComponent("sources.json") }
    var dropFolder: URL { root.appendingPathComponent("Games", isDirectory: true) }
    #endif

    init() {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        root = support.appendingPathComponent("OpenBGI", isDirectory: true)
        try? FileManager.default.createDirectory(at: root.appendingPathComponent("Installs"), withIntermediateDirectories: true)
        load()
        if let data = try? Data(contentsOf: ignoredURL), let set = try? JSONDecoder().decode(Set<String>.self, from: data) {
            ignoredIDs = set
        }
        #if os(macOS)
        try? FileManager.default.createDirectory(at: dropFolder, withIntermediateDirectories: true)
        if let data = try? Data(contentsOf: sourcesURL), let list = try? JSONDecoder().decode([String].self, from: data) {
            sources = list
        }
        #else
        ensureDocumentsFolder()
        #endif
        detectCrash()
        sync()
    }

    nonisolated static var documents: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }

    func folder(_ i: Install) -> URL { root.appendingPathComponent("Installs/\(i.id)", isDirectory: true) }
    func dataURL(_ i: Install) -> URL { folder(i).appendingPathComponent("Data", isDirectory: true) }
    func logURL(_ i: Install) -> URL { folder(i).appendingPathComponent("last-run.log") }
    func flagURL(_ i: Install) -> URL { folder(i).appendingPathComponent("running.flag") }

    func gameURL(_ i: Install) -> URL {
        switch i.storage {
        case .documents(let folder):
            return folder == "." ? Self.documents : Self.documents.appendingPathComponent(folder, isDirectory: true)
        case .path(let path):
            return URL(fileURLWithPath: path, isDirectory: true)
        }
    }

    func install(_ id: String) -> Install? { installs.first { $0.id == id } }

    private func load() {
        guard let data = try? Data(contentsOf: index), let list = try? JSONDecoder().decode([Install].self, from: data) else { return }
        installs = list
    }

    private func save() {
        if let data = try? JSONEncoder().encode(installs) {
            try? data.write(to: index, options: .atomic)
        }
    }

    func update(_ id: String, _ change: (inout Install) -> Void) {
        guard let idx = installs.firstIndex(where: { $0.id == id }) else { return }
        change(&installs[idx])
        save()
    }

    func options(_ id: String) -> Binding<PlayOptions> {
        Binding(
            get: { self.install(id)?.options ?? PlayOptions() },
            set: { value in self.update(id) { $0.options = value } }
        )
    }

    func rename(_ id: String, to name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        if !trimmed.isEmpty { update(id) { $0.name = trimmed } }
    }

    private func saveIgnored() {
        if let data = try? JSONEncoder().encode(ignoredIDs) {
            try? data.write(to: ignoredURL, options: .atomic)
        }
    }

    func remove(_ id: String, deleteSaves: Bool = false) {
        guard playing != id else { return }
        if let item = install(id) {
            #if os(macOS)
            if case .path(let path) = item.storage {
                let stdPath = URL(fileURLWithPath: path, isDirectory: true).standardizedFileURL.path
                let before = sources.count
                sources.removeAll { URL(fileURLWithPath: $0, isDirectory: true).standardizedFileURL.path == stdPath }
                if sources.count != before, let data = try? JSONEncoder().encode(sources) {
                    try? data.write(to: sourcesURL, options: .atomic)
                }
            }
            #endif
            ignoredIDs.insert(id)
            saveIgnored()
            if deleteSaves {
                try? FileManager.default.removeItem(at: folder(item))
            }
        }
        installs.removeAll { $0.id == id }
        status.removeValue(forKey: id)
        probes.removeValue(forKey: id)
        save()
    }

    #if os(iOS)
    func ensureDocumentsFolder() {
        let docs = Self.documents
        try? FileManager.default.createDirectory(at: docs, withIntermediateDirectories: true)
        let existing = (try? FileManager.default.contentsOfDirectory(
            at: docs,
            includingPropertiesForKeys: nil,
            options: [.skipsHiddenFiles]
        )) ?? []
        if existing.isEmpty {
            let readme = docs.appendingPathComponent("README.txt")
            let note = """
            OpenBGI Games Folder
            --------------------
            Copy your BGI game folders (containing autoload.arc and system.arc) into this OpenBGI folder.
            Then return to OpenBGI and pull down or tap Refresh.
            """
            try? note.write(to: readme, atomically: true, encoding: .utf8)
        }
    }

    func importGameFolder(from sourceURL: URL) async -> String? {
        ensureDocumentsFolder()
        let docs = Self.documents
        let folderName = sourceURL.lastPathComponent
        let dest = docs.appendingPathComponent(folderName, isDirectory: true)
        let errorMessage: String? = await Task.detached(priority: .userInitiated) {
            let accessed = sourceURL.startAccessingSecurityScopedResource()
            defer {
                if accessed { sourceURL.stopAccessingSecurityScopedResource() }
            }
            let fm = FileManager.default
            if fm.fileExists(atPath: dest.path) {
                return String(localized: "A folder named \"\(folderName)\" already exists in Documents.")
            }
            do {
                try fm.copyItem(at: sourceURL, to: dest)
                return nil
            } catch {
                return error.localizedDescription
            }
        }.value
        sync()
        return errorMessage
    }

    func canDeleteFiles(_ i: Install) -> Bool {
        if case .documents(let folder) = i.storage {
            return folder != "."
        }
        return false
    }

    func deleteGame(_ i: Install) {
        guard playing != i.id else { return }
        if case .documents(let folder) = i.storage, folder != "." {
            try? FileManager.default.removeItem(at: gameURL(i))
            try? FileManager.default.removeItem(at: self.folder(i))
        } else {
            ignoredIDs.insert(i.id)
            saveIgnored()
        }
        installs.removeAll { $0.id == i.id }
        status.removeValue(forKey: i.id)
        probes.removeValue(forKey: i.id)
        save()
    }
    #endif

    #if os(macOS)
    func revealGameFolder(_ i: Install) {
        let url = gameURL(i)
        guard FileManager.default.fileExists(atPath: url.path) else { return }
        NSWorkspace.shared.activateFileViewerSelecting([url])
    }

    func revealDataFolder(_ i: Install) {
        let url = dataURL(i)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        NSWorkspace.shared.open(url)
    }
    #endif

    private func stableID(for key: String) -> String {
        var h: UInt64 = 14695981039346656037
        for b in key.utf8 { h = (h ^ UInt64(b)) &* 1099511628211 }
        return String(h, radix: 16)
    }

    private func discoverFolders(under base: URL, maxDepth: Int) -> [URL] {
        var found: [URL] = []
        func walk(_ url: URL, depth: Int) {
            if Engine.detect(at: url) != nil {
                found.append(url)
                return
            }
            guard depth < maxDepth else { return }
            guard let items = try? FileManager.default.contentsOfDirectory(
                at: url, includingPropertiesForKeys: [.isDirectoryKey], options: [.skipsHiddenFiles]
            ) else { return }
            for item in items {
                let isDir = (try? item.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) ?? false
                if isDir { walk(item, depth: depth + 1) }
            }
        }
        walk(base, depth: 0)
        return found
    }

    func sync() {
        #if os(iOS)
        let docs = Self.documents
        let stdDocs = docs.resolvingSymlinksInPath().standardizedFileURL
        let docsPrefix = stdDocs.path.hasSuffix("/") ? stdDocs.path : stdDocs.path + "/"
        let discovered = discoverFolders(under: docs, maxDepth: 2)
        var seenIDs: Set<String> = []
        for url in discovered {
            let stdURL = url.resolvingSymlinksInPath().standardizedFileURL
            let rel: String
            if stdURL == stdDocs {
                rel = "."
            } else if stdURL.path.hasPrefix(docsPrefix) {
                rel = String(stdURL.path.dropFirst(docsPrefix.count))
            } else {
                rel = stdURL.lastPathComponent
            }
            let id = stableID(for: "documents:\(rel)")
            seenIDs.insert(id)
            if !ignoredIDs.contains(id), install(id) == nil, let det = Engine.detect(at: url) {
                let display = rel == "." ? det.name : url.lastPathComponent
                let item = Install(id: id, gameID: det.id, name: display, storage: .documents(folder: rel))
                installs.append(item)
            }
        }
        installs.removeAll { item in
            if case .documents = item.storage { return !seenIDs.contains(item.id) }
            return false
        }
        save()
        #else
        var roots = [dropFolder] + sources.map { URL(fileURLWithPath: $0, isDirectory: true) }
        var seenPaths: Set<String> = []
        for r in roots {
            for url in discoverFolders(under: r, maxDepth: 2) {
                let p = url.standardizedFileURL.path
                guard seenPaths.insert(p).inserted else { continue }
                let id = stableID(for: "path:\(p)")
                if !ignoredIDs.contains(id), install(id) == nil, let det = Engine.detect(at: url) {
                    installs.append(Install(id: id, gameID: det.id, name: url.lastPathComponent, storage: .path(p)))
                }
            }
        }
        save()
        #endif

        for item in installs {
            let url = gameURL(item)
            guard FileManager.default.fileExists(atPath: url.path) else {
                status[item.id] = .missing
                probes.removeValue(forKey: item.id)
                continue
            }
            let p = Engine.probe(at: url)
            probes[item.id] = p
            if !p.ok {
                status[item.id] = .incomplete(missing: p.missing.isEmpty ? p.message : p.missing)
            } else if !p.complete {
                status[item.id] = .incomplete(missing: p.missing)
            } else {
                status[item.id] = .ready
            }
        }
    }

    #if os(macOS)
    @discardableResult
    func addSource(_ url: URL) -> String? {
        let stdURL = url.standardizedFileURL
        let path = stdURL.path
        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: path, isDirectory: &isDir), isDir.boolValue else {
            return nil
        }
        let discovered = discoverFolders(under: stdURL, maxDepth: 2)
        for gameDir in discovered {
            let id = stableID(for: "path:\(gameDir.standardizedFileURL.path)")
            ignoredIDs.remove(id)
        }
        saveIgnored()
        if !sources.contains(path) {
            sources.append(path)
            if let data = try? JSONEncoder().encode(sources) {
                try? data.write(to: sourcesURL, options: .atomic)
            }
        }
        sync()
        if let first = discovered.first {
            return stableID(for: "path:\(first.standardizedFileURL.path)")
        }
        return nil
    }
    #endif

    // MARK: Saves & Logs

    func dataBytes(_ i: Install) -> Int64 {
        let e = FileManager.default.enumerator(at: dataURL(i), includingPropertiesForKeys: [.fileSizeKey])
        var total: Int64 = 0
        while let url = e?.nextObject() as? URL {
            total += Int64((try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
        }
        return total
    }

    func clearData(_ i: Install) {
        try? FileManager.default.removeItem(at: dataURL(i))
        try? FileManager.default.createDirectory(at: dataURL(i), withIntermediateDirectories: true)
        objectWillChange.send()
    }

    func exportData(_ i: Install) -> URL? {
        var coordinationError: NSError?
        var result: URL?
        let safe = i.name.components(separatedBy: CharacterSet.alphanumerics.inverted).filter { !$0.isEmpty }.joined(separator: "_")
        try? FileManager.default.createDirectory(at: dataURL(i), withIntermediateDirectories: true)
        NSFileCoordinator().coordinate(readingItemAt: dataURL(i), options: .forUploading, error: &coordinationError) { zip in
            let dest = FileManager.default.temporaryDirectory.appendingPathComponent("\(safe.isEmpty ? "OpenBGI" : safe)-data.zip")
            try? FileManager.default.removeItem(at: dest)
            if (try? FileManager.default.copyItem(at: zip, to: dest)) != nil { result = dest }
        }
        return result
    }

    func logText(_ i: Install) -> String {
        guard let data = try? Data(contentsOf: logURL(i)) else { return "" }
        return String(decoding: data.suffix(24_000), as: UTF8.self)
    }

    func clearLog(_ i: Install) {
        try? FileManager.default.removeItem(at: logURL(i))
        objectWillChange.send()
    }

    // MARK: Playing

    private func detectCrash() {
        for i in installs where FileManager.default.fileExists(atPath: flagURL(i).path) {
            crashedInstall = i.name
            try? FileManager.default.removeItem(at: flagURL(i))
        }
    }

    func arguments(_ i: Install) -> [String] {
        var a = [
            gameURL(i).path,
            "--launcher",
            "--window",
            "--data-dir", dataURL(i).path,
            "--entry", i.options.entryArc, i.options.entryName
        ]
        if i.options.driver != .auto {
            a += ["--driver", i.options.driver.rawValue]
        }
        return a
    }

    func canPlay(_ i: Install) -> Bool {
        guard playing == nil else { return false }
        return probes[i.id]?.ok ?? false
    }

    func play(_ i: Install) {
        guard canPlay(i) else { return }
        try? FileManager.default.createDirectory(at: dataURL(i), withIntermediateDirectories: true)
        try? FileManager.default.createDirectory(at: logURL(i).deletingLastPathComponent(), withIntermediateDirectories: true)
        try? "".write(to: logURL(i), atomically: true, encoding: .utf8)

        #if os(iOS)
        playing = i.id
        update(i.id) { $0.lastPlayedAt = Date() }
        let args = arguments(i), log = logURL(i), flag = flagURL(i)
        FileManager.default.createFile(atPath: flag.path, contents: nil)
        let started = Date()
        Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 150_000_000)
            let error = Engine.run(arguments: args, log: log)
            try? FileManager.default.removeItem(at: flag)
            guard let self else { return }
            self.playing = nil
            self.update(i.id) { $0.playSeconds += Date().timeIntervalSince(started) }
            Engine.restoreLauncherWindow()
            if let error { self.lastError = error }
        }
        #else
        guard let helper = Bundle.main.bundleURL.appendingPathComponent("Contents/Helpers/obgi_run") as URL?,
              FileManager.default.isExecutableFile(atPath: helper.path) else {
            lastError = "Missing helper binary: Contents/Helpers/obgi_run"
            return
        }
        let proc = Process()
        proc.executableURL = helper
        var procArgs = arguments(i) + ["--log-file", logURL(i).path]
        if let resPath = Bundle.main.resourcePath {
            procArgs += ["--resources-dir", resPath]
        }
        proc.arguments = procArgs
        let pipe = Pipe()
        proc.standardError = pipe
        playing = i.id
        update(i.id) { $0.lastPlayedAt = Date() }
        FileManager.default.createFile(atPath: flagURL(i).path, contents: nil)
        let started = Date()
        player = proc
        NSApp.windows.forEach { $0.orderOut(nil) }
        proc.terminationHandler = { [weak self] p in
            let errData = pipe.fileHandleForReading.readDataToEndOfFile()
            Task { @MainActor [weak self] in
                guard let self else { return }
                try? FileManager.default.removeItem(at: self.flagURL(i))
                self.player = nil
                self.playing = nil
                self.update(i.id) { $0.playSeconds += Date().timeIntervalSince(started) }
                NSApp.activate(ignoringOtherApps: true)
                NSApp.windows.forEach { $0.makeKeyAndOrderFront(nil) }
                if p.terminationStatus != 0 && p.terminationStatus != 15 {
                    let msg = String(decoding: errData, as: UTF8.self).trimmingCharacters(in: .whitespacesAndNewlines)
                    self.lastError = msg.isEmpty ? "Engine exited with code \(p.terminationStatus)" : msg
                }
            }
        }
        do {
            try proc.run()
        } catch {
            try? FileManager.default.removeItem(at: flagURL(i))
            player = nil
            playing = nil
            NSApp.windows.forEach { $0.makeKeyAndOrderFront(nil) }
            lastError = error.localizedDescription
        }
        #endif
    }

    #if os(macOS)
    func stop() {
        player?.terminate()
    }
    #endif
}
