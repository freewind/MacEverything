import Foundation

/// Compact modification-time text for result rows.
///
/// Relative while the file is fresh (`now`, `5m`, `3h`, `2d`); anything three
/// days or older falls back to an absolute timestamp so old rows stay readable.
enum TimeText {
    /// Age at which the absolute timestamp takes over (3 days).
    static let absoluteThreshold: TimeInterval = 3 * 24 * 60 * 60

    private static let absoluteFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd HH:mm"
        return formatter
    }()

    /// Format a record's modification time (Unix seconds).
    static func display(unixSeconds: time_t, now: Date = Date()) -> String {
        display(date: Date(timeIntervalSince1970: TimeInterval(unixSeconds)), now: now)
    }

    static func display(date: Date, now: Date = Date()) -> String {
        let age = now.timeIntervalSince(date)

        // Future timestamps (clock skew) are shown verbatim instead of as a
        // nonsensical relative value.
        if age < 0 || age >= absoluteThreshold {
            return absoluteFormatter.string(from: date)
        }
        if age < 60 { return "now" }
        if age < 3_600 { return "\(Int(age / 60))m" }
        if age < 86_400 { return "\(Int(age / 3_600))h" }
        return "\(Int(age / 86_400))d"
    }
}
