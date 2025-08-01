#include "PresetUpdater.hpp"

#include <algorithm>
#include <thread>
#include <unordered_map>
#include <ostream>
#include <utility>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/msgdlg.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/format.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/UpdateDialogs.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Config/Version.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "slic3r/GUI/MarkdownTip.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"

namespace fs = boost::filesystem;
using Slic3r::GUI::Config::Index;
using Slic3r::GUI::Config::Version;
using Slic3r::GUI::Config::Snapshot;
using Slic3r::GUI::Config::SnapshotDB;


// FIXME: Incompat bundle resolution doesn't deal with inherited user presets

namespace Slic3r {


static const char *INDEX_FILENAME = "index.idx";
static const char *TMP_EXTENSION = ".data";
static const char *PRESET_SUBPATH = "presets";
static const char *PLUGINS_SUBPATH = "plugins";

int copy_file_fix(const fs::path &source, const fs::path &target, std::string& error_message)
{
	BOOST_LOG_TRIVIAL(debug) << format("PresetUpdater: Copying %1% -> %2%", source, target);

	//CopyFileResult cfr = Slic3r::GUI::copy_file_gui(source.string(), target.string(), error_message, false);
	CopyFileResult cfr = copy_file(source.string(), target.string(), error_message, false);
	if (cfr != CopyFileResult::SUCCESS) {
		BOOST_LOG_TRIVIAL(error) << "Copying failed(" << cfr << "): " << error_message;
		return -1;
	}
	// Permissions should be copied from the source file by copy_file(). We are not sure about the source
	// permissions, let's rewrite them with 644.
	static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
	fs::permissions(target, perms);

    return 0;
}

//BBS: add directory copy
int copy_directory_fix(const fs::path &source, const fs::path &target, std::string& error_message)
{
    int ret = 0;
    BOOST_LOG_TRIVIAL(debug) << format("PresetUpdater: Copying %1% -> %2%", source, target);

    if (fs::exists(target)) {
        boost::system::error_code ec;
        fs::remove_all(target, ec);
        if (ec) {
            error_message = ec.message();
            BOOST_LOG_TRIVIAL(error) << "copy_directory_fix: Failed to remove existing target directory: " + error_message;
            return -1;
        }
    }
    boost::system::error_code ec;
    fs::create_directories(target, ec);
    if (ec) {
        error_message = ec.message();
        BOOST_LOG_TRIVIAL(error) << "copy_directory_fix: Failed to create target directory: " + error_message;
        return -2;
    }
    for (auto &dir_entry : fs::directory_iterator(source))
    {
        fs::path source_file = dir_entry.path();
        fs::path target_file = target / dir_entry.path().filename();

        std::string name = dir_entry.path().filename().string();

        if (fs::is_directory(dir_entry)) {
            ret = copy_directory_fix(source_file, target_file, error_message);
            if (ret)
                return ret;
        }
        else {
            //CopyFileResult cfr = Slic3r::GUI::copy_file_gui(source_file, target_file, error_message, false);
            CopyFileResult cfr = copy_file(source_file.string(), target_file.string(), error_message, false);
            if (cfr != CopyFileResult::SUCCESS) {
                BOOST_LOG_TRIVIAL(error) << "Copying failed(" << cfr << "): " << error_message;
                return -3;
            }
        }
    }
    return 0;
}

struct Update
{
	fs::path source;
	fs::path target;

	Version version;
	std::string vendor;
	//BBS: use changelog string instead of url
	std::string change_log;
	std::string descriptions;

	bool forced_update;
	//BBS: add directory support
	bool is_directory {false};

	Update() {}
	//BBS: add directory support
	//BBS: use changelog string instead of url
	Update(fs::path &&source, fs::path &&target, const Version &version, std::string vendor, std::string changelog, std::string description, bool forced = false, bool is_dir = false)
		: source(std::move(source))
		, target(std::move(target))
		, version(version)
		, vendor(std::move(vendor))
		, change_log(std::move(changelog))
		, descriptions(std::move(description))
		, forced_update(forced)
		, is_directory(is_dir)
	{}

    //BBS: add directory support
    int install() const
    {
        int ret = 0;
        std::string error_message;

        if (is_directory) {
            ret = copy_directory_fix(source, target, error_message);
        }
        else {
            ret = copy_file_fix(source, target, error_message);
        }

        /*if (ret) {
            throw Slic3r::CriticalException(GUI::format(
                _L("Copying of file %1% to %2% failed: %3%"),
                source, target, error_message));
        }*/

        return ret;
    }

	friend std::ostream& operator<<(std::ostream& os, const Update &self)
	{
        os << "Update(" << PathSanitizer::sanitize(self.source.string()) << " -> " << PathSanitizer::sanitize(self.target.string()) << ')';
		return os;
	}
};

struct Incompat
{
	fs::path bundle;
	Version version;
	std::string vendor;
	//BBS: add directory support
	bool is_directory {false};

	Incompat(fs::path &&bundle, const Version &version, std::string vendor, bool is_dir = false)
		: bundle(std::move(bundle))
		, version(version)
		, vendor(std::move(vendor))
		, is_directory(is_dir)
	{}

	void remove() {
		// Remove the bundle file
		if (is_directory) {
			if (fs::exists(bundle))
                fs::remove_all(bundle);
		}
		else {
			if (fs::exists(bundle))
				fs::remove(bundle);
		}
	}

	friend std::ostream& operator<<(std::ostream& os , const Incompat &self) {
		os << "Incompat(" << self.bundle.string() << ')';
		return os;
	}
};

struct Updates
{
	std::vector<Incompat> incompats;
	std::vector<Update> updates;
};


wxDEFINE_EVENT(EVT_SLIC3R_VERSION_ONLINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE, wxCommandEvent);


struct PresetUpdater::priv
{
	std::vector<Index> index_db;

	bool enabled_version_check;
	bool enabled_config_update;
	std::string version_check_url;

	fs::path cache_path;
	fs::path rsrc_path;
	fs::path vendor_path;

	bool cancel;
	std::thread thread;

	bool has_waiting_updates { false };
	Updates waiting_updates;
	bool has_waiting_printer_updates { false };
    Updates waiting_printer_updates;

    struct Resource
    {
        std::string              version;
        std::string              description;
        std::string              url;
        std::string              vendor;
        bool                     force{false};
        std::string              cache_root;
        std::vector<std::string> sub_caches;
    };

    priv();

	void set_download_prefs(AppConfig *app_config);
	bool get_file(const std::string &url, const fs::path &target_path) const;
	//BBS: refine preset update logic
    bool extract_file(const fs::path &source_path, const fs::path &dest_path = {});
	void prune_tmps() const;
	void sync_version() const;
	void parse_version_string(const std::string& body) const;
    bool sync_resources(std::string http_url, std::map<std::string, Resource> &resources, bool check_patch = false,  std::string current_version="", std::string changelog_file="");

    void remove_config_files(std::string vendor, std::string sub_path) const;
    //compare_count means how many sub-versions in AA.BB.CC.DD need to be checked
    bool get_valid_ota_version(Semver& app_version, Semver& current_version, Semver& cached_version, int compare_count) const;
    void parse_ota_files(std::string ota_json, std::string& version, bool& force_upgrade, std::string& description) const;

    bool sync_config(std::string http_url, const VendorMap vendors);
    void sync_tooltip(std::string http_url, std::string language);
    void sync_plugins(std::string http_url, std::string plugin_version);
    void sync_printer_config(std::string http_url);
    bool get_cached_plugins_version(std::string &cached_version, bool& force);

