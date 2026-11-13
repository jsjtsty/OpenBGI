import Foundation
#if os(iOS)
import UIKit
#endif

enum Engine {
    static var version: String {
        String(cString: obgi_version())
    }

    static func detect(at dir: URL) -> DetectedGame? {
        var info = obgi_game_info()
        let ok = dir.path.withCString { obgi_detect($0, &info) }
        guard ok != 0 else { return nil }
        let id = withUnsafePointer(to: &info.id) { $0.withMemoryRebound(to: CChar.self, capacity: 64) { String(cString: $0) } }
        let name = withUnsafePointer(to: &info.name) { $0.withMemoryRebound(to: CChar.self, capacity: 128) { String(cString: $0) } }
        return DetectedGame(id: id, name: name)
    }

    static func probe(at dir: URL) -> ProbeInfo {
        var r = obgi_probe_result()
        _ = dir.path.withCString { obgi_probe($0, &r) }
        let gameID = withUnsafePointer(to: &r.game_id) { $0.withMemoryRebound(to: CChar.self, capacity: 64) { String(cString: $0) } }
        let gameName = withUnsafePointer(to: &r.game_name) { $0.withMemoryRebound(to: CChar.self, capacity: 128) { String(cString: $0) } }
        let missing = withUnsafePointer(to: &r.missing) { $0.withMemoryRebound(to: CChar.self, capacity: 512) { String(cString: $0) } }
        let message = withUnsafePointer(to: &r.message) { $0.withMemoryRebound(to: CChar.self, capacity: 512) { String(cString: $0) } }
        return ProbeInfo(
            ok: r.ok != 0,
            complete: r.complete != 0,
            arcsFound: Int(r.arcs_found),
            hasScPatch: r.has_sc_patch != 0,
            gameID: gameID,
            gameName: gameName,
            missing: missing,
            message: message
        )
    }

    #if os(iOS)
    @MainActor
    static func run(arguments: [String], log: URL) -> String? {
        var argv = ["obgi_run"] + arguments
        if let resPath = Bundle.main.resourcePath {
            argv += ["--resources-dir", resPath]
        }
        argv += ["--log-file", log.path]
        let cStrings = argv.map { strdup($0) }
        defer { cStrings.forEach { free($0) } }
        var ptrs: [UnsafeMutablePointer<CChar>?] = cStrings
        var errBuf = [CChar](repeating: 0, count: 1024)
        let rc = ptrs.withUnsafeMutableBufferPointer { buf in
            obgi_run(Int32(buf.count), buf.baseAddress, &errBuf, errBuf.count)
        }
        if rc != 0 {
            let msg = String(cString: errBuf)
            return msg.isEmpty ? "Engine exited with code \(rc)" : msg
        }
        return nil
    }

    @MainActor
    static func restoreLauncherWindow() {
        for scene in UIApplication.shared.connectedScenes {
            guard let ws = scene as? UIWindowScene else { continue }
            for window in ws.windows where window.rootViewController != nil {
                window.makeKeyAndVisible()
                return
            }
        }
    }
    #endif
}
