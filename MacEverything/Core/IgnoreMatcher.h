#pragma once
#include <memory>
#include <string>
#include <vector>

/// One parsed ignore pattern (gitignore syntax).
struct IgnoreRule {
    std::vector<std::string> segments;  // pattern split by '/', "**" kept as its own segment
    bool negated = false;               // leading '!' — re-include
    bool dirOnly = false;               // trailing '/' — matches directories only
    bool basenameOnly = false;          // no '/' in pattern — matches the name at any depth
};

/// Gitignore-format path matching.
///
/// Supported syntax: '# comment', blank lines, '!negation', leading '/' anchoring,
/// trailing '/' (directory only), '*', '?', '[...]' char classes, '\' escapes and
/// '**' (multi-segment wildcard). Matching is case-insensitive, like git on macOS.
///
/// Instances are immutable once built: parse a full rule list, then publish the
/// matcher with std::shared_ptr so readers never observe a half-updated state.
class IgnoreMatcher {
public:
    IgnoreMatcher() = default;

    /// Parse gitignore-style lines (comments and blanks are dropped).
    void setLines(const std::vector<std::string>& lines);

    /// Parse from one text blob where each line is a pattern.
    void setText(const std::string& text);

    /// True when no usable pattern is present.
    bool isEmpty() const { return rules_.empty(); }

    /// Number of parsed rules (used for diagnostics).
    size_t ruleCount() const { return rules_.size(); }

    /// relPath is relative to the ignore base and has no leading '/'.
    /// Returns true when the path must be skipped.
    bool isIgnored(const std::string& relPath, bool isDir) const;

    /// Convenience: strip `base` from `fullPath` and evaluate the remainder.
    /// Returns false when fullPath is not under base.
    bool isIgnoredFullPath(const std::string& base, const std::string& fullPath, bool isDir) const;

    /// Turn a list of lines into a shared, immutable matcher.
    static std::shared_ptr<IgnoreMatcher> fromLines(const std::vector<std::string>& lines);

    /// Turn free-form text (one pattern per line) into a shared matcher.
    static std::shared_ptr<IgnoreMatcher> fromText(const std::string& text);

private:
    std::vector<IgnoreRule> rules_;

    /// Last-match-wins evaluation for exactly this path (no ancestor expansion).
    bool matchPathOnly(const std::string& relPath, bool isDir) const;

    static bool ruleMatchesPath(const IgnoreRule& rule,
                                const std::vector<std::string>& pathSegments);
    static bool segmentsMatch(const std::vector<std::string>& pattern, size_t patternIndex,
                              const std::vector<std::string>& path, size_t pathIndex);
    static bool segmentMatch(const std::string& pattern, const std::string& text);
    static bool charClassMatch(const std::string& charClass, char c);
    static std::vector<std::string> splitSegments(const std::string& path);
    static std::string trimRight(const std::string& s);
    static char lowerAscii(char c);
};
