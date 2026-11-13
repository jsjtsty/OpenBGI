import Foundation
import SwiftUI
#if os(macOS)
import AppKit
#else
import UIKit
#endif

struct GameRowView: View {
    @ObservedObject var store: LibraryStore
    let install: Install

    private var status: InstallStatus { store.status[install.id] ?? .missing }
    private var probe: ProbeInfo? { store.probes[install.id] }
    private var isPlaying: Bool { store.playing == install.id }

    var body: some View {
        HStack(spacing: 10) {
            ZStack {
                RoundedRectangle(cornerRadius: 8)
                    .fill(isPlaying ? Color.accentColor.gradient : Color.indigo.gradient)
                    .frame(width: 32, height: 32)
                Image(systemName: isPlaying ? "play.fill" : "gamecontroller.fill")
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundStyle(.white)
            }

            VStack(alignment: .leading, spacing: 2) {
                Text(install.name)
                    .font(.body)
                    .lineLimit(1)

                HStack(spacing: 6) {
                    Circle()
                        .fill(statusColor)
                        .frame(width: 6, height: 6)
                    Text(subtitleText)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                    if probe?.hasScPatch == true {
                        Text("SC")
                            .font(.system(size: 9, weight: .semibold))
                            .padding(.horizontal, 4)
                            .padding(.vertical, 1)
                            .background(.blue.opacity(0.15), in: Capsule())
                            .foregroundStyle(.blue)
                    }
                }
            }
            Spacer(minLength: 0)
        }
        .padding(.vertical, 2)
    }

    private var statusColor: Color {
        if isPlaying { return .blue }
        switch status {
        case .ready: return .green
        case .incomplete: return .orange
        case .missing: return .red
        }
    }

