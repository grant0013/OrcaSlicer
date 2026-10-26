#include "MoonrakerFilamentLibrary.hpp"

#include "CrealityPrintAgent.hpp"
#include "Http.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

namespace Slic3r {
namespace MoonrakerFilamentLibrary {

namespace {

// Printer profile field, CFS_FILAMENT_SAVE parameter and Orca filament option.
// Mirrors SETTINGS in the printer's hark_cfs_slots.py.
struct Setting
{
    const char* field;
    const char* param;
    const char* key;
    bool        is_int;
};

const Setting SETTINGS[] = {
    {"nozzle_temp", "NOZZLE", "nozzle_temperature", true},
    {"nozzle_first", "NOZZLE_FIRST", "nozzle_temperature_initial_layer", true},
    {"min_temp", "MIN_TEMP", "nozzle_temperature_range_low", true},
    {"max_temp", "MAX_TEMP", "nozzle_temperature_range_high", true},
    {"bed_temp", "BED", "hot_plate_temp", true},
    {"pressure_advance", "PA", "pressure_advance", false},
    {"flow_ratio", "FLOW", "filament_flow_ratio", false},
    {"retract_length", "RETRACT", "filament_retraction_length", false},
    {"retract_speed", "RETRACT_SPEED", "filament_retraction_speed", false},
    {"fan_min", "FAN_MIN", "fan_min_speed", true},
    {"fan_max", "FAN_MAX", "fan_max_speed", true},
};

// Ranges CFS_FILAMENT_SAVE accepts (get_int maxval 500, get_float maxval 10).
constexpr double MAX_INT_SETTING   = 500.;
constexpr double MAX_FLOAT_SETTING = 10.;

std::string json_string(const nlohmann::json& obj, const char* key)
{
    auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string format_number(double value, bool is_int)
{
    if (is_int)
        return std::to_string(std::lround(value));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", value);
    return buf;
}

// The printer stores 0 for a setting it has no value for. A fan speed of 0 is real,
// so fans count as unset only when both are 0.
bool has_setting(const Profile& profile, const std::string& field)
{
    auto it = profile.settings.find(field);
    if (it == profile.settings.end())
        return false;
    if (field == "fan_min" || field == "fan_max") {
        auto other = profile.settings.find(field == "fan_min" ? "fan_max" : "fan_min");
        return it->second != 0. || (other != profile.settings.end() && other->second != 0.);
    }
    return it->second != 0.;
}

// Set every extruder variant of a numeric filament option to one value.
bool set_number(DynamicPrintConfig& config, const std::string& key, double value, bool is_int)
{
    auto* opt = dynamic_cast<ConfigOptionVectorBase*>(config.option(key));
    if (opt == nullptr)
        return false;
    const std::string one    = format_number(value, is_int);
    std::string       values = one;
    for (size_t i = 1; i < opt->size(); ++i)
        values += "," + one;
    const std::string before = opt->serialize();
    opt->deserialize(values);
    return opt->serialize() != before;
}

bool set_string(DynamicPrintConfig& config, const std::string& key, const std::string& value)
{
    auto* opt = config.option<ConfigOptionStrings>(key);
    if (opt == nullptr || value.empty())
        return false;
    std::vector<std::string> values(std::max<size_t>(1, opt->values.size()), value);
    if (opt->values == values)
        return false;
    opt->values = std::move(values);
    return true;
}

// Write the printer profile into a filament config. Returns whether anything changed.
bool apply_profile(DynamicPrintConfig& config, const Profile& profile, const std::string& bed_temp_key)
{
    bool changed = set_string(config, "filament_type", profile.type);
    changed |= set_string(config, "filament_vendor", profile.vendor);
    for (const Setting& s : SETTINGS) {
        if (!has_setting(profile, s.field))
            continue;
        const double value = profile.settings.at(s.field);
        if (std::string(s.field) == "bed_temp") {
            // One bed temperature on the printer: the smooth and textured PEI plates
            // it ships with, plus whichever plate the project uses.
            std::set<std::string> keys{"hot_plate_temp", "textured_plate_temp", bed_temp_key};
            for (const std::string& key : keys) {
                changed |= set_number(config, key, value, true);
                changed |= set_number(config, key + "_initial_layer", value, true);
            }
        } else {
            changed |= set_number(config, s.key, value, s.is_int);
        }
    }
    return changed;
}

bool set_colour(DynamicPrintConfig& config, const Profile& profile)
{
    return profile.color.size() == 6 && set_string(config, "default_filament_colour", "#" + profile.color);
}

bool lists_printer(const Preset& preset, const std::string& printer)
{
    const auto* compatible = preset.config.option<ConfigOptionStrings>("compatible_printers");
    return compatible != nullptr && std::find(compatible->values.begin(), compatible->values.end(), printer) != compatible->values.end();
}

// The preset a new one is cloned from: the one filament sync would have picked for this
// spool, else a generic preset of its material, else any preset for this printer.
const Preset* find_template(const PresetCollection& filaments, const Profile& profile)
{
    const std::string matched = CrealityPrintAgent::match_filament_preset(filaments, profile.vendor, profile.name, profile.type);
    const Preset*     best    = nullptr;
    int               best_rank = 0;
    for (const Preset& p : filaments.get_presets()) {
        if (!p.is_compatible || p.is_default)
            continue;
        int rank = 1;
        if (!matched.empty() && p.filament_id == matched)
            rank += 8;
        if (boost::iequals(p.config.opt_string("filament_type", 0u), profile.type))
            rank += 4;
        if (p.is_system)
            rank += 2;
        if (boost::starts_with(p.name, "Generic "))
            rank += 1;
        if (rank > best_rank) {
            best      = &p;
            best_rank = rank;
        }
    }
    return best;
}

const nlohmann::json& list_of(const nlohmann::json& status, const char* key)
{
    static const nlohmann::json empty = nlohmann::json::array();
    auto it = status.find(key);
    return it != status.end() && it->is_array() ? *it : empty;
}

std::string find_id_by_name(const nlohmann::json& entries, const std::string& name)
{
    for (const auto& entry : entries)
        if (entry.is_object() && boost::iequals(json_string(entry, "name"), name))
            return json_string(entry, "id");
    return {};
}

// Klipper ends a command's parameters at '#', ';' or '*', and quotes delimit values.
std::string clean_param(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](char c) { return c == '#' || c == ';' || c == '*' || c == '"' || c == '\n' || c == '\r'; }),
                value.end());
    boost::trim(value);
    return value;
}

bool option_value(const DynamicPrintConfig& config, const std::string& key, double& value)
{
    const ConfigOption* opt = config.option(key);
    if (const auto* o = dynamic_cast<const ConfigOptionInts*>(opt); o && !o->values.empty())
        value = o->values.front();
    else if (const auto* o = dynamic_cast<const ConfigOptionIntsNullable*>(opt); o && !o->values.empty() && !o->is_nil(0))
        value = o->values.front();
    else if (const auto* o = dynamic_cast<const ConfigOptionFloats*>(opt); o && !o->values.empty())
        value = o->values.front();
    else if (const auto* o = dynamic_cast<const ConfigOptionFloatsNullable*>(opt); o && !o->values.empty() && !o->is_nil(0))
        value = o->values.front();
    else
        return false;
    return true;
}

} // namespace

