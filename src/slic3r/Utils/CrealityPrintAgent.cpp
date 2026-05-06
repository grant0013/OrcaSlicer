#include "CrealityPrintAgent.hpp"

#include <boost/log/trivial.hpp>

#define CrealityPrintAgent_VERSION "0.1.0"

namespace Slic3r {

// ---------------------------------------------------------------------------
// Construction / metadata
// ---------------------------------------------------------------------------

CrealityPrintAgent::CrealityPrintAgent(std::string log_dir)
    : MoonrakerPrinterAgent(std::move(log_dir))
{
}

AgentInfo CrealityPrintAgent::get_agent_info_static()
{
    return AgentInfo{
        "crealityprint",
        "CrealityPrint",
        CrealityPrintAgent_VERSION,
        "Creality K-series printer agent (CFS-aware filament sync)"
    };
}

// ---------------------------------------------------------------------------
// Model whitelist + naming
// ---------------------------------------------------------------------------

bool CrealityPrintAgent::is_supported_model(const std::string& model_id)
{
    return model_id == "F008"  // K2 Plus
        || model_id == "F012"  // K2 Pro
        || model_id == "F021"; // K2 / K2 Combo
}

std::string CrealityPrintAgent::model_name_for(const std::string& model_id)
{
    if (model_id == "F008") return "K2 Plus";
    if (model_id == "F012") return "K2 Pro";
    if (model_id == "F021") return "K2";
    return "unknown (" + model_id + ")";
}

// ---------------------------------------------------------------------------
// Filament sync entry point
// ---------------------------------------------------------------------------

bool CrealityPrintAgent::fetch_filament_info(std::string dev_id)
{
    std::string error;

    // 1. Determine printer model via Moonraker base. If model is not a CFS-capable
    //    K-series board, fall back to base class.
    //    NOTE: device_info is inherited from MoonrakerPrinterAgent (protected member).
    const std::string model_id = device_info.model_id;
    if (!is_supported_model(model_id)) {
        BOOST_LOG_TRIVIAL(info)
            << "CrealityPrintAgent::fetch_filament_info: model "
            << model_id << " is not CFS-capable, deferring to base agent.";
        return MoonrakerPrinterAgent::fetch_filament_info(std::move(dev_id));
    }

    BOOST_LOG_TRIVIAL(info)
        << "CrealityPrintAgent::fetch_filament_info: detected "
        << model_name_for(model_id) << " (" << model_id << ")";

    // 2. Query CFS slots over the Creality web-server websocket.
    std::vector<CFSSlot> slots;
    if (!fetch_cfs_slots(device_info.dev_ip, slots, error)) {
        BOOST_LOG_TRIVIAL(warning)
            << "CrealityPrintAgent::fetch_filament_info: CFS query failed: " << error;
        return false;
    }

    BOOST_LOG_TRIVIAL(info)
        << "CrealityPrintAgent::fetch_filament_info: " << slots.size()
        << " CFS slots reported.";

    // 3. Convert each CFS slot to AmsTrayData and publish to the device model.
    //    TODO(harktech): wire into device model — see Qidi/SnapmakerPrinterAgent for pattern.
    //    For now this is a stub that only logs; the UI will not yet update.
    for (const auto& s : slots) {
        BOOST_LOG_TRIVIAL(debug)
            << "  box=" << s.box_id << " slot=" << s.slot_id
            << " color=" << s.color_hex
            << " type=" << s.filament_type
            << " brand=" << s.brand_name
            << " vendor=" << s.vendor;
    }

    return true;
}

// ---------------------------------------------------------------------------
// CFS websocket query — STUB. To be implemented in harktech/cfs-agent-protocol.
// ---------------------------------------------------------------------------

bool CrealityPrintAgent::fetch_cfs_slots(const std::string&    base_ip,
                                        std::vector<CFSSlot>& slots,
                                        std::string&          error) const
{
    (void)base_ip;
    (void)slots;
    error = "fetch_cfs_slots not yet implemented (skeleton commit)";
    BOOST_LOG_TRIVIAL(warning) << "CrealityPrintAgent::" << error;

    // Hardcoded test data so the UI/dispatch layer can be exercised before the
    // real websocket protocol is wired up.
    slots.clear();
    slots.push_back({0, 0, "#FF0000", "PLA",      "Hyper PLA",   "Creality"});
    slots.push_back({0, 1, "#000000", "PLA",      "PLA",         ""});
    slots.push_back({0, 2, "#FFFFFF", "PLA Silk", "PLA Silk",    ""});
    slots.push_back({0, 3, "#FF8000", "PETG",     "Hyper PETG",  "Creality"});
    return true;  // return true for skeleton wiring; flip to false once real protocol lands
}

// ---------------------------------------------------------------------------
// Filament-type / preset mapping helpers
// ---------------------------------------------------------------------------

std::string CrealityPrintAgent::normalize_filament_type(const std::string& filament_type)
{
    // Strip common subtype suffixes so we can fall back to base-type matching.
    // Examples: "PLA Silk" → "PLA", "PLA+" → "PLA", "ABS Pro" → "ABS".
    static const std::vector<std::string> bases = {"PLA", "ABS", "ASA", "PETG", "PET", "TPU", "PC", "PA", "ASA"};
    for (const auto& base : bases) {
        if (filament_type.rfind(base, 0) == 0) return base;
    }
    return filament_type;
}

std::string CrealityPrintAgent::map_to_orca_setting_id(const std::string& vendor,
                                                      const std::string& brand_name,
                                                      const std::string& base_type) const
{
    // TODO(harktech): port the 3-tier matching from QidiPrinterAgent::fetch_slot_info():
    //   1. exact match by Creality-specific filament_id (if present)
    //   2. vendor + brand_name fuzzy match against visible base presets
    //   3. fall back to filament_id_by_type using the base type
    (void)vendor;
    (void)brand_name;
    return base_type;  // placeholder
}

} // namespace Slic3r
