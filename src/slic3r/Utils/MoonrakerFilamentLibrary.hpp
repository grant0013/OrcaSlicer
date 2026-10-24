#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace Slic3r {

class DynamicPrintConfig;
class PresetBundle;

// Filament profiles kept by a Klipper printer, and their Orca filament presets.
//
// A printer can publish the full profile of the filament in each lane, not just its
// material type: name, vendor, temperatures, pressure advance, flow, retraction and
// fans. The Creality K2 open stack does this through the `hark_cfs_slots` Klipper
// object and the Moonraker `lane_data` namespace (name, vendor and filament_id per lane).
// Filament sync turns each such profile into a user filament preset for the selected
// printer, so the slot maps to the printer's own settings instead of "Generic <type>".
// The same printer accepts presets back with CFS_FILAMENT_SAVE.
namespace MoonrakerFilamentLibrary {

struct Profile
{
    std::string library_id; // printer-side id, e.g. "cr-04001" (Creality) or "u-my-petg" (user)
    std::string name;       // "Hyper PLA"
    std::string vendor;     // "Creality"
    std::string type;       // "PLA"
    std::string color;      // "RRGGBB", may be empty
    // Printer field -> value: nozzle_temp, nozzle_first, min_temp, max_temp, bed_temp,
    // pressure_advance, flow_ratio, retract_length, retract_speed, fan_min, fan_max.
    std::map<std::string, double> settings;
};

// Read the settings fields a lane_data entry or hark_cfs_slots record carries into profile.settings.
void read_settings(const nlohmann::json& obj, Profile& profile);

// Query the hark_cfs_slots printer object. Returns false when the printer does not have it.
bool fetch_status(const std::string& base_url, const std::string& api_key, nlohmann::json& status, std::string& error);

// Loaded slots of a hark_cfs_slots status, by lane index (slot A = 0).
std::map<int, Profile> slot_profiles(const nlohmann::json& status);

// Find or create the user filament preset that carries this profile for the selected
// printer, updating its settings when the printer's profile has changed. Returns its
// filament_id, or an empty string when no preset could be made. GUI thread only.
// `changed` is set when a preset was created or rewritten.
std::string sync_preset(PresetBundle& bundle, const Profile& profile, bool& changed);

// Name a preset has in the printer library: the preset name without its " @<printer>" scope.
std::string library_name(const std::string& preset_name);

// Build the CFS_FILAMENT_SAVE command that stores a filament preset in the printer library.
// `status` is the printer's hark_cfs_slots status: an entry of the same name is updated
// (a user entry) or copied (a Creality entry) instead of adding a duplicate.
// `bed_temp_key` is the plate temperature option to send as the bed temperature. Settings
// outside the range the printer accepts are left out and listed in `skipped`.
std::string build_save_command(const DynamicPrintConfig& filament,
                               const std::string&        name,
                               const nlohmann::json&     status,
                               const std::string&        bed_temp_key,
                               std::string&              skipped);

// Run a G-code script through Moonraker. On failure `error` holds Moonraker's message.
bool run_gcode(const std::string& base_url, const std::string& api_key, const std::string& script, std::string& error);

// "192.168.1.5:7125" -> "http://192.168.1.5:7125", trailing slashes dropped.
std::string host_to_base_url(const std::string& print_host);

} // namespace MoonrakerFilamentLibrary
} // namespace Slic3r