    private var subtitleText: String {
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

struct InstallDetailView: View {
    @ObservedObject var store: LibraryStore
    let install: Install
    @State private var showLog = false
    @State private var showClearConfirm = false
    @State private var showRemoveConfirm = false
    @State private var showRenameAlert = false
    @State private var renameText = ""
    @State private var exportURL: URL?

    private var probe: ProbeInfo? { store.probes[install.id] }
    private var status: InstallStatus { store.status[install.id] ?? .missing }
    private var isPlaying: Bool { store.playing == install.id }

    var body: some View {
        Form {
            Section {
                HStack(spacing: 14) {
                    RoundedRectangle(cornerRadius: 14)
                        .fill(isPlaying ? Color.accentColor.gradient : Color.indigo.gradient)
                        .frame(width: 60, height: 60)
                        .overlay {
                            Image(systemName: isPlaying ? "play.fill" : "gamecontroller.fill")
                                .font(.title2)
                                .foregroundStyle(.white)
                        }
                        .shadow(color: .indigo.opacity(0.25), radius: 6, y: 3)

                    VStack(alignment: .leading, spacing: 5) {
                        Text(install.name)
                            .font(.title3.weight(.semibold))
                            .lineLimit(2)

                        HStack(spacing: 6) {
                            Text(install.displayPath)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .lineLimit(1)
                                .truncationMode(.middle)
                            #if os(macOS)
                            Button {
                                store.revealGameFolder(install)
                            } label: {
                                Image(systemName: "arrow.up.right.square")
                                    .font(.caption)
                            }
                            .buttonStyle(.borderless)
                            .help("Reveal in Finder")
                            #endif
                        }

                        HStack(spacing: 8) {
                            statusBadge
                            if probe?.hasScPatch == true {
                                Text("Simplified Chinese")
                                    .font(.caption2.weight(.medium))
                                    .padding(.horizontal, 6)
                                    .padding(.vertical, 2)
                                    .background(.blue.opacity(0.14), in: Capsule())
                                    .foregroundStyle(.blue)
                            }
                        }
                    }
                    Spacer()
                }
                .padding(.vertical, 4)

                HStack(spacing: 10) {
                    Button {
                        store.play(install)
                    } label: {
                        Label(isPlaying ? "Running" : "Play", systemImage: "play.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(!store.canPlay(install))

                    #if os(macOS)
                    if isPlaying {
                        Button(role: .destructive) {
                            store.stop()
                        } label: {
                            Label("Stop", systemImage: "stop.fill")
                        }
                        .controlSize(.large)
                    }
                    #endif
                }
            }

            Section("Engine Options") {
                Picker("GPU Driver", selection: store.options(install.id).driver) {
                    Text("Auto").tag(GpuDriver.auto)
                    Text("Metal").tag(GpuDriver.metal)
                }
            }

            Section("Data & Saves") {
                LabeledContent("Save Data Size", value: ByteCountFormatter.string(fromByteCount: store.dataBytes(install), countStyle: .file))
                if let last = install.lastPlayedAt {
                    LabeledContent("Last Played", value: last.formatted(date: .abbreviated, time: .shortened))
                }
                if install.playSeconds > 0 {
                    LabeledContent("Play Time", value: formatDuration(install.playSeconds))
                }
                HStack {
                    Button("Export Saves...") {
                        exportURL = store.exportData(install)
                    }
                    #if os(macOS)
                    Button("Open Save Folder") {
                        store.revealDataFolder(install)
                    }
                    #endif
                    Spacer()
                    Button("Clear Saves", role: .destructive) {
                        showClearConfirm = true
                    }
                    .disabled(isPlaying)
                }
            }

            Section("Diagnostics") {
                if let p = probe {
                    LabeledContent("Game ID", value: p.gameID)
                    LabeledContent("Archives Found", value: "\(p.arcsFound)")
                    if p.hasScPatch {
                        LabeledContent("Script Patch", value: "Simplified Chinese")
                    }
                    if !p.missing.isEmpty {
                        LabeledContent("Issues", value: p.missing)
                    }
                }
                Button("View Last Run Log") {
                    showLog = true
                }
            }

            Section {
                #if os(macOS)
                Button(role: .destructive) {
                    showRemoveConfirm = true
                } label: {
                    Label("Remove from Library...", systemImage: "trash")
                }
                .disabled(isPlaying)
                #else
                Button(role: .destructive) {
                    showRemoveConfirm = true
                } label: {
                    Label(store.canDeleteFiles(install) ? "Delete Game..." : "Remove from Library", systemImage: "trash")
                }
                .disabled(isPlaying)
                #endif
            }
        }
        .formStyle(.grouped)
        .navigationTitle(install.name)
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Menu {
                    Button {
                        renameText = install.name
                        showRenameAlert = true
                    } label: {
                        Label("Rename...", systemImage: "pencil")
                    }

                    #if os(macOS)
                    Button {
                        store.revealGameFolder(install)
                    } label: {
                        Label("Reveal in Finder", systemImage: "folder")
                    }
                    #endif

                    Divider()

                    Button(role: .destructive) {
                        showRemoveConfirm = true
                    } label: {
                        #if os(macOS)
                        Label("Remove from Library...", systemImage: "trash")
                        #else
                        Label(store.canDeleteFiles(install) ? "Delete Game..." : "Remove from Library", systemImage: "trash")
                        #endif
                    }
                    .disabled(isPlaying)
                } label: {
                    Label("More", systemImage: "ellipsis.circle")
                }
            }
        }
        .sheet(isPresented: $showLog) {
            LogSheetView(title: install.name, text: store.logText(install)) {
                store.clearLog(install)
            }
        }
        .sheet(item: $exportURL) { url in
            ShareSheet(url: url)
        }
        .alert("Rename Game", isPresented: $showRenameAlert) {
            TextField("Game Name", text: $renameText)
            Button("Save") {
                store.rename(install.id, to: renameText)
            }
            Button("Cancel", role: .cancel) {}
        }
        .confirmationDialog("Clear all saves and runtime data for \(install.name)?", isPresented: $showClearConfirm) {
            Button("Clear Saves", role: .destructive) { store.clearData(install) }
        }
        .confirmationDialog(removeDialogTitle, isPresented: $showRemoveConfirm, titleVisibility: .visible) {
            #if os(macOS)
            Button("Remove from Library") {
                store.remove(install.id, deleteSaves: false)
            }
            Button("Remove & Delete Saves", role: .destructive) {
                store.remove(install.id, deleteSaves: true)
            }
            #else
            Button(store.canDeleteFiles(install) ? "Delete Game & Saves" : "Remove from Library", role: .destructive) {
                store.deleteGame(install)
            }
            #endif
            Button("Cancel", role: .cancel) {}
        } message: {
            #if os(macOS)
            Text("Game files in \"\(install.displayPath)\" will not be deleted from disk.")
            #else
            if store.canDeleteFiles(install) {
                Text("This will permanently delete the game folder and saves from Documents.")
            } else {
                Text("Files directly in Documents will not be deleted.")
            }
            #endif
        }
    }

    private var removeDialogTitle: String {
        #if os(macOS)
        return String(localized: "Remove \"\(install.name)\" from library?")
        #else
        if store.canDeleteFiles(install) {
            return String(localized: "Delete \"\(install.name)\"?")
        }
        return String(localized: "Remove \"\(install.name)\" from library?")
        #endif
    }

    @ViewBuilder
    private var statusBadge: some View {
        switch status {
        case .ready:
            Label("Ready", systemImage: "checkmark.circle.fill")
                .font(.caption)
                .foregroundStyle(.green)
        case .incomplete(let missing):
            Label("Incomplete: \(missing)", systemImage: "exclamationmark.triangle.fill")
                .font(.caption)
                .foregroundStyle(.orange)
        case .missing:
            Label("Folder Missing", systemImage: "xmark.circle.fill")
                .font(.caption)
                .foregroundStyle(.red)
        }
    }

    private func formatDuration(_ sec: Double) -> String {
        let mins = Int(sec) / 60
        if mins < 60 { return "\(mins)m" }
        return "\(mins / 60)h \(mins % 60)m"
    }
}

struct LogSheetView: View {
    let title: String
    let text: String
    var onClear: (() -> Void)? = nil
    @Environment(\.dismiss) private var dismiss
    @State private var copied = false

    var body: some View {
        NavigationStack {
            ScrollView {
                Text(text.isEmpty ? "No log output recorded yet." : text)
                    .font(.system(.caption, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding()
                    .textSelection(.enabled)
            }
            .navigationTitle("\(title) Log")
            .toolbar {
                ToolbarItemGroup(placement: .cancellationAction) {
                    if !text.isEmpty {
                        Button(copied ? "Copied" : "Copy") {
                            copyToPasteboard(text)
                            copied = true
                        }
                        if let onClear {
                            Button("Clear", role: .destructive) {
                                onClear()
                                dismiss()
                            }
                        }
                    }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        #if os(macOS)
        .frame(minWidth: 540, minHeight: 380)
        #endif
    }

    private func copyToPasteboard(_ s: String) {
        #if os(macOS)
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(s, forType: .string)
        #else
        UIPasteboard.general.string = s
        #endif
    }
}

struct AboutView: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Form {
                Section("OpenBGI") {
                    LabeledContent("Kernel Version", value: Engine.version)
                    Text("BGI-compatible runtime launcher for macOS and iOS.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
            .formStyle(.grouped)
            .navigationTitle("About OpenBGI")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        #if os(macOS)
        .frame(minWidth: 380, minHeight: 220)
        #endif
    }
}

extension URL: @retroactive Identifiable {
    public var id: String { absoluteString }
}

struct ShareSheet: View {
    let url: URL
    var body: some View {
        #if os(iOS)
        ActivityView(url: url)
        #else
        VStack(spacing: 12) {
            Text("Exported archive ready:")
            Text(url.path).font(.caption).textSelection(.enabled)
            Button("Reveal in Finder") {
                NSWorkspace.shared.activateFileViewerSelecting([url])
            }
        }
        .padding()
        .frame(minWidth: 360, minHeight: 160)
        #endif
    }
}

#if os(iOS)
private struct ActivityView: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: [url], applicationActivities: nil)
    }
    func updateUIViewController(_ uiViewController: UIActivityViewController, context: Context) {}
}
#endif
