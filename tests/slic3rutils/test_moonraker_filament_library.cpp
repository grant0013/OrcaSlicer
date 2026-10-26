#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>
#include <string>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/MoonrakerFilamentLibrary.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;
namespace Library = Slic3r::MoonrakerFilamentLibrary;

namespace {

// What a K2 open stack printer reports for its hark_cfs_slots object, trimmed to two slots
// and one entry of each library list.
nlohmann::json k2_status()
{
    return nlohmann::json::parse(R"({
        "slots": {
            "A": {"loaded": true, "filament": "cr-04001", "type": "PLA", "name": "CR-PLA", "brand": "Creality",
                  "color": "000000", "nozzle_temp": 220, "nozzle_first": 225, "min_temp": 190, "max_temp": 240,
                  "bed_temp": 50, "pressure_advance": 0.04, "flow_ratio": 0.95, "retract_length": 1.0,
                  "retract_speed": 0, "fan_min": 100, "fan_max": 100},
            "B": {"loaded": false, "filament": "", "type": "", "name": ""},
            "C": {"loaded": true, "filament": "u-my-petg", "type": "PETG", "name": "My PETG", "brand": "Sunlu",
                  "color": "26EEEE", "nozzle_temp": 250, "pressure_advance": 0.07}
        },
        "library": [{"id": "u-my-petg", "name": "My PETG", "type": "PETG"}],
        "materials": [{"id": "cr-04001", "name": "CR-PLA", "type": "PLA"}]
    })");
}

DynamicPrintConfig petg_filament()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionStrings>("filament_type")->values           = {"PETG"};
    config.option<ConfigOptionStrings>("filament_vendor")->values         = {"Sunlu"};
    config.option<ConfigOptionStrings>("default_filament_colour")->values = {"#26EEEE"};
    config.option<ConfigOptionInts>("nozzle_temperature")->values         = {250};
    config.option<ConfigOptionInts>("textured_plate_temp")->values        = {75};
    config.option<ConfigOptionFloats>("pressure_advance")->values         = {0.07};
    return config;
}

} // namespace

TEST_CASE("Loaded K2 slots become profiles keyed by lane", "[MoonrakerFilamentLibrary]")
{
    const auto profiles = Library::slot_profiles(k2_status());

    REQUIRE(profiles.size() == 2); // slot B is empty
    const auto &pla = profiles.at(0);
    CHECK(pla.library_id == "cr-04001");
    CHECK(pla.name == "CR-PLA");
    CHECK(pla.vendor == "Creality");
    CHECK(pla.type == "PLA");
    CHECK(pla.color == "000000");
    CHECK_THAT(pla.settings.at("nozzle_first"), WithinAbs(225., 1e-9));
    CHECK_THAT(pla.settings.at("pressure_advance"), WithinAbs(0.04, 1e-9));
    CHECK(profiles.at(2).library_id == "u-my-petg");
}

TEST_CASE("Slots with one printer profile share a preset unless their settings differ", "[MoonrakerFilamentLibrary]")
{
    Library::Profile black;
    black.library_id                   = "cr-04001";
    black.name                         = "CR-PLA";
    black.color                        = "000000";
    black.settings["pressure_advance"] = 0.04;

    std::map<int, Library::Profile> profiles;
    profiles[1]       = black; // slot B
    profiles[2]       = black; // slot C, same settings
    profiles[2].color = "FB8C00";
    profiles[3]       = black; // slot D, its own pressure advance
    profiles[3].color = "808080";
    profiles[3].settings["pressure_advance"] = 0.0741;

    Library::assign_slot_presets(profiles);

    CHECK(profiles.at(1).variant.empty());
    CHECK(profiles.at(1).color == "000000"); // the first slot sets the shared preset's colour
    CHECK(profiles.at(2).variant.empty());
    CHECK(profiles.at(2).color.empty());
    CHECK(profiles.at(3).variant == "slot D");
    CHECK(profiles.at(3).color == "808080");

    CHECK(Library::preset_name(profiles.at(1), "Creality K2 Mainline") == "CR-PLA @Creality K2 Mainline");
    CHECK(Library::preset_name(profiles.at(3), "Creality K2 Mainline") == "CR-PLA (slot D) @Creality K2 Mainline");
    CHECK(Library::profile_filament_id(profiles.at(1)) == Library::profile_filament_id(profiles.at(2)));
    CHECK(Library::profile_filament_id(profiles.at(1)) != Library::profile_filament_id(profiles.at(3)));
    // Presets made before slot variants existed keep their filament_id
    CHECK(Library::profile_filament_id(profiles.at(1)) == "Pf986c83");
}

TEST_CASE("Saving updates the printer's entry of the same name", "[MoonrakerFilamentLibrary]")
{
    std::string skipped;
    const std::string command = Library::build_save_command(petg_filament(), "My PETG", k2_status(), "textured_plate_temp", skipped);

    CHECK(command.rfind("CFS_FILAMENT_SAVE ID=u-my-petg NAME=\"My PETG\" TYPE=\"PETG\" BRAND=\"Sunlu\" COLOR=26EEEE", 0) == 0);
    CHECK(command.find(" NOZZLE=250") != std::string::npos);
    CHECK(command.find(" BED=75") != std::string::npos); // the project's plate, not hot_plate_temp
    CHECK(command.find(" PA=0.07") != std::string::npos);
    CHECK(command.find('#') == std::string::npos);
}

TEST_CASE("Saving a Creality profile's name copies that profile", "[MoonrakerFilamentLibrary]")
{
    std::string skipped;
    const std::string command = Library::build_save_command(petg_filament(), "cr-pla", k2_status(), "hot_plate_temp", skipped);
    CHECK(command.rfind("CFS_FILAMENT_SAVE BASE=cr-04001 NAME=\"cr-pla\"", 0) == 0);

    const std::string fresh = Library::build_save_command(petg_filament(), "Other PETG", k2_status(), "hot_plate_temp", skipped);
    CHECK(fresh.rfind("CFS_FILAMENT_SAVE NAME=\"Other PETG\"", 0) == 0);
}

TEST_CASE("Settings the printer would reject are left out", "[MoonrakerFilamentLibrary]")
{
    DynamicPrintConfig config = petg_filament();
    // A filament override, so not among the full print config defaults
    config.option<ConfigOptionFloatsNullable>("filament_retraction_speed", true)->values = {30.};
    std::string skipped;
    const std::string command = Library::build_save_command(config, "My PETG; \"quoted\"", k2_status(), "hot_plate_temp", skipped);

    CHECK(command.find("RETRACT_SPEED") == std::string::npos);
    CHECK(skipped == "filament_retraction_speed 30");
    CHECK(command.find("NAME=\"My PETG quoted\"") != std::string::npos);
}

TEST_CASE("Preset names and hosts map to the printer's terms", "[MoonrakerFilamentLibrary]")
{
    CHECK(Library::library_name("Hyper PLA @Creality K2 Mainline") == "Hyper PLA");
    CHECK(Library::library_name("My PETG") == "My PETG");
    CHECK(Library::host_to_base_url("192.168.3.113:7126/") == "http://192.168.3.113:7126");
    CHECK(Library::host_to_base_url("https://k2.local") == "https://k2.local");
}
