#include "PrintJob.hpp"
#include <regex>
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "bambu_networking.hpp"

namespace Slic3r {
namespace GUI {

#define CHECK_GCODE_FAILED_STR      _L("Abnormal print file data. Please slice again.")
#define PRINTJOB_CANCEL_STR         _L("Task canceled.")
#define TIMEOUT_TO_UPLOAD_STR       _L("Upload task timed out. Please check the network status and try again.")
#define FAILED_IN_CLOUD_SERVICE_STR _L("Cloud service connection failed. Please try again.")
#define FILE_IS_NOT_EXISTS_STR      _L("Print file not found. please slice again.")
#define FILE_OVER_SIZE_STR          _L("The print file exceeds the maximum allowable size (1GB). Please simplify the model and slice again.")
#define PRINT_CANCELED_STR          _L("Task canceled.")
#define SEND_PRINT_FAILED_STR       _L("Failed to send the print job. Please try again.")
#define UPLOAD_FTP_FAILED_STR       _L("Failed to upload file to ftp. Please try again.")
#define PRINT_SIGNED_STR            _L("Your software is not signed, and some printing functions have been restricted. Please use the officially signed software version.")

#define DESC_NETWORK_ERROR      _L("Check the current status of the bambu server by clicking on the link above.")
#define DESC_FILE_TOO_LARGE     _L("The size of the print file is too large. Please adjust the file size and try again.")
#define DESC_FAIL_NOT_EXIST     _L("Print file not found, Please slice it again and send it for printing.")
#define DESC_UPLOAD_FTP_FAILED  _L("Failed to upload print file to FTP. Please check the network status and try again.")

#define SENDING_OVER_LAN_STR    _L("Sending print job over LAN")
#define SENDING_OVER_CLOUD_STR  _L("Sending print job through cloud service")

#define wait_sending_finish     _L("Print task sending times out.")
//static wxString desc_wait_sending_finish    = _L("The printer timed out while receiving a print job. Please check if the network is functioning properly and send the print again.");
//static wxString desc_wait_sending_finish    = _L("The printer timed out while receiving a print job. Please check if the network is functioning properly.");

PrintJob::PrintJob(std::shared_ptr<ProgressIndicator> pri, Plater* plater, std::string dev_id)
: PlaterJob{ std::move(pri), plater },
    m_dev_id(dev_id),
    m_is_calibration_task(false)
{
    m_print_job_completed_id = plater->get_print_finished_event();
}

void PrintJob::prepare()
{
    if (job_data.is_from_plater)
        m_plater->get_print_job_data(&job_data);
    if (&job_data) {
        std::string temp_file = Slic3r::resources_dir() + "/check_access_code.txt";
        auto check_access_code_path = temp_file.c_str();
        job_data._temp_path = fs::path(check_access_code_path);
    }

    m_print_stage = BBL::SendingPrintJobStage::PrintingStageLimit;
}

void PrintJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &/*e*/) {
        PlaterJob::on_exception(eptr);
    }
}

void PrintJob::on_success(std::function<void()> success)
{
    m_success_fun = success;
}

std::string PrintJob::truncate_string(const std::string& str, size_t maxLength)
{
    if (str.length() <= maxLength)
    {
        return str;
    }

    wxString local_str = wxString::FromUTF8(str);
    wxString truncatedStr;

    for (auto i = 1; i < local_str.Length(); i++) {
        wxString tagStr = local_str.Mid(0, i);
        if (tagStr.ToUTF8().length() >= maxLength) {
            truncatedStr = local_str.Mid(0, i - 1);
            break;
        }
    }
    return truncatedStr.utf8_string();
}


