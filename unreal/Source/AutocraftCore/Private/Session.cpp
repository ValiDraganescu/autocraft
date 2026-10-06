// Port of Sources/GameCore/Session.swift, and the Codable shape of every
// type a session holds (Swift synthesises it; here each type lists its
// fields once in `fields`, which both reads and writes).
#include "Session.h"

#ifndef JSON_NOEXCEPTION
#define JSON_NOEXCEPTION 1
#endif
#include "../ThirdParty/json.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <random>

namespace ac {
namespace coding {

using json = nlohmann::json;

/// Reads one JSON object into a value's fields: a missing key fails a
/// field Swift requires, and leaves an optional nil.
struct Reader {
    const json& obj;
    std::string& error;
    bool ok = true;

    void fail(const char* key, const char* why) {
        if (!ok) return;
        ok = false;
        error = std::string(why) + " \"" + key + "\"";
    }
    /// A field Swift requires.
    template <class T> void operator()(const char* key, T& v);
    /// An optional field: missing or null is nil.
    template <class T> void operator()(const char* key, std::optional<T>& v);
    /// `decodeIfPresent(...) ?? default`: missing or null keeps the default.
    template <class T> void orDefault(const char* key, T& v);
    /// Like `orDefault`; the writer leaves a zero out (a field added since
    /// old saves, which they would not have).
    template <class T> void orZero(const char* key, T& v) { orDefault(key, v); }
};

/// Writes a value's fields into a JSON object; nil optionals are left out.
struct Writer {
    json out = json::object();
    template <class T> void operator()(const char* key, const T& v);
    template <class T> void operator()(const char* key, const std::optional<T>& v);
    template <class T> void orDefault(const char* key, const T& v);
    template <class T> void orZero(const char* key, const T& v) { if (v != T{}) orDefault(key, v); }
};

// Each type's fields, in Swift's declaration order.
template <class IO> void fields(IO& io, Listener& v);
template <class IO> void fields(IO& io, Session& v);
template <class IO> void fields(IO& io, GameState& v);
template <class IO> void fields(IO& io, Player& v);
template <class IO> void fields(IO& io, OreDeposit& v);
template <class IO> void fields(IO& io, Well& v);
template <class IO> void fields(IO& io, Structure& v);
template <class IO> void fields(IO& io, BuildOrder& v);
template <class IO> void fields(IO& io, Unit& v);
template <class IO> void fields(IO& io, Rush& v);
template <class IO> void fields(IO& io, Burning& v);
template <class IO> void fields(IO& io, Intel& v);
template <class IO> void fields(IO& io, Intel::Seen& v);
template <class IO> void fields(IO& io, Directives& v);
template <class IO> void fields(IO& io, Request& v);
template <class IO> void fields(IO& io, Objective& v);
template <class IO> void fields(IO& io, PilotRecord& v);
template <class IO> void fields(IO& io, Tally& v);
template <class IO> void fields(IO& io, Stamp& v);
template <class IO> void fields(IO& io, Standing& v);
template <class IO> void fields(IO& io, Doodad& v);
template <class IO> void fields(IO& io, MapDefinition& v);
template <class IO> void fields(IO& io, GroundRect& v);
template <class IO> void fields(IO& io, Plateau& v);
template <class IO> void fields(IO& io, Ramp& v);
template <class IO> void fields(IO& io, GroundPatch& v);
template <class IO> void fields(IO& io, OreSpec& v);
template <class IO> void fields(IO& io, BaseSite& v);
template <class IO> void fields(IO& io, MapView& v);
template <class IO> void fields(IO& io, CanvasProjection& v);
template <class IO> void fields(IO& io, CanvasRect& v);

template <class T> concept HasFields = requires(Reader& r, T& t) { fields(r, t); };

// MARK: - Reading values

bool get(const json& j, double& v);
bool get(const json& j, int64_t& v);
bool get(const json& j, uint64_t& v);
bool get(const json& j, uint32_t& v);
bool get(const json& j, bool& v);
bool get(const json& j, std::string& v);
bool get(const json& j, Vec2& v);
bool get(const json& j, Date& v);
bool get(const json& j, Mission& v);
bool get(const json& j, Request::What& v);
bool get(const json& j, KindRecord& v);
template <class E> requires std::is_enum_v<E> bool get(const json& j, E& v);
template <class T> bool get(const json& j, std::vector<T>& v);
template <class T> bool get(const json& j, std::set<T>& v);
template <class K, class T> bool get(const json& j, std::map<K, T>& v);
template <HasFields T> bool get(const json& j, T& v);

bool get(const json& j, double& v) {
    if (j.is_number()) { v = j.get<double>(); return true; }
    if (j.is_string()) {
        const auto& s = j.get_ref<const std::string&>();
        if (s == SessionStore::floats.inf) { v = std::numeric_limits<double>::infinity(); return true; }
        if (s == SessionStore::floats.minus) { v = -std::numeric_limits<double>::infinity(); return true; }
        if (s == SessionStore::floats.nan) { v = std::numeric_limits<double>::quiet_NaN(); return true; }
    }
    return false;
}

bool get(const json& j, int64_t& v) {
    if (j.is_number_unsigned()) {
        const uint64_t u = j.get<uint64_t>();
        if (u > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return false;
        v = static_cast<int64_t>(u);
        return true;
    }
    if (j.is_number_integer()) { v = j.get<int64_t>(); return true; }
    if (j.is_number_float()) {
        const double d = j.get<double>();
        if (d != std::trunc(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0) return false;
        v = static_cast<int64_t>(d);
        return true;
    }
    return false;
}

bool get(const json& j, uint64_t& v) {
    if (j.is_number_unsigned()) { v = j.get<uint64_t>(); return true; }
    if (j.is_number_integer()) {
        const int64_t i = j.get<int64_t>();
        if (i < 0) return false;
        v = static_cast<uint64_t>(i);
        return true;
    }
    if (j.is_number_float()) {
        const double d = j.get<double>();
        if (d != std::trunc(d) || d < 0 || d >= 18446744073709551616.0) return false;
        v = static_cast<uint64_t>(d);
        return true;
    }
    return false;
}

bool get(const json& j, uint32_t& v) {
    uint64_t u = 0;
    if (!get(j, u) || u > std::numeric_limits<uint32_t>::max()) return false;
    v = static_cast<uint32_t>(u);
    return true;
}

bool get(const json& j, bool& v) {
    if (!j.is_boolean()) return false;
    v = j.get<bool>();
    return true;
}

bool get(const json& j, std::string& v) {
    if (!j.is_string()) return false;
    v = j.get<std::string>();
    return true;
}

/// `SIMD2<Double>`: an unkeyed container of two numbers.
bool get(const json& j, Vec2& v) {
    if (!j.is_array() || j.size() < 2) return false;
    return get(j[0], v.x) && get(j[1], v.y);
}

namespace {
/// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's
/// `days_from_civil`).
int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += m <= 2;
}

bool digits(const std::string& s, size_t at, size_t n, int64_t& out) {
    if (at + n > s.size()) return false;
    out = 0;
    for (size_t i = at; i < at + n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        out = out * 10 + (s[i] - '0');
    }
    return true;
}
} // namespace

/// `.iso8601`: `yyyy-MM-ddTHH:mm:ss` and `Z` or `±HH:mm`.
bool parseISO8601(const std::string& s, Date& v) {
    int64_t y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (!digits(s, 0, 4, y) || s.size() < 20 || s[4] != '-' || !digits(s, 5, 2, mo) || s[7] != '-' ||
        !digits(s, 8, 2, d) || s[10] != 'T' || !digits(s, 11, 2, h) || s[13] != ':' || !digits(s, 14, 2, mi) ||
        s[16] != ':' || !digits(s, 17, 2, se))
        return false;
    int64_t offset = 0;
    if (s.size() == 20 && s[19] == 'Z') {
        offset = 0;
    } else if (s.size() == 25 && (s[19] == '+' || s[19] == '-') && s[22] == ':') {
        int64_t oh = 0, om = 0;
        if (!digits(s, 20, 2, oh) || !digits(s, 23, 2, om)) return false;
        offset = (oh * 3600 + om * 60) * (s[19] == '-' ? -1 : 1);
    } else {
        return false;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return false;
    const int64_t secs = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se - offset;
    v.since1970 = static_cast<double>(secs);
    return true;
}

std::string formatISO8601(const Date& v) {
    const int64_t secs = static_cast<int64_t>(std::floor(v.since1970));
    int64_t days = secs / 86400, rem = secs % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    int64_t y = 0, m = 0, d = 0;
    civilFromDays(days, y, m, d);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lldZ", static_cast<long long>(y),
                  static_cast<long long>(m), static_cast<long long>(d), static_cast<long long>(rem / 3600),
                  static_cast<long long>(rem % 3600 / 60), static_cast<long long>(rem % 60));
    return buf;
}

bool get(const json& j, Date& v) {
    if (!j.is_string()) return false;
    return parseISO8601(j.get_ref<const std::string&>(), v);
}

template <class E> requires std::is_enum_v<E> bool get(const json& j, E& v) {
    if (!j.is_string()) return false;
    auto e = parse<E>(j.get_ref<const std::string&>());
    if (!e) return false;
    v = *e;
    return true;
}

template <class T> bool get(const json& j, std::vector<T>& v) {
    if (!j.is_array()) return false;
    v.clear();
    v.reserve(j.size());
    for (const auto& e : j) {
        T t{};
        if (!get(e, t)) return false;
        v.push_back(std::move(t));
    }
    return true;
}

template <class T> bool get(const json& j, std::set<T>& v) {
    if (!j.is_array()) return false;
    v.clear();
    for (const auto& e : j) {
        T t{};
        if (!get(e, t)) return false;
        v.insert(t);
    }
    return true;
}

/// A dictionary keyed by an enum (`CodingKeyRepresentable`): an object.
template <class K, class T> bool get(const json& j, std::map<K, T>& v) {
    if (!j.is_object()) return false;
    v.clear();
    for (auto it = j.begin(); it != j.end(); ++it) {
        auto k = parse<K>(it.key());
        if (!k) return false;
        T t{};
        if (!get(it.value(), t)) return false;
        v.emplace(*k, std::move(t));
    }
    return true;
}

template <HasFields T> bool get(const json& j, T& v) {
    if (!j.is_object()) return false;
    std::string error;
    Reader r{j, error};
    fields(r, v);
    return r.ok;
}

/// One case of an enum with payloads: `{"case": {payload}}`, and nothing else.
bool oneCase(const json& j, std::string& name, const json*& payload) {
    if (!j.is_object() || j.size() != 1) return false;
    auto it = j.begin();
    name = it.key();
    payload = &it.value();
    return payload->is_object();
}

template <class C> bool atCase(const json& p, C& c) {
    return p.contains("_0") && get(p["_0"], c.at);
}
template <class C> bool squadCase(const json& p, C& c) {
    return p.contains("objective") && p.contains("at") && get(p["objective"], c.objective) && get(p["at"], c.at);
}

bool get(const json& j, Mission& v) {
    std::string name;
    const json* p = nullptr;
    if (!oneCase(j, name, p)) return false;
    if (name == "raid") { Mission::Raid c{}; if (!atCase(*p, c)) return false; v = c; return true; }
    if (name == "drop") { Mission::Drop c{}; if (!atCase(*p, c)) return false; v = c; return true; }
    if (name == "fallBack") { Mission::FallBack c{}; if (!atCase(*p, c)) return false; v = c; return true; }
    if (name == "hunt") { Mission::Hunt c{}; if (!atCase(*p, c)) return false; v = c; return true; }
    if (name == "assault") { Mission::Assault c{}; if (!squadCase(*p, c)) return false; v = c; return true; }
    if (name == "hold") { Mission::Hold c{}; if (!squadCase(*p, c)) return false; v = c; return true; }
    if (name == "regroup") { Mission::Regroup c{}; if (!squadCase(*p, c)) return false; v = c; return true; }
    if (name == "follow") { Mission::Follow c{}; if (!p->contains("unit") || !get((*p)["unit"], c.unit)) return false; v = c; return true; }
    return false;
}

bool get(const json& j, Request::What& v) {
    std::string name;
    const json* p = nullptr;
    if (!oneCase(j, name, p) || !p->contains("_0")) return false;
    const json& x = (*p)["_0"];
    if (name == "unit") { Request::What::Unit c{}; if (!get(x, c.kind)) return false; v = c; return true; }
    if (name == "building") { Request::What::Building c{}; if (!get(x, c.kind)) return false; v = c; return true; }
    if (name == "upgrade") { Request::What::Upgrade c{}; if (!get(x, c.upgrade)) return false; v = c; return true; }
    return false;
}

/// Tolerant: a field missing reads as its default, and a pick no longer
/// in the catalogue ends the list there.
bool get(const json& j, KindRecord& v) {
    if (!j.is_object()) return false;
    std::string error;
    Reader r{j, error};
    r.orDefault("xp", v.xp);
    std::vector<std::string> names;
    r.orDefault("picks", names);
    v.picks.clear();
    for (const auto& n : names) {
        auto p = parse<Perk>(n);
        if (!p) break;
        v.picks.push_back(*p);
    }
    r.orDefault("count", v.count);
    r("burstUntil", v.burstUntil);
    r("burstReady", v.burstReady);
    r("healedAt", v.healedAt);
    r.orDefault("tally", v.tally);
    r.orDefault("stamps", v.stamps);
    return r.ok;
}

template <class T> void Reader::operator()(const char* key, T& v) {
    if (!ok) return;
    auto it = obj.find(key);
    if (it == obj.end()) return fail(key, "missing");
    if (!get(*it, v)) fail(key, "cannot read");
}

template <class T> void Reader::operator()(const char* key, std::optional<T>& v) {
    if (!ok) return;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) { v.reset(); return; }
    T t{};
    if (!get(*it, t)) return fail(key, "cannot read");
    v = std::move(t);
}

template <class T> void Reader::orDefault(const char* key, T& v) {
    if (!ok) return;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return;
    if (!get(*it, v)) fail(key, "cannot read");
}

// MARK: - Writing values

json put(double v);
json put(int64_t v);
json put(uint64_t v);
json put(uint32_t v);
json put(bool v);
json put(const std::string& v);
json put(const Vec2& v);
json put(const Date& v);
json put(const Mission& v);
json put(const Request::What& v);
json put(const KindRecord& v);
template <class E> requires std::is_enum_v<E> json put(E v);
template <class T> json put(const std::vector<T>& v);
template <class T> json put(const std::set<T>& v);
template <class K, class T> json put(const std::map<K, T>& v);
template <HasFields T> json put(const T& v);

/// A whole number as an integer (as Swift writes `100.0`: `100`); -0 keeps
/// its sign; infinities and NaN as Swift's strings.
json put(double v) {
    if (std::isnan(v)) return std::string(SessionStore::floats.nan);
    if (std::isinf(v)) return std::string(v > 0 ? SessionStore::floats.inf : SessionStore::floats.minus);
    if (v == std::trunc(v) && std::fabs(v) < 9007199254740992.0 && !(v == 0 && std::signbit(v)))
        return static_cast<int64_t>(v);
    return v;
}
json put(int64_t v) { return v; }
json put(uint64_t v) { return v; }
json put(uint32_t v) { return static_cast<uint64_t>(v); }
json put(bool v) { return v; }
json put(const std::string& v) { return v; }
json put(const Vec2& v) { return json::array({put(v.x), put(v.y)}); }
json put(const Date& v) { return formatISO8601(v); }

template <class E> requires std::is_enum_v<E> json put(E v) { return std::string(rawValue(v)); }

template <class T> json put(const std::vector<T>& v) {
    json out = json::array();
    for (const auto& e : v) out.push_back(put(e));
    return out;
}

template <class T> json put(const std::set<T>& v) {
    json out = json::array();
    for (const auto& e : v) out.push_back(put(e));
    return out;
}

template <class K, class T> json put(const std::map<K, T>& v) {
    json out = json::object();
    for (const auto& [k, e] : v) out[std::string(rawValue(k))] = put(e);
    return out;
}

template <HasFields T> json put(const T& v) {
    Writer w;
    fields(w, const_cast<T&>(v));
    return w.out;
}

json put(const Mission& v) {
    json p = json::object();
    const char* name = "";
    if (auto c = v.as<Mission::Raid>()) { name = "raid"; p["_0"] = put(c->at); }
    else if (auto c1 = v.as<Mission::Drop>()) { name = "drop"; p["_0"] = put(c1->at); }
    else if (auto c2 = v.as<Mission::FallBack>()) { name = "fallBack"; p["_0"] = put(c2->at); }
    else if (auto c3 = v.as<Mission::Hunt>()) { name = "hunt"; p["_0"] = put(c3->at); }
    else if (auto c4 = v.as<Mission::Assault>()) { name = "assault"; p["objective"] = put(c4->objective); p["at"] = put(c4->at); }
    else if (auto c5 = v.as<Mission::Hold>()) { name = "hold"; p["objective"] = put(c5->objective); p["at"] = put(c5->at); }
    else if (auto c6 = v.as<Mission::Regroup>()) { name = "regroup"; p["objective"] = put(c6->objective); p["at"] = put(c6->at); }
    else if (auto c7 = v.as<Mission::Follow>()) { name = "follow"; p["unit"] = put(c7->unit); }
    json out = json::object();
    out[name] = p;
    return out;
}

json put(const Request::What& v) {
    json p = json::object();
    const char* name = "";
    if (auto c = v.as<Request::What::Unit>()) { name = "unit"; p["_0"] = put(c->kind); }
    else if (auto c1 = v.as<Request::What::Building>()) { name = "building"; p["_0"] = put(c1->kind); }
    else if (auto c2 = v.as<Request::What::Upgrade>()) { name = "upgrade"; p["_0"] = put(c2->upgrade); }
    json out = json::object();
    out[name] = p;
    return out;
}

json put(const KindRecord& v) {
    Writer w;
    w("xp", v.xp);
    w("picks", v.picks);
    w("count", v.count);
    w("burstUntil", v.burstUntil);
    w("burstReady", v.burstReady);
    w("healedAt", v.healedAt);
    w("tally", v.tally);
    w("stamps", v.stamps);
    return w.out;
}

template <class T> void Writer::operator()(const char* key, const T& v) { out[key] = put(v); }
template <class T> void Writer::operator()(const char* key, const std::optional<T>& v) {
    if (v) out[key] = put(*v);
}
template <class T> void Writer::orDefault(const char* key, const T& v) { out[key] = put(v); }

// MARK: - The fields

template <class IO> void fields(IO& io, Listener& v) {
    io("position", v.position);
    io("range", v.range);
    io("local", v.local);
    io("facing", v.facing);
}

template <class IO> void fields(IO& io, Session& v) {
    io("id", v.id);
    io("signature", v.signature);
    io("created", v.created);
    io("mapName", v.mapName);
    io("mapVersion", v.mapVersion);
    io("state", v.state);
    io("listener", v.listener);
    io("ai", v.ai);
    io("fog", v.fog);
    io("game", v.game);
    io("gameStarted", v.gameStarted);
}

template <class IO> void fields(IO& io, GameState& v) {
    io("players", v.players);
    io("time", v.time);
    io("patches", v.patches);
    io("wells", v.wells);
    io("structures", v.structures);
    io("units", v.units);
    io("nextID", v.nextID);
    io("winner", v.winner);
    io("endedAt", v.endedAt);
    io("score", v.score);
    io("intel", v.intel);
    io("doodads", v.doodads);
}

template <class IO> void fields(IO& io, Player& v) {
    io("ore", v.ore);
    io("hydrogen", v.hydrogen);
    io("supplyUsed", v.supplyUsed);
    io("supplyCap", v.supplyCap);
    io("totalMined", v.totalMined);
    io("attack", v.attack);
    io("retreating", v.retreating);
    io("retreatedAt", v.retreatedAt);
    io("defending", v.defending);
    io("reinforce", v.reinforce);
    io("waves", v.waves);
    io("aggression", v.aggression);
    io("greed", v.greed);
    io("garrisonPerBase", v.garrisonPerBase);
    io("comets", v.comets);
    io("start", v.start);
    io("style", v.style);
    io("fireflies", v.fireflies);
    io("drops", v.drops);
    io("upgrades", v.upgrades);
    io("directives", v.directives);
    io("pilot", v.pilot);
    io("unitsLost", v.unitsLost);
    io("team", v.team);
}

template <class IO> void fields(IO& io, OreDeposit& v) {
    io("id", v.id);
    io("position", v.position);
    io("angle", v.angle);
    io("initial", v.initial);
    io("remaining", v.remaining);
    io("depletedAt", v.depletedAt);
    io("miner", v.miner);
}

template <class IO> void fields(IO& io, Well& v) {
    io("position", v.position);
    io("remaining", v.remaining);
    io("harvester", v.harvester);
}

template <class IO> void fields(IO& io, Structure& v) {
    io("id", v.id);
    io("kind", v.kind);
    io("owner", v.owner);
    io("position", v.position);
    io("hp", v.hp);
    io("buildLeft", v.buildLeft);
    io("builder", v.builder);
    io("training", v.training);
    io("line", v.line);
    io("crew", v.crew);
    io("addon", v.addon);
    io("parent", v.parent);
    io("research", v.research);
    io("researchLeft", v.researchLeft);
    io("target", v.target);
    io("cooldown", v.cooldown);
    io("aim", v.aim);
}

template <class IO> void fields(IO& io, BuildOrder& v) {
    io("kind", v.kind);
    io("position", v.position);
}

template <class IO> void fields(IO& io, Unit& v) {
    io("id", v.id);
    io("kind", v.kind);
    io("owner", v.owner);
    io("position", v.position);
    io("hp", v.hp);
    io("heading", v.heading);
    io("task", v.task);
    io("patch", v.patch);
    io("timer", v.timer);
    io("carrying", v.carrying);
    io("stride", v.stride);
    io("order", v.order);
    io("structure", v.structure);
    io("goal", v.goal);
    io("waypoints", v.waypoints);
    io("target", v.target);
    io("cooldown", v.cooldown);
    io("moving", v.moving);
    io("hydrogen", v.hydrogen);
    io("hitAt", v.hitAt);
    io("slowUntil", v.slowUntil);
    io("slowedTo", v.slowedTo);
    io("anchor", v.anchor);
    io("anchored", v.anchored);
    io("aim", v.aim);
    io("jumpFrom", v.jumpFrom);
    io("jumpTo", v.jumpTo);
    io("energy", v.energy);
    io("cargo", v.cargo);
    io("mission", v.mission);
    io("autoFollow", v.autoFollow);
    io("burstFrom", v.burstFrom);
    io("firedAt", v.firedAt);
    io("railCooldown", v.railCooldown);
    io("shotFrom", v.shotFrom);
    io("shield", v.shield);
    io("bonusHP", v.bonusHP);
    io("boardedAt", v.boardedAt);
    io("rush", v.rush);
    io("rushUntil", v.rushUntil);
    io("burning", v.burning);
    io("lockTarget", v.lockTarget);
    io("lockFrom", v.lockFrom);
    io("lockAt", v.lockAt);
    io("stompReady", v.stompReady);
    io("stompedAt", v.stompedAt);
    io("struckAt", v.struckAt);
}

template <class IO> void fields(IO& io, Rush& v) {
    io("seconds", v.seconds);
    io("speed", v.speed);
    io("fireRate", v.fireRate);
}

template <class IO> void fields(IO& io, Burning& v) {
    io("until", v.until);
    io("perSecond", v.perSecond);
    io("by", v.by);
}

template <class IO> void fields(IO& io, Intel& v) {
    io("buildings", v.buildings);
    io("units", v.units);
    io("explored", v.explored);
    io("sites", v.sites);
}

template <class IO> void fields(IO& io, Intel::Seen& v) {
    io("unit", v.unit);
    io("at", v.at);
}

/// Fields a saved game does not have yet take their defaults.
template <class IO> void fields(IO& io, Directives& v) {
    io.orDefault("queue", v.queue);
    io.orDefault("keepOre", v.keepOre);
    io.orDefault("keepHydrogen", v.keepHydrogen);
    io.orDefault("harvest", v.harvest);
    io.orDefault("objectives", v.objectives);
    io.orDefault("stance", v.stance);
    io.orDefault("nextID", v.nextID);
}

template <class IO> void fields(IO& io, Request& v) {
    io("id", v.id);
    io("what", v.what);
    io("count", v.count);
    io("repeats", v.repeats);
    io("at", v.at);
    io("site", v.site);
}

template <class IO> void fields(IO& io, Objective& v) {
    io("id", v.id);
    io("kind", v.kind);
    io("at", v.at);
    io("structure", v.structure);
}

template <class IO> void fields(IO& io, PilotRecord& v) { io("kinds", v.kinds); }

/// Tolerant: a count missing reads as 0.
template <class IO> void fields(IO& io, Tally& v) {
    io.orDefault("seconds", v.seconds);
    io.orDefault("takeovers", v.takeovers);
    io.orDefault("unitDamage", v.unitDamage);
    io.orDefault("buildingDamage", v.buildingDamage);
    io.orDefault("kills", v.kills);
    io.orDefault("razed", v.razed);
    io.orDefault("damageTaken", v.damageTaken);
    io.orDefault("shieldAbsorbed", v.shieldAbsorbed);
    io.orDefault("deaths", v.deaths);
    io.orDefault("ore", v.ore);
    io.orDefault("hydrogen", v.hydrogen);
    io.orDefault("built", v.built);
    io.orDefault("repaired", v.repaired);
    io.orDefault("healed", v.healed);
    io.orDefault("carried", v.carried);
    io.orDefault("burn", v.burn);
    io.orDefault("blastHits", v.blastHits);
    io.orZero("airKills", v.airKills);
    io.orZero("stomps", v.stomps);
    io.orZero("stompHits", v.stompHits);
    io.orZero("strikes", v.strikes);
    io.orZero("strikeHits", v.strikeHits);
    io.orZero("hiddenSeconds", v.hiddenSeconds);
}

template <class IO> void fields(IO& io, Stamp& v) {
    io("level", v.level);
    io("at", v.at);
    io("driven", v.driven);
    io("standing", v.standing);
    io("picked", v.picked);
}

template <class IO> void fields(IO& io, Standing& v) {
    io("army", v.army);
    io("workers", v.workers);
    io("ore", v.ore);
    io("hydrogen", v.hydrogen);
    io("supplyUsed", v.supplyUsed);
    io("supplyCap", v.supplyCap);
    io("buildings", v.buildings);
    io("mined", v.mined);
}

template <class IO> void fields(IO& io, Doodad& v) {
    io("kind", v.kind);
    io("position", v.position);
    io("rotation", v.rotation);
    io("scale", v.scale);
    io("variant", v.variant);
    io("mirrored", v.mirrored);
}

template <class IO> void fields(IO& io, MapDefinition& v) {
    io("name", v.name);
    io("version", v.version);
    io("seed", v.seed);
    io("bounds", v.bounds);
    io("levelHeight", v.levelHeight);
    io("plateaus", v.plateaus);
    io("ramps", v.ramps);
    io("patches", v.patches);
    io("bases", v.bases);
    io("starts", v.starts);
    io("doodads", v.doodads);
    io("mirrorX", v.mirrorX);
    io("squareSymmetric", v.squareSymmetric);
    io("view", v.view);
    io("playground", v.playground);
}

template <class IO> void fields(IO& io, GroundRect& v) {
    io("minX", v.minX);
    io("minZ", v.minZ);
    io("maxX", v.maxX);
    io("maxZ", v.maxZ);
}

template <class IO> void fields(IO& io, Plateau& v) {
    io("polygon", v.polygon);
    io("level", v.level);
}

template <class IO> void fields(IO& io, Ramp& v) {
    io("low", v.low);
    io("high", v.high);
    io("width", v.width);
    io("lowLevel", v.lowLevel);
    io("highLevel", v.highLevel);
}

template <class IO> void fields(IO& io, GroundPatch& v) {
    io("kind", v.kind);
    io("center", v.center);
    io("radius", v.radius);
}

template <class IO> void fields(IO& io, OreSpec& v) {
    io("position", v.position);
    io("amount", v.amount);
    io("angle", v.angle);
}

template <class IO> void fields(IO& io, BaseSite& v) {
    io("center", v.center);
    io("level", v.level);
    io("ore", v.ore);
    io("wells", v.wells);
}

template <class IO> void fields(IO& io, MapView& v) {
    io("projection", v.projection);
    io("screens", v.screens);
    io("covered", v.covered);
}

template <class IO> void fields(IO& io, CanvasProjection& v) {
    io("canvasWidth", v.canvasWidth);
    io("canvasHeight", v.canvasHeight);
    io("pointsPerCell", v.pointsPerCell);
    io("pitchDegrees", v.pitchDegrees);
    io("fovDegrees", v.fovDegrees);
}

template <class IO> void fields(IO& io, CanvasRect& v) {
    io("x", v.x);
    io("y", v.y);
    io("width", v.width);
    io("height", v.height);
}

/// The parts of a session that outlive its game.
struct Settings {
    std::optional<Listener> listener;
    std::optional<bool> ai;
    std::optional<bool> fog;
};

template <class IO> void fields(IO& io, Settings& v) {
    io("listener", v.listener);
    io("ai", v.ai);
    io("fog", v.fog);
}

template <class T> std::optional<T> decodeText(std::string_view text, std::string* error) {
    json j = json::parse(text.begin(), text.end(), nullptr, false);
    if (j.is_discarded()) {
        if (error) *error = "not JSON";
        return std::nullopt;
    }
    T v{};
    if constexpr (HasFields<T>) {
        if (!j.is_object()) {
            if (error) *error = "not an object";
            return std::nullopt;
        }
        std::string why;
        Reader r{j, why};
        fields(r, v);
        if (!r.ok) {
            if (error) *error = why;
            return std::nullopt;
        }
    } else {
        if (!get(j, v)) {
            if (error) *error = "cannot read";
            return std::nullopt;
        }
    }
    return v;
}

} // namespace coding

// MARK: - SessionStore

template <class T> std::optional<T> SessionStore::decode(std::string_view json, std::string* error) {
    return coding::decodeText<T>(json, error);
}

template <class T> std::string SessionStore::encode(const T& value, bool pretty) {
    return coding::put(value).dump(pretty ? 2 : -1);
}

#define AC_CODABLE(T)                                                                          \
    template std::optional<T> SessionStore::decode<T>(std::string_view, std::string*);        \
    template std::string SessionStore::encode<T>(const T&, bool);
AC_CODABLE(Session)
AC_CODABLE(GameState)
AC_CODABLE(MapDefinition)
AC_CODABLE(Player)
AC_CODABLE(Unit)
AC_CODABLE(Structure)
AC_CODABLE(Intel)
AC_CODABLE(Directives)
AC_CODABLE(PilotRecord)
AC_CODABLE(KindRecord)
AC_CODABLE(Tally)
AC_CODABLE(Listener)
#undef AC_CODABLE

Date Date::now() {
    using namespace std::chrono;
    return Date{duration<double>(system_clock::now().time_since_epoch()).count()};
}

std::string newUUID() {
    static thread_local std::mt19937_64 gen{std::random_device{}()};
    const uint64_t a = gen(), b = gen();
    unsigned char bytes[16];
    for (int i = 0; i < 8; i++) bytes[i] = static_cast<unsigned char>(a >> (i * 8));
    for (int i = 0; i < 8; i++) bytes[8 + i] = static_cast<unsigned char>(b >> (i * 8));
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40); // version 4
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80); // RFC 4122 variant
    char buf[40];
    std::snprintf(buf, sizeof buf, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", bytes[0],
                  bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
                  bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return buf;
}