void read_settings(const nlohmann::json& obj, Profile& profile)
{
    for (const Setting& s : SETTINGS) {
        auto it = obj.find(s.field);
        if (it != obj.end() && it->is_number())
            profile.settings[s.field] = it->get<double>();
    }
}

void assign_slot_presets(std::map<int, Profile>& profiles)
{
    std::map<std::string, const Profile*> first_by_id;
    for (auto& [lane, profile] : profiles) {
        auto [first, inserted] = first_by_id.emplace(profile.library_id, &profile);
        if (inserted)
            continue;
        if (profile.settings != first->second->settings)
            profile.variant = lane >= 0 && lane < 26 ? std::string("slot ") + char('A' + lane) : "slot " + std::to_string(lane + 1);
        else
            profile.color.clear(); // the first slot's preset, whose colour that slot sets
    }
}

// Stable filament_id for a printer profile, in the "P" + 7 hex digits form of
// user-created filaments. FNV-1a, so it is the same on every platform and run.
std::string profile_filament_id(const Profile& profile)
{
    std::string key = "moonraker-filament:" + (profile.library_id.empty() ? profile.vendor + "/" + profile.name : profile.library_id);
    if (!profile.variant.empty())
        key += "#" + profile.variant;
    uint32_t hash = 2166136261u;
    for (unsigned char c : key) {
        hash ^= c;
        hash *= 16777619u;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "P%07x", hash & 0xfffffffu);
    return buf;
}

