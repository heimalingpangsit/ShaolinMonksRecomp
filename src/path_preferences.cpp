#include "path_preferences.h"
#include "file_ops.h"
#include "save_storage.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <vector>
namespace fs = std::filesystem;
namespace shaolin::paths {
namespace {
fs::path Normal(const fs::path& path) { return fs::weakly_canonical(fs::absolute(path)); }
bool Same(const fs::path& a, const fs::path& b) { return files::Within(a, b) && files::Within(b, a); }
void Validate(const Locations& old, const Locations& next) {
    for (const auto& pair : {std::pair{old.data, next.data}, std::pair{old.saves, next.saves}}) {
        if (Same(pair.first, pair.second)) continue;
        if (pair.second == pair.second.root_path() || files::Within(pair.second, pair.first) || files::Within(pair.first, pair.second))
            throw std::runtime_error("Choose separate, non-nested source and destination folders");
        if (fs::exists(pair.second) && (!fs::is_directory(pair.second) || !fs::is_empty(pair.second)))
            throw std::runtime_error("The destination must be empty: " + files::Utf8(pair.second));
    }
    // Linux defaults have saves inside data; keep that existing layout valid.
    if (!Same(old.saves, next.saves) && (files::Within(next.saves, old.data) || files::Within(old.data, next.saves)))
        throw std::runtime_error("The new saves folder must be separate from the current data folder");
    if (!Same(old.data, next.data) && files::Within(next.data, old.saves))
        throw std::runtime_error("The data folder cannot be placed inside the current saves folder");
    if (!Same(next.data, old.data) && (files::Within(next.saves, next.data) || files::Within(next.data, next.saves)))
        throw std::runtime_error("Choose separate data and saves destinations");
}
void Save(const fs::path& locator, const Locations& current, const Locations* next = nullptr) {
    toml::table table{{"data", files::Utf8(current.data)}, {"saves", files::Utf8(current.saves)}};
    if (next) table.insert("pending", toml::table{{"data", files::Utf8(next->data)}, {"saves", files::Utf8(next->saves)}});
    std::ostringstream out; out << table; files::AtomicWrite(locator, out.str());
}
struct Copy {
    fs::path source, destination, staging;
    std::vector<fs::path> entries;
    bool published = false;
    std::string original_selection;
};
void Prepare(Copy& copy, const fs::path& locator, const fs::path& excluded) {
    copy.staging = files::UniqueSibling(copy.destination, ".moving-");
    fs::create_directories(copy.staging);
    if (!fs::exists(copy.source)) return;
    for (fs::recursive_directory_iterator it(copy.source), end; it != end; ++it) {
        const auto& e = *it;
        if (e.path().filename() == ".lock" || Same(e.path(), locator) || (!excluded.empty() && Same(e.path(), excluded))) {
            if (e.is_directory()) it.disable_recursion_pending();
            continue;
        }
        if (e.is_symlink() || (!e.is_directory() && !e.is_regular_file())) throw std::runtime_error("Folder moves refuse links and special files");
        const auto rel = e.path().lexically_relative(copy.source);
        const auto target = copy.staging / rel;
        if (e.is_directory()) fs::create_directory(target);
        else {
            fs::copy_file(e.path(), target);
            files::FlushFile(target);
            if (!files::SameBytes(e.path(), target)) throw std::runtime_error("The copied file could not be verified: " + files::Utf8(e.path()));
        }
        copy.entries.push_back(rel);
    }
}
void Publish(Copy& copy) {
    if (fs::exists(copy.destination)) {
        if (!fs::is_empty(copy.destination)) throw std::runtime_error("The destination is no longer empty");
        fs::remove(copy.destination);
    }
    fs::rename(copy.staging, copy.destination); copy.published = true;
}
bool RemoveCopied(const Copy& copy) {
    bool complete = true;
    for (auto it = copy.entries.rbegin(); it != copy.entries.rend(); ++it) {
        std::error_code ec;
        const auto original = copy.source / *it, target = copy.destination / *it;
        if (fs::is_directory(original, ec)) fs::remove(original, ec);
        else if (files::SameBytes(original, target)) fs::remove(original, ec);
        if (ec && !fs::is_directory(original, ec)) complete = false;
    }
    return complete;
}
}
bool ScheduleLocations(const fs::path& locator, const Locations& current, const Locations& requested, std::string& error) {
    try {
        Locations old{Normal(current.data), Normal(current.saves)}, next{Normal(requested.data), Normal(requested.saves)};
        Validate(old, next); Save(locator, old, &next); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
bool LoadLocations(const fs::path& locator, const Locations& defaults, Locations& result, std::string& warning, bool apply_pending) {
    result = defaults;
    try {
        if (!fs::exists(locator)) return true;
        const auto table = toml::parse_file(files::Utf8(locator));
        result.data = Normal(files::FromUtf8(table["data"].value_or(files::Utf8(defaults.data))));
        result.saves = Normal(files::FromUtf8(table["saves"].value_or(files::Utf8(defaults.saves))));
        if (!apply_pending || !table["pending"].is_table()) return true;
        const Locations next{Normal(files::FromUtf8(table["pending"]["data"].value_or(files::Utf8(result.data)))),
                             Normal(files::FromUtf8(table["pending"]["saves"].value_or(files::Utf8(result.saves))))};
        std::vector<Copy> copies;
        bool committed = false;
        try {
            Validate(result, next);
            storage::DirectoryLock data_lock, save_lock;
            std::string error;
            fs::create_directories(result.data);
            if (!data_lock.Acquire(result.data, error)) throw std::runtime_error(error);
            if (fs::is_directory(result.saves / "hdd") && !save_lock.Acquire(result.saves / "hdd", error)) throw std::runtime_error(error);
            if (!Same(result.data, next.data)) {
                copies.push_back({result.data, next.data});
                Prepare(copies.back(), locator, files::Within(result.saves, result.data) ? result.saves : fs::path{});
                const auto selection = copies.back().staging / "game-source.toml";
                if (fs::is_regular_file(selection)) {
                    auto t = toml::parse_file(files::Utf8(selection));
                    if (auto s = t["disc"].value<std::string>()) {
                        const auto disc = Normal(files::FromUtf8(*s));
                        if (files::Within(disc, result.data)) {
                            std::ifstream original(selection, std::ios::binary);
                            copies.back().original_selection = {std::istreambuf_iterator<char>(original), {}};
                            original.close();
                            t.insert_or_assign("disc", files::Utf8(next.data / disc.lexically_relative(result.data)));
                            std::ostringstream out; out << t; files::AtomicWrite(selection, out.str());
                        }
                    }
                }
            }
            if (!Same(result.saves, next.saves)) { copies.push_back({result.saves, next.saves}); Prepare(copies.back(), locator, {}); }
            for (auto& c : copies) Publish(c);
            Save(locator, next); // Old roots stay authoritative until every copy is published.
            committed = true;
            result = next;
            for (const auto& c : copies) if (!RemoveCopied(c)) warning = "The new folders are in use, but some old files could not be removed. Your verified copies are safe.";
            // game-source.toml was intentionally rewritten, so byte comparison differs.
            for (const auto& c : copies) if (!c.original_selection.empty()) {
                std::ifstream original(c.source / "game-source.toml", std::ios::binary);
                const std::string contents{std::istreambuf_iterator<char>(original), {}};
                if (contents == c.original_selection) { std::error_code ec; fs::remove(c.source / "game-source.toml", ec); }
            }
        } catch (const std::exception& e) {
            if (committed) { warning = "The new folders are in use. Old-file cleanup could not finish: " + std::string(e.what()); return true; }
            warning = "Folder move failed. The previous folders are still in use. " + std::string(e.what());
            for (const auto& c : copies) {
                std::error_code ec;
                if (c.published) fs::remove_all(c.destination, ec);
                else if (!c.staging.empty()) fs::remove_all(c.staging, ec);
            }
            Save(locator, result);
        }
        return true;
    } catch (const std::exception& e) { warning = "Cannot read folder preferences: " + std::string(e.what()); return false; }
}
}
