import AppKit
import SwiftUI
import UniformTypeIdentifiers

@main
struct OpenBGIMacOSApp: App {
    @StateObject private var store = LibraryStore()
    @State private var selection: String?
    @State private var searchText = ""
    @State private var showAbout = false
    @State private var isDropTargeted = false
    @State private var pendingRemove: Install?
    @State private var pendingRename: Install?
    @State private var renameText = ""

    private var filteredInstalls: [Install] {
        let q = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !q.isEmpty else { return store.installs }
        return store.installs.filter {
            $0.name.localizedCaseInsensitiveContains(q) || $0.gameID.localizedCaseInsensitiveContains(q)
        }
    }

    var body: some Scene {
        WindowGroup("OpenBGI") {
            NavigationSplitView {
                List(filteredInstalls, selection: $selection) { install in
                    GameRowView(store: store, install: install)
                        .tag(install.id)
                        .contextMenu {
                            Button {
                                store.play(install)
                            } label: {
                                Label("Play", systemImage: "play.fill")
                            }
                            .disabled(!store.canPlay(install))

                            Button {
                                renameText = install.name
                                pendingRename = install
                            } label: {
                                Label("Rename...", systemImage: "pencil")
                            }

                            Button {
                                store.revealGameFolder(install)
                            } label: {
                                Label("Reveal in Finder", systemImage: "folder")
                            }

                            Divider()

                            Button(role: .destructive) {
                                pendingRemove = install
                            } label: {
                                Label("Remove from Library...", systemImage: "trash")
                            }
                            .disabled(store.playing == install.id)
                        }
                }
                .onDeleteCommand {
                    if let id = selection, let install = store.install(id), store.playing != id {
                        pendingRemove = install
                    }
                }
                .searchable(text: $searchText, placement: .sidebar, prompt: "Search Games")
                .navigationSplitViewColumnWidth(min: 220, ideal: 260)
            } detail: {
                if let id = selection ?? store.installs.first?.id,
                   let install = store.install(id) {
                    InstallDetailView(store: store, install: install)
                } else {
                    ContentUnavailableView {
                        Label("No BGI Games Found", systemImage: "folder")
                    } description: {
                        Text("Click Add Folder or drop a game folder here (must contain autoload.arc and system.arc).")
                    } actions: {
                        Button("Add Folder...") { pickFolder() }
                            .buttonStyle(.borderedProminent)
                    }
                }
            }
            .overlay {
                if isDropTargeted {
                    RoundedRectangle(cornerRadius: 12)
                        .strokeBorder(Color.accentColor, style: StrokeStyle(lineWidth: 3, dash: [8, 4]))
                        .background(Color.accentColor.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
                        .padding(10)
                        .allowsHitTesting(false)
                }
            }
            .toolbar {
                ToolbarItemGroup {
                    Button { pickFolder() } label: {
                        Label("Add Folder", systemImage: "folder.badge.plus")
                    }
                    Button { store.sync() } label: {
                        Label("Rescan", systemImage: "arrow.clockwise")
                    }
                }
            }
            .onDrop(of: [.fileURL], isTargeted: $isDropTargeted) { providers in
                for provider in providers {
                    _ = provider.loadObject(ofClass: URL.self) { url, _ in
                        guard let url else { return }
                        Task { @MainActor in
                            if let addedID = store.addSource(url) {
                                selection = addedID
                            }
                        }
                    }
                }
                return true
            }
            .sheet(isPresented: $showAbout) { AboutView() }
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
            .alert("Rename Game", isPresented: Binding(
                get: { pendingRename != nil },
                set: { if !$0 { pendingRename = nil } }
            )) {
                TextField("Game Name", text: $renameText)
                Button("Save") {
                    if let target = pendingRename {
                        store.rename(target.id, to: renameText)
                    }
                    pendingRename = nil
                }
                Button("Cancel", role: .cancel) { pendingRename = nil }
            }
            .confirmationDialog(
                "Remove \"\(pendingRemove?.name ?? "")\" from library?",
                isPresented: Binding(
                    get: { pendingRemove != nil },
                    set: { if !$0 { pendingRemove = nil } }
                ),
                titleVisibility: .visible
            ) {
                Button("Remove from Library") {
                    if let target = pendingRemove {
                        store.remove(target.id, deleteSaves: false)
                    }
                    pendingRemove = nil
                }
                Button("Remove & Delete Saves", role: .destructive) {
                    if let target = pendingRemove {
                        store.remove(target.id, deleteSaves: true)
                    }
                    pendingRemove = nil
                }
                Button("Cancel", role: .cancel) { pendingRemove = nil }
            } message: {
                if let target = pendingRemove {
                    Text("Game files in \"\(target.displayPath)\" will not be deleted from disk.")
                }
            }
            .onAppear {
                if selection == nil { selection = store.installs.first?.id }
            }
            .onChange(of: store.installs) { _, list in
                if let sel = selection, !list.contains(where: { $0.id == sel }) {
                    selection = list.first?.id
                } else if selection == nil {
                    selection = list.first?.id
                }
            }
        }
        .commands {
            CommandGroup(replacing: .appInfo) {
                Button("About OpenBGI") { showAbout = true }
            }
            CommandGroup(after: .newItem) {
                Button("Add Folder...") { pickFolder() }
                    .keyboardShortcut("o", modifiers: .command)
                Button("Rescan") { store.sync() }
                    .keyboardShortcut("r", modifiers: .command)
            }
        }
    }

    private func pickFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Add Game Folder"
        if panel.runModal() == .OK, let url = panel.url {
            if let addedID = store.addSource(url) {
                selection = addedID
            } else {
                selection = store.installs.last?.id
            }
        }
    }
}
