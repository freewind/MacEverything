#pragma once
#include <shared_mutex>
#include <string>
#include <vector>

/// Gitignore rule evaluation backed by libgit2 — the same implementation git uses.
///
/// The rules live in a throwaway bare repository: libgit2 keeps a per-repository
/// "internal" rule list that is configured in memory and never persisted, and a
/// bare repository has no workdir, so libgit2 neither reads `.gitignore` files
/// from the filesystem nor needs a real repo tree. That yields deterministic,
/// authoritative gitignore semantics: anchoring, negation, directory-only
/// patterns, '**', and macOS case-insensitive matching.
///
/// Rule lookups may run concurrently; replacing the rule set takes the write
/// lock, because libgit2 does not synchronize its rule vector against readers.
class IgnoreRules {
public:
    IgnoreRules() = default;
    ~IgnoreRules();

    IgnoreRules(const IgnoreRules&) = delete;
    IgnoreRules& operator=(const IgnoreRules&) = delete;

    /// Create (or reopen) the bare repository used to host the rules.
    /// `repoDir` must be writable. Returns false when libgit2 is unavailable.
    bool init(const std::string& repoDir);

    /// True once the backing repository exists.
    bool isReady() const;

    /// Replace the active rules with gitignore-format text (one rule per line).
    bool setRules(const std::string& rulesText);

    /// Replace the active rules from individual lines.
    bool setRuleLines(const std::vector<std::string>& lines);

    /// True when no usable rule is configured.
    bool isEmpty() const;

    /// Number of configured rule lines (diagnostics).
    size_t ruleCount() const;

    /// Configured rules, one line per entry.
    std::vector<std::string> ruleLines() const;

    /// fullPath must be absolute. Directories get a trailing slash, which is how
    /// gitignore distinguishes directory-only patterns from file patterns.
    bool isIgnored(const std::string& fullPath, bool isDir) const;

    /// Expand a leading "~/" to $HOME. gitignore itself has no tilde expansion;
    /// this is a convenience for the settings UI.
    static std::string expandTilde(const std::string& line);

private:
    /// Caller must hold the unique lock.
    bool applyRulesLocked();

    mutable std::shared_mutex mutex_;
    void* repo_ = nullptr;  // git_repository*
    bool libgit2Ready_ = false;
    std::vector<std::string> lines_;
    std::string text_;
};