/// A fresh game; the win tally carries over while the map keeps its players.
void Session::restart(const MapDefinition& map) {
    // Teams carry over too (C++ only), while the map has starts for them.
    const auto teams = state.teams();
    const size_t players = teams && teams->size() <= map.starts.size() ? teams->size() : map.starts.size();
    state = GameState::new_(map, state.score.size() == players ? std::optional(state.score) : std::nullopt, 0, teams);
    created = Date::now();
    mapName = map.name;
    mapVersion = map.version;
    game = newUUID();
    gameStarted = created;
}

/// The next game after a victory lap (`Simulation.newGame`): a new id,
/// started now. The caller copies the new game's state in.
void Session::nextGame() {
    game = newUUID();
    gameStarted = Date::now();
}

/// A session saved before tracking gets a game id, started when the
/// session was (`created`). Nothing changes for one that has them.
void Session::fillGame() {
    if (!game) game = newUUID();
    if (!gameStarted) gameStarted = created;
}

std::optional<std::string> readFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::nullopt;
    std::string out;
    char buf[65536];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) return std::nullopt;
    return out;
}

bool writeFileAtomically(const std::string& path, const std::string& contents) {
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool wrote = std::fwrite(contents.data(), 1, contents.size(), f) == contents.size();
    const bool closed = std::fclose(f) == 0;
    std::error_code ec;
    if (!wrote || !closed) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string SessionStore::defaultDirectory() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/Library/Application Support/Autocraft/sessions";
}