	//BBS: refine preset update logic
	bool install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const;
	void check_installed_vendor_profiles() const;
    Updates get_printer_config_updates(bool update = false) const;
	Updates get_config_updates(const Semver& old_slic3r_version) const;
	bool perform_updates(Updates &&updates, bool snapshot = true) const;
	void set_waiting_updates(Updates u);
};

//BBS: change directories by design
PresetUpdater::priv::priv()
	: cache_path(fs::path(Slic3r::data_dir()) / "ota")
	, rsrc_path(fs::path(resources_dir()) / "profiles")
	, vendor_path(fs::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR)
	, cancel(false)
{
	//BBS: refine preset updater logic
	enabled_version_check = true;
	set_download_prefs(GUI::wxGetApp().app_config);
	// Install indicies from resources. Only installs those that are either missing or older than in resources.
	check_installed_vendor_profiles();
    perform_updates(get_printer_config_updates(), false);
	// Load indices from the cache directory.
	//index_db = Index::load_db();
}

// Pull relevant preferences from AppConfig
void PresetUpdater::priv::set_download_prefs(AppConfig *app_config)
{
	version_check_url = app_config->version_check_url();
	//TODO: for debug currently
	if (version_check_url.empty())
		enabled_config_update = true;
	else
		enabled_config_update = false;
}

//BBS: refine the Preset Updater logic
// Downloads a file (http get operation). Cancels if the Updater is being destroyed.
bool PresetUpdater::priv::get_file(const std::string &url, const fs::path &target_path) const
{
    bool res = false;
    fs::path tmp_path = target_path;
    tmp_path += format(".%1%%2%", get_current_pid(), TMP_EXTENSION);

    BOOST_LOG_TRIVIAL(info) << format("[BBS Updater]download file `%1%`, stored to `%2%`, tmp path `%3%`",
        url,
        PathSanitizer::sanitize(target_path),
        PathSanitizer::sanitize(tmp_path));

    Slic3r::Http::get(url)
        .on_progress([this](Slic3r::Http::Progress, bool &cancel_http) {
            if (cancel) {
                cancel_http = true;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned http_status) {
            (void)body;
            BOOST_LOG_TRIVIAL(error) << format("[BBS Updater]getting: `%1%`: http status %2%, %3%",
                url,
                http_status,
                error);
        })
        .on_complete([&](std::string body, unsigned /* http_status */) {
            fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            file.write(body.c_str(), body.size());
            file.close();
            fs::rename(tmp_path, target_path);
            res = true;
        })
        .perform_sync();

    return res;
}

//BBS: refine preset update logic
bool PresetUpdater::priv::extract_file(const fs::path &source_path, const fs::path &dest_path)
{
    bool res = true;
    std::string file_path = source_path.string();
    std::string parent_path = (!dest_path.empty() ? dest_path : source_path.parent_path()).string();
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);

    if (!open_zip_reader(&archive, file_path))
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to open zip reader for "<<file_path;
        return false;
    }

    mz_uint num_entries = mz_zip_reader_get_num_files(&archive);

    mz_zip_archive_file_stat stat;
    // we first loop the entries to read from the archive the .amf file only, in order to extract the version from it
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        if (mz_zip_reader_file_stat(&archive, i, &stat))
        {
            std::string dest_file = parent_path+"/"+stat.m_filename;
            if (stat.m_is_directory) {
                fs::path dest_path(dest_file);
                if (!fs::exists(dest_path))
                    fs::create_directories(dest_path);
				continue;
            }
            else if (stat.m_uncomp_size == 0) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]Unzip: invalid size for file "<<stat.m_filename;
                continue;
            }
            try
            {
                res = mz_zip_reader_extract_to_file(&archive, stat.m_file_index, dest_file.c_str(), 0);
                if (!res) {
                    BOOST_LOG_TRIVIAL(error) << "[BBL Updater]extract file " << stat.m_filename << " to dest " << PathSanitizer::sanitize(dest_file) << " failed";
                    close_zip_reader(&archive);
                    return res;
                }
                BOOST_LOG_TRIVIAL(info) << "[BBL Updater]successfully extract file " << stat.m_file_index << " to " << PathSanitizer::sanitize(dest_file);
            }
            catch (const std::exception& e)
            {
                // ensure the zip archive is closed and rethrow the exception
                close_zip_reader(&archive);
                BOOST_LOG_TRIVIAL(error) << "[BBL Updater]Archive read exception:"<<e.what();
                return false;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]Unzip: read file stat failed";
        }
    }
    close_zip_reader(&archive);

	return true;
}

// Remove leftover paritally downloaded files, if any.
void PresetUpdater::priv::prune_tmps() const
{
    for (auto &dir_entry : fs::directory_iterator(cache_path))
		if (is_plain_file(dir_entry) && dir_entry.path().extension() == TMP_EXTENSION) {
			BOOST_LOG_TRIVIAL(debug) << "[BBL Updater]remove old cached files: " << dir_entry.path().string();
			fs::remove(dir_entry.path());
		}
}

//BBS: refine the Preset Updater logic
// Get Slic3rPE version available online, save in AppConfig.
void PresetUpdater::priv::sync_version() const
{
	if (! enabled_version_check) { return; }

#if 0
	Http::get(version_check_url)
		.size_limit(SLIC3R_VERSION_BODY_MAX)
		.on_progress([this](Http::Progress, bool &cancel) {
			cancel = this->cancel;
		})
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			(void)body;
			BOOST_LOG_TRIVIAL(error) << format("Error getting: `%1%`: HTTP %2%, %3%",
				version_check_url,
				http_status,
				error);
		})
		.on_complete([&](std::string body, unsigned /* http_status */) {
			boost::trim(body);
			parse_version_string(body);
		})
		.perform_sync();
#endif
}