std::string preset_name(const Profile& profile, const std::string& printer)
{
    std::string name = profile.name;
    for (char& c : name)
        if (c == '/' || c == '\\')
            c = '-';
    if (!profile.variant.empty())
        name += " (" + profile.variant + ")";
    return name + " @" + printer;
}

bool fetch_status(const std::string& base_url, const std::string& api_key, nlohmann::json& status, std::string& error)
{
    std::string body;
    bool        ok   = false;
    auto        http = Http::get(base_url + "/printer/objects/query?hark_cfs_slots");
    if (!api_key.empty())
        http.header("X-Api-Key", api_key);
    http.timeout_connect(5)
        .timeout_max(10)
        .on_complete([&](std::string response, unsigned status_code) {
            body = std::move(response);
            ok   = status_code == 200;
            if (!ok)
                error = "HTTP " + std::to_string(status_code);
        })
        .on_error([&](std::string, std::string err, unsigned) { error = err; })
        .perform_sync();
    if (!ok)
        return false;

    auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded()) {
        error = "invalid JSON from the printer";
        return false;
    }
    // Moonraker leaves an object the printer does not have out of the status.
    const nlohmann::json found = json.contains("result") && json["result"].is_object() && json["result"].contains("status") ?
                                     json["result"]["status"].value("hark_cfs_slots", nlohmann::json()) :
                                     nlohmann::json();
    if (!found.is_object() || !found.contains("slots")) {
        error = "the printer has no hark_cfs_slots object";
        return false;
    }
    status = found;
    return true;
}

std::map<int, Profile> slot_profiles(const nlohmann::json& status)
{
    std::map<int, Profile> profiles;
    auto                   slots = status.find("slots");
    if (slots == status.end() || !slots->is_object())
        return profiles;
    for (const auto& [slot, rec] : slots->items()) {
        auto loaded = rec.is_object() ? rec.find("loaded") : rec.end();
        if (slot.size() != 1 || slot[0] < 'A' || slot[0] > 'Z' || !rec.is_object() || loaded == rec.end() || !loaded->is_boolean() ||
            !loaded->get<bool>())
            continue;
        Profile profile;
        profile.library_id = json_string(rec, "filament");
        profile.name       = json_string(rec, "name");
        profile.vendor     = json_string(rec, "brand");
        profile.type       = json_string(rec, "type");
        profile.color      = json_string(rec, "color");
        read_settings(rec, profile);
        profiles[slot[0] - 'A'] = std::move(profile);
    }
    return profiles;
}

std::string sync_preset(PresetBundle& bundle, const Profile& profile, bool& changed)
{
    if (profile.name.empty() || profile.type.empty())
        return {};

    PresetCollection& filaments    = bundle.filaments;
    const std::string printer      = bundle.printers.get_selected_preset().name;
    const std::string name         = preset_name(profile, printer);
    const std::string filament_id  = profile_filament_id(profile);
    const std::string bed_temp_key = bundle.project_config.has("curr_bed_type") ?
                                         get_bed_temp_key(bundle.project_config.opt_enum<BedType>("curr_bed_type")) :
                                         "hot_plate_temp";

    // The preset an earlier sync made, else a preset of the same name (for example one
    // the user sent to the printer from Orca).
    const Preset* existing = nullptr;
    for (const Preset& p : filaments.get_presets())
        if (p.is_user() && p.filament_id == filament_id && lists_printer(p, printer)) {
            existing = &p;
            break;
        }
    if (existing == nullptr)
        existing = filaments.find_preset(name, false);
    if (existing != nullptr && !existing->is_user())
        return existing->filament_id;

    // sync_ams_list matches a slot to a base preset by filament_id, so the preset is
    // a user root preset: no parent whose filament_id it would otherwise share.
    std::string id = filament_id;
    if (existing != nullptr) {
        if (existing->inherits().empty() && !existing->filament_id.empty())
            id = existing->filament_id;
        DynamicPrintConfig config  = existing->config;
        bool               differs = apply_profile(config, profile, bed_temp_key);
        differs |= set_colour(config, profile);
        if (!differs && existing->inherits().empty() && id == existing->filament_id)
            return id;
        if (filaments.get_edited_preset().name == existing->name && filaments.current_is_dirty()) {
            BOOST_LOG_TRIVIAL(warning) << "MoonrakerFilamentLibrary: \"" << existing->name
                                       << "\" has unsaved changes, not updating it from the printer";
            return existing->inherits().empty() ? existing->filament_id : std::string();
        }
    }

    const Preset* source = existing != nullptr ? existing : find_template(filaments, profile);
    if (source == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "MoonrakerFilamentLibrary: no filament preset for " << printer << " to base \"" << profile.name
                                   << "\" on";
        return {};
    }

    const bool               is_new      = existing == nullptr;
    const std::string        source_name = source->name; // cloning inserts into the collection, so `source` may not survive it
    std::vector<std::string> failures;
    const bool ok = filaments.clone_presets({source}, failures, [&](Preset& preset, Preset::Type&) {
        preset.name        = name;
        preset.filament_id = id;
        if (auto* compatible = preset.config.option<ConfigOptionStrings>("compatible_printers"))
            compatible->values = {printer};
        apply_profile(preset.config, profile, bed_temp_key);
        set_colour(preset.config, profile);
    }, !is_new);
    if (!ok) {
        BOOST_LOG_TRIVIAL(warning) << "MoonrakerFilamentLibrary: could not save filament preset \"" << name << "\"";
        return {};
    }
    changed = true;
    BOOST_LOG_TRIVIAL(info) << "MoonrakerFilamentLibrary: " << (is_new ? "created" : "updated") << " \"" << name << "\" (filament_id "
                            << id << ", printer id " << profile.library_id << ") from " << source_name;
    return id;
}

