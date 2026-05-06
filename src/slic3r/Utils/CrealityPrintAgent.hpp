#ifndef __CREALITY_PRINT_AGENT_HPP__
#define __CREALITY_PRINT_AGENT_HPP__

#include "MoonrakerPrinterAgent.hpp"

#include <map>
#include <string>
#include <vector>

namespace Slic3r {

/**
 * CrealityPrintAgent — filament sync for Creality K-series printers with CFS.
 *
 * Architecture:
 *   - Inherits MoonrakerPrinterAgent for all communication / certificates / discovery /
 *     binding / print-job operations. Most K-series boards run Klipper + Moonraker, so
 *     the inherited base behaviour is correct.
 *   - Overrides only fetch_filament_info() to query Creality's web-server (port 80)
 *     for CFS slot state — this is a separate protocol from Moonraker and provides
 *     loaded-filament data Moonraker does not expose.
 *
 * Supported models (queried via /info → "model" field):
 *   F008 — K2 Plus    (CFS-capable)
 *   F012 — K2 Pro     (CFS-capable)
 *   F021 — K2 / K2 Combo (CFS-capable when CFS box attached)
 *
 * For unsupported models, falls back to MoonrakerPrinterAgent::fetch_filament_info().
 */
class CrealityPrintAgent final : public MoonrakerPrinterAgent
{
public:
    explicit CrealityPrintAgent(std::string log_dir);
    ~CrealityPrintAgent() override = default;

    static AgentInfo get_agent_info_static();
    AgentInfo        get_agent_info() override { return get_agent_info_static(); }

    // Override filament sync — query CFS slots from Creality web-server.
    bool fetch_filament_info(std::string dev_id) override;

private:
    /// One CFS slot's loaded-filament data (parsed from Creality web-server response).
    struct CFSSlot
    {
        int         box_id;        ///< CFS unit index (0 for first box, 1 for chained second box, …)
        int         slot_id;       ///< Slot index within the box (0–3)
        std::string color_hex;     ///< e.g. "#FFAA00"
        std::string filament_type; ///< Base type, e.g. "PLA", "ABS", "PETG"
        std::string brand_name;    ///< e.g. "Hyper PLA", "PLA Silk"
        std::string vendor;        ///< e.g. "Creality", "eSUN", "" if unknown
    };

    /// Whitelist of supported model IDs (from /info endpoint).
    static bool is_supported_model(const std::string& model_id);

    /// Map a Creality model ID to a human-readable name. F021→"K2", F008→"K2 Plus", etc.
    static std::string model_name_for(const std::string& model_id);

    /**
     * Open a websocket to Creality's web-server (port 80) and query CFS slot state.
     * @param base_ip      Printer IP (extracted from device_info.base_url).
     * @param slots        Output: parsed slot data, one entry per loaded CFS slot.
     * @param error        Output: human-readable error on failure.
     * @return true on success, false on protocol/network error.
     */
    bool fetch_cfs_slots(const std::string&      base_ip,
                         std::vector<CFSSlot>&   slots,
                         std::string&            error) const;

    /// Strip Orca-style filament-type variants ("PLA Silk", "PLA+") down to base ("PLA").
    static std::string normalize_filament_type(const std::string& filament_type);

    /// Resolve (vendor, brand_name, base_type) → Orca preset filament_id.
    /// Returns empty string when no preset matches.
    std::string map_to_orca_setting_id(const std::string& vendor,
                                       const std::string& brand_name,
                                       const std::string& base_type) const;
};

} // namespace Slic3r

#endif // __CREALITY_PRINT_AGENT_HPP__