// Parses version string obtained in sync_version() and sends events to UI thread.
// Version string must contain release version on first line. Follows non-mandatory alpha / beta releases on following lines (alpha=2.0.0-alpha1).
void PresetUpdater::priv::parse_version_string(const std::string& body) const
{
#if 0
	// release version
	std::string version;
	const auto first_nl_pos = body.find_first_of("\n\r");
	if (first_nl_pos != std::string::npos)
		version = body.substr(0, first_nl_pos);
	else
		version = body;
	boost::optional<Semver> release_version = Semver::parse(version);
	if (!release_version) {
		BOOST_LOG_TRIVIAL(error) << format("Received invalid contents from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
		return;
	}
	BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
	wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
	evt->SetString(GUI::from_u8(version));
	GUI::wxGetApp().QueueEvent(evt);

	// alpha / beta version
	std::vector<std::string> prerelease_versions;
	size_t nexn_nl_pos = first_nl_pos;
	while (nexn_nl_pos != std::string::npos && body.size() > nexn_nl_pos + 1) {
		const auto last_nl_pos = nexn_nl_pos;
		nexn_nl_pos = body.find_first_of("\n\r", last_nl_pos + 1);
		std::string line;
		if (nexn_nl_pos == std::string::npos)
			line = body.substr(last_nl_pos + 1);
		else
			line = body.substr(last_nl_pos + 1, nexn_nl_pos - last_nl_pos - 1);

		// alpha
		if (line.substr(0, 6) == "alpha=") {
			version = line.substr(6);
			if (!Semver::parse(version)) {
				BOOST_LOG_TRIVIAL(error) << format("Received invalid contents for alpha release from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
				return;
			}
			prerelease_versions.emplace_back(version);
		// beta
		}
		else if (line.substr(0, 5) == "beta=") {
			version = line.substr(5);
			if (!Semver::parse(version)) {
				BOOST_LOG_TRIVIAL(error) << format("Received invalid contents for beta release from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
				return;
			}
			prerelease_versions.emplace_back(version);
		}
	}
	// find recent version that is newer than last full release.
	boost::optional<Semver> recent_version;
	for (const std::string& ver_string : prerelease_versions) {
		boost::optional<Semver> ver = Semver::parse(ver_string);
		if (ver && *release_version < *ver && ((recent_version && *recent_version < *ver) || !recent_version)) {
			recent_version = ver;
			version = ver_string;
		}
	}
	if (recent_version) {
		BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
		wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE);
		evt->SetString(GUI::from_u8(version));
		GUI::wxGetApp().QueueEvent(evt);
	}
#endif
    return;
}

//BBS: refine the Preset Updater logic
// Download vendor indices. Also download new bundles if an index indicates there's a new one available.
// Both are saved in cache.
bool PresetUpdater::priv::sync_resources(std::string http_url, std::map<std::string, Resource> &resources, bool check_patch, std::string current_version_str, std::string changelog_file)
{
    bool has_new_update = false;
    std::map<std::string, Resource>    resource_list;

    BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater]: sync_resources get preferred setting version for app version %1%, url: %2%, current_version_str %3%, check_patch %4%")%SLIC3R_APP_NAME%http_url%current_version_str%check_patch;

    std::string query_params = "?";
    bool        first        = true;
    for (auto resource_it : resources) {
        if (cancel) { return false; }
        auto resource_name = resource_it.first;
        boost::to_lower(resource_name);
        std::string query_resource = (boost::format("%1%=%2%")
            % resource_name % resource_it.second.version).str();
        if (!first) query_params += "&";
        query_params += query_resource;
        first = false;
    }

    std::string url = http_url;
    url += query_params;
    Slic3r::Http http = Slic3r::Http::get(url);
    BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater]: sync_resources request_url: %1%")%url;
    http.on_progress([this](Slic3r::Http::Progress, bool &cancel_http) {
            if (cancel) {
                cancel_http = true;
            }
        })
        .on_complete([this, &resource_list, resources](std::string body, unsigned) {
            try {
                BOOST_LOG_TRIVIAL(info) << "[BBL Updater]: request_resources, body=" << body;

                json        j       = json::parse(body);
                std::string message = j["message"].get<std::string>();

                if (message == "success") {
                    json resource = j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            std::string version;
                            std::string url;
                            std::string resource;
                            std::string description;
                            bool force_upgrade = false;
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(), "type")) {
                                    resource = sub_iter.value();
                                    BOOST_LOG_TRIVIAL(trace) << "[BBL Updater]: get version of settings's type, " << sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "version")) {
                                    version = sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "description")) {
                                    description = sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "url")) {
                                    url = sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(), "force_update")) {
                                    force_upgrade = sub_iter.value();
                                }
                            }
                            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]: get type " << resource << ", version " << version << ", url " << url<<", force_update "<<force_upgrade;

                            resource_list.emplace(resource, Resource{version, description, url, "", force_upgrade});
                        }
                    }
                } else {
                    BOOST_LOG_TRIVIAL(error) << "[BBL Updater]: get version of settings failed, body=" << body;
                }
            } catch (std::exception &e) {
                BOOST_LOG_TRIVIAL(error) << (boost::format("[BBL Updater]: get version of settings failed, exception=%1% body=%2%") % e.what() % body).str();
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[BBL Updater]: get version of settings failed, body=" << body;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("[BBL Updater]: status=%1%, error=%2%, body=%3%") % status % error % body;
        })
        .perform_sync();

    for (auto & resource_it : resources) {
        if (cancel) { return false; }

        auto resource = resource_it.second;
        std::string resource_name = resource_it.first;
        boost::to_lower(resource_name);
        auto        resource_update = resource_list.find(resource_name);
        if (resource_update == resource_list.end()) {
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Vendor " << resource_name << " can not get setting versions online";
            continue;
        }
        Semver online_version = resource_update->second.version;
        // Semver current_version = get_version_from_json(vendor_root_config.string());
        Semver current_version = current_version_str.empty()?resource.version:current_version_str;
        bool version_match = ((online_version.maj() == current_version.maj()) && (online_version.min() == current_version.min()));
        if (version_match && check_patch) {
            int online_cc_patch = online_version.patch()/100;
            int current_cc_patch = current_version.patch()/100;
            if (online_cc_patch != current_cc_patch) {
                version_match = false;
                BOOST_LOG_TRIVIAL(warning) << boost::format("[BBL Updater]: online patch CC not match: online_cc_patch=%1%, current_cc_patch=%2%") % online_cc_patch % current_cc_patch;
            }
        }
        if (version_match && (current_version < online_version)) {
            if (cancel) { return false; }

            // need to download the online files
            fs::path cache_path(resource.cache_root);
            std::string online_url      = resource_update->second.url;
            std::string cache_file_path = (fs::temp_directory_path() / (fs::unique_path().string() + TMP_EXTENSION)).string();
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Downloading resource: " << resource_name << ", version " << online_version.to_string();
            if (!get_file(online_url, cache_file_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]download resource " << resource_name << " failed, url: " << online_url;
                continue;
            }
            if (cancel) { return false; }

            // remove previous files before
            if (resource.vendor.empty()) {
                if (resource.sub_caches.empty()) {
                    if (fs::exists(cache_path)) {
                        fs::remove_all(cache_path);
                        BOOST_LOG_TRIVIAL(info) << "[BBL Updater]remove cache path " << PathSanitizer::sanitize(cache_path.string());
                    }
                } else {
                    for (auto sub : resource.sub_caches) {
                        if (fs::exists(cache_path / sub)) {
                            fs::remove_all(cache_path / sub);
                            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]remove cache path " << PathSanitizer::sanitize((cache_path / sub).string());
                        }
                    }
                }
            }
            else {
                remove_config_files(resource.vendor, PRESET_SUBPATH);
            }
            // extract the file downloaded
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]start to unzip the downloaded file " << PathSanitizer::sanitize(cache_file_path) << " to "<< PathSanitizer::sanitize(cache_path);
            if (!fs::exists(cache_path))
                fs::create_directories(cache_path);
            if (!extract_file(cache_file_path, cache_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]extract resource " << resource_it.first << " failed, path: " << PathSanitizer::sanitize(cache_file_path);
                continue;
            }
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]finished unzip the downloaded file " << PathSanitizer::sanitize(cache_file_path);

            // save the description to disk
            if (changelog_file.empty())
                changelog_file = (cache_path / "changelog.json").string();
            else
                changelog_file = (cache_path / changelog_file).string();

            try {
                json j;
                //record the headers
                j["version"] = resource_update->second.version;
                j["description"] = resource_update->second.description;
                j["force"] = resource_update->second.force;

                boost::nowide::ofstream c;
                c.open(changelog_file, std::ios::out | std::ios::trunc);
                c << std::setw(4) << j << std::endl;
                c.close();

                has_new_update = true;
            }
            catch(std::exception &err) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": save to "<<changelog_file<<" got a generic exception, reason = " << err.what();
            }

            resource_it.second = resource_update->second;
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << boost::format("[BBL Updater]: online version=%1%, current_version=%2%, no need to download") % online_version.to_string() % current_version.to_string();
        }
    }
    return has_new_update;
}

bool PresetUpdater::priv::get_valid_ota_version(Semver& app_version, Semver& current_version, Semver& cached_version, int compare_count) const
{
    bool valid = false;
    int app_patch_cc = app_version.patch()/100;
    int cached_patch_cc = cached_version.patch()/100;
    int curent_patch_cc = current_version.patch()/100;

    int app_patch_dd = app_version.patch()%100;
    int cached_patch_dd = cached_version.patch() % 100;
    int curent_patch_dd = current_version.patch()%100;

    if (compare_count <= 1) {
        if ((cached_version.maj() == app_version.maj())
            && ((cached_version.min() > current_version.min())
              || ((cached_version.min() == current_version.min()) && (cached_patch_cc > curent_patch_cc))
              || ((cached_version.min() == current_version.min()) && (cached_patch_cc == curent_patch_cc)  && (cached_patch_dd > curent_patch_dd) )))
              valid = true;
    }
    else if (compare_count == 2) {
        if ((cached_version.maj() == app_version.maj()) && (cached_version.min() == app_version.min())
            && ((cached_patch_cc > curent_patch_cc)
              || ((cached_patch_cc == curent_patch_cc)  && (cached_patch_dd > curent_patch_dd) )))
              valid = true;
    }
    else if (compare_count == 3) {
        if ((cached_version.maj() == app_version.maj()) && (cached_version.min() == app_version.min()) && (cached_patch_cc == app_patch_cc)
            && (cached_patch_dd > curent_patch_dd))
              valid = true;
    }
    return valid;
}


void PresetUpdater::priv::parse_ota_files(std::string ota_json, std::string& version, bool& force_upgrade, std::string& description) const
{
    version.clear();
    if (fs::exists(ota_json)) {
        try {
            boost::nowide::ifstream ifs(ota_json);
            json j;
            ifs >> j;

            if (j.contains("version"))
                version = j["version"];
            if (j.contains("force"))
                force_upgrade = j["force"];
            if (j.contains("description"))
                description = j["description"];

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": ota_json %1%, version %2%, force %3%, description %4%")%ota_json %version %force_upgrade %description;
        }
        catch(nlohmann::detail::parse_error &err) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": parse "<<ota_json<<" got a nlohmann::detail::parse_error, reason = " << err.what();
        }
    }
    return;
}

