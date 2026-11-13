import Foundation

enum GpuDriver: String, Codable, CaseIterable {
    case auto
    case metal
}

struct PlayOptions: Codable, Equatable {
    var driver: GpuDriver = .auto
    var entryArc: String = "autoload.arc"
    var entryName: String = "ipl._bp"
}

enum InstallStorage: Codable, Equatable, Hashable {
    case documents(folder: String)
    case path(String)
}

struct Install: Identifiable, Codable, Equatable {
    var id: String
    var gameID: String
    var name: String
    var storage: InstallStorage
    var options = PlayOptions()
    var addedAt = Date()
    var lastPlayedAt: Date?
    var playSeconds: Double = 0

    var displayPath: String {
        switch storage {
        case .documents(let folder):
            return folder == "." ? "Documents" : "Documents/\(folder)"
        case .path(let path):
            return (path as NSString).abbreviatingWithTildeInPath
        }
    }
}

enum InstallStatus: Equatable {
    case ready
    case incomplete(missing: String)
    case missing
}

struct ProbeInfo: Equatable {
    var ok: Bool
    var complete: Bool
    var arcsFound: Int
    var hasScPatch: Bool
    var gameID: String
    var gameName: String
    var missing: String
    var message: String
}

struct DetectedGame: Equatable {
    var id: String
    var name: String
}
