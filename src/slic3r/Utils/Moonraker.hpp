#ifndef slic3r_Moonraker_hpp_
#define slic3r_Moonraker_hpp_

#include <string>
#include <boost/optional.hpp>
#include <wx/string.h>
#include <wx/arrstr.h>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"


namespace Slic3r {

class DynamicPrintConfig;
class Http;

// Moonraker is the JSON / WebSocket gateway that ships in front of Klipper
// (and on Klipper-API-compatible firmwares like the Prusa-Firmware-Buddy
// Buddy-Klipper fork). REST shape differs from OctoPrint: distinct paths,
// JSON body for print/start, {"result":...}/{"error":...} envelope.
//
// Endpoints used:
//   GET  /server/info                      -- connection test, reads klippy_state
//   POST /server/files/upload (multipart)  -- upload gcode (form fields: file, root)
//   POST /printer/print/start (json)       -- {"filename":"<name>.gcode"} starts print
//
// Auth: X-Api-Key header if `printhost_apikey` is non-empty; Moonraker accepts
// unauthenticated LAN access by default, so the key is optional. HTTP Basic /
// Digest are not part of the Moonraker spec and are not sent.
// Snapshot of the motion limits a Klipper printer actually enforces, queried
// live from Moonraker. Fields are optional because availability depends on
// kinematics (deltas have no stepper_x/y range) and config (a printer without
// an [extruder] section reports no extrude limits). Toolhead values are the
// LIVE limits — they reflect runtime SET_VELOCITY_LIMIT overrides, which is
// what the machine will really do, unlike the static config file.
struct MoonrakerMachineLimits
{
    // result.status.toolhead (live)
    boost::optional<double> max_velocity;
    boost::optional<double> max_accel;
    boost::optional<double> square_corner_velocity;
    // result.status.configfile.settings.printer
    boost::optional<double> max_z_velocity;
    boost::optional<double> max_z_accel;
    // result.status.configfile.settings.extruder
    boost::optional<double> max_extrude_only_velocity;
    boost::optional<double> max_extrude_only_accel;
    // result.status.configfile.settings.stepper_x/y/z position ranges
    boost::optional<double> bed_min_x, bed_max_x;
    boost::optional<double> bed_min_y, bed_max_y;
    boost::optional<double> max_z;
    std::string kinematics;
};

class Moonraker : public PrintHost
{
public:
    Moonraker(DynamicPrintConfig *config);
    ~Moonraker() override = default;

    const char* get_name() const override;

    bool test(wxString &curl_msg) const override;
    wxString get_test_ok_msg() const override;
    wxString get_test_failed_msg(wxString &msg) const override;
    bool upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    // Moonraker announces itself via DNS-SD as _moonraker._tcp (zeroconf
    // component, on by default in standard installs) — Browse can find it.
    bool has_auto_discovery() const override { return true; }
    bool can_test() const override { return true; }

    // Query the motion limits Klipper enforces right now (live toolhead values,
    // plus static config for what toolhead doesn't expose). Synchronous with
    // bounded timeouts; returns false with a user-facing message in `error`.
    bool query_machine_limits(MoonrakerMachineLimits &limits, wxString &error) const;
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint; }
    std::string get_host() const override { return m_host; }
    bool get_storage(wxArrayString &storage_path, wxArrayString &storage_name) const override;
    const std::string& get_apikey() const { return m_apikey; }
    const std::string& get_cafile() const { return m_cafile; }

protected:
    std::string m_host;
    std::string m_apikey;
    std::string m_cafile;
    bool        m_ssl_revoke_best_effort;

    void set_auth(Http &http) const;
    std::string make_url(const std::string &path) const;
    bool start_print(wxString &error_msg, const std::string &filename) const;
};

}

#endif