void PresetUpdater::priv::remove_config_files(std::string vendor, std::string sub_path) const
{
    fs::path cache_folder = sub_path.empty()?cache_path : (cache_path / sub_path);
    fs::path vendor_folder = sub_path.empty()?(cache_path / vendor): (cache_path / sub_path / vendor);
    std::string vendor_ota_json = cache_folder.string() + "/"+vendor+".changelog";
    std::string vendor_json = cache_folder.string() + "/"+vendor+".json";
    if (fs::exists(vendor_ota_json)) {
        try {
            fs::remove(vendor_ota_json);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Failed  removing vendor ota json: " << vendor_ota_json;
        }
    }

    if (fs::exists(vendor_json)) {
        try {
            fs::remove(vendor_json);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Failed  removing vendor json: " << vendor_json;
        }
    }

    if (fs::exists(vendor_folder)) {
        try {
            fs::remove_all(vendor_folder);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Failed  removing the vendor directory: " << vendor_folder.string();
        }
    }
    return;
}

//BBS: refine the Preset Updater logic
// Download vendor indices. Also download new bundles if an index indicates there's a new one available.
// Both are saved in cache.
bool PresetUpdater::priv::sync_config(std::string http_url, const VendorMap vendors)
{
    bool has_new_config = false;
    //std::map<std::string, std::pair<Semver, std::string>> vendor_list;
    //std::map<std::string, std::string> vendor_descriptions;

    BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater]: sync_config Syncing configuration enter");

    if (!enabled_config_update) { return has_new_config; }

    BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater]: sync_config get preferred setting version for app version %1%, http_url: %2%")%SLIC3R_VERSION%http_url;

    auto cache_folder = cache_path / PRESET_SUBPATH;
    std::string curr_app_version = SLIC3R_VERSION;
    std::string using_app_version = curr_app_version.substr(0, 6) + "00.00";

    for (auto vendor_it :vendors) {
        const VendorProfile& vendor_profile = vendor_it.second;
        std::string vendor_name = vendor_profile.id, vendor = vendor_profile.id;
        boost::to_lower(vendor_name);
        if (vendor != PresetBundle::BBL_BUNDLE)
            continue;

        std::string vendor_ota_json = cache_folder.string() + "/"+vendor+".changelog";
        std::string vendor_resource_json = vendor_path.string() + "/"+vendor+".json";
        Semver current_preset_semver = get_version_from_json(vendor_resource_json);
        std::string cached_version, description;
        bool force = false, remove_previous = true, valid_version = false;

        //check previous cached config files, and delete unused
        parse_ota_files(vendor_ota_json, cached_version, force, description);
        if (!cached_version.empty()) {
            Semver app_semver = curr_app_version;
            Semver cached_semver = cached_version;

            valid_version = get_valid_ota_version(app_semver, current_preset_semver, cached_semver, 2);

            if (valid_version) {
                remove_previous = false;
                current_preset_semver = cached_semver;
                has_new_config = true;
            }
        }

        //remove previous config files
        if (remove_previous)
            remove_config_files(vendor, PRESET_SUBPATH);

        //check online versions
        try {
            std::map<std::string, Resource> resources
            {
                {"slicer/settings/"+vendor_name, { using_app_version, "", "", vendor, false, cache_folder.string()}}
            };
            has_new_config |= sync_resources(http_url, resources, false, current_preset_semver.to_string(), vendor+".changelog");
        }
        catch (std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater] sync_plugins: %1%", e.what());
        }
    }
#if 0
    std::string app_version = SLIC3R_VERSION;
    std::string query_version = app_version.substr(0, 6) + "00.00";
    std::string query_params = "?";
    std::string vendor_string = PresetBundle::BBL_BUNDLE;
    boost::to_lower(vendor_string);
    std::string query_vendor = (boost::format("slicer/settings/%1%=%2%")%vendor_string % query_version ).str();
    query_params += query_vendor;
    /*bool first = true;
    for (auto vendor_it :vendors) {
        if (cancel) { return; }

        const VendorProfile& vendor_profile = vendor_it.second;
        std::string vendor_name = vendor_profile.id;
        boost::to_lower(vendor_name);

        std::string query_vendor = (boost::format("slicer/settings/%1%=%2%")
            % vendor_name
            % GUI::VersionInfo::convert_full_version(SLIC3R_VERSION)
            ).str();
        if (!first)
            query_params += "&";
        query_params += query_vendor;
    }*/

    std::string url = http_url;
    url += query_params;
    Slic3r::Http http = Slic3r::Http::get(url);
    BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater]: sync_config request_url: %1%")%url;
    http.on_progress([this](Slic3r::Http::Progress, bool &cancel_http) {
            if (cancel) {
                cancel_http = true;
            }
        })
        .on_complete(
        [this, &vendor_list, &vendor_descriptions, vendors](std::string body, unsigned) {
            try {
                BOOST_LOG_TRIVIAL(info) << "[BBL Updater]::body=" << body;

                json j = json::parse(body);
                std::string message = j["message"].get<std::string>();

                if (message == "success") {
                    json resource =j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            Semver version;
                            std::string url;
                            std::string type;
                            std::string vendor;
                            std::string description;
                            bool force_upgrade = false;
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(),"type")) {
                                    type = sub_iter.value();
                                    BOOST_LOG_TRIVIAL(trace) << "[BBL Updater]: get version of settings's type, " << sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(),"version")) {
                                    version = *(Semver::parse(sub_iter.value()));
                                }
                                else if (boost::iequals(sub_iter.key(),"description")) {
                                    description = sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(),"url")) {
                                    url = sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(), "force_update")) {
                                    force_upgrade = sub_iter.value();
                                }

                            }
                            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]: get type "<< type <<", version "<<version.to_string()<<", url " << url;

                            for (auto vendor_it :vendors) {
                                const VendorProfile& vendor_profile = vendor_it.second;
                                std::string vendor_name = vendor_profile.id;
                                boost::to_lower(vendor_name);
                                if (type.find(vendor_name) != std::string::npos) {
                                    vendor = vendor_profile.id;
                                    break;
                                }
                            }
                            if (!vendor.empty()) {
                                vendor_list.emplace(vendor, std::pair<Semver, std::string>(version, url));
                                vendor_descriptions.emplace(vendor, description);
                            }
                        }
                    }
                    json software = j.at("software");
                    if (software.is_object() && !software.empty()) {
                        Semver cloud_version;
                        for (auto iter = software.begin(); iter != software.end(); iter++) {
                            if (boost::iequals(iter.key(), "version")) {
                                cloud_version = *(Semver::parse(iter.value()));
                                GUI::wxGetApp().app_config->set("app", "cloud_version",cloud_version.to_string());
                                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Bambu Studio has newer version and the version is: " << cloud_version.to_string();
                            } else if (boost::iequals(iter.key(), "url")) {
                                std::string url = iter.value();
                                GUI::wxGetApp().app_config->set("app", "cloud_software_url", url);
                                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Bambu Studio has newer version and download url is: " << url;
                            }
                        }
                    } else {
                        GUI::wxGetApp().app_config->erase("app", "cloud_version");
                        GUI::wxGetApp().app_config->erase("app", "cloud_software_url");
                        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Bambu Studio is newest version.";
                    }
                    GUI::wxGetApp().CallAfter([]() { GUI::wxGetApp().app_config->save(); });
                }
                else {
                    BOOST_LOG_TRIVIAL(error) << "[BBL Updater]: get version of settings failed, body=" << body;
                }
            }
            catch (std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << (boost::format("[BBL Updater]: get version of settings failed, exception=%1% body=%2%")
                    % e.what()
                    % body).str();
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[BBL Updater]: get version of settings failed,, body=" << body;
            }
        }
    )
    .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("[BBL Updater]: status=%1%, error=%2%, body=%3%")
                % status
                % error
                % body;
        }
    )
    .perform_sync();

    for (auto vendor_it :vendors) {
        if (cancel) { return; }

        const VendorProfile& vendor_profile = vendor_it.second;
        std::string vendor_name = vendor_profile.id;
        auto vendor_update = vendor_list.find(vendor_name);
        if (vendor_update == vendor_list.end()) {
			BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Vendor " << vendor_name << " can not get setting versions online";
			continue;
		}
        Semver online_version = vendor_update->second.first;
        //Semver current_version = get_version_from_json(vendor_root_config.string());
        Semver current_version = vendor_profile.config_version;
        bool version_match = ((online_version.maj() == current_version.maj()) && (online_version.min() == current_version.min()));
        if (version_match && (current_version < online_version)) {
            auto cache_file = cache_path / (vendor_name+".json");
            auto cache_print_dir = (cache_path / vendor_name / PRESET_PRINT_NAME);
            auto cache_filament_dir = (cache_path / vendor_name / PRESET_FILAMENT_NAME);
            auto cache_machine_dir = (cache_path / vendor_name / PRESET_PRINTER_NAME);

            if (( fs::exists(cache_file))
                &&( fs::exists(cache_print_dir))
                &&( fs::exists(cache_filament_dir))
                &&( fs::exists(cache_machine_dir))) {
                Semver version = get_version_from_json(cache_file.string());
                bool cached_version_match = ((online_version.maj() == version.maj()) && (online_version.min() == version.min()));
                if (cached_version_match && (version >= online_version)) {
                    //already downloaded before
                    BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Vendor " << vendor_name << ", already cached a version "<<version.to_string();
                    continue;
                }
            }
            if (cancel) { return; }

            //need to download the online files
            std::string online_url = vendor_update->second.second;
            std::string cache_file_path = (cache_path / (vendor_name + TMP_EXTENSION)).string();
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Downloading online settings for vendor: " << vendor_name<<", version "<<online_version.to_string();
            if (!get_file(online_url, cache_file_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]download settings for vendor "<<vendor_name<<" failed, url: " << online_url;
                continue;
            }
		    if (cancel) { return; }

            //remove previous files before
            if (fs::exists(cache_print_dir))
                fs::remove_all(cache_print_dir);
            if (fs::exists(cache_filament_dir))
                fs::remove_all(cache_filament_dir);
            if (fs::exists(cache_machine_dir))
                fs::remove_all(cache_machine_dir);
            //extract the file downloaded
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]start to unzip the downloaded file "<< cache_file_path;
            if (!extract_file(cache_file_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]extract settings for vendor "<<vendor_name<<" failed, path: " << cache_file_path;
                continue;
            }
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]finished unzip the downloaded file "<< cache_file_path;

            auto vendor_description = vendor_descriptions.find(vendor_name);
            if (vendor_description != vendor_descriptions.end()) {
                //save the description to disk
                std::string changelog_file = (cache_path / (vendor_name + ".changelog")).string();

                boost::nowide::ofstream c;
                c.open(changelog_file, std::ios::out | std::ios::trunc);
                c << vendor_description->second << std::endl;
                c.close();
            }
        }
    }
