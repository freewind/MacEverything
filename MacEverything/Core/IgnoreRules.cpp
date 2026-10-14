#include "IgnoreRules.h"
#include "Logger.h"

#include <cstdlib>
#include <filesystem>

#include <git2.h>
#include <git2/sys/errors.h>  // git_error_clear

namespace {

constexpr const char* kTag = "IgnoreRules";
constexpr const char* kWarmupPath = "/__ignore_rules_warmup__";

std::string homeDirectory() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : std::string();
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

void logGitError(const char* what) {
    const git_error* err = git_error_last();
    LOG_WARN(kTag, what << ": " << (err && err->message ? err->message : "unknown error"));
}

}  // namespace

IgnoreRules::~IgnoreRules() {
    if (repo_) {
        git_repository_free(static_cast<git_repository*>(repo_));
        repo_ = nullptr;
    }
    if (libgit2Ready_) {
        git_libgit2_shutdown();
        libgit2Ready_ = false;
    }
}

bool IgnoreRules::init(const std::string& repoDir) {
    std::unique_lock lock(mutex_);
    if (repo_) return true;
    if (repoDir.empty()) return false;

    if (git_libgit2_init() < 0) {
        logGitError("git_libgit2_init failed");
        return false;
    }
    libgit2Ready_ = true;

    std::error_code ec;
    std::filesystem::create_directories(repoDir, ec);

    git_repository* repo = nullptr;
    int error = git_repository_open(&repo, repoDir.c_str());
    if (error != 0) {
        git_error_clear();
        error = git_repository_init(&repo, repoDir.c_str(), /*is_bare=*/1);
    }
    if (error != 0) {
        logGitError("failed to prepare ignore repository");
        return false;
    }

    repo_ = repo;
    LOG_INFO(kTag, "Ignore rules repository ready at " << repoDir);
    return true;
}

bool IgnoreRules::isReady() const {
    std::shared_lock lock(mutex_);
    return repo_ != nullptr;
}

bool IgnoreRules::applyRulesLocked() {
    if (!repo_) return false;
    auto* repo = static_cast<git_repository*>(repo_);

    if (git_ignore_clear_internal_rules(repo) != 0) {
        logGitError("failed to clear ignore rules");
        return false;
    }

    if (!text_.empty() && git_ignore_add_rule(repo, text_.c_str()) != 0) {
        logGitError("failed to add ignore rules");
        return false;
    }

    // Warm up the repository's lazy attribute cache so that concurrent lookups
    // never race on its first initialization.
    int ignored = 0;
    if (git_ignore_path_is_ignored(&ignored, repo, kWarmupPath) != 0) {
        git_error_clear();
    }
    return true;
}

bool IgnoreRules::setRuleLines(const std::vector<std::string>& lines) {
    std::vector<std::string> expanded;
    expanded.reserve(lines.size());
    for (const auto& line : lines) {
        expanded.push_back(expandTilde(line));
    }

    std::unique_lock lock(mutex_);
    lines_ = std::move(expanded);

    text_.clear();
    for (const auto& line : lines_) {
        text_ += line;
        text_ += '\n';
    }

    if (!repo_) {
        // Rules are kept so a later init() can apply them.
        return false;
    }
    return applyRulesLocked();
}

bool IgnoreRules::setRules(const std::string& rulesText) {
    return setRuleLines(splitLines(rulesText));
}

bool IgnoreRules::isEmpty() const {
    std::shared_lock lock(mutex_);
    return lines_.empty();
}

size_t IgnoreRules::ruleCount() const {
    std::shared_lock lock(mutex_);
    return lines_.size();
}

std::vector<std::string> IgnoreRules::ruleLines() const {
    std::shared_lock lock(mutex_);
    return lines_;
}

bool IgnoreRules::isIgnored(const std::string& fullPath, bool isDir) const {
    if (fullPath.empty()) return false;

    std::shared_lock lock(mutex_);
    if (!repo_ || lines_.empty()) return false;

    std::string path = fullPath;
    if (isDir && path.back() != '/') path += '/';

    int ignored = 0;
    if (git_ignore_path_is_ignored(&ignored, static_cast<git_repository*>(repo_),
                                   path.c_str()) != 0) {
        git_error_clear();
        return false;
    }
    return ignored != 0;
}

std::string IgnoreRules::expandTilde(const std::string& line) {
    if (line.size() < 2 || line[0] != '~' || line[1] != '/') return line;

    std::string home = homeDirectory();
    if (home.empty()) return line;
    return home + line.substr(1);
}
