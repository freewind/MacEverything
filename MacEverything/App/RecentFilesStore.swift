import Foundation

/// Files the user opened from MacEverything, newest first.
///
/// "Recent" means "recently used", which a file's modification time cannot
/// express: a file can be months old and still be the one you just opened.
/// Bounded, persisted in UserDefaults.
final class RecentFilesStore {
    static let shared = RecentFilesStore()

    /// Maximum number of remembered paths.
    static let limit = 100

    struct Entry: Codable {
        let path: String
        let openedAt: Date
    }

    private static let defaultsKey = "recentOpenedFiles"

    private let defaults: UserDefaults
    private var entries: [Entry]

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        self.entries = Self.load(from: defaults)
    }

    /// Record that the user opened or revealed `fullPath`.
    func record(fullPath: String) {
        guard !fullPath.isEmpty else { return }

        entries.removeAll { $0.path == fullPath }
        entries.insert(Entry(path: fullPath, openedAt: Date()), at: 0)
        if entries.count > Self.limit {
            entries.removeLast(entries.count - Self.limit)
        }
        save()
    }

    /// Remembered paths, newest first. Entries whose file no longer exists are
    /// pruned on the way out so the list never points at deleted files.
    func recentEntries() -> [Entry] {
        let alive = entries.filter { FileManager.default.fileExists(atPath: $0.path) }
        if alive.count != entries.count {
            entries = alive
            save()
        }
        return entries
    }

    private func save() {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .secondsSince1970
        if let data = try? encoder.encode(entries) {
            defaults.set(data, forKey: Self.defaultsKey)
        }
    }

    private static func load(from defaults: UserDefaults) -> [Entry] {
        guard let data = defaults.data(forKey: defaultsKey) else { return [] }
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .secondsSince1970
        return (try? decoder.decode([Entry].self, from: data)) ?? []
    }
}