#endif
    return has_new_config;
}

void PresetUpdater::priv::sync_tooltip(std::string http_url, std::string language)
{
    try {
        std::string common_version = "00.00.00.00";
        std::string language_version = "00.00.00.00";
        fs::path cache_root = fs::path(data_dir()) / "resources/tooltip";
        try {
            auto vf = cache_root / "common" / "version";
            if (fs::exists(vf)) Slic3r::load_string_file(vf, common_version);
            vf = cache_root / language / "version";
            if (fs::exists(vf)) Slic3r::load_string_file(vf, language_version);
        } catch (...) {}
        std::map<std::string, Resource> resources
        {
            {"slicer/tooltip/common", { common_version, "", "", "", false, (cache_root / "common").string() }},
            {"slicer/tooltip/" + language, { language_version, "", "", "", false, (cache_root / language).string() }}
        };
        sync_resources(http_url, resources);
        for (auto &r : resources) {
            if (!r.second.url.empty()) {
                GUI::MarkdownTip::Reload();
                break;
            }
        }
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater] sync_tooltip: %1%", e.what());
    }
}

// return true means there are plugins files
bool PresetUpdater::priv::get_cached_plugins_version(std::string& cached_version, bool &force)
{
    auto cache_folder = cache_path / "plugins";
    std::string network_library, player_library, live555_library;
    bool has_plugins = false;

#if defined(_MSC_VER) || defined(_WIN32)
    network_library = cache_folder.string() + "/bambu_networking.dll";
    player_library  = cache_folder.string() + "/BambuSource.dll";
    live555_library = cache_folder.string() + "/live555.dll";
#elif defined(__WXMAC__)
    network_library = cache_folder.string() + "/libbambu_networking.dylib";
    player_library  = cache_folder.string() + "/libBambuSource.dylib";
    live555_library = cache_folder.string() + "/liblive555.dylib";
#else
    network_library = cache_folder.string() + "/libbambu_networking.so";
    player_library  = cache_folder.string() + "/libBambuSource.so";
    live555_library = cache_folder.string() + "/liblive555.so";
#endif

    std::string changelog_file = cache_folder.string() + "/network_plugins.json";
    if (fs::exists(network_library)
        && fs::exists(player_library)
        && fs::exists(live555_library)
        && fs::exists(changelog_file))
    {
        has_plugins = true;
        std::string description;
        parse_ota_files(changelog_file, cached_version, force, description);
    }

    return has_plugins;
}

void PresetUpdater::priv::sync_plugins(std::string http_url, std::string plugin_version)
{
    if (plugin_version == "00.00.00.00") {
        BOOST_LOG_TRIVIAL(info) << "non need to sync plugins for there is no plugins currently.";
        return;
    }
    std::string curr_version = SLIC3R_VERSION;
    std::string using_version = curr_version.substr(0, 9) + "00";
    auto cache_plugin_folder = cache_path / PLUGINS_SUBPATH;

    std::string cached_version;
    bool force_upgrade = false;
    get_cached_plugins_version(cached_version, force_upgrade);
    if (!cached_version.empty()) {
        bool need_delete_cache = true;
        Semver current_semver = curr_version;
        Semver cached_semver = cached_version;
        Semver current_plugin_semver = plugin_version;

        bool valid_version = get_valid_ota_version(current_semver, current_plugin_semver, cached_semver, 3);

        if (valid_version) {
            BOOST_LOG_TRIVIAL(info) << boost::format("cached plugins version %1% newer than current %2%")%cached_version%curr_version;
            plugin_version = cached_version;
            need_delete_cache = false;
        }

        if (need_delete_cache) {
            if (fs::exists(cache_plugin_folder))
            {
                BOOST_LOG_TRIVIAL(info) << "[remove_old_networking_plugins] remove the plugins directory " << PathSanitizer::sanitize(cache_plugin_folder);
                try {
                    fs::remove_all(cache_plugin_folder);
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "Failed  removing the plugins file " << PathSanitizer::sanitize(cache_plugin_folder);
                }
            }
            //create this directory again
            if (!fs::exists(cache_plugin_folder)) {
                fs::create_directory(cache_plugin_folder);
            }
        }
    }

#if defined(__WINDOWS__)
    if (GUI::wxGetApp().is_running_on_arm64()) {
        //set to arm64 for plugins
        std::map<std::string, std::string> current_headers = Slic3r::Http::get_extra_headers();
        current_headers["X-BBL-OS-Type"] = "windows_arm";

        Slic3r::Http::set_extra_headers(current_headers);
        BOOST_LOG_TRIVIAL(info) << boost::format("set X-BBL-OS-Type to windows_arm");
    }
#endif
    try {
        std::map<std::string, Resource> resources
        {
            {"slicer/plugins/cloud", { using_version, "", "", "", false, cache_plugin_folder.string()}}
        };
        sync_resources(http_url, resources, true, plugin_version, "network_plugins.json");
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater] sync_plugins: %1%", e.what());
    }
#if defined(__WINDOWS__)
    if (GUI::wxGetApp().is_running_on_arm64()) {
        //set back
        std::map<std::string, std::string> current_headers = Slic3r::Http::get_extra_headers();
        current_headers["X-BBL-OS-Type"] = "windows";

        Slic3r::Http::set_extra_headers(current_headers);
        BOOST_LOG_TRIVIAL(info) << boost::format("set X-BBL-OS-Type back to windows");
    }