std::string library_name(const std::string& preset_name)
{
    std::string name = preset_name.substr(0, preset_name.find(" @"));
    boost::trim(name);
    return name;
}

std::string build_save_command(const DynamicPrintConfig& filament,
                               const std::string&        name,
                               const nlohmann::json&     status,
                               const std::string&        bed_temp_key,
                               std::string&              skipped)
{
    std::string command = "CFS_FILAMENT_SAVE";
    // Update the printer's own entry of this name, or start a copy of a Creality entry
    // of this name. Without an ID the printer adds a new entry on every save.
    if (std::string id = find_id_by_name(list_of(status, "library"), name); !id.empty())
        command += " ID=" + id;
    else if (std::string base = find_id_by_name(list_of(status, "materials"), name); !base.empty())
        command += " BASE=" + base;

    command += " NAME=\"" + clean_param(name) + "\"";
    command += " TYPE=\"" + clean_param(filament.opt_string("filament_type", 0u)) + "\"";
    if (const std::string vendor = clean_param(filament.opt_string("filament_vendor", 0u)); !vendor.empty())
        command += " BRAND=\"" + vendor + "\"";
    std::string colour = clean_param(filament.opt_string("default_filament_colour", 0u));
    if (colour.size() == 9)
        colour.resize(6); // RRGGBBAA
    if (colour.size() == 6)
        command += " COLOR=" + colour;

    skipped.clear();
    for (const Setting& s : SETTINGS) {
        double value = 0.;
        if (!option_value(filament, std::string(s.field) == "bed_temp" ? bed_temp_key : s.key, value))
            continue;
        if (value < 0. || value > (s.is_int ? MAX_INT_SETTING : MAX_FLOAT_SETTING)) {
            skipped += (skipped.empty() ? "" : ", ") + std::string(s.key) + " " + format_number(value, s.is_int);
            continue;
        }
        command += " " + std::string(s.param) + "=" + format_number(value, s.is_int);
    }
    return command;
}

bool run_gcode(const std::string& base_url, const std::string& api_key, const std::string& script, std::string& error)
{
    bool ok   = false;
    auto http = Http::post(base_url + "/printer/gcode/script");
    if (!api_key.empty())
        http.header("X-Api-Key", api_key);
    http.header("Content-Type", "application/json")
        .set_post_body(nlohmann::json{{"script", script}}.dump())
        .timeout_connect(5)
        .timeout_max(20)
        .on_complete([&](std::string, unsigned status_code) {
            ok = status_code == 200;
            if (!ok)
                error = "HTTP " + std::to_string(status_code);
        })
        .on_error([&](std::string body, std::string err, unsigned) {
            // Klipper's error ("TYPE is required", "Unknown command ...") comes back in the body.
            auto json = nlohmann::json::parse(body, nullptr, false);
            if (!json.is_discarded() && json.contains("error") && json["error"].is_object())
                error = json_string(json["error"], "message");
            if (error.empty())
                error = err;
        })
        .perform_sync();
    return ok;
}

std::string host_to_base_url(const std::string& print_host)
{
    std::string url = boost::trim_copy(print_host);
    if (!url.empty() && url.find("://") == std::string::npos)
        url = "http://" + url;
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    return url;
}

} // namespace MoonrakerFilamentLibrary
} // namespace Slic3r
