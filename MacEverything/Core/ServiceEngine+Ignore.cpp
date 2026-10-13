#include "ServiceEngine.h"
#include "Logger.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
constexpr const char* kSettingsFileName = "content_settings.conf";
constexpr const char* kExtensionsKey = "extensions";
constexpr const char* kMaxFileSizeKey = "maxFileSize";
constexpr const char* kIgnoreKey = "ignore";
}  // namespace

// ═══════════════════════════════════════════════════════
//  Ignore rules (gitignore syntax)
// ═══════════════════════════════════════════════════════

std::shared_ptr<const IgnoreMatcher> ServiceEngine::ignoreMatcherSnapshot() {
    std::shared_lock lock(ignoreMutex_);
    return ignoreMatcher_;
}

void ServiceEngine::applyIgnoreToEngine(const std::shared_ptr<SearchEngine>& engine) {
    if (!engine) return;
    engine->setIgnoreMatcher(ignoreMatcherSnapshot(), config_.scanRoot);
}

void ServiceEngine::applyIgnoreToContentIndex(const std::shared_ptr<ContentIndex>& contentIndex) {
    if (!contentIndex) return;
    contentIndex->setIgnoreMatcher(ignoreMatcherSnapshot(), config_.scanRoot);
}

void ServiceEngine::setIgnorePatterns(const std::vector<std::string>& patterns) {
    auto matcher = IgnoreMatcher::fromLines(patterns);
    {
        std::unique_lock lock(ignoreMutex_);
        ignorePatterns_ = patterns;
        ignoreMatcher_ = matcher;
    }

    LOG_INFO("ServiceEngine", "Ignore rules updated: " << matcher->ruleCount()
             << " active pattern(s)");

    applyIgnoreToEngine(safeEngine());
    applyIgnoreToContentIndex(safeContentIndex());
}

std::vector<std::string> ServiceEngine::getIgnorePatterns() {
    std::shared_lock lock(ignoreMutex_);
    return ignorePatterns_;
}

bool ServiceEngine::isPathIgnored(const std::string& fullPath, bool isDir) {
    auto matcher = ignoreMatcherSnapshot();
    if (!matcher || matcher->isEmpty()) return false;
    return matcher->isIgnoredFullPath(config_.scanRoot, fullPath, isDir);
}

// ═══════════════════════════════════════════════════════
//  Persisted content settings
// ═══════════════════════════════════════════════════════
//
//  cachePath/content_settings.conf — plain text, one key per line:
//    extensions=h;cpp;swift
//    maxFileSize=1048576
//    ignore=node_modules/      (repeatable, gitignore syntax)
//
//  Kept human-editable on purpose: the file is small, and a plain format
//  avoids pulling a JSON parser into the core.

std::string ServiceEngine::contentSettingsPath() const {
    if (config_.cachePath.empty()) return {};
    return config_.cachePath + "/" + kSettingsFileName;
}

void ServiceEngine::loadContentSettings() {
    std::string path = contentSettingsPath();
    if (path.empty()) return;

    std::ifstream in(path);
    if (!in) {
        LOG_INFO("ServiceEngine", "No content settings file at " << path << " — using defaults");
        return;
    }

    std::vector<std::string> extensions;
    std::vector<std::string> patterns;
    uint64_t maxFileSize = 0;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        if (key == kExtensionsKey) {
            size_t start = 0;
            while (start <= value.size()) {
                size_t sep = value.find(';', start);
                std::string ext = (sep == std::string::npos)
                    ? value.substr(start)
                    : value.substr(start, sep - start);
                if (!ext.empty()) extensions.push_back(std::move(ext));
                if (sep == std::string::npos) break;
                start = sep + 1;
            }
        } else if (key == kMaxFileSizeKey) {
            try {
                maxFileSize = std::stoull(value);
            } catch (...) {
                LOG_WARN("ServiceEngine", "Ignoring malformed maxFileSize in content settings");
            }
        } else if (key == kIgnoreKey) {
            patterns.push_back(value);
        }
    }

    if (auto contentIndex = safeContentIndex()) {
        if (!extensions.empty()) contentIndex->setExtensions(extensions);
        if (maxFileSize > 0) contentIndex->setMaxFileSize(maxFileSize);
    }
    setIgnorePatterns(patterns);

    LOG_INFO("ServiceEngine", "Loaded content settings from " << path << ": "
             << extensions.size() << " extension(s), " << patterns.size() << " ignore pattern(s)");
}

void ServiceEngine::saveContentSettings() {
    std::string path = contentSettingsPath();
    if (path.empty()) {
        LOG_WARN("ServiceEngine", "Cannot save content settings — cache path is empty");
        return;
    }

    std::vector<std::string> extensions;
    uint64_t maxFileSize = 0;
    if (auto contentIndex = safeContentIndex()) {
        extensions = contentIndex->getExtensions();
        maxFileSize = contentIndex->getMaxFileSize();
    }
    std::vector<std::string> patterns = getIgnorePatterns();

    fs::create_directories(config_.cachePath);

    std::string tmpPath = path + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out) {
            LOG_ERROR("ServiceEngine", "Failed to open " << tmpPath << " for writing");
            return;
        }
        out << "# MacEverything content settings — generated by Content Settings\n";
        out << kExtensionsKey << "=";
        for (size_t i = 0; i < extensions.size(); i++) {
            if (i > 0) out << ';';
            out << extensions[i];
        }
        out << "\n";
        out << kMaxFileSizeKey << "=" << maxFileSize << "\n";
        for (const auto& pattern : patterns) {
            out << kIgnoreKey << "=" << pattern << "\n";
        }
    }

    // Atomic replace so a crash mid-write cannot truncate existing settings.
    std::error_code ec;
    fs::rename(tmpPath, path, ec);
    if (ec) {
        LOG_ERROR("ServiceEngine", "Failed to replace content settings: " << ec.message());
        return;
    }

    LOG_INFO("ServiceEngine", "Content settings saved to " << path
             << " (" << patterns.size() << " ignore pattern(s))");
}