#endif

    bool result = get_cached_plugins_version(cached_version, force_upgrade);
    if (result) {
        BOOST_LOG_TRIVIAL(info) << format("[BBL Updater] found new plugins: %1%, prompt to update, force_upgrade %2%", cached_version, force_upgrade);
        if (force_upgrade) {
            auto app_config = GUI::wxGetApp().app_config;
            if (!app_config)
                GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::BBLPluginUpdateAvailable);
            else
                app_config->set("update_network_plugin", "true");
        }
        else
            GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::BBLPluginUpdateAvailable);
    }
}

void PresetUpdater::priv::sync_printer_config(std::string http_url)
{
    std::string curr_version  = SLIC3R_VERSION;
    std::string using_version = curr_version.substr(0, 6) + "00.00";

    std::string cached_version;
    std::string data_dir_str = data_dir();
    fs::path data_dir_path(data_dir_str);
    auto                    config_folder = data_dir_path / "printers";
    auto                    cache_folder = data_dir_path / "ota" / "printers";

    try {
        if (fs::exists(config_folder / "version.txt")) {
            Slic3r::load_string_file(config_folder / "version.txt", curr_version);
            boost::algorithm::trim(curr_version);
        }
    } catch (...) {}
    try {
        if (fs::exists(cache_folder / "version.txt")) {
            Slic3r::load_string_file(cache_folder / "version.txt", cached_version);
            boost::algorithm::trim(cached_version);
        }
    } catch (...) {}
    if (!cached_version.empty()) {
        bool   need_delete_cache = false;
        Semver current_semver = curr_version;
        Semver cached_semver = cached_version;

        if ((cached_semver.maj() != current_semver.maj()) || (cached_semver.min() != current_semver.min())) {
            need_delete_cache = true;
            BOOST_LOG_TRIVIAL(info) << boost::format("cached printer config version %1% not match with current %2%") % cached_version % curr_version;
        }
        else if (cached_semver.patch() <= current_semver.patch()) {
            need_delete_cache = true;
            BOOST_LOG_TRIVIAL(info) << boost::format("cached printer config version %1% not newer than current %2%") % cached_version % curr_version;
        }
        else {
            using_version = cached_version;
        }

        if (need_delete_cache) {
            boost::system::error_code ec;
            fs::remove_all(cache_folder, ec);
            cached_version = curr_version;
        }
    }
    else
        cached_version = curr_version;

    try {
        std::map<std::string, Resource> resources{{"slicer/printer/bbl", {using_version, "", "", "", false, cache_folder.string()}}};
        sync_resources(http_url, resources, false, cached_version, "printer.json");
    } catch (std::exception &e) {
        BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater] sync_printer_config: %1%", e.what());
    }

    bool result = false;
    try {
        if (fs::exists(cache_folder / "version.txt")) {
            Slic3r::load_string_file(cache_folder / "version.txt", cached_version);
            boost::algorithm::trim(cached_version);
            result = true;
        }
    } catch (...) {}
    if (result) {
        BOOST_LOG_TRIVIAL(info) << format("[BBL Updater] found new printer config: %1%, prompt to update", cached_version);
        waiting_printer_updates = get_printer_config_updates(true);
        if (waiting_printer_updates.updates.size() > 0) {
            has_waiting_printer_updates = true;
            GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::BBLPrinterConfigUpdateAvailable);
        }
    }
}

bool PresetUpdater::priv::install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const
{
    Updates updates;

    BOOST_LOG_TRIVIAL(info) << format("Installing %1% bundles from resources ...", bundles.size());

    for (const auto &bundle : bundles) {
        //BBS: add directory support
        auto print_in_rsrc = this->rsrc_path / bundle;
        auto print_in_vendors = this->vendor_path / bundle;
        fs::path print_folder(print_in_vendors);

        auto path_in_rsrc = (this->rsrc_path / bundle).replace_extension(".json");
        auto path_in_vendors = (this->vendor_path / bundle).replace_extension(".json");

        //delete the json at first
        boost::system::error_code ec;
        if (fs::exists(path_in_vendors)) {
            fs::remove(path_in_vendors, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(error) << boost::format("install_bundles_rsrc: Failed to remove file %1%, error %2% ") % path_in_vendors.string() % ec.message();
                return false;
            }
        }

        if (fs::exists(print_folder)) {
            fs::remove_all(print_folder, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(error) << boost::format("install_bundles_rsrc: Failed to remove directory %1%, error %2% ") % print_folder.string() % ec.message();
                return false;
            }
        }
        fs::create_directories(print_folder, ec);
        if (ec) {
            BOOST_LOG_TRIVIAL(error) << boost::format("install_bundles_rsrc: Failed to create directory %1%, error %2% ")% print_folder.string() %ec.message();
            return false;
        }
        updates.updates.emplace_back(std::move(print_in_rsrc), std::move(print_in_vendors), Version(), bundle, "", "", false, true);

        //copy json at the last
        updates.updates.emplace_back(std::move(path_in_rsrc), std::move(path_in_vendors), Version(), bundle, "", "");
    }

    return perform_updates(std::move(updates), snapshot);
}


//BBS: refine preset update logic
// Install indicies from resources. Only installs those that are either missing or older than in resources.
void PresetUpdater::priv::check_installed_vendor_profiles() const
{
    BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:Checking whether the profile from resource is newer";

    AppConfig *app_config = GUI::wxGetApp().app_config;
    const auto enabled_vendors = app_config->vendors();

    //BBS: refine the init check logic
    std::vector<std::string> bundles;
    for (auto &dir_entry : fs::directory_iterator(rsrc_path)) {
        const auto &path = dir_entry.path();
        std::string file_path = path.string();
        if (is_json_file(file_path)) {
            const auto path_in_vendor = vendor_path / path.filename();
            std::string vendor_name = path.filename().string();
            // Remove the .json suffix.
            vendor_name.erase(vendor_name.size() - 5);
            if (enabled_config_update) {
                if ( fs::exists(path_in_vendor)) {
                    if ((vendor_name == PresetBundle::BBL_BUNDLE) || (enabled_vendors.find(vendor_name) != enabled_vendors.end())) {
                        Semver resource_ver = get_version_from_json(file_path);
                        Semver vendor_ver = get_version_from_json(path_in_vendor.string());

                        bool version_match = ((resource_ver.maj() == vendor_ver.maj()) && (resource_ver.min() == vendor_ver.min()));

                        if (!version_match || (vendor_ver < resource_ver)) {
                            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:found vendor "<<vendor_name<<" different version "<<resource_ver.to_string() <<" from resource, old version "<<vendor_ver.to_string()<<", will copy from resource";
                            bundles.push_back(vendor_name);
                        }
                    }
                    else {
                        //need to be removed because not installed
                        fs::remove(path_in_vendor);
                        const auto path_of_vendor = vendor_path / vendor_name;
                        if (fs::exists(path_of_vendor))
                            fs::remove_all(path_of_vendor);
                    }
                }
                else if ((vendor_name == PresetBundle::BBL_BUNDLE) || (enabled_vendors.find(vendor_name) != enabled_vendors.end())) {//if vendor has no file, copy it from resource for BBL
                    bundles.push_back(vendor_name);
                }
            }
            else if ((vendor_name == PresetBundle::BBL_BUNDLE) || (enabled_vendors.find(vendor_name) != enabled_vendors.end())) { //always update configs from resource to vendor for BBL
                bundles.push_back(vendor_name);
            }
        }
    }

    if (bundles.size() > 0)
        install_bundles_rsrc(bundles, false);
}

Updates PresetUpdater::priv::get_printer_config_updates(bool update) const
{
    std::string             data_dir_str = data_dir();
    fs::path data_dir_path(data_dir_str);
    fs::path resc_dir_path(resources_dir());
    auto                    config_folder = data_dir_path / "printers";
    auto                    resc_folder   = (update ? cache_path : resc_dir_path) / "printers";
    std::string             curr_version;
    std::string             resc_version;
    try {
        if (fs::exists(resc_folder / "version.txt")) {
            Slic3r::load_string_file(resc_folder / "version.txt", resc_version);
            boost::algorithm::trim(resc_version);
        }
    } catch (...) {}
    try {
        if (fs::exists(config_folder / "version.txt")) {
            Slic3r::load_string_file(config_folder / "version.txt", curr_version);
            boost::algorithm::trim(curr_version);
        }
    } catch (...) {}

    if (!curr_version.empty()) {
        Semver curr_ver = curr_version;
        Semver resc_ver   = resc_version;

        bool version_match = ((resc_ver.maj() == curr_ver.maj()) && (resc_ver.min() == curr_ver.min()));

        if (!version_match || (curr_ver < resc_ver)) {
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:found different version " << resc_version << " from resource, old version " << curr_version<<", will copy from resource";
        } else {
            return {};
        }
    }
    Updates updates;
    Version version;
    version.config_version = resc_version;
    std::string change_log;
    if (update) {
        std::string changelog_file = (resc_folder / "printer.json").string();
        try {
            boost::nowide::ifstream ifs(changelog_file);
            json                    j;
            ifs >> j;
            version.comment = j["description"];
        } catch (...) {}
    }
    updates.updates.emplace_back(std::move(resc_folder), std::move(config_folder), version, "bbl", change_log, version.comment, false, true);
    return updates;
}

