import SwiftUI

/// Ignore rules for indexing and search (gitignore syntax).
///
/// Deliberately separate from Content Settings: this window only manages ignore
/// rules, and Save applies them to queries immediately without reindexing.
struct IgnoreSettingsView: View {
    @State private var ignoreText: String = ""
    @State private var initialIgnoreText: String = ""
    @State private var statusMessage: String = ""
    @State private var showReindexConfirmation: Bool = false

    private let bridge = MacSearchBridge.shared()

    var body: some View {
        Form {
            Section("Ignore Paths") {
                VStack(alignment: .leading, spacing: 8) {
                    Text("One gitignore-style pattern per line. Ignored files and folders are skipped by indexing and search.")
                        .font(.caption)
                        .foregroundStyle(.secondary)

                    TextEditor(text: $ignoreText)
                        .font(.system(.caption, design: .monospaced))
                        .frame(minHeight: 180)
                        .overlay(
                            RoundedRectangle(cornerRadius: 6)
                                .stroke(Color.secondary.opacity(0.35), lineWidth: 1)
                        )

                    Text("Examples: node_modules/   *.log   **/pnpm/store/   ~/tmp/   /build   !keep.log")
                        .font(.caption2)
                        .foregroundStyle(.secondary)

                    Text("Save makes the rules apply to search right away — existing index entries are just filtered out. Reindex is only needed to physically drop them from the stored index, and takes much longer.")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            Section {
                HStack {
                    Button("Save") { saveSettings() }
                    Button("Reindex") { showReindexConfirmation = true }
                    Spacer()
                    if !statusLabel.isEmpty {
                        Text(statusLabel)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .padding()
        .frame(width: 420, height: 460)
        .onAppear { loadSettings() }
        .alert("Reindex the whole index?", isPresented: $showReindexConfirmation) {
            Button("Cancel", role: .cancel) {}
            Button("Reindex", role: .destructive) { reindex() }
        } message: {
            Text("This rescans every file from disk and can take a long time. Ignore rules already apply to search right after Save — reindex only when you want ignored entries physically removed from the stored index.")
        }
    }

    private var ignoreLines: [String] {
        ignoreText.components(separatedBy: "\n")
    }

    private var isDirty: Bool {
        ignoreText != initialIgnoreText
    }

    private var statusLabel: String {
        if !statusMessage.isEmpty { return statusMessage }
        return isDirty ? "Unsaved changes" : ""
    }

    private func loadSettings() {
        let patterns = (bridge.contentGetIgnorePatterns() as? [String]) ?? []
        ignoreText = patterns.joined(separator: "\n")
        initialIgnoreText = ignoreText
    }

    private func applyIgnoreRules() {
        bridge.setContentIgnorePatterns(ignoreLines)
        bridge.contentSaveSettings()
        initialIgnoreText = ignoreText
        // Query-side effect only: re-run the current search so the new rules
        // take effect without waiting for a reindex.
        NotificationCenter.default.post(name: .contentSettingsChanged, object: nil)
    }

    private func saveSettings() {
        applyIgnoreRules()
        statusMessage = "Saved"
    }

    private func reindex() {
        applyIgnoreRules()
        statusMessage = "Reindexing…"
        // Rebuild the saved index so ignored paths leave it. ContentView owns the
        // rescan pipeline; a notification keeps both indexes in sync.
        NotificationCenter.default.post(name: .rebuildIndex, object: nil)
    }
}
