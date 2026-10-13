#include "IgnoreMatcher.h"

#include <algorithm>
#include <cstddef>

namespace {

/// True and writes `rel` when `fullPath` lies strictly under `base`.
bool relativeUnder(const std::string& base, const std::string& fullPath, std::string& rel) {
    if (base.empty() || fullPath.size() <= base.size()) return false;
    if (fullPath.compare(0, base.size(), base) != 0) return false;

    size_t pos = base.size();
    if (base.back() != '/') {
        if (fullPath[pos] != '/') return false;
        pos++;
    } else if (fullPath[pos] == '/') {
        pos++;
    }
    if (pos >= fullPath.size()) return false;

    rel = fullPath.substr(pos);
    return true;
}

/// Drop one leading "/private" so symlinked system paths compare equal.
std::string stripPrivatePrefix(const std::string& path) {
    constexpr const char* kPrivate = "/private";
    constexpr size_t kLen = 8;
    if (path.size() > kLen && path.compare(0, kLen, kPrivate) == 0 && path[kLen] == '/') {
        return path.substr(kLen);
    }
    return path;
}

}  // namespace

// ═══════════════════════════════════════════════════════
//  Small string helpers (kept local — matching must not
//  depend on CoreFoundation for thread safety)
// ═══════════════════════════════════════════════════════

char IgnoreMatcher::lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string IgnoreMatcher::trimRight(const std::string& s) {
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' ||
                       s[end - 1] == '\n')) {
        end--;
    }
    return s.substr(0, end);
}

std::vector<std::string> IgnoreMatcher::splitSegments(const std::string& path) {
    std::vector<std::string> segments;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        std::string segment = (slash == std::string::npos)
            ? path.substr(start)
            : path.substr(start, slash - start);
        if (!segment.empty()) segments.push_back(std::move(segment));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return segments;
}

// ═══════════════════════════════════════════════════════
//  Pattern parsing
// ═══════════════════════════════════════════════════════

void IgnoreMatcher::setLines(const std::vector<std::string>& lines) {
    rules_.clear();
    rules_.reserve(lines.size());

    for (const auto& rawLine : lines) {
        std::string line = trimRight(rawLine);
        if (line.empty() || line[0] == '#') continue;

        IgnoreRule rule;
        size_t start = 0;
        if (line[0] == '!') {
            rule.negated = true;
            start = 1;
        } else if (line.size() >= 2 && line[0] == '\\' && line[1] == '!') {
            start = 1;  // escaped '!' — literal pattern
        }

        std::string pattern = line.substr(start);
        if (pattern.empty()) continue;

        // Trailing '/' — directories only.
        if (pattern.back() == '/') {
            rule.dirOnly = true;
            pattern.pop_back();
        }
        if (pattern.empty()) continue;

        bool leadingSlash = (pattern[0] == '/');
        if (leadingSlash) pattern.erase(0, 1);

        // A leading "**/" behaves like a non-anchored pattern.
        if (pattern.rfind("**/", 0) == 0) {
            pattern = pattern.substr(3);
            leadingSlash = false;
        }
        if (pattern.empty()) continue;

        bool hasSlash = pattern.find('/') != std::string::npos;
        rule.basenameOnly = !leadingSlash && !hasSlash;
        rule.segments = splitSegments(pattern);
        if (rule.segments.empty()) continue;

        rules_.push_back(std::move(rule));
    }
}

void IgnoreMatcher::setText(const std::string& text) {
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
    setLines(lines);
}

std::shared_ptr<IgnoreMatcher> IgnoreMatcher::fromLines(const std::vector<std::string>& lines) {
    auto matcher = std::make_shared<IgnoreMatcher>();
    matcher->setLines(lines);
    return matcher;
}

std::shared_ptr<IgnoreMatcher> IgnoreMatcher::fromText(const std::string& text) {
    auto matcher = std::make_shared<IgnoreMatcher>();
    matcher->setText(text);
    return matcher;
}

// ═══════════════════════════════════════════════════════
//  Segment matching (* ? [...] \ escapes, no '/' crossing)
// ═══════════════════════════════════════════════════════

bool IgnoreMatcher::charClassMatch(const std::string& charClass, char c) {
    if (charClass.empty()) return false;

    size_t i = 0;
    bool negate = false;
    if (charClass[0] == '!' || charClass[0] == '^') {
        negate = true;
        i = 1;
    }

    bool matched = false;
    while (i < charClass.size()) {
        char lo = charClass[i];
        if (lo == '\\' && i + 1 < charClass.size()) {
            lo = charClass[++i];
        }
        if (i + 2 < charClass.size() && charClass[i + 1] == '-') {
            char hi = charClass[i + 2];
            if (hi == '\\' && i + 3 < charClass.size()) hi = charClass[i + 3];
            if (c >= lo && c <= hi) matched = true;
            i += (charClass[i + 2] == '\\') ? 4 : 3;
            continue;
        }
        if (c == lo) matched = true;
        i++;
    }
    return negate ? !matched : matched;
}