// Generates a list of bundle updates that are to be performed.
// Version of slic3r that was running the last time and which was read out from PrusaSlicer.ini is provided
// as a parameter.
//BBS: refine the Preset Updater logic
Updates PresetUpdater::priv::get_config_updates(const Semver &old_slic3r_version) const
{
    Updates updates;
    auto cache_folder = cache_path / PRESET_SUBPATH;

    BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:Checking for cached configuration updates...";

    //remove previous old datas
    remove_config_files("BBL", std::string());

    if (fs::exists(cache_folder)) {
        for (auto& dir_entry : fs::directory_iterator(cache_folder)) {
            const auto& path = dir_entry.path();
            std::string file_path = path.string();
            if (is_json_file(file_path)) {
                const auto path_in_vendor = vendor_path / path.filename();
                std::string vendor_name = path.filename().string(), cached_version, description;
                // Remove the .json suffix.
                vendor_name.erase(vendor_name.size() - 5);
                auto print_in_cache = (cache_folder / vendor_name / PRESET_PRINT_NAME);
                auto filament_in_cache = (cache_folder / vendor_name / PRESET_FILAMENT_NAME);
                auto machine_in_cache = (cache_folder / vendor_name / PRESET_PRINTER_NAME);

                std::string changelog_file = (cache_folder / (vendor_name + ".changelog")).string();
                bool force_update;

                if ((fs::exists(path_in_vendor))
                    && (fs::exists(print_in_cache))
                    && (fs::exists(filament_in_cache))
                    && (fs::exists(machine_in_cache))) {

                    parse_ota_files(changelog_file, cached_version, force_update, description);
                    if (!cached_version.empty()) {
                        BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater] found new presets of vendor: %1%, version %2%, force_upgrade %3%") % vendor_name % cached_version % force_update;
                        std::string app_version = SLIC3R_VERSION;
                        Semver app_semver = app_version;
                        Semver cached_semver = cached_version;
                        Semver current_preset_semver = get_version_from_json(path_in_vendor.string());

                        bool valid_version = get_valid_ota_version(app_semver, current_preset_semver, cached_semver, 2);

                        if (valid_version) {
                            BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater] need to update vendor: %1%'s settings from version %2%  to newer version %3%, force_upgrade %4%")
                                % vendor_name % current_preset_semver.to_string() % cached_semver.to_string() % force_update;
                            Version version;
                            version.config_version = cached_semver;
                            //version.comment = description;

                            //BBS: add directory support
                            updates.updates.emplace_back(cache_folder / vendor_name, vendor_path / vendor_name, Version(), vendor_name, "", "", force_update, true);

                            updates.updates.emplace_back(std::move(file_path), std::move(path_in_vendor.string()), std::move(version), vendor_name, description, "", force_update, false);
                        }
                    }
                }
            }
        }
    }

	return updates;
}

//BBS: switch to new BBL.json configs
bool PresetUpdater::priv::perform_updates(Updates &&updates, bool snapshot) const
{
    int ret = 0;
    //std::string vendor_path;
    //std::string vendor_name;
    if (updates.incompats.size() > 0) {
        //if (snapshot) {
        //	BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
        //	if (! GUI::Config::take_config_snapshot_cancel_on_error(*GUI::wxGetApp().app_config, Snapshot::SNAPSHOT_DOWNGRADE, "",
        //		_u8L("Continue and install configuration updates?")))
        //		return false;
        //}
        BOOST_LOG_TRIVIAL(info) << format("[BBL Updater]:Deleting %1% incompatible bundles", updates.incompats.size());

        for (auto &incompat : updates.incompats) {
            BOOST_LOG_TRIVIAL(info) << '\t' << incompat;
            incompat.remove();
        }
    } else if (updates.updates.size() > 0) {
        //if (snapshot) {
        //	BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
        //	if (! GUI::Config::take_config_snapshot_cancel_on_error(*GUI::wxGetApp().app_config, Snapshot::SNAPSHOT_UPGRADE, "",
        //		_u8L("Continue and install configuration updates?")))
        //		return false;
        //}

        BOOST_LOG_TRIVIAL(info) << format("[BBL Updater]:Performing %1% updates", updates.updates.size());

        for (const auto &update : updates.updates) {
            BOOST_LOG_TRIVIAL(info) << '\t' << update;

            ret = update.install();
            if (ret) {
                BOOST_LOG_TRIVIAL(error) << boost::format("[BBL Updater]:perform_updates to %1% failed, ret=%2%")% update.target.string() % ret;
                break;
            }
            //if (!update.is_directory) {
            //    vendor_path = update.source.parent_path().string();
            //    vendor_name = update.vendor;
            //}
        }

        //if (!vendor_path.empty()) {
        //    PresetBundle bundle;
        //    // Throw when parsing invalid configuration. Only valid configuration is supposed to be provided over the air.
        //    bundle.load_vendor_configs_from_json(vendor_path, vendor_name, PresetBundle::LoadConfigBundleAttribute::LoadSystem, ForwardCompatibilitySubstitutionRule::Disable);

        //    BOOST_LOG_TRIVIAL(info) << format("Deleting %1% conflicting presets", bundle.prints.size() + bundle.filaments.size() + bundle.printers.size());

        //    auto preset_remover = [](const Preset& preset) {
        //        BOOST_LOG_TRIVIAL(info) << '\t' << preset.file;
        //        fs::remove(preset.file);
        //    };

        //    for (const auto &preset : bundle.prints)    { preset_remover(preset); }
        //    for (const auto &preset : bundle.filaments) { preset_remover(preset); }
        //    for (const auto &preset : bundle.printers)  { preset_remover(preset); }
        //}
    }

    return (ret == 0);
}

void PresetUpdater::priv::set_waiting_updates(Updates u)
{
	waiting_updates = u;
	has_waiting_updates = true;
}

PresetUpdater::PresetUpdater() :
	p(new priv())
{}


// Public

PresetUpdater::~PresetUpdater()
{
	if (p && p->thread.joinable()) {
		// This will stop transfers being done by the thread, if any.
		// Cancelling takes some time, but should complete soon enough.
		p->cancel = true;
		p->thread.join();
	}
}

//BBS: change directories by design
//BBS: refine the preset updater logic
void PresetUpdater::sync(std::string http_url, std::string language, std::string plugin_version, PresetBundle *preset_bundle)
{
    //p->set_download_prefs(GUI::wxGetApp().app_config);
    if (!p->enabled_version_check && !p->enabled_config_update) { return; }

    // Copy the whole vendors data for use in the background thread
    // Unfortunatelly as of C++11, it needs to be copied again
    // into the closure (but perhaps the compiler can elide this).
    VendorMap vendors = preset_bundle ? preset_bundle->vendors : VendorMap{};

    p->thread = std::thread([this, vendors, http_url, language, plugin_version]() {
        this->p->prune_tmps();
        if (p->cancel)
            return;
        //not used
        //this->p->sync_version();
        //if (p->cancel)
        //    return;
        if (!vendors.empty()) {
            bool has_new_config = this->p->sync_config(http_url, std::move(vendors));
            if (p->cancel)
                return;
            if (has_new_config) {
                GUI::wxGetApp().CallAfter([] {
                    GUI::wxGetApp().check_config_updates_from_updater();
                    });
            }
        }
        if (p->cancel)
            return;
        this->p->sync_plugins(http_url, plugin_version);
        this->p->sync_printer_config(http_url);
        //if (p->cancel)
        //  return;
        //remove the tooltip currently
        //this->p->sync_tooltip(http_url, language);
    });
}