std::string SessionStore::url(const std::string& id) const { return directory + "/" + id + ".json"; }

/// The saved session for a configuration, or a new one when there is none
/// or its map has changed since it was saved.
SessionStore::Opened SessionStore::open(const ScreenConfig& config, const MapDefinition& map) const {
    return open(config.sessionID(), config.signature(), map);
}

/// The saved session under `id` (window mode has its own, apart from
/// the wallpaper's), or a new one on `map`. Either has a game id.
SessionStore::Opened SessionStore::open(const std::string& id, const std::string& signature,
                                        const MapDefinition& map) const {
    const auto data = readFile(url(id));
    if (data) {
        auto s = decode<Session>(*data);
        if (s && s->mapName == map.name && s->mapVersion == map.version) {
            s->fillGame();
            return Opened{*s, true};
        }
    }
    Session s{.id = id, .signature = signature, .created = Date::now(), .mapName = map.name,
              .mapVersion = map.version, .state = GameState::new_(map)};
    s.fillGame();
    // An old or unreadable game still keeps "my location" and the AI setting.
    if (data) {
        if (auto old = coding::decodeText<coding::Settings>(*data, nullptr)) {
            s.listener = old->listener;
            s.ai = old->ai;
            s.fog = old->fog;
        }
    }
    return Opened{s, false};
}

/// Start the configuration's world over: a fresh game on the map,
/// keeping "my location" and the AI setting.
std::optional<Session> SessionStore::restart(const ScreenConfig& config, const MapDefinition& map) const {
    Session s = open(config, map).session;
    s.restart(map);
    if (!save(s)) return std::nullopt;
    return s;
}

/// Start `id` over on `map`, keeping "my location", the AI and the fog.
std::optional<Session> SessionStore::restart(const std::string& id, const std::string& signature,
                                             const MapDefinition& map) const {
    Session s = open(id, signature, map).session;
    s.restart(map);
    if (!save(s)) return std::nullopt;
    return s;
}

bool SessionStore::save(const Session& session) const {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;
    return writeFileAtomically(url(session.id), encode(session));
}

std::optional<Session> SessionStore::load(const std::string& path, std::string* error) {
    const auto data = readFile(path);
    if (!data) {
        if (error) *error = "cannot read " + path;
        return std::nullopt;
    }
    return decode<Session>(*data, error);
}

bool SessionStore::save(const Session& session, const std::string& path) {
    return writeFileAtomically(path, encode(session));
}

} // namespace ac