bool IgnoreMatcher::segmentMatch(const std::string& pattern, const std::string& text) {
    // Iterative matcher with backtracking for '*'.
    size_t pi = 0, ti = 0;
    size_t starPi = std::string::npos, starTi = 0;

    while (ti < text.size()) {
        bool consumed = false;

        if (pi < pattern.size()) {
            char pc = pattern[pi];
            if (pc == '*') {
                starPi = ++pi;
                starTi = ti;
                consumed = true;
            } else if (pc == '?') {
                pi++;
                ti++;
                consumed = true;
            } else if (pc == '[') {
                size_t close = pattern.find(']', pi + 1);
                if (close != std::string::npos) {
                    if (charClassMatch(pattern.substr(pi + 1, close - pi - 1), text[ti])) {
                        pi = close + 1;
                        ti++;
                        consumed = true;
                    }
                } else if (text[ti] == '[') {
                    pi++;
                    ti++;
                    consumed = true;
                }
            } else if (pc == '\\' && pi + 1 < pattern.size()) {
                if (lowerAscii(pattern[pi + 1]) == lowerAscii(text[ti])) {
                    pi += 2;
                    ti++;
                    consumed = true;
                }
            } else if (lowerAscii(pc) == lowerAscii(text[ti])) {
                pi++;
                ti++;
                consumed = true;
            }
        }

        if (consumed) continue;

        if (starPi != std::string::npos) {
            pi = starPi;
            ti = ++starTi;
            continue;
        }
        return false;
    }

    while (pi < pattern.size() && pattern[pi] == '*') pi++;
    return pi == pattern.size();
}

bool IgnoreMatcher::segmentsMatch(const std::vector<std::string>& pattern, size_t patternIndex,
                                  const std::vector<std::string>& path, size_t pathIndex) {
    if (patternIndex == pattern.size()) return pathIndex == path.size();

    if (pattern[patternIndex] == "**") {
        // '**' matches zero or more whole path segments.
        for (size_t k = pathIndex; k <= path.size(); k++) {
            if (segmentsMatch(pattern, patternIndex + 1, path, k)) return true;
        }
        return false;
    }

    if (pathIndex == path.size()) return false;
    if (!segmentMatch(pattern[patternIndex], path[pathIndex])) return false;
    return segmentsMatch(pattern, patternIndex + 1, path, pathIndex + 1);
}

bool IgnoreMatcher::ruleMatchesPath(const IgnoreRule& rule,
                                    const std::vector<std::string>& pathSegments) {
    if (pathSegments.empty()) return false;
    if (rule.basenameOnly) {
        return segmentMatch(rule.segments[0], pathSegments.back());
    }
    return segmentsMatch(rule.segments, 0, pathSegments, 0);
}

// ═══════════════════════════════════════════════════════
//  Public matching
// ═══════════════════════════════════════════════════════

bool IgnoreMatcher::matchPathOnly(const std::string& relPath, bool isDir) const {
    auto segments = splitSegments(relPath);
    if (segments.empty()) return false;

    bool ignored = false;
    for (const auto& rule : rules_) {
        if (rule.dirOnly && !isDir) continue;
        if (!ruleMatchesPath(rule, segments)) continue;
        ignored = !rule.negated;
    }
    return ignored;
}

bool IgnoreMatcher::isIgnored(const std::string& relPath, bool isDir) const {
    if (rules_.empty() || relPath.empty()) return false;

    auto segments = splitSegments(relPath);
    if (segments.empty()) return false;

    // Git cannot re-include a file whose parent directory is excluded, so an
    // ignored ancestor short-circuits every later rule.
    std::string prefix;
    for (size_t i = 0; i + 1 < segments.size(); i++) {
        if (i > 0) prefix += '/';
        prefix += segments[i];
        if (matchPathOnly(prefix, true)) return true;
    }

    return matchPathOnly(relPath, isDir);
}

bool IgnoreMatcher::isIgnoredFullPath(const std::string& base, const std::string& fullPath,
                                      bool isDir) const {
    if (rules_.empty() || base.empty()) return false;

    std::string rel;
    if (relativeUnder(base, fullPath, rel)) return isIgnored(rel, isDir);

    // macOS exposes /tmp, /var and /etc as symlinks into /private. FSEvents
    // reports resolved paths while the scan root may be given unresolved (or
    // the other way round), so compare both spellings.
    std::string strippedBase = stripPrivatePrefix(base);
    if (strippedBase != base && relativeUnder(strippedBase, fullPath, rel)) {
        return isIgnored(rel, isDir);
    }
    std::string strippedPath = stripPrivatePrefix(fullPath);
    if (strippedPath != fullPath && relativeUnder(base, strippedPath, rel)) {
        return isIgnored(rel, isDir);
    }
    return false;
}