void PresetUpdater::slic3r_update_notify()
{
	if (! p->enabled_version_check)
		return;
}

static bool reload_configs_update_gui()
{
	wxString header = _L("Need to check the unsaved changes before configuration updates.");
	if (!GUI::wxGetApp().check_and_save_current_preset_changes(_L("Configuration updates"), header, false ))
		return false;

	// Reload global configuration
	auto* app_config = GUI::wxGetApp().app_config;
	// System profiles should not trigger any substitutions, user profiles may trigger substitutions, but these substitutions
	// were already presented to the user on application start up. Just do substitutions now and keep quiet about it.
	// However throw on substitutions in system profiles, those shall never happen with system profiles installed over the air.
	GUI::wxGetApp().preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem);
	GUI::wxGetApp().load_current_presets();
	GUI::wxGetApp().plater()->set_bed_shape();

	return true;
}

//BBS: refine the preset updater logic
PresetUpdater::UpdateResult PresetUpdater::config_update(const Semver& old_slic3r_version, UpdateParams params) const
{
    if (! p->enabled_config_update) { return R_NOOP; }

    auto updates = p->get_config_updates(old_slic3r_version);
    //if (updates.incompats.size() > 0) {
    //	BOOST_LOG_TRIVIAL(info) << format("%1% bundles incompatible. Asking for action...", updates.incompats.size());

    //	std::unordered_map<std::string, wxString> incompats_map;
    //	for (const auto &incompat : updates.incompats) {
    //		const auto min_slic3r = incompat.version.min_slic3r_version;
    //		const auto max_slic3r = incompat.version.max_slic3r_version;
    //		wxString restrictions;
    //		if (min_slic3r != Semver::zero() && max_slic3r != Semver::inf()) {
    //               restrictions = GUI::format_wxstr(_L("requires min. %s and max. %s"),
    //                   min_slic3r.to_string(),
    //                   max_slic3r.to_string());
    //		} else if (min_slic3r != Semver::zero()) {
    //			restrictions = GUI::format_wxstr(_L("requires min. %s"), min_slic3r.to_string());
    //			BOOST_LOG_TRIVIAL(debug) << "Bundle is not downgrade, user will now have to do whole wizard. This should not happen.";
    //		} else {
    //               restrictions = GUI::format_wxstr(_L("requires max. %s"), max_slic3r.to_string());
    //		}

    //		incompats_map.emplace(std::make_pair(incompat.vendor, std::move(restrictions)));
    //	}

    //	GUI::MsgDataIncompatible dlg(std::move(incompats_map));
    //	const auto res = dlg.ShowModal();
    //	if (res == wxID_REPLACE) {
    //		BOOST_LOG_TRIVIAL(info) << "User wants to re-configure...";

    //		// This effectively removes the incompatible bundles:
    //		// (snapshot is taken beforehand)
    //		if (! p->perform_updates(std::move(updates)) ||
    //			! GUI::wxGetApp().run_wizard(GUI::ConfigWizard::RR_DATA_INCOMPAT))
    //			return R_INCOMPAT_EXIT;

    //		return R_INCOMPAT_CONFIGURED;
    //	}
    //	else {
    //		BOOST_LOG_TRIVIAL(info) << "User wants to exit Slic3r, bye...";
    //		return R_INCOMPAT_EXIT;
    //	}

    //} else
    if (updates.updates.size() > 0) {

        bool force_update = false;
        for (const auto& update : updates.updates) {
            force_update = (update.forced_update ? true : force_update);
            //td::cout << update.forced_update << std::endl;
            //BOOST_LOG_TRIVIAL(info) << format("Update requires higher version.");
        }

        //forced update
        if (force_update)
        {
            BOOST_LOG_TRIVIAL(info) << format("[BBL Updater]:Force updating will start, size %1% ", updates.updates.size());
            bool ret = p->perform_updates(std::move(updates));
            if (!ret) {
                BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater]:perform_updates failed");
                return R_INCOMPAT_EXIT;
            }

            ret = reload_configs_update_gui();
            if (!ret) {
                BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater]:reload_configs_update_gui failed");
                return R_ALL_CANCELED;
            }
            Semver cur_ver = GUI::wxGetApp().preset_bundle->get_vendor_profile_version(PresetBundle::BBL_BUNDLE);

            GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::PresetUpdateFinished, GUI::NotificationManager::NotificationLevel::ImportantNotificationLevel,  _u8L("Configuration package updated to ")+cur_ver.to_string());

            return R_UPDATE_INSTALLED;
        }

        // regular update
        if (params == UpdateParams::SHOW_NOTIFICATION) {
            p->set_waiting_updates(updates);
            GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::PresetUpdateAvailable);
        }
        else {
            BOOST_LOG_TRIVIAL(info) << format("[BBL Updater]:Configuration package available. size %1%, need to confirm...", p->waiting_updates.updates.size());

            std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
            for (const auto& update : updates.updates) {
                //BBS: skip directory
                if (update.is_directory)
                    continue;
                std::string changelog = update.change_log;
                updates_msg.emplace_back(update.vendor, update.version.config_version, update.descriptions, std::move(changelog));
            }

            GUI::MsgUpdateConfig dlg(updates_msg, params == UpdateParams::FORCED_BEFORE_WIZARD);

            const auto res = dlg.ShowModal();
            if (res == wxID_OK) {
                BOOST_LOG_TRIVIAL(debug) << "[BBL Updater]:selected yes to update";
                if (!p->perform_updates(std::move(updates)))
                    return R_INCOMPAT_EXIT;

                if (!reload_configs_update_gui())
                    return R_ALL_CANCELED;
                return R_UPDATE_INSTALLED;
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:selected no for updating";
                if (params == UpdateParams::FORCED_BEFORE_WIZARD && res == wxID_CANCEL)
                    return R_ALL_CANCELED;
                return R_UPDATE_REJECT;
            }
        }

        // MsgUpdateConfig will show after the notificaation is clicked
    } else {
        BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:No configuration updates available.";
    }

	return R_NOOP;
}

//BBS: add json related logic
bool PresetUpdater::install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const
{
	return p->install_bundles_rsrc(bundles, snapshot);
}

void PresetUpdater::on_update_notification_confirm()
{
	if (!p->has_waiting_updates)
		return;
	BOOST_LOG_TRIVIAL(info) << format("Update of %1% bundles available. Asking for confirmation ...", p->waiting_updates.updates.size());

	std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
	for (const auto& update : p->waiting_updates.updates) {
		//BBS: skip directory
		if (update.is_directory)
			continue;
		std::string changelog = update.change_log;
		updates_msg.emplace_back(update.vendor, update.version.config_version, update.descriptions, std::move(changelog));
	}

	GUI::MsgUpdateConfig dlg(updates_msg);

	const auto res = dlg.ShowModal();
	if (res == wxID_OK) {
		BOOST_LOG_TRIVIAL(debug) << "User agreed to perform the update";
		if (p->perform_updates(std::move(p->waiting_updates)) &&
			reload_configs_update_gui()) {
			p->has_waiting_updates = false;
		}
	}
	else {
		BOOST_LOG_TRIVIAL(info) << "User refused the update";
	}
}

void PresetUpdater::do_printer_config_update()
{
    if (!p->has_waiting_printer_updates)
        return;
    BOOST_LOG_TRIVIAL(info) << "Update of printer configs available. Asking for confirmation ...";

    std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
    for (const auto &update : p->waiting_printer_updates.updates) {
        std::string changelog = update.change_log;
        updates_msg.emplace_back(update.vendor, update.version.config_version, update.descriptions, std::move(changelog));
    }

    GUI::MsgUpdateConfig dlg(updates_msg);

    const auto res = dlg.ShowModal();
    if (res == wxID_OK) {
        BOOST_LOG_TRIVIAL(debug) << "User agreed to perform the update";
        if (p->perform_updates(std::move(p->waiting_printer_updates)))
            p->has_waiting_printer_updates = false;
    } else {
        BOOST_LOG_TRIVIAL(info) << "User refused the update";
    }
}

bool PresetUpdater::version_check_enabled() const
{
	return p->enabled_version_check;
}

}
