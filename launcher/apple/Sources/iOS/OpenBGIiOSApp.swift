import SwiftUI
import UIKit
import UniformTypeIdentifiers

@main
struct OpenBGIiOSApp: App {
    @StateObject private var store = LibraryStore()
    @Environment(\.scenePhase) private var scenePhase
    @State private var showAbout = false
    @State private var showImporter = false
    @State private var isImporting = false
    @State private var importError: String?
    @State private var searchText = ""
    @State private var pendingDelete: Install?

    private let rescanTimer = Timer.publish(every: 3, on: .main, in: .common).autoconnect()

    private var filteredInstalls: [Install] {
        let q = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        if q.isEmpty { return store.installs }
        return store.installs.filter { $0.name.localizedCaseInsensitiveContains(q) }
    }

    var body: some Scene {
        WindowGroup {
            NavigationStack {
                Group {
                    if store.installs.isEmpty {
                        emptyView
                    } else {
                        libraryList
                    }
                }
                .navigationTitle("OpenBGI")
                .toolbar {
                    ToolbarItemGroup(placement: .topBarLeading) {
                        Button {
                            showImporter = true
                        } label: {
                            Image(systemName: "folder.badge.plus")
                        }
                        .disabled(isImporting)

                        Button {
                            store.sync()
                        } label: {
                            Image(systemName: "arrow.clockwise")
                        }
                        .disabled(isImporting)
                    }
                    ToolbarItem(placement: .topBarTrailing) {
                        Button {
                            showAbout = true
                        } label: {
                            Image(systemName: "info.circle")
                        }
                    }
                }
                .overlay {
                    if isImporting {
                        ZStack {
                            Color.black.opacity(0.25).ignoresSafeArea()
                            VStack(spacing: 12) {
                                ProgressView()
                                    .controlSize(.large)
                                Text("Importing game folder...")
                                    .font(.subheadline.weight(.medium))
                            }
                            .padding(24)
                            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
                        }
                    }
                }
            }
            .sheet(isPresented: $showAbout) { AboutView() }
            .fileImporter(isPresented: $showImporter, allowedContentTypes: [.folder]) { result in
                guard case .success(let url) = result else { return }
                isImporting = true
                Task {
                    let err = await store.importGameFolder(from: url)
                    isImporting = false
                    if let err { importError = err }
                }
            }
            .alert("Import Failed", isPresented: Binding(
                get: { importError != nil },
                set: { if !$0 { importError = nil } }
            )) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(importError ?? "")
            }
            .alert("Engine Error", isPresented: Binding(
                get: { store.lastError != nil },
                set: { if !$0 { store.lastError = nil } }
            )) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(store.lastError ?? "")
            }
            .alert("Previous Run Ended Unexpectedly", isPresented: Binding(
                get: { store.crashedInstall != nil },
                set: { if !$0 { store.crashedInstall = nil } }
            )) {
                Button("OK", role: .cancel) {}
            } message: {
                Text("\(store.crashedInstall ?? "The game") exited unexpectedly during its last session.")
            }
            .confirmationDialog(
                pendingDelete.map { store.canDeleteFiles($0) ? String(localized: "Delete \"\($0.name)\"?") : String(localized: "Remove \"\($0.name)\" from library?") } ?? "",
                isPresented: Binding(
                    get: { pendingDelete != nil },
                    set: { if !$0 { pendingDelete = nil } }
                ),
                titleVisibility: .visible
            ) {
                if let target = pendingDelete {
                    Button(store.canDeleteFiles(target) ? "Delete Game & Saves" : "Remove from Library", role: .destructive) {
                        store.deleteGame(target)
                        pendingDelete = nil
                    }
                }
                Button("Cancel", role: .cancel) { pendingDelete = nil }
            } message: {
                if let target = pendingDelete {
                    if store.canDeleteFiles(target) {
                        Text("This will permanently delete the game folder and saves from Documents.")
                    } else {
                        Text("Files directly in Documents will not be deleted.")
                    }
                }
            }
            .onReceive(rescanTimer) { _ in
                if scenePhase == .active && store.playing == nil {
                    store.sync()
                }
            }
            .onChange(of: scenePhase) { _, phase in
                if phase == .active && store.playing == nil {
                    store.sync()
                }
            }
        }
    }

    private var libraryList: some View {
        List {
            Section {
                ForEach(filteredInstalls) { install in
                    NavigationLink {
                        InstallDetailView(store: store, install: install)
                    } label: {
                        iOSGameCardRow(store: store, install: install)
                    }
                    .swipeActions(edge: .trailing, allowsFullSwipe: false) {
                        if store.playing != install.id {
                            Button(role: .destructive) {
                                pendingDelete = install
                            } label: {
                                Label(store.canDeleteFiles(install) ? "Delete" : "Remove", systemImage: "trash")
                            }
                        }
                    }
                }
            } header: {
                HStack {
                    Text("Games")
                    Spacer()
                    Text("\(filteredInstalls.count)")
                        .foregroundStyle(.secondary)
                }
            } footer: {
                Text("Put game folders in Files -> On My iPhone -> OpenBGI, or tap the folder button above to import.")
                    .font(.footnote)
            }
        }
        .listStyle(.insetGrouped)
        .refreshable {
            store.sync()
        }
        .if(store.installs.count > 1) { view in
            view.searchable(text: $searchText, prompt: "Search Games")
        }
    }

    private var emptyView: some View {
        ScrollView {
            VStack(spacing: 24) {
                VStack(spacing: 12) {
                    ZStack {
                        RoundedRectangle(cornerRadius: 22, style: .continuous)
                            .fill(Color.indigo.gradient)
                            .frame(width: 76, height: 76)
                            .shadow(color: .indigo.opacity(0.28), radius: 12, y: 6)
                        Image(systemName: "gamecontroller.fill")
                            .font(.system(size: 34, weight: .semibold))
                            .foregroundStyle(.white)
                    }

                    Text("No BGI Games Found")
                        .font(.title2.weight(.bold))

                    Text("Add a game folder containing autoload.arc and system.arc to start playing.")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 12)
                }
                .padding(.top, 24)

                VStack(alignment: .leading, spacing: 14) {
                    stepRow(
                        number: "1",
                        title: "Open the Files App",
                        detail: "Navigate to On My iPhone -> OpenBGI."
                    )
                    Divider()
                    stepRow(
                        number: "2",
                        title: "Copy Your Game Folder",
                        detail: "Place the folder with autoload.arc and system.arc inside OpenBGI."
                    )
                    Divider()
                    stepRow(
                        number: "3",
                        title: "Return & Play",
                        detail: "OpenBGI detects new games automatically, or tap Rescan."
                    )
                }
                .padding(16)
                .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 16, style: .continuous))

                VStack(spacing: 10) {
                    Button {
                        if let url = URL(string: "shareddocuments://") {
                            UIApplication.shared.open(url)
                        }
                    } label: {
                        Label("Open Files App", systemImage: "folder")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)

                    HStack(spacing: 10) {
                        Button {
                            showImporter = true
                        } label: {
                            Label("Import Folder...", systemImage: "folder.badge.plus")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.large)

                        Button {
                            store.sync()
                        } label: {
                            Label("Rescan", systemImage: "arrow.clockwise")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.large)
                    }
                }
            }
            .padding(.horizontal, 20)
            .padding(.bottom, 32)
        }
        .background(Color(.systemGroupedBackground))
        .refreshable {
            store.sync()
        }
    }

    private func stepRow(number: String, title: LocalizedStringKey, detail: LocalizedStringKey) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Text(number)
                .font(.caption.weight(.bold))
                .foregroundStyle(.white)
                .frame(width: 22, height: 22)
                .background(Color.accentColor, in: Circle())
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.subheadline.weight(.semibold))
                Text(detail)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer(minLength: 0)
        }
    }
}