wxString PrintJob::get_http_error_msg(unsigned int status, std::string body)
{
    try {
        int code = 0;
        std::string error;
        std::string message;
        wxString result;
        if (status >= 400 && status < 500)
            try {
            json j = json::parse(body);
            if (j.contains("code")) {
                if (!j["code"].is_null())
                    code = j["code"].get<int>();
            }
            if (j.contains("error")) {
                if (!j["error"].is_null())
                    error = j["error"].get<std::string>();
            }
            if (j.contains("message")) {
                if (!j["message"].is_null())
                    message = j["message"].get<std::string>();
            }
            //switch (status) {
            //    ;
            //}
        }
        catch (...) {
            ;
        }
        else if (status == 503) {
            return _L("Service Unavailable");
        }
        else {
            wxString unkown_text = _L("Unknown Error.");
            unkown_text += wxString::Format("status=%u, body=%s", status, body);
            BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;
            return unkown_text;
        }

        BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;

        result = wxString::Format("code=%u, error=%s", code, from_u8(error));
        return result;
    } catch(...) {
        ;
    }
    return wxEmptyString;
}

void PrintJob::process()
{
    /* display info */
    wxString msg;
    wxString error_str;
    int curr_percent = 10;
    NetworkAgent* m_agent = wxGetApp().getAgent();
    AppConfig* config = wxGetApp().app_config;

    if (this->connection_type == "lan") {
        msg = _L("Sending print job over LAN");
    }
    else {
        msg = _L("Sending print job through cloud service");
    }

    int result = -1;
    //unsigned int http_code;
    std::string http_body;

    int total_plate_num = plate_data.plate_count;
    if (!plate_data.is_valid) {
        total_plate_num =  m_plater->get_partplate_list().get_plate_count();
        PartPlate *plate = m_plater->get_partplate_list().get_plate(job_data.plate_idx);
        if (plate == nullptr) {
            plate = m_plater->get_partplate_list().get_curr_plate();
            if (plate == nullptr) return;
        }

        /* check gcode is valid */
        if (!plate->is_valid_gcode_file() && m_print_type == "from_normal") {
            update_status(curr_percent, CHECK_GCODE_FAILED_STR);
            return;
        }

        if (was_canceled()) {
            update_status(curr_percent, PRINTJOB_CANCEL_STR);
            return;
        }
    }

    m_project_name = truncate_string(m_project_name, 100);
    int curr_plate_idx = 0;

    if (m_print_type == "from_normal") {
        if (plate_data.is_valid)
            curr_plate_idx = plate_data.cur_plate_index;
        if (job_data.plate_idx >= 0)
            curr_plate_idx = job_data.plate_idx + 1;
        else if (job_data.plate_idx == PLATE_CURRENT_IDX)
            curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
        else if (job_data.plate_idx == PLATE_ALL_IDX)
            curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
        else
            curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
    }
    else if(m_print_type == "from_sdcard_view") {
        curr_plate_idx = m_print_from_sdc_plate_idx;
    }

    PartPlate* curr_plate = m_plater->get_partplate_list().get_curr_plate();
    if (curr_plate) {
        this->task_bed_type = bed_type_to_gcode_string(plate_data.is_valid ? plate_data.bed_type : curr_plate->get_bed_type(true));
    }

    BBL::PrintParams params;

    // local print access
    params.dev_ip = m_dev_ip;
    params.use_ssl_for_ftp  = m_local_use_ssl_for_ftp;
    params.use_ssl_for_mqtt  = m_local_use_ssl_for_mqtt;
    params.username = "bblp";
    params.password = m_access_code;

    // check access code and ip address
    if (this->connection_type == "lan" && m_print_type == "from_normal") {
        params.dev_id = m_dev_id;
        params.project_name = "verify_job";
        params.filename = job_data._temp_path.string();
        params.connection_type = this->connection_type;

        result = m_agent->start_send_gcode_to_sdcard(params, nullptr, nullptr, nullptr);
        if (result != 0) {
            BOOST_LOG_TRIVIAL(error) << "access code is invalid";
            m_enter_ip_address_fun_fail();
            m_job_finished = true;
            return;
        }

        params.project_name = "";
        params.filename = "";
    }

    params.dev_id               = m_dev_id;
    params.ftp_folder           = m_ftp_folder;
    params.filename             = job_data._3mf_path.string();
    params.config_filename      = job_data._3mf_config_path.string();
    params.plate_index          = curr_plate_idx;
    params.task_bed_leveling    = this->task_bed_leveling;
    params.task_flow_cali       = this->task_flow_cali;
    params.task_vibration_cali  = this->task_vibration_cali;
    params.task_layer_inspect   = this->task_layer_inspect;
    params.task_record_timelapse= this->task_record_timelapse;
    params.ams_mapping          = this->task_ams_mapping;
    params.ams_mapping2         = this->task_ams_mapping2;
    params.ams_mapping_info     = this->task_ams_mapping_info;
    params.nozzles_info         = this->task_nozzles_info;
    params.connection_type      = this->connection_type;
    params.task_use_ams         = this->task_use_ams;
    params.task_bed_type        = this->task_bed_type;
    params.print_type           = this->m_print_type;
    params.auto_bed_leveling    = this->auto_bed_leveling;
    params.auto_flow_cali       = this->auto_flow_cali;
    params.auto_offset_cali     = this->auto_offset_cali;
    params.task_ext_change_assist = this->task_ext_change_assist;

    if (m_print_type == "from_sdcard_view") {
        params.dst_file = m_dst_path;
    }

    if (wxGetApp().model().model_info && wxGetApp().model().model_info.get()) {
        ModelInfo* model_info = wxGetApp().model().model_info.get();
        auto origin_profile_id = model_info->metadata_items.find(BBL_DESIGNER_PROFILE_ID_TAG);
        if (origin_profile_id != model_info->metadata_items.end()) {
            try {
                params.origin_profile_id    = stoi(origin_profile_id->second.c_str());
            }
            catch(...) {}
        }
        auto origin_model_id = model_info->metadata_items.find(BBL_DESIGNER_MODEL_ID_TAG);
        if (origin_model_id != model_info->metadata_items.end()) {
            try {
                params.origin_model_id = origin_model_id->second;
            }
            catch(...) {}
        }

        auto profile_name = model_info->metadata_items.find(BBL_DESIGNER_PROFILE_TITLE_TAG);
        if (profile_name != model_info->metadata_items.end()) {
            try {
                params.preset_name = profile_name->second;
            }
            catch (...) {}
        }

        auto model_name = model_info->metadata_items.find(BBL_DESIGNER_MODEL_TITLE_TAG);
        if (model_name != model_info->metadata_items.end()) {
            try {

                std::string mall_model_name = model_name->second;
                std::replace(mall_model_name.begin(), mall_model_name.end(), ' ', '_');
                const char* unusable_symbols = "<>[]:/\\|?*\" ";
                for (const char* symbol = unusable_symbols; *symbol != '\0'; ++symbol) {
                    std::replace(mall_model_name.begin(), mall_model_name.end(), *symbol, '_');
                }

                std::regex pattern("_+");
                params.project_name = std::regex_replace(mall_model_name, pattern, "_");
                params.project_name = truncate_string(params.project_name, 100);
            }
            catch (...) {}
        }
    }

    params.stl_design_id = 0;

    if (!wxGetApp().model().stl_design_id.empty()) {

        auto country_code = wxGetApp().app_config->get_country_code();
        bool match_code = false;

        if (wxGetApp().model().stl_design_country == "DEV" && (country_code == "ENV_CN_DEV" || country_code == "NEW_ENV_DEV_HOST")) {
            match_code = true;
        }

        if (wxGetApp().model().stl_design_country == "QA" && (country_code == "ENV_CN_QA" || country_code == "NEW_ENV_QAT_HOST")) {
            match_code = true;
        }

        if (wxGetApp().model().stl_design_country == "CN_PRE" && (country_code == "ENV_CN_PRE" || country_code == "NEW_ENV_PRE_HOST")) {
            match_code = true;
        }

        if (wxGetApp().model().stl_design_country == "US_PRE" && country_code == "ENV_US_PRE") {
            match_code = true;
        }

        if (country_code == wxGetApp().model().stl_design_country) {
            match_code = true;
        }

        if (match_code) {
            int stl_design_id = 0;
            try {
                stl_design_id = std::stoi(wxGetApp().model().stl_design_id);
            }
            catch (...) {
                stl_design_id = 0;
            }
            params.stl_design_id = stl_design_id;
        }
    }

    const auto& model_design_id = wxGetApp().model().design_id;
    if (params.stl_design_id == 0 || !model_design_id.empty()) {
        if (model_design_id.empty()) {
            params.stl_design_id = 0;
        } else {
            try {
                params.stl_design_id = std::stoi(model_design_id);
            } catch (...) {
                params.stl_design_id = 0;
            }
        }
    }

    if (params.preset_name.empty() && m_print_type == "from_normal") { params.preset_name = wxString::Format("%s_plate_%d", m_project_name, curr_plate_idx).ToStdString(); }
    if (params.project_name.empty()) {params.project_name = m_project_name;}

    if (m_is_calibration_task) {
        params.project_name = m_project_name;
        params.origin_model_id = "";
    }

    wxString error_text;
    wxString msg_text;


    const int StagePercentPoint[(int)PrintingStageFinished + 1] = {
        20,     // PrintingStageCreate
        30,     // PrintingStageUpload
        70,     // PrintingStageWaiting
        75,     // PrintingStageRecord
        97,     // PrintingStageSending
        100,    // PrintingStageFinished
        100     // PrintingStageFinished
    };

    bool is_try_lan_mode = false;
    bool is_try_lan_mode_failed = false;

    auto update_fn = [this,
        &is_try_lan_mode,
        &is_try_lan_mode_failed,
        &msg,
        &error_str,
        &curr_percent,
        &error_text,
        StagePercentPoint
    ](int stage, int code, std::string info) {
                        m_print_stage = stage;
                        if (stage == BBL::SendingPrintJobStage::PrintingStageCreate && !is_try_lan_mode_failed) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageUpload && !is_try_lan_mode_failed) {
                            if (code >= 0 && code <= 100 && !info.empty()) {
                                if (this->connection_type == "lan") {
                                    msg = _L("Sending print job over LAN");
                                } else {
                                    msg = _L("Sending print job through cloud service");
                                }
                                msg += wxString::Format("(%s)", info);
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageWaiting) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        else  if (stage == BBL::SendingPrintJobStage::PrintingStageRecord && !is_try_lan_mode) {
                            msg = _L("Sending print configuration");
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageSending && !is_try_lan_mode) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageFinished) {
                            msg = wxString::Format(_L("Successfully sent. Will automatically jump to the device page in %ss"), info);
                            if (m_print_job_completed_id == wxGetApp().plater()->get_send_calibration_finished_event()) {
                                msg = wxString::Format(_L("Successfully sent. Will automatically jump to the next page in %ss"), info);
                            }
                        } else {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }

                        // update current percnet
                        if (stage >= 0 && stage <= (int) PrintingStageFinished) {
                            curr_percent = StagePercentPoint[stage];
                            if ((stage == BBL::SendingPrintJobStage::PrintingStageUpload
                                || stage == BBL::SendingPrintJobStage::PrintingStageRecord)
                                && (code > 0 && code <= 100)) {
                                curr_percent = (StagePercentPoint[stage + 1] - StagePercentPoint[stage]) * code / 100 + StagePercentPoint[stage];
                            }
                        }

                        //get errors
                        if (code > 100 || code < 0 || stage == BBL::SendingPrintJobStage::PrintingStageERROR) {
                            if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE) {
                                m_plater->update_print_error_info(code, DESC_FILE_TOO_LARGE.ToStdString(), info);
                            }else if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST){
                                m_plater->update_print_error_info(code, DESC_FAIL_NOT_EXIST.ToStdString(), info);
                            }else if (code == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || code == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
                                m_plater->update_print_error_info(code, DESC_UPLOAD_FTP_FAILED.ToStdString(), info);
                            }else {
                                m_plater->update_print_error_info(code, DESC_NETWORK_ERROR.ToStdString(), info);
                            }
                        }
                        else {
                             this->update_status(curr_percent, msg);
                        }
                    };

    auto cancel_fn = [this]() {
            return was_canceled();
        };


    DeviceManager* dev = wxGetApp().getDeviceManager();
    MachineObject* obj = dev->get_selected_machine();

    auto wait_fn = [this, curr_percent, &obj](int state, std::string job_info) {
            BOOST_LOG_TRIVIAL(info) << "print_job: get_job_info = " << job_info;

            if (!obj->is_support_wait_sending_finish) {
                return true;
            }

            std::string curr_job_id;
            json job_info_j;
            try {
                job_info_j.parse(job_info);
                if (job_info_j.contains("job_id")) {
                    curr_job_id = JsonValParser::get_longlong_val(job_info_j["job_id"]);
                }
                BOOST_LOG_TRIVIAL(trace) << "print_job: curr_obj_id=" << curr_job_id;

            } catch(...) {
                ;
            }

            if (obj) {
                int time_out = 0;
                while (time_out < PRINT_JOB_SENDING_TIMEOUT) {
                    BOOST_LOG_TRIVIAL(trace) << "print_job: obj job_id = " << obj->job_id_;
                    if (!obj->job_id_.empty() && obj->job_id_.compare(curr_job_id) == 0) {
                        BOOST_LOG_TRIVIAL(info) << "print_job: got job_id = " << obj->job_id_ << ", time_out=" << time_out;
                        return true;
                    }
                    if (obj->is_in_printing_status(obj->print_status)) {
                        BOOST_LOG_TRIVIAL(info) << "print_job: printer has enter printing status, s = " << obj->print_status;
                        return true;
                    }

                    if (this->was_canceled()) {
                        BOOST_LOG_TRIVIAL(info) << "print_job: user cancel the job" << obj->job_id_;
                        return true;
                    }

                    time_out++;
                    boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
                }
                //this->update_status(curr_percent, _L("Print task sending times out."));
                //m_plater->update_print_error_info(BAMBU_NETWORK_ERR_TIMEOUT, wait_sending_finish.ToStdString(), desc_wait_sending_finish.ToStdString());
                BOOST_LOG_TRIVIAL(info) << "print_job: timeout, cancel the job" << obj->job_id_;
                /* handle tiemout */
                //obj->command_task_cancel(curr_job_id);
                //return false;
                return true;
            }
            BOOST_LOG_TRIVIAL(info) << "print_job: obj is null";
            return true;
    };

    if (m_print_type == "from_sdcard_view") {
        BOOST_LOG_TRIVIAL(info) << "print_job: try to send with cloud, model is sdcard view";
        this->update_status(curr_percent, _L("Sending print job through cloud service"));
        result = m_agent->start_sdcard_print(params, update_fn, cancel_fn);
    } else if (params.connection_type != "lan") {
        if (params.dev_ip.empty())
            params.comments = "no_ip";
        else if (this->cloud_print_only)
            params.comments = "low_version";
        else if (!this->has_sdcard)
            params.comments = "no_sdcard";
        else if (params.password.empty())
            params.comments = "no_password";


        //use ftp only
        if (!wxGetApp().app_config->get("lan_mode_only").empty() && wxGetApp().app_config->get("lan_mode_only") == "1") {

            if (params.password.empty() || params.dev_ip.empty()) {
                error_text = wxString::Format("Access code:%s Ip address:%s", params.password, params.dev_ip);
                result = BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED;
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "print_job: use ftp send print only";
                this->update_status(curr_percent, _L("Sending print job over LAN"));
                is_try_lan_mode = true;
                result = m_agent->start_local_print_with_record(params, update_fn, cancel_fn, wait_fn);
                if (result < 0) {
                    error_text = wxString::Format("Access code:%s Ip address:%s", params.password, params.dev_ip);
                    // try to send with cloud
                    BOOST_LOG_TRIVIAL(warning) << "print_job: use ftp send print failed";
                }
            }
        }
        else {
            if (!this->cloud_print_only
                && !params.password.empty()
                && !params.dev_ip.empty()
                && this->has_sdcard) {
                // try to send local with record
                BOOST_LOG_TRIVIAL(info) << "print_job: try to start local print with record";
                this->update_status(curr_percent, _L("Sending print job over LAN"));
                result = m_agent->start_local_print_with_record(params, update_fn, cancel_fn, wait_fn);
                if (result == 0) {
                    params.comments = "";
                }
                else if (result == BAMBU_NETWORK_ERR_PRINT_WR_UPLOAD_FTP_FAILED) {
                    params.comments = "upload_failed";
                }
                else {
                    params.comments = (boost::format("failed(%1%)") % result).str();
                }
                if (result < 0) {
                    is_try_lan_mode_failed = true;
                    // try to send with cloud
                    BOOST_LOG_TRIVIAL(warning) << "print_job: try to send with cloud";
                    this->update_status(curr_percent, _L("Sending print job through cloud service"));
                    result = m_agent->start_print(params, update_fn, cancel_fn, wait_fn);
                }
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "print_job: send with cloud";
                this->update_status(curr_percent, _L("Sending print job through cloud service"));
                result = m_agent->start_print(params, update_fn, cancel_fn, wait_fn);
            }
        }
    } else {
        if (this->has_sdcard) {
            this->update_status(curr_percent, _L("Sending print job over LAN"));
            result = m_agent->start_local_print(params, update_fn, cancel_fn);
        } else {
            this->update_status(curr_percent, _L("Storage needs to be inserted before printing via LAN."));
            return;
        }
    }

    if (result < 0) {
        curr_percent = -1;
        if (result == BAMBU_NETOWRK_ERR_PRINT_SP_ENC_FLAG_NOT_READY) {
            msg_text = _L("Retrieving printer information, please try again later.");
        }
        else if (result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST) {
            msg_text = FILE_IS_NOT_EXISTS_STR;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE || result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE) {
            msg_text = FILE_OVER_SIZE_STR;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_WR_CHECK_MD5_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SP_CHECK_MD5_FAILED) {
            msg_text = FAILED_IN_CLOUD_SERVICE_STR;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_WR_GET_NOTIFICATION_TIMEOUT || result == BAMBU_NETWORK_ERR_PRINT_SP_GET_NOTIFICATION_TIMEOUT) {
            msg_text = TIMEOUT_TO_UPLOAD_STR;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
            msg_text = UPLOAD_FTP_FAILED_STR;
        } else if (result == BAMBU_NETWORK_ERR_CANCELED) {
            msg_text = PRINT_CANCELED_STR;
            this->update_status(0, msg_text);
        } else if (result == BAMBU_NETWORK_SIGNED_ERROR) {
            msg_text = PRINT_SIGNED_STR;
        } else {
            msg_text = SEND_PRINT_FAILED_STR;
        }

        if (result != BAMBU_NETWORK_ERR_CANCELED) {
            this->show_error_info(msg_text, 0, "", "");
        }

        BOOST_LOG_TRIVIAL(error) << "print_job: failed, result = " << result;
    } else {
        // wait for printer mqtt ready the same job id

        wxGetApp().plater()->record_slice_preset("print");

        BOOST_LOG_TRIVIAL(error) << "print_job: send ok.";
        wxCommandEvent* evt = new wxCommandEvent(m_print_job_completed_id);
        if (!m_completed_evt_data.empty())
            evt->SetString(m_completed_evt_data);
        else
            evt->SetString(m_dev_id);
        if (m_print_job_completed_id == wxGetApp().plater()->get_send_calibration_finished_event()) {
            int sel = wxGetApp().mainframe->get_calibration_curr_tab();
            if (sel >= 0) {
                evt->SetInt(sel);
            }
        }
        wxQueueEvent(m_plater, evt);
        m_job_finished = true;
    }
}

void PrintJob::finalize() {
    if (was_canceled()) return;

    Job::finalize();
}

void PrintJob::set_project_name(std::string name)
{
    m_project_name = name;
}

void PrintJob::set_dst_name(std::string path)
{
    m_dst_path = path;
}


void PrintJob::on_check_ip_address_fail(std::function<void()> func)
{
    m_enter_ip_address_fun_fail = func;
}

void PrintJob::on_check_ip_address_success(std::function<void()> func)
{
    m_enter_ip_address_fun_success = func;
}

void PrintJob::connect_to_local_mqtt()
{
    this->update_status(0, wxEmptyString);
}

void PrintJob::set_calibration_task(bool is_calibration)
{
    m_is_calibration_task = is_calibration;
}

}} // namespace Slic3r::GUI
