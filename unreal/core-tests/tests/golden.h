// Helpers for tests that read goldens and fixtures: paths, JSON files, and
// JSON trees compared with numbers exact.
#pragma once

#ifndef JSON_NOEXCEPTION
#define JSON_NOEXCEPTION 1
#endif
#include "json.hpp"

#include "Session.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace golden {

using json = nlohmann::json;

/// A path under the repository root.
inline std::string repo(const std::string& relative) { return std::string(AC_REPO_ROOT) + "/" + relative; }
/// A file in unreal/core-tests/golden.
inline std::string path(const std::string& name) { return repo("unreal/core-tests/golden/" + name); }

/// A JSON file, or a discarded value when it is missing or not JSON.
inline json load(const std::string& file) {
    auto text = ac::readFile(file);
    if (!text) return json(json::value_t::discarded);
    return json::parse(*text, nullptr, false);
}

/// Two JSON trees are the same: numbers exactly (an integer and a float
/// compare as doubles), objects key by key. Arrays under a key named in
/// `unordered` (Swift `Set`s, written in hash order) compare as sorted.
/// On a difference, `where` says where.
inline bool same(const json& a, const json& b, std::string& where, const std::string& at = "",
                 bool unordered = false) {
    if (a.is_number() && b.is_number()) {
        if (a.is_number_float() || b.is_number_float()) {
            if (a.get<double>() == b.get<double>()) return true;
        } else if (a.dump() == b.dump()) {
            return true;
        }
        where = at + ": " + a.dump() + " vs " + b.dump();
        return false;
    }
    if (a.type() != b.type()) {
        where = at + ": " + a.dump() + " vs " + b.dump();
        return false;
    }
    if (a.is_object()) {
        for (auto it = a.begin(); it != a.end(); ++it) {
            if (!b.contains(it.key())) { where = at + "/" + it.key() + ": only in the first"; return false; }
            if (!same(it.value(), b[it.key()], where, at + "/" + it.key(), it.key() == "upgrades")) return false;
        }
        for (auto it = b.begin(); it != b.end(); ++it)
            if (!a.contains(it.key())) { where = at + "/" + it.key() + ": only in the second"; return false; }
        return true;
    }
    if (a.is_array()) {
        if (a.size() != b.size()) { where = at + ": " + std::to_string(a.size()) + " vs " + std::to_string(b.size()) + " items"; return false; }
        if (unordered) {
            std::vector<std::string> x, y;
            for (const auto& e : a) x.push_back(e.dump());
            for (const auto& e : b) y.push_back(e.dump());
            std::sort(x.begin(), x.end());
            std::sort(y.begin(), y.end());
            if (x == y) return true;
            where = at + ": " + a.dump() + " vs " + b.dump();
            return false;
        }
        for (size_t i = 0; i < a.size(); i++)
            if (!same(a[i], b[i], where, at + "/" + std::to_string(i))) return false;
        return true;
    }
    if (a == b) return true;
    where = at + ": " + a.dump() + " vs " + b.dump();
    return false;
}

} // namespace golden