private struct iOSGameCardRow: View {
    @ObservedObject var store: LibraryStore
    let install: Install

    private var status: InstallStatus { store.status[install.id] ?? .missing }
    private var probe: ProbeInfo? { store.probes[install.id] }
    private var isPlaying: Bool { store.playing == install.id }

    var body: some View {
        HStack(spacing: 12) {
            ZStack {
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(isPlaying ? Color.accentColor.gradient : Color.indigo.gradient)
                    .frame(width: 50, height: 50)
                    .shadow(color: (isPlaying ? Color.accentColor : Color.indigo).opacity(0.22), radius: 5, y: 2)
                Image(systemName: isPlaying ? "play.fill" : "gamecontroller.fill")
                    .font(.system(size: 20, weight: .semibold))
                    .foregroundStyle(.white)
            }

            VStack(alignment: .leading, spacing: 4) {
                Text(install.name)
                    .font(.headline)
                    .lineLimit(1)

                HStack(spacing: 6) {
                    Circle()
                        .fill(statusColor)
                        .frame(width: 7, height: 7)
                    Text(statusText)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)

                    if probe?.hasScPatch == true {
                        Text("SC")
                            .font(.system(size: 10, weight: .semibold))
                            .padding(.horizontal, 5)
                            .padding(.vertical, 1)
                            .background(.blue.opacity(0.14), in: Capsule())
                            .foregroundStyle(.blue)
                    }
                }
            }

            Spacer(minLength: 8)

            Button {
                store.play(install)
            } label: {
                Label(isPlaying ? "Running" : "Play", systemImage: "play.fill")
                    .font(.subheadline.weight(.semibold))
                    .labelStyle(.iconOnly)
                    .frame(width: 36, height: 36)
            }
            .buttonStyle(.borderedProminent)
            .clipShape(Circle())
            .disabled(!store.canPlay(install))
        }
        .padding(.vertical, 4)
    }

    private var statusColor: Color {
        if isPlaying { return .blue }
        switch status {
        case .ready: return .green
        case .incomplete: return .orange
        case .missing: return .red
        }
    }

    private var statusText: String {
        if isPlaying { return String(localized: "Running") }
        switch status {
        case .ready:
            if install.playSeconds >= 60 {
                let mins = Int(install.playSeconds) / 60
                return mins < 60 ? "\(mins)m" : "\(mins / 60)h \(mins % 60)m"
            }
            return String(localized: "Ready")
        case .incomplete:
            return String(localized: "Incomplete")
        case .missing:
            return String(localized: "Folder Missing")
        }
    }
}

private extension View {
    @ViewBuilder
    func `if`<Content: View>(_ condition: Bool, transform: (Self) -> Content) -> some View {
        if condition {
            transform(self)
        } else {
            self
        }
    }
}
