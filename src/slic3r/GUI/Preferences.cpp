#include "Preferences.hpp"
#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include <wx/notebook.h>
#include "Notebook.hpp"
#include "OG_CustomCtrl.hpp"
#include "wx/graphics.h"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/TextInput.hpp"
#include <wx/listimpl.cpp>
#include <map>
#include "Gizmos/GLGizmoBase.hpp"
#include "OpenGLManager.hpp"
#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#endif // _MSW_DARK_MODE
#endif //__WINDOWS__

namespace Slic3r { namespace GUI {

WX_DEFINE_LIST(RadioSelectorList);
wxDEFINE_EVENT(EVT_PREFERENCES_SELECT_TAB, wxCommandEvent);


class MyscrolledWindow : public wxScrolledWindow {
public:
    MyscrolledWindow(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxVSCROLL) : wxScrolledWindow(parent, id, pos, size, style) {}

    bool ShouldScrollToChildOnFocus(wxWindow* child) override { return false; }
};


wxBoxSizer *PreferencesDialog::create_item_title(wxString title, wxWindow *parent, wxString tooltip)
{
    wxBoxSizer *m_sizer_title = new wxBoxSizer(wxHORIZONTAL);

    auto m_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    m_title->SetForegroundColour(DESIGN_GRAY800_COLOR);
    m_title->SetFont(::Label::Head_13);
    m_title->Wrap(-1);
    //m_title->SetToolTip(tooltip);

    auto m_line = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line->SetBackgroundColour(DESIGN_GRAY400_COLOR);

    m_sizer_title->Add(m_title, 0, wxALIGN_CENTER | wxALL, 3);
    m_sizer_title->Add(0, 0, 0,  wxLEFT, 9);
    //m_sizer_title->Add(m_line, 0, wxEXPAND, 0);
    wxBoxSizer *sizer_line = new wxBoxSizer(wxVERTICAL);
    sizer_line->Add( m_line, 0, wxEXPAND, 0 );
    m_sizer_title->Add( sizer_line, 1, wxALIGN_CENTER, 0 );
    //m_sizer_title->Add( 0, 0, 0, wxEXPAND|wxLEFT, 80 );

    return m_sizer_title;
}

wxBoxSizer *PreferencesDialog::create_item_combobox(wxString title, wxWindow *parent, wxString tooltip, std::string param, const std::vector<wxString>& label_list, const std::vector<std::string>& value_list, std::function<void(int)> callback)
{
    auto get_value_idx = [value_list](const std::string value) {
        size_t idx = 0;
        auto iter = std::find(value_list.begin(), value_list.end(), value);
        if (iter != value_list.end())
            idx = std::distance(value_list.begin(), iter);
        return idx;
        };

    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_LARGE_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);

    std::vector<wxString>::iterator iter;
    for (auto label : label_list)
        combobox->Append(label);

    auto old_value = app_config->get(param);
    if (!old_value.empty()) {
        combobox->SetSelection(get_value_idx(old_value));
    }
    else {
        combobox->SetSelection(0);
    }

    m_sizer_combox->Add(combobox, 0, wxALIGN_CENTER, 0);

    //// save config
    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param, value_list, callback](wxCommandEvent &e) {
        app_config->set(param, value_list[e.GetSelection()]);
        app_config->save();
        if (callback) {
            callback(e.GetSelection());
        }
        e.Skip();
    });
    return m_sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_language_combobox(
    wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<const wxLanguageInfo *> vlist)
{
    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);


    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_LARGE_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);
    auto language = app_config->get(param);
    m_current_language_selected = -1;
    std::vector<wxString>::iterator iter;
    for (size_t i = 0; i < vlist.size(); ++i) {
        auto language_name = vlist[i]->Description;

        if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_CHINESE_SIMPLIFIED)) {
            language_name = wxString::FromUTF8("\xe4\xb8\xad\xe6\x96\x87\x28\xe7\xae\x80\xe4\xbd\x93\x29");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_SPANISH)) {
            language_name = wxString::FromUTF8("\x45\x73\x70\x61\xc3\xb1\x6f\x6c");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_GERMAN)) {
            language_name = wxString::FromUTF8("Deutsch");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_SWEDISH)) {
            language_name = wxString::FromUTF8("\x53\x76\x65\x6e\x73\x6b\x61"); //Svenska
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_DUTCH)) {
            language_name = wxString::FromUTF8("Nederlands");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_FRENCH)) {
            language_name = wxString::FromUTF8("\x46\x72\x61\x6E\xC3\xA7\x61\x69\x73");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_HUNGARIAN)) {
            language_name = wxString::FromUTF8("Magyar");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_JAPANESE)) {
            language_name = wxString::FromUTF8("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_ITALIAN)) {
            language_name = wxString::FromUTF8("\x69\x74\x61\x6c\x69\x61\x6e\x6f");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_KOREAN)) {
            language_name = wxString::FromUTF8("\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_RUSSIAN)) {
            language_name = wxString::FromUTF8("\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_CZECH)) {
            if (wxGetApp().app_config->get("language") == "ja_JP") {
                language_name = wxString::FromUTF8("\x43\x7A\x65\x63\x68");
            }
            else{
                language_name = wxString::FromUTF8("\xC4\x8D\x65\xC5\xA1\x74\x69\x6E\x61");
            }
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_UKRAINIAN)) {
            if (wxGetApp().app_config->get("language") == "ja_JP") {
                language_name = wxString::FromUTF8("\x55\x6B\x72\x61\x69\x6E\x69\x61\x6E");
            } else {
                language_name = wxString::FromUTF8("\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0");
            }
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_PORTUGUESE_BRAZILIAN)) {
            language_name = wxString::FromUTF8("\x50\x6F\x72\x74\x75\x67\x75\xC3\xAA\x73\x20\x28\x42\x72\x61\x73\x69\x6C\x29");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_TURKISH)) {
            language_name = wxString::FromUTF8("\x54\xC3\xBC\x72\x6B\xC3\xA7\x65");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_POLISH)) {
            language_name = wxString::FromUTF8("Polski");
        }

        if (language == vlist[i]->CanonicalName) {
            m_current_language_selected = i;
        }
        combobox->Append(language_name);
    }
    if (m_current_language_selected == -1 && language.size() >= 5) {
        language = language.substr(0, 2);
        for (size_t i = 0; i < vlist.size(); ++i) {
            if (vlist[i]->CanonicalName.StartsWith(language)) {
                m_current_language_selected = i;
                break;
            }
        }
    }
    combobox->SetSelection(m_current_language_selected);

    m_sizer_combox->Add(combobox, 0, wxALIGN_CENTER, 0);

    combobox->Bind(wxEVT_LEFT_DOWN, [this, combobox](wxMouseEvent &e) {
        m_current_language_selected = combobox->GetSelection();
        e.Skip();
    });

    combobox->Bind(wxEVT_COMBOBOX, [this, param, vlist, combobox](wxCommandEvent &e) {
        if (combobox->GetSelection() == m_current_language_selected)
            return;

        if (e.GetString().mb_str() != app_config->get(param)) {
            {
                //check if the project has changed
                if (wxGetApp().plater()->is_project_dirty()) {
                    auto result = MessageDialog(static_cast<wxWindow*>(this), _L("The current project has unsaved changes, save it before continuing?"),
                        wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Save"), wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxCENTRE).ShowModal();

                    if (result == wxID_YES) {
                        wxGetApp().plater()->save_project();
                    }
                }


                // the dialog needs to be destroyed before the call to switch_language()
                // or sometimes the application crashes into wxDialogBase() destructor
                // so we put it into an inner scope
                MessageDialog msg_wingow(nullptr, _L("Switching the language requires application restart.\n") + "\n" + _L("Do you want to continue?"),
                                         _L("Language selection"), wxICON_QUESTION | wxOK | wxCANCEL);
                if (msg_wingow.ShowModal() == wxID_CANCEL) {
                    combobox->SetSelection(m_current_language_selected);
                    return;
                }
            }

            auto check = [this](bool yes_or_no) {
                // if (yes_or_no)
                //    return true;
                int act_btns = UnsavedChangesDialog::ActionButtons::SAVE;
                return wxGetApp().check_and_keep_current_preset_changes(_L("Switching application language"),
                                                                        _L("Switching application language while some presets are modified."), act_btns);
            };

            m_current_language_selected = combobox->GetSelection();
            if (m_current_language_selected >= 0 && m_current_language_selected < vlist.size()) {
                app_config->set(param, vlist[m_current_language_selected]->CanonicalName.ToUTF8().data());
                app_config->save();

                wxGetApp().load_language(vlist[m_current_language_selected]->CanonicalName, false);
                Close();
                // Reparent(nullptr);
                GetParent()->RemoveChild(this);
                Label::initSysFont(app_config->get_language_code());
                wxGetApp().recreate_GUI(_L("Changing application language"));
            }
        }

        e.Skip();
    });

    return m_sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_region_combobox(wxString title, wxWindow *parent, wxString tooltip, std::vector<wxString> vlist)
{
    std::vector<wxString> local_regions = {"Asia-Pacific", "China", "Europe", "North America", "Others"};

    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_LARGE_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);
    m_sizer_combox->Add(combobox, 0, wxALIGN_CENTER, 0);

    std::vector<wxString>::iterator iter;
    for (iter = vlist.begin(); iter != vlist.end(); iter++) { combobox->Append(*iter); }

    AppConfig * config       = GUI::wxGetApp().app_config;

    int         current_region = 0;
    if (!config->get("region").empty()) {
        std::string country_code = config->get("region");
        for (auto i = 0; i < vlist.size(); i++) {
            if (local_regions[i].ToStdString() == country_code) {
                combobox->SetSelection(i);
                current_region = i;
            }
        }
    }

    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this, combobox, current_region, local_regions](wxCommandEvent &e) {
        auto region_index = e.GetSelection();
        auto region       = local_regions[region_index];

        /*auto area   = "";
        if (region == "CHN" || region == "China")
            area = "CN";
        else if (region == "USA")
            area = "US";
        else if (region == "Asia-Pacific")
            area = "Others";
        else if (region == "Europe")
            area = "US";
        else if (region == "North America")
            area = "US";
        else
            area = "Others";*/
        combobox->SetSelection(region_index);
        NetworkAgent* agent = wxGetApp().getAgent();
        AppConfig* config = GUI::wxGetApp().app_config;
        if (agent) {
            MessageDialog msg_wingow(this, _L("Changing the region will log out your account.\n") + "\n" + _L("Do you want to continue?"), _L("Region selection"),
                                     wxICON_QUESTION | wxOK | wxCANCEL);
            if (msg_wingow.ShowModal() == wxID_CANCEL) {
                combobox->SetSelection(current_region);
                return;
            } else {
                wxGetApp().request_user_logout();
                config->set("region", region.ToStdString());
                auto area = config->get_country_code();
                if (agent) {
                    agent->set_country_code(area);
                }
                EndModal(wxID_CANCEL);
            }
        } else {
            config->set("region", region.ToStdString());
        }

        wxGetApp().update_publish_status();
        e.Skip();
    });

    return m_sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_loglevel_combobox(wxString title, wxWindow *parent, wxString tooltip, std::vector<wxString> vlist)
{
    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);

    auto                            combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);

    std::vector<wxString>::iterator iter;
    for (iter = vlist.begin(); iter != vlist.end(); iter++) { combobox->Append(*iter); }

    auto severity_level = app_config->get("severity_level");
    if (!severity_level.empty()) { combobox->SetValue(severity_level); }

    m_sizer_combox->Add(combobox, 0, wxALIGN_CENTER, 0);

    //// save config
    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
        auto level = Slic3r::get_string_logging_level(e.GetSelection());
        Slic3r::set_logging_level(Slic3r::level_string_to_boost(level));
        app_config->set("severity_level",level);
        app_config->save();
        e.Skip();
     });
    return m_sizer_combox;
}


wxBoxSizer *PreferencesDialog::create_item_multiple_combobox(
    wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<wxString> vlista, std::vector<wxString> vlistb)
{
    std::vector<wxString> params;
    Split(app_config->get(param), "/", params);

    std::vector<wxString>::iterator iter;

   wxBoxSizer *m_sizer_tcombox= new wxBoxSizer(wxHORIZONTAL);
   m_sizer_tcombox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

   auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
   combo_title->SetToolTip(tooltip);
   combo_title->Wrap(-1);
   combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
   combo_title->SetFont(::Label::Body_13);
   m_sizer_tcombox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);

   auto combobox_left = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
   combobox_left->SetFont(::Label::Body_13);
   combobox_left->GetDropDown().SetFont(::Label::Body_13);


   for (iter = vlista.begin(); iter != vlista.end(); iter++) { combobox_left->Append(*iter); }
   combobox_left->SetValue(std::string(params[0].mb_str()));
   m_sizer_tcombox->Add(combobox_left, 0, wxALIGN_CENTER, 0);

   auto combo_title_add = new wxStaticText(parent, wxID_ANY, wxT("+"), wxDefaultPosition, wxDefaultSize, 0);
   combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
   combo_title->SetFont(::Label::Body_13);
   combo_title_add->Wrap(-1);
   m_sizer_tcombox->Add(combo_title_add, 0, wxALIGN_CENTER | wxALL, 3);

   auto combobox_right = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
   combobox_right->SetFont(::Label::Body_13);
   combobox_right->GetDropDown().SetFont(::Label::Body_13);

   for (iter = vlistb.begin(); iter != vlistb.end(); iter++) { combobox_right->Append(*iter); }
   combobox_right->SetValue(std::string(params[1].mb_str()));
   m_sizer_tcombox->Add(combobox_right, 0, wxALIGN_CENTER, 0);

    // save config
    combobox_left->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param, combobox_right](wxCommandEvent &e) {
        auto config = e.GetString() + wxString("/") + combobox_right->GetValue();
        app_config->set(param, std::string(config.mb_str()));
        app_config->save();
        e.Skip();
    });

    combobox_right->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param, combobox_left](wxCommandEvent &e) {
        auto config = combobox_left->GetValue() + wxString("/") + e.GetString();
        app_config->set(param, std::string(config.mb_str()));
        app_config->save();
        e.Skip();
    });

    return m_sizer_tcombox;
}

wxBoxSizer *PreferencesDialog::create_item_input(wxString title, wxString title2, wxWindow *parent, wxString tooltip, std::string param, std::function<void(wxString)> onchange)
{
    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    auto        input_title   = new wxStaticText(parent, wxID_ANY, title);
    input_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    input_title->SetFont(::Label::Body_13);
    input_title->SetToolTip(tooltip);
    input_title->Wrap(-1);

    auto       input = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, DESIGN_INPUT_SIZE, wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    input->SetBackgroundColor(input_bg);
    input->GetTextCtrl()->SetValue(app_config->get(param));
    wxTextValidator validator(wxFILTER_DIGITS);
    input->GetTextCtrl()->SetValidator(validator);

    auto second_title = new wxStaticText(parent, wxID_ANY, title2, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    second_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    second_title->SetFont(::Label::Body_13);
    second_title->SetToolTip(tooltip);
    second_title->Wrap(-1);

    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    sizer_input->Add(input_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);
    sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL, 0);
    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 3);
    sizer_input->Add(second_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);

    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, param, input, onchange](wxCommandEvent &e) {
        auto value = input->GetTextCtrl()->GetValue();
        app_config->set(param, std::string(value.mb_str()));
        app_config->save();
        onchange(value);
        e.Skip();
    });

    input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, param, input, onchange](wxFocusEvent &e) {
        auto value = input->GetTextCtrl()->GetValue();
        app_config->set(param, std::string(value.mb_str()));
        app_config->save();
        onchange(value);
        e.Skip();
    });

    return sizer_input;
}

wxBoxSizer *PreferencesDialog::create_item_range_input(
    wxString title, wxWindow *parent, wxString tooltip, std::string param, float range_min, float range_max, int keep_digital, std::function<void(wxString)> onchange)
{
    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    auto        input_title = new wxStaticText(parent, wxID_ANY, title);
    input_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    input_title->SetFont(::Label::Body_13);
    input_title->SetToolTip(tooltip);
    input_title->Wrap(-1);

    auto float_value = std::atof(app_config->get(param).c_str());
    if (float_value < range_min || float_value > range_max) {
        float_value = range_min;
        app_config->set(param, std::to_string(range_min));
        app_config->save();
    }
    auto       input = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, DESIGN_INPUT_SIZE, wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    input->SetBackgroundColor(input_bg);
    input->GetTextCtrl()->SetValue(app_config->get(param));
    wxTextValidator validator(wxFILTER_NUMERIC);
    input->GetTextCtrl()->SetValidator(validator);

    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    sizer_input->Add(input_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);
    sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL, 0);
    auto format_str=[](int keep_digital,float val){
        std::stringstream ss;
        ss << std::fixed << std::setprecision(keep_digital) << val;
        return ss.str();
    };
    auto set_value_to_app = [this, param, onchange, input, range_min, range_max, format_str, keep_digital](float value, bool update_slider) {
        if (value < range_min) { value = range_min; }
        if (value > range_max) { value = range_max; }
        auto str = format_str(keep_digital, value);
        app_config->set(param, str);
        app_config->save();
        if (onchange) {
            onchange(str);
        }
        input->GetTextCtrl()->SetValue(str);
    };
    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, set_value_to_app, input](wxCommandEvent &e) {
        auto value = std::atof(input->GetTextCtrl()->GetValue().c_str());
        set_value_to_app(value,true);
        e.Skip();
    });

    input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, set_value_to_app, input](wxFocusEvent &e) {
        auto value = std::atof(input->GetTextCtrl()->GetValue().c_str());
        set_value_to_app(value, true);
        e.Skip();
    });

    return sizer_input;
}

wxBoxSizer *PreferencesDialog::create_item_backup_input(wxString title, wxWindow *parent, wxString tooltip, std::string param)
{
    wxBoxSizer *m_sizer_input = new wxBoxSizer(wxHORIZONTAL);
    auto input_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    input_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    input_title->SetFont(::Label::Body_13);
    input_title->SetToolTip(tooltip);
    input_title->Wrap(-1);

    auto input = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, DESIGN_INPUT_SIZE, wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    input->SetBackgroundColor(input_bg);
    input->GetTextCtrl()->SetValue(app_config->get(param));
    wxTextValidator validator(wxFILTER_DIGITS);
    input->GetTextCtrl()->SetValidator(validator);


    auto second_title = new wxStaticText(parent, wxID_ANY, _L("Second"), wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    second_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    second_title->SetFont(::Label::Body_13);
    second_title->SetToolTip(tooltip);
    second_title->Wrap(-1);

    m_sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    m_sizer_input->Add(input_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);
    m_sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL, 0);
    m_sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 3);
    m_sizer_input->Add(second_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);


    input->GetTextCtrl()->Bind(wxEVT_COMMAND_TEXT_UPDATED, [this, param, input](wxCommandEvent &e) {
        m_backup_interval_time = input->GetTextCtrl()->GetValue();
        e.Skip();
    });

    std::function<void()> backup_interval = [this, param, input]() {
        m_backup_interval_time = input->GetTextCtrl()->GetValue();
        app_config->set("backup_interval", std::string(m_backup_interval_time.mb_str()));
        app_config->save();
        long backup_interval = 0;
        m_backup_interval_time.ToLong(&backup_interval);
        Slic3r::set_backup_interval(backup_interval);
    };

    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [backup_interval](wxCommandEvent &e) {
        backup_interval();
        e.Skip();
    });

     input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [backup_interval](wxFocusEvent &e) {
        backup_interval();
        e.Skip();
    });

    if (app_config->get("backup_switch") == "true") {
        input->Enable(true);
        input->Refresh();
    } else {
        input->Enable(false);
        input->Refresh();
    }

    if (param == "backup_interval") { m_backup_interval_textinput = input; }
    return m_sizer_input;
}


wxBoxSizer *PreferencesDialog::create_item_switch(wxString title, wxWindow *parent, wxString tooltip ,std::string param)
{
    wxBoxSizer *m_sizer_switch = new wxBoxSizer(wxHORIZONTAL);
    auto switch_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    switch_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    switch_title->SetFont(::Label::Body_13);
    switch_title->SetToolTip(tooltip);
    switch_title->Wrap(-1);
    auto switchbox = new ::SwitchButton(parent, wxID_ANY);

    /*auto index = app_config->get(param);
    if (!index.empty()) { combobox->SetSelection(atoi(index.c_str())); }*/

    m_sizer_switch->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    m_sizer_switch->Add(switch_title, 0, wxALIGN_CENTER | wxALL, 3);
    m_sizer_switch->Add( 0, 0, 1, wxEXPAND, 0 );
    m_sizer_switch->Add(switchbox, 0, wxALIGN_CENTER, 0);
    m_sizer_switch->Add( 0, 0, 0, wxEXPAND|wxLEFT, 40 );

    //// save config
    switchbox->Bind(wxEVT_TOGGLEBUTTON, [this, param](wxCommandEvent &e) {
        /* app_config->set(param, std::to_string(e.GetSelection()));
         app_config->save();*/
         e.Skip();
    });
    return m_sizer_switch;
}

wxBoxSizer* PreferencesDialog::create_item_darkmode_checkbox(wxString title, wxWindow* parent, wxString tooltip, int padding_left, std::string param)
{
    wxBoxSizer* m_sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto checkbox = new ::CheckBox(parent);
    checkbox->SetValue((app_config->get(param) == "1") ? true : false);
    m_dark_mode_ckeckbox = checkbox;

    m_sizer_checkbox->Add(checkbox, 0, wxALIGN_CENTER, 0);
    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 8);

    auto checkbox_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    checkbox_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    checkbox_title->SetFont(::Label::Body_13);

    auto size = checkbox_title->GetTextExtent(title);
    checkbox_title->SetMinSize(wxSize(size.x + FromDIP(40), -1));
    checkbox_title->Wrap(-1);
    m_sizer_checkbox->Add(checkbox_title, 0, wxALIGN_CENTER | wxALL, 3);


    //// save config
    checkbox->Bind(wxEVT_TOGGLEBUTTON, [this, checkbox, param](wxCommandEvent& e) {
        app_config->set(param, checkbox->GetValue() ? "1" : "0");
        app_config->save();
        wxGetApp().Update_dark_mode_flag();

        //dark mode
#ifdef _MSW_DARK_MODE
        wxGetApp().force_colors_update();
        wxGetApp().update_ui_from_settings();
        set_dark_mode();
#endif
        SimpleEvent evt = SimpleEvent(EVT_GLCANVAS_COLOR_MODE_CHANGED);
        wxPostEvent(wxGetApp().plater(), evt);
        e.Skip();
        });

    checkbox->SetToolTip(tooltip);
    return m_sizer_checkbox;
}

void PreferencesDialog::set_dark_mode()
{
#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
    NppDarkMode::SetDarkExplorerTheme(this->GetHWND());
    NppDarkMode::SetDarkTitleBar(this->GetHWND());
    wxGetApp().UpdateDlgDarkUI(this);
    SetActiveWindow(wxGetApp().mainframe->GetHWND());
    SetActiveWindow(GetHWND());
#endif
#endif
}

wxBoxSizer *PreferencesDialog::create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param)
{
    wxBoxSizer *m_sizer_checkbox  = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto checkbox = new ::CheckBox(parent);
    if (param == "privacyuse") {
        checkbox->SetValue((app_config->get("firstguide", param) == "true") ? true : false);
    } else if (param == "auto_stop_liveview") {
        checkbox->SetValue((app_config->get("liveview", param) == "true") ? false : true);
    } else {
        checkbox->SetValue((app_config->get(param) == "true") ? true : false);
    }

    m_sizer_checkbox->Add(checkbox, 0, wxALIGN_CENTER, 0);
    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 8);

    auto checkbox_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    checkbox_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    checkbox_title->SetFont(::Label::Body_13);

    auto size = checkbox_title->GetTextExtent(title);
    checkbox_title->SetMinSize(wxSize(size.x + FromDIP(5), -1));
    checkbox_title->Wrap(-1);
    m_sizer_checkbox->Add(checkbox_title, 0, wxALIGN_CENTER | wxALL, 3);


     //// save config
    checkbox->Bind(wxEVT_TOGGLEBUTTON, [this, checkbox, param](wxCommandEvent &e) {
        if (param == "privacyuse") {
            app_config->set("firstguide", param, checkbox->GetValue());
            NetworkAgent* agent = GUI::wxGetApp().getAgent();
            if (!checkbox->GetValue()) {
                if (agent) {
                    agent->track_enable(false);
                    agent->track_remove_files();
                }
            }
            wxGetApp().save_privacy_policy_history(checkbox->GetValue(), "preferences");
            app_config->save();
        }
        else if (param == "auto_stop_liveview") {
            app_config->set("liveview", param, !checkbox->GetValue());
        }
        else {
            app_config->set_bool(param, checkbox->GetValue());
            app_config->save();
        }

        if (param == "staff_pick_switch") {
            bool pbool = app_config->get("staff_pick_switch") == "true";
            wxGetApp().switch_staff_pick(pbool);
        }

         // backup
        if (param == "backup_switch") {
            bool pbool = app_config->get("backup_switch") == "true" ? true : false;
            std::string backup_interval = "10";
            app_config->get("backup_interval", backup_interval);
            Slic3r::set_backup_interval(pbool ? boost::lexical_cast<long>(backup_interval) : 0);
            if (m_backup_interval_textinput != nullptr) { m_backup_interval_textinput->Enable(pbool); }
        }

        if (param == "sync_user_preset") {
            bool sync = app_config->get("sync_user_preset") == "true" ? true : false;
            if (sync) {
                wxGetApp().start_sync_user_preset();
            } else {
                wxGetApp().stop_sync_user_preset();
            }
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: " << (sync ? "true" : "false");
        }

        #ifdef __WXMSW__
        if (param == "associate_3mf") {
             bool pbool = app_config->get("associate_3mf") == "true" ? true : false;
             if (pbool) {
                 wxGetApp().associate_files(L"3mf");
             } else {
                 wxGetApp().disassociate_files(L"3mf");
             }
        }

        if (param == "associate_stl") {
            bool pbool = app_config->get("associate_stl") == "true" ? true : false;
            if (pbool) {
                wxGetApp().associate_files(L"stl");
            } else {
                wxGetApp().disassociate_files(L"stl");
            }
        }

        if (param == "associate_step") {
            bool pbool = app_config->get("associate_step") == "true" ? true : false;
            if (pbool) {
                wxGetApp().associate_files(L"step");
            } else {
                wxGetApp().disassociate_files(L"step");
            }
        }

        #endif // __WXMSW__

        if (param == "developer_mode")
        {
            m_developer_mode_def = app_config->get("developer_mode");
            if (m_developer_mode_def == "true") {
                Slic3r::GUI::wxGetApp().save_mode(comDevelop);
            } else {
                Slic3r::GUI::wxGetApp().save_mode(comAdvanced);
            }
        }

        // webview  dump_vedio
        if (param == "internal_developer_mode") {
            m_internal_developer_mode_def = app_config->get("internal_developer_mode");
            if (m_internal_developer_mode_def == "true") {
                Slic3r::GUI::wxGetApp().update_internal_development();
                Slic3r::GUI::wxGetApp().mainframe->show_log_window();
            } else {
                Slic3r::GUI::wxGetApp().update_internal_development();
            }
        }

        if (param == "show_print_history") {
            auto show_history = app_config->get_bool("show_print_history");
            if (show_history == true) {
                if (wxGetApp().mainframe && wxGetApp().mainframe->m_webview) { wxGetApp().mainframe->m_webview->ShowUserPrintTask(true,true); }
            } else {
                if (wxGetApp().mainframe && wxGetApp().mainframe->m_webview) { wxGetApp().mainframe->m_webview->ShowUserPrintTask(false); }
            }
        }

        if (param == "enable_lod") {
            if (wxGetApp().plater()->is_project_dirty()) {
                auto result = MessageDialog(static_cast<wxWindow *>(this), _L("The current project has unsaved changes, save it before continuing?"),
                                            wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Save"), wxYES_NO  | wxYES_DEFAULT | wxCENTRE)
                                  .ShowModal();
                if (result == wxID_YES) {
                    wxGetApp().plater()->save_project();
                }
            }
            MessageDialog msg_wingow(nullptr, _L("Please note that the model show will undergo certain changes at small pixels case.\nEnabled LOD requires application restart.") + "\n" + _L("Do you want to continue?"), _L("Enable LOD"),
                wxYES| wxYES_DEFAULT | wxCANCEL | wxCENTRE);
            if (msg_wingow.ShowModal() == wxID_YES) {
                Close();
                GetParent()->RemoveChild(this);
                wxGetApp().recreate_GUI(_L("Enable LOD"));
            } else {
                checkbox->SetValue(!checkbox->GetValue());
                app_config->set_bool(param, checkbox->GetValue());
                app_config->save();
            }
        }

        if (param == "enable_high_low_temp_mixed_printing") {
            if (checkbox->GetValue()) {
                const wxString warning_title = _L("Bed Temperature Difference Warning");
                const wxString warning_message =
                    _L("Using filaments with significantly different temperatures may cause:\n"
                        "• Extruder clogging\n"
                        "• Nozzle damage\n"
                        "• Layer adhesion issues\n\n"
                        "Continue with enabling this feature?");
                std::function<void(const wxString&)> link_callback = [](const wxString&) {
                            const std::string lang_code = wxGetApp().app_config->get("language");
                            const wxString region = (lang_code.find("zh") != std::string::npos) ? L"zh" : L"en";
                            const wxString wiki_url = wxString::Format(
                                L"https://wiki.bambulab.com/%s/filament-acc/filament/h2d-filament-config-limit",
                                region
                            );
                            wxGetApp().open_browser_with_warning_dialog(wiki_url);
                            };

                MessageDialog msg_dialog(
                    nullptr,
                    warning_message,
                    warning_title,
                    wxICON_WARNING | wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxCENTRE,
                    wxEmptyString,
                    _L("Click Wiki for help."),
                    link_callback
                );

                if (msg_dialog.ShowModal() != wxID_YES) {
                    checkbox->SetValue(false);
                    app_config->set_bool(param, false);
                    app_config->save();
                }
            }
        }
        e.Skip();
    });

    //// for debug mode
    if (param == "developer_mode") { m_developer_mode_ckeckbox = checkbox; }
    if (param == "internal_developer_mode") { m_internal_developer_mode_ckeckbox = checkbox; }


    checkbox->SetToolTip(tooltip);
    return m_sizer_checkbox;
}

wxBoxSizer *PreferencesDialog::create_item_button(wxString title, wxString title2, wxWindow *parent, wxString tooltip, std::function<void()> onclick)
{
    wxBoxSizer *m_sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    auto m_staticTextPath = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    // m_staticTextPath->SetMaxSize(wxSize(FromDIP(440), -1));
    m_staticTextPath->SetForegroundColour(DESIGN_GRAY900_COLOR);
    m_staticTextPath->SetFont(::Label::Body_13);
    m_staticTextPath->Wrap(-1);

    auto m_button_download = new Button(parent, title2);

    StateColor abort_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
                        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    m_button_download->SetBackgroundColor(abort_bg);
    StateColor abort_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_download->SetBorderColor(abort_bd);
    StateColor abort_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_download->SetTextColor(abort_text);
    m_button_download->SetFont(Label::Body_10);
    m_button_download->SetMinSize(wxSize(FromDIP(58), FromDIP(22)));
    m_button_download->SetSize(wxSize(FromDIP(58), FromDIP(22)));
    m_button_download->SetCornerRadius(FromDIP(12));
    m_button_download->SetToolTip(tooltip);

    m_button_download->Bind(wxEVT_BUTTON, [this, onclick](auto &e) { onclick(); });

    m_sizer_checkbox->Add(m_staticTextPath, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    m_sizer_checkbox->Add(m_button_download, 0, wxALL, FromDIP(5));

    return m_sizer_checkbox;
}

wxWindow* PreferencesDialog::create_item_downloads(wxWindow* parent, int padding_left, std::string param)
{
    wxString download_path = wxString::FromUTF8(app_config->get("download_path"));
    auto item_panel = new wxWindow(parent, wxID_ANY);
    item_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* m_sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    auto m_staticTextPath = new wxStaticText(item_panel, wxID_ANY, download_path, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    //m_staticTextPath->SetMaxSize(wxSize(FromDIP(440), -1));
    m_staticTextPath->SetForegroundColour(DESIGN_GRAY600_COLOR);
    m_staticTextPath->SetFont(::Label::Body_13);
    m_staticTextPath->Wrap(-1);

    auto m_button_download = new Button(item_panel, _L("Browse"));

    StateColor abort_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
    std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
    std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    m_button_download->SetBackgroundColor(abort_bg);
    StateColor abort_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_download->SetBorderColor(abort_bd);
    StateColor abort_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_download->SetTextColor(abort_text);
    m_button_download->SetFont(Label::Body_10);
    m_button_download->SetMinSize(wxSize(FromDIP(58), FromDIP(22)));
    m_button_download->SetSize(wxSize(FromDIP(58), FromDIP(22)));
    m_button_download->SetCornerRadius(FromDIP(12));

    m_button_download->Bind(wxEVT_BUTTON, [this, m_staticTextPath, item_panel](auto& e) {
        wxString defaultPath = wxT("/");
        wxDirDialog dialog(this, _L("Choose Download Directory"), defaultPath, wxDD_NEW_DIR_BUTTON);

        if (dialog.ShowModal() == wxID_OK) {
            wxString download_path = dialog.GetPath();
            std::string download_path_str = download_path.ToUTF8().data();
            app_config->set("download_path", download_path_str);
            m_staticTextPath->SetLabelText(download_path);
            item_panel->Layout();
        }
        });

    m_sizer_checkbox->Add(m_staticTextPath, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    m_sizer_checkbox->Add(m_button_download, 0, wxALL, FromDIP(5));

    item_panel->SetSizer(m_sizer_checkbox);
    item_panel->Layout();

    return item_panel;
}

wxWindow *PreferencesDialog ::create_item_radiobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, int groupid, std::string param)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(28)));
    item->SetBackgroundColour(*wxWHITE);

    RadioBox *radiobox = new RadioBox(item);
    radiobox->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - radiobox->GetSize().GetHeight()) / 2));
    radiobox->Bind(wxEVT_LEFT_DOWN, &PreferencesDialog::OnSelectRadio, this);

    RadioSelector *rs = new RadioSelector;
    rs->m_groupid     = groupid;
    rs->m_param_name  = param;
    rs->m_radiobox    = radiobox;
    rs->m_selected    = false;
    m_radio_group.Append(rs);

    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetPosition(wxPoint(padding_left + radiobox->GetSize().GetWidth() + 10, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return item;
}

PreferencesDialog::PreferencesDialog(wxWindow *parent, wxWindowID id, const wxString &title, const wxPoint &pos, const wxSize &size, long style)
    : DPIDialog(parent, id, _L("Preferences"), pos, size, style)
{
    SetBackgroundColour(*wxWHITE);
    create();
    wxGetApp().UpdateDlgDarkUI(this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        try {
            NetworkAgent* agent = GUI::wxGetApp().getAgent();
            if (agent) {
                json j;
                std::string value;
                value = wxGetApp().app_config->get("auto_calculate_flush");
                j["auto_flushing"] = value;
                agent->track_event("preferences_changed", j.dump());
            }
        } catch(...) {}
        event.Skip();
        });
}

void PreferencesDialog::create()
{
    app_config             = get_app_config();
    m_backup_interval_time = app_config->get("backup_interval");

    // set icon for dialog
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetSizeHints(wxDefaultSize, wxDefaultSize);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new MyscrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scrolledWindow->SetScrollRate(5, 5);

    m_sizer_body = new wxBoxSizer(wxVERTICAL);

    auto m_top_line = new wxPanel(m_scrolledWindow, wxID_ANY, wxDefaultPosition, wxSize(DESIGN_RESOUTION_PREFERENCES.x, 1), wxTAB_TRAVERSAL);
    m_top_line->SetBackgroundColour(DESIGN_GRAY400_COLOR);

    m_sizer_body->Add(m_top_line, 0, wxEXPAND, 0);

    auto general_page = create_general_page();
#if !BBL_RELEASE_TO_PUBLIC
    auto debug_page   = create_debug_page();
#endif

    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(28));
    m_sizer_body->Add(general_page, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(38));
#if !BBL_RELEASE_TO_PUBLIC
    m_sizer_body->Add(debug_page, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(38));
#endif
    m_sizer_body->Add(0, 0, 0, wxBOTTOM, FromDIP(28));
    m_scrolledWindow->SetSizerAndFit(m_sizer_body);

    main_sizer->Add(m_scrolledWindow, 1, wxEXPAND);

    SetSizer(main_sizer);
    Layout();
    Fit();
    int screen_height = wxGetDisplaySize().GetY();
    if (this->GetSize().GetY() > screen_height)
        this->SetSize(this->GetSize().GetX() + FromDIP(40), screen_height * 4 / 5);

    CenterOnParent();
    wxPoint start_pos = this->GetPosition();
    if (start_pos.y < 0) { this->SetPosition(wxPoint(start_pos.x, 0)); }

    //select first
    auto event = wxCommandEvent(EVT_PREFERENCES_SELECT_TAB);
    event.SetInt(0);
    event.SetEventObject(this);
    wxPostEvent(this, event);
}

PreferencesDialog::~PreferencesDialog()
{
    m_radio_group.DeleteContents(true);
    m_hash_selector.clear();
}

void PreferencesDialog::on_dpi_changed(const wxRect &suggested_rect) { this->Refresh(); }

void PreferencesDialog::Split(const std::string &src, const std::string &separator, std::vector<wxString> &dest)
{
    std::string            str = src;
    std::string            substring;
    std::string::size_type start = 0, index;
    dest.clear();
    index = str.find_first_of(separator, start);
    do {
        if (index != std::string::npos) {
            substring = str.substr(start, index - start);
            dest.push_back(substring);
            start = index + separator.size();
            index = str.find(separator, start);
            if (start == std::string::npos) break;
        }
    } while (index != std::string::npos);

    substring = str.substr(start);
    dest.push_back(substring);
}

wxWindow* PreferencesDialog::create_general_page()
{
    auto page = new wxWindow(m_scrolledWindow, wxID_ANY);
    page->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *sizer_page = new wxBoxSizer(wxVERTICAL);

    auto title_general_settings = create_item_title(_L("General Settings"), page, _L("General Settings"));
    auto translations = wxTranslations::Get()->GetAvailableTranslations(SLIC3R_APP_KEY);
    std::vector<const wxLanguageInfo *> language_infos;
    language_infos.emplace_back(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH));
    for (size_t i = 0; i < translations.GetCount(); ++i) {
        const wxLanguageInfo *langinfo = wxLocale::FindLanguageInfo(translations[i]);

        if (langinfo == nullptr) continue;

        for (auto si = 0; si < s_supported_languages.size(); si++) {
            if (langinfo == wxLocale::GetLanguageInfo(s_supported_languages[si])) {
                language_infos.emplace_back(langinfo);
            }
        }
        //if (langinfo != nullptr) language_infos.emplace_back(langinfo);
    }
    sort_remove_duplicates(language_infos);
    std::sort(language_infos.begin(), language_infos.end(), [](const wxLanguageInfo *l, const wxLanguageInfo *r) { return l->Description < r->Description; });
    auto item_language = create_item_language_combobox(_L("Language"), page, _L("Language"), 50, "language", language_infos);

    std::vector<wxString> Regions         = {_L("Asia-Pacific"), _L("Chinese Mainland"), _L("Europe"), _L("North America"), _L("Others")};
    auto                  item_region= create_item_region_combobox(_L("Login Region"), page, _L("Login Region"), Regions);

    std::vector<wxString> Units         = {_L("Metric") + " (mm, g)", _L("Imperial") + " (in, oz)"};
    auto item_currency = create_item_combobox(_L("Units"), page, _L("Units"), "use_inches", Units,{"0","1"});
    auto item_single_instance = create_item_checkbox(_L("Keep only one Bambu Studio instance"), page,
#if __APPLE__
        _L("On OSX there is always only one instance of app running by default. However it is allowed to run multiple instances "
			  "of same app from the command line. In such case this settings will allow only one instance."),
#else
        _L("If this is enabled, when starting Bambu Studio and another instance of the same Bambu Studio is already running, that instance will be reactivated instead."),
#endif
        50, "single_instance");

    auto item_auto_transfer_when_switch_preset = create_item_checkbox(_L("Automatically transfer modified value when switching process and filament presets"), page,_L("After closing, a popup will appear to ask each time"), 50, "auto_transfer_when_switch_preset");
    auto item_bed_type_follow_preset = create_item_checkbox(_L("Auto plate type"), page,
                                                         _L("Studio will remember build plate selected last time for certain printer model."), 50,
                                                         "user_bed_type");
    std::vector<wxString> FlushOptionLabels = {_L("All"),_L("Color change"),_L("Disabled")};
    std::vector<std::string> FlushOptionValues = { "all","color change","disabled" };
    auto item_auto_flush = create_item_combobox(_L("Auto Flush"), page, _L("Auto calculate flush volumes"), "auto_calculate_flush", FlushOptionLabels, FlushOptionValues);
    //auto item_hints = create_item_checkbox(_L("Show \"Tip of the day\" notification after start"), page, _L("If enabled, useful hints are displayed at startup."), 50, "show_hints");
    auto item_multi_machine = create_item_checkbox(_L("Multi-device Management(Take effect after restarting Studio)."), page, _L("With this option enabled, you can send a task to multiple devices at the same time and manage multiple devices."), 50, "enable_multi_machine");
    auto item_step_mesh_setting = create_item_checkbox(_L("Show the step mesh parameter setting dialog."), page, _L("If enabled,a parameter settings dialog will appear during STEP file import."), 50, "enable_step_mesh_setting");
    auto item_beta_version_update = create_item_checkbox(_L("Support beta version update."), page, _L("With this option enabled, you can receive beta version updates."), 50, "enable_beta_version_update");
    auto item_mix_print_high_low_temperature = create_item_checkbox(_L("Remove the restriction on mixed printing of high and low temperature filaments."), page, _L("With this option enabled, you can print materials with a large temperature difference together."), 50, "enable_high_low_temp_mixed_printing");
    auto item_restore_hide_pop_ups = create_item_button(_L("Clear my choice for synchronizing printer preset after loading the file."), _L("Clear"), page, _L("Clear my choice for synchronizing printer preset after loading the file."), []() {
        wxGetApp().app_config->erase("app", "sync_after_load_file_show_flag");
    });
    auto  item_restore_hide_3mf_info = create_item_button(_L("Clear my choice for Load 3mf dialog settings."), _L("Clear"), page, _L("Show the warning dialog again when importing non-Bambu 3MF files"),[]() {
        wxGetApp().app_config->erase("app", "skip_non_bambu_3mf_warning");
    });
    auto _3d_settings    = create_item_title(_L("3D Settings"), page, _L("3D Settings"));
    auto item_mouse_zoom_settings  = create_item_checkbox(_L("Zoom to mouse position"), page,
                                                         _L("Zoom in towards the mouse pointer's position in the 3D view, rather than the 2D window center."), 50,
                                                         "zoom_to_mouse");
    auto  item_show_shells_in_preview_settings = create_item_checkbox(_L("Always show shells in preview"), page,
                                                         _L("Always show shells or not in preview view tab.If change value,you should reslice."), 50,
                                                         "show_shells_in_preview");
    auto  item_import_single_svg_and_split         = create_item_checkbox(_L("Import a single SVG and split it"), page,
                                                                     _L("Import a single SVG and then split it to several parts."), 50,
                                                                     "import_single_svg_and_split");
    auto  item_gamma_correct_in_import_obj = create_item_checkbox(_L("Enable gamma correction for the imported obj file"), page,
                                                                 _L("Perform gamma correction on color after importing the obj model."), 50,
                                                                 "gamma_correct_in_import_obj");
    auto  enable_lod_settings       = create_item_checkbox(_L("Improve rendering performance by lod"), page,
                                                         _L("Improved rendering performance under the scene of multiple plates and many models."), 50,
                                                         "enable_lod");

    std::vector<wxString> toolbar_style = { _L("Collapsible"), _L("Uncollapsible") };
    auto item_toolbar_style = create_item_combobox(_L("Toolbar Style"), page, _L("Toolbar Style"), "toolbar_style", toolbar_style, { "0","1" }, [](int idx)->void {
        const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
        p_ogl_manager->set_toolbar_rendering_style(idx);
    });

    float range_min = 1.0, range_max = 2.5;
    auto item_grabber_size_settings = create_item_range_input(_L("Grabber scale"), page,
                                                              _L("Set grabber size for move,rotate,scale tool.") + _L("Value range") + ":[" + std::to_string(range_min) + "," +
                                                                  std::to_string(range_max) +
                                                                  "]","grabber_size_factor", range_min, range_max, 1,
        [](wxString value) {
            double d_value = 0;
            if (value.ToDouble(&d_value)) {
                GLGizmoBase::Grabber::GrabberSizeFactor = d_value;
            }
        });
    auto title_presets = create_item_title(_L("Presets"), page, _L("Presets"));
    auto item_user_sync        = create_item_checkbox(_L("Auto sync user presets(Printer/Filament/Process)"), page, _L("If enabled, auto sync user presets with cloud after Bambu Studio startup or presets modified."), 50, "sync_user_preset");
    auto item_system_sync        = create_item_checkbox(_L("Auto check for system presets updates"), page, _L("If enabled, auto check whether there are system presets updates after Bambu Studio startup."), 50, "sync_system_preset");

#ifdef _WIN32
    auto title_associate_file = create_item_title(_L("Associate Files To Bambu Studio"), page, _L("Associate Files To Bambu Studio"));

    // associate file
    auto item_associate_3mf  = create_item_checkbox(_L("Associate .3mf files to Bambu Studio"), page,
                                                        _L("If enabled, sets Bambu Studio as default application to open .3mf files"), 50, "associate_3mf");
    auto item_associate_stl  = create_item_checkbox(_L("Associate .stl files to Bambu Studio"), page,
                                                        _L("If enabled, sets Bambu Studio as default application to open .stl files"), 50, "associate_stl");
    auto item_associate_step = create_item_checkbox(_L("Associate .step/.stp files to Bambu Studio"), page,
                                                         _L("If enabled, sets Bambu Studio as default application to open .step files"), 50, "associate_step");
#endif // _WIN32

    auto title_modelmall = create_item_title(_L("Online Models"), page, _L("Online Models"));
    // auto item_backup = create_item_switch(_L("Backup switch"), page, _L("Backup switch"), "units");
    auto item_modelmall = create_item_checkbox(_L("Show online staff-picked models on the home page"), page, _L("Show online staff-picked models on the home page"), 50, "staff_pick_switch");

    auto item_show_history = create_item_checkbox(_L("Show history on the home page"), page, _L("Show history on the home page"), 50, "show_print_history");
    auto title_project = create_item_title(_L("Project"), page, "");
    auto item_max_recent_count = create_item_input(_L("Maximum recent projects"), "", page, _L("Maximum count of recent projects"), "max_recent_count", [](wxString value) {
        long max = 0;
        if (value.ToLong(&max))
            wxGetApp().mainframe->set_max_recent_count(max);
    });
    auto item_save_choise = create_item_button(_L("Clear my choice on the unsaved projects."), _L("Clear"), page, _L("Clear my choice on the unsaved projects."), []() {
        wxGetApp().app_config->set("save_project_choise", "");
    });
    // auto item_backup = create_item_switch(_L("Backup switch"), page, _L("Backup switch"), "units");
    auto item_gcodes_warning = create_item_checkbox(_L("No warnings when loading 3MF with modified G-codes"), page,_L("No warnings when loading 3MF with modified G-codes"), 50, "no_warn_when_modified_gcodes");
    auto item_backup  = create_item_checkbox(_L("Auto-Backup"), page,_L("Backup your project periodically for restoring from the occasional crash."), 50, "backup_switch");
    auto item_backup_interval = create_item_backup_input(_L("every"), page, _L("The peroid of backup in seconds."), "backup_interval");

    //downloads
    auto title_downloads = create_item_title(_L("Downloads"), page, _L("Downloads"));
    auto item_downloads = create_item_downloads(page,50,"download_path");

    auto title_media = create_item_title(_L("Media"), page, _L("Media"));
    auto item_auto_stop_liveview = create_item_checkbox(_L("Keep liveview when printing."), page, _L("By default, Liveview will pause after 15 minutes of inactivity on the computer. Check this box to disable this feature during printing."), 50, "auto_stop_liveview");

    //dark mode
#ifdef _WIN32
    auto title_darkmode = create_item_title(_L("Dark Mode"), page, _L("Dark Mode"));
    auto item_darkmode = create_item_darkmode_checkbox(_L("Enable dark mode"), page,_L("Enable dark mode"), 50, "dark_color_mode");
#endif

#if 0
    auto title_filament_group = create_item_title(_L("Filament Grouping"), page, _L("Filament Grouping"));
    //temporarily disable it
    //auto item_ignore_ext_filament = create_item_checkbox(_L("Ignore ext filament when auto grouping"), page, _L("Ignore ext filament when auto grouping"), 50, "ignore_ext_filament_when_group");
    auto item_pop_up_filament_map_dialog = create_item_checkbox(_L("Pop up to select filament grouping mode"), page, _L("Pop up to select filament grouping mode"), 50, "pop_up_filament_map_dialog");
#endif

    auto title_user_experience = create_item_title(_L("User Experience"), page, _L("User Experience"));
    auto item_priv_policy = create_item_checkbox(_L("Join Customer Experience Improvement Program."), page, "", 50, "privacyuse");
    wxHyperlinkCtrl* hyperlink = new wxHyperlinkCtrl(page, wxID_ANY, _L("What data would be collected?"), "https://bambulab.com/en/policies/privacy");
    hyperlink->SetFont(Label::Head_13);
    item_priv_policy->Add(hyperlink, 0, wxALIGN_CENTER, 0);

    auto title_develop_mode = create_item_title(_L("Develop Mode"), page, _L("Develop Mode"));
    auto item_develop_mode  = create_item_checkbox(_L("Develop mode"), page, _L("Develop mode"), 50, "developer_mode");
    auto item_skip_ams_blacklist_check  = create_item_checkbox(_L("Skip AMS blacklist check"), page, _L("Skip AMS blacklist check"), 50, "skip_ams_blacklist_check");

    sizer_page->Add(title_general_settings, 0, wxEXPAND, 0);
    sizer_page->Add(item_language, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_region, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_currency, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_auto_flush, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_single_instance, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_bed_type_follow_preset, 0, wxTOP, FromDIP(3));
    //sizer_page->Add(item_hints, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_multi_machine, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_step_mesh_setting, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_beta_version_update, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_auto_transfer_when_switch_preset, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_mix_print_high_low_temperature, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_restore_hide_pop_ups, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_restore_hide_3mf_info, 0, wxTOP, FromDIP(3));
    sizer_page->Add(_3d_settings, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_mouse_zoom_settings, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_show_shells_in_preview_settings, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_import_single_svg_and_split, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_gamma_correct_in_import_obj, 0, wxTOP, FromDIP(3));

    sizer_page->Add(enable_lod_settings, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_toolbar_style, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_grabber_size_settings, 0, wxTOP, FromDIP(3));
    sizer_page->Add(title_presets, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_user_sync, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_system_sync, 0, wxTOP, FromDIP(3));
#ifdef _WIN32
    sizer_page->Add(title_associate_file, 0, wxTOP| wxEXPAND, FromDIP(20));
    sizer_page->Add(item_associate_3mf, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_associate_stl, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_associate_step, 0, wxTOP, FromDIP(3));
#endif // _WIN32
    auto item_title_modelmall   = sizer_page->Add(title_modelmall, 0, wxTOP | wxEXPAND, FromDIP(20));
    auto item_item_modelmall    = sizer_page->Add(item_modelmall, 0, wxTOP, FromDIP(3));
    auto item_item_show_history = sizer_page->Add(item_show_history, 0, wxTOP, FromDIP(3));

    auto update_modelmall = [this, item_title_modelmall, item_item_modelmall, item_item_show_history](wxEvent &e) {
        bool has_model_mall = wxGetApp().has_model_mall();
        item_title_modelmall->Show(has_model_mall);
        item_item_modelmall->Show(has_model_mall);
        item_item_show_history->Show(has_model_mall);
        Layout();
        Fit();
    };
    wxCommandEvent eee(wxEVT_COMBOBOX);
    update_modelmall(eee);
    item_region->GetItem(size_t(2))->GetWindow()->Bind(wxEVT_COMBOBOX, update_modelmall);
    sizer_page->Add(title_project, 0, wxTOP| wxEXPAND, FromDIP(20));
    sizer_page->Add(item_max_recent_count, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_save_choise, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_gcodes_warning, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_backup, 0, wxTOP,FromDIP(3));
    item_backup->Add(item_backup_interval, 0, wxLEFT, 0);

    sizer_page->Add(title_downloads, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_downloads, 0, wxEXPAND, FromDIP(3));

    sizer_page->Add(title_media, 0, wxTOP| wxEXPAND, FromDIP(20));
    sizer_page->Add(item_auto_stop_liveview, 0, wxEXPAND, FromDIP(3));

#ifdef _WIN32
    sizer_page->Add(title_darkmode, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_darkmode, 0, wxEXPAND, FromDIP(3));
#endif

#if 0
    sizer_page->Add(title_filament_group, 0, wxTOP | wxEXPAND, FromDIP(20));
    //sizer_page->Add(item_ignore_ext_filament, 0, wxEXPAND, FromDIP(3));
    sizer_page->Add(item_pop_up_filament_map_dialog, 0, wxEXPAND, FromDIP(3));
#endif

    sizer_page->Add(title_user_experience, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_priv_policy, 0, wxTOP, FromDIP(3));

    sizer_page->Add(title_develop_mode, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_develop_mode, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_skip_ams_blacklist_check, 0, wxTOP, FromDIP(3));

    page->SetSizer(sizer_page);
    page->Layout();
    sizer_page->Fit(page);
    return page;
}

void PreferencesDialog::create_gui_page()
{
    auto page = new wxWindow(this, wxID_ANY);
    wxBoxSizer *sizer_page = new wxBoxSizer(wxVERTICAL);

    auto title_index_and_tip = create_item_title(_L("Home page and daily tips"), page, _L("Home page and daily tips"));
    auto item_home_page      = create_item_checkbox(_L("Show home page on startup"), page, _L("Show home page on startup"), 50, "show_home_page");
    //auto item_daily_tip      = create_item_checkbox(_L("Show daily tip on startup"), page, _L("Show daily tip on startup"), 50, "show_daily_tips");

    sizer_page->Add(title_index_and_tip, 0, wxTOP, 26);
    sizer_page->Add(item_home_page, 0, wxTOP, 6);
    //sizer_page->Add(item_daily_tip, 0, wxTOP, 6);

    page->SetSizer(sizer_page);
    page->Layout();
    sizer_page->Fit(page);
}

void PreferencesDialog::create_sync_page()
{
    auto page = new wxWindow(this, wxID_ANY);
    wxBoxSizer *sizer_page = new wxBoxSizer(wxVERTICAL);

     auto title_sync_settingy   = create_item_title(_L("Sync settings"), page, _L("Sync settings"));
    auto item_user_sync        = create_item_checkbox(_L("User sync"), page, _L("User sync"), 50, "user_sync_switch");
    auto item_preset_sync      = create_item_checkbox(_L("Preset sync"), page, _L("Preset sync"), 50, "preset_sync_switch");
    auto item_preferences_sync = create_item_checkbox(_L("Preferences sync"), page, _L("Preferences sync"), 50, "preferences_sync_switch");

    sizer_page->Add(title_sync_settingy, 0, wxTOP, 26);
    sizer_page->Add(item_user_sync, 0, wxTOP, 6);
    sizer_page->Add(item_preset_sync, 0, wxTOP, 6);
    sizer_page->Add(item_preferences_sync, 0, wxTOP, 6);

    page->SetSizer(sizer_page);
    page->Layout();
    sizer_page->Fit(page);
}

void PreferencesDialog::create_shortcuts_page()
{
    auto page = new wxWindow(this, wxID_ANY);
    wxBoxSizer *sizer_page = new wxBoxSizer(wxVERTICAL);

    auto title_view_control = create_item_title(_L("View control settings"), page, _L("View control settings"));
    std::vector<wxString> keyboard_supported;
    Split(app_config->get("keyboard_supported"), "/", keyboard_supported);

    std::vector<wxString> mouse_supported;
    Split(app_config->get("mouse_supported"), "/", mouse_supported);

    auto item_rotate_view = create_item_multiple_combobox(_L("Rotate of view"), page, _L("Rotate of view"), 10, "rotate_view", keyboard_supported,
                                                               mouse_supported);
    auto item_move_view   = create_item_multiple_combobox(_L("Move of view"), page, _L("Move of view"), 10, "move_view", keyboard_supported, mouse_supported);
    auto item_zoom_view   = create_item_multiple_combobox(_L("Zoom of view"), page, _L("Zoom of view"), 10, "rotate_view", keyboard_supported, mouse_supported);

    auto title_other = create_item_title(_L("Other"), page, _L("Other"));
    auto item_other  = create_item_checkbox(_L("Mouse wheel reverses when zooming"), page, _L("Mouse wheel reverses when zooming"), 50, "mouse_wheel");

    sizer_page->Add(title_view_control, 0, wxTOP, 26);
    sizer_page->Add(item_rotate_view, 0, wxTOP, 8);
    sizer_page->Add(item_move_view, 0, wxTOP, 8);
    sizer_page->Add(item_zoom_view, 0, wxTOP, 8);
    // sizer_page->Add(item_precise_control, 0, wxTOP, 0);
    sizer_page->Add(title_other, 0, wxTOP, 20);
    sizer_page->Add(item_other, 0, wxTOP, 5);

    page->SetSizer(sizer_page);
    page->Layout();
    sizer_page->Fit(page);
}

wxWindow* PreferencesDialog::create_debug_page()
{
    auto page = new wxWindow(m_scrolledWindow, wxID_ANY);
    page->SetBackgroundColour(*wxWHITE);

    m_internal_developer_mode_def = app_config->get("internal_developer_mode");
    m_backup_interval_def = app_config->get("backup_interval");
    m_iot_environment_def = app_config->get("iot_environment");

    wxBoxSizer *bSizer = new wxBoxSizer(wxVERTICAL);


    auto enable_ssl_for_mqtt = create_item_checkbox(_L("Enable SSL(MQTT)"), page, _L("Enable SSL(MQTT)"), 50, "enable_ssl_for_mqtt");
    auto enable_ssl_for_ftp = create_item_checkbox(_L("Enable SSL(FTP)"), page, _L("Enable SSL(MQTT)"), 50, "enable_ssl_for_ftp");
    auto item_internal_developer = create_item_checkbox(_L("Internal developer mode"), page, _L("Internal developer mode"), 50, "internal_developer_mode");

    auto title_log_level = create_item_title(_L("Log Level"), page, _L("Log Level"));
    auto log_level_list  = std::vector<wxString>{_L("fatal"), _L("error"), _L("warning"), _L("info"), _L("debug"), _L("trace")};
    auto loglevel_combox = create_item_loglevel_combobox(_L("Log Level"), page, _L("Log Level"), log_level_list);

    auto title_host = create_item_title(_L("Host Setting"), page, _L("Host Setting"));
    auto radio1     = create_item_radiobox(_L("DEV host: api-dev.bambu-lab.com/v1"), page, wxEmptyString, 50, 1, "dev_host");
    auto radio2     = create_item_radiobox(_L("QA  host: api-qa.bambu-lab.com/v1"), page, wxEmptyString, 50, 1, "qa_host");
    auto radio3     = create_item_radiobox(_L("PRE host: api-pre.bambu-lab.com/v1"), page, wxEmptyString, 50, 1, "pre_host");
    auto radio4     = create_item_radiobox(_L("Product host"), page, wxEmptyString, 50, 1, "product_host");

    if (m_iot_environment_def == ENV_DEV_HOST) {
        on_select_radio("dev_host");
    } else if (m_iot_environment_def == ENV_QAT_HOST) {
        on_select_radio("qa_host");
    } else if (m_iot_environment_def == ENV_PRE_HOST) {
        on_select_radio("pre_host");
    } else if (m_iot_environment_def == ENV_PRODUCT_HOST) {
        on_select_radio("product_host");
    }


    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
        std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));
    StateColor btn_bd_white(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    Button* debug_button = new Button(page, _L("debug save button"));
    debug_button->SetBackgroundColor(btn_bg_white);
    debug_button->SetBorderColor(btn_bd_white);
    debug_button->SetFont(Label::Body_13);


    debug_button->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        // success message box
        MessageDialog dialog(this, _L("save debug settings"), _L("DEBUG settings have saved successfully!"), wxNO_DEFAULT | wxYES_NO | wxICON_INFORMATION);
        dialog.SetSize(400,-1);
        switch (dialog.ShowModal()) {
        case wxID_NO: {
            //if (m_developer_mode_def != app_config->get("developer_mode")) {
            //    app_config->set_bool("developer_mode", m_developer_mode_def == "true" ? true : false);
            //    m_developer_mode_ckeckbox->SetValue(m_developer_mode_def == "true" ? true : false);
            //}
            //if (m_internal_developer_mode_def != app_config->get("internal_developer_mode")) {
            //    app_config->set_bool("internal_developer_mode", m_internal_developer_mode_def == "true" ? true : false);
            //    m_internal_developer_mode_ckeckbox->SetValue(m_internal_developer_mode_def == "true" ? true : false);
            //}

            if (m_backup_interval_def != m_backup_interval_time) { m_backup_interval_textinput->GetTextCtrl()->SetValue(m_backup_interval_def); }

            if (m_iot_environment_def == ENV_DEV_HOST) {
                on_select_radio("dev_host");
            } else if (m_iot_environment_def == ENV_QAT_HOST) {
                on_select_radio("qa_host");
            } else if (m_iot_environment_def == ENV_PRE_HOST) {
                on_select_radio("pre_host");
            } else if (m_iot_environment_def == ENV_PRODUCT_HOST) {
                on_select_radio("product_host");
            }

            break;
        }

        case wxID_YES: {
            // bbs  domain changed
            auto param = get_select_radio(1);

            std::map<wxString, wxString> iot_environment_map;
            iot_environment_map["dev_host"] = ENV_DEV_HOST;
            iot_environment_map["qa_host"]  = ENV_QAT_HOST;
            iot_environment_map["pre_host"] = ENV_PRE_HOST;
            iot_environment_map["product_host"] = ENV_PRODUCT_HOST;

            //if (iot_environment_map[param] != m_iot_environment_def) {
            if (true) {
                NetworkAgent* agent = wxGetApp().getAgent();
                if (param == "dev_host") {
                    app_config->set("iot_environment", ENV_DEV_HOST);
                }
                else if (param == "qa_host") {
                    app_config->set("iot_environment", ENV_QAT_HOST);
                }
                else if (param == "pre_host") {
                    app_config->set("iot_environment", ENV_PRE_HOST);
                }
                else if (param == "product_host") {
                    app_config->set("iot_environment", ENV_PRODUCT_HOST);
                }


                wxGetApp().update_publish_status();

                AppConfig* config = GUI::wxGetApp().app_config;
                std::string country_code = config->get_country_code();
                if (agent) {
                    wxGetApp().request_user_logout();
                    agent->set_country_code(country_code);
                }
                ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, _L("Warning"), ConfirmBeforeSendDialog::ButtonStyle::ONLY_CONFIRM);
                confirm_dlg.update_text(_L("Switch cloud environment, Please login again!"));
                confirm_dlg.on_show();
            }

            // bbs  backup
            //app_config->set("backup_interval", std::string(m_backup_interval_time.mb_str()));
            app_config->save();
            Slic3r::set_backup_interval(boost::lexical_cast<long>(app_config->get("backup_interval")));

            this->Close();
            break;
        }
        }
    });


    bSizer->Add(enable_ssl_for_mqtt, 0, wxTOP, FromDIP(3));
    bSizer->Add(enable_ssl_for_ftp, 0, wxTOP, FromDIP(3));
    bSizer->Add(item_internal_developer, 0, wxTOP, FromDIP(3));
    bSizer->Add(title_log_level, 0, wxTOP| wxEXPAND, FromDIP(20));
    bSizer->Add(loglevel_combox, 0, wxTOP, FromDIP(3));
    bSizer->Add(title_host, 0, wxTOP| wxEXPAND, FromDIP(20));
    bSizer->Add(radio1, 0, wxEXPAND | wxTOP, FromDIP(3));
    bSizer->Add(radio2, 0, wxEXPAND | wxTOP, FromDIP(3));
    bSizer->Add(radio3, 0, wxEXPAND | wxTOP, FromDIP(3));
    bSizer->Add(radio4, 0, wxEXPAND | wxTOP, FromDIP(3));
    bSizer->Add(debug_button, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(15));

    page->SetSizer(bSizer);
    page->Layout();
    bSizer->Fit(page);
    return page;
}

void PreferencesDialog::on_select_radio(std::string param)
{
    RadioSelectorList::Node *node    = m_radio_group.GetFirst();
    auto                     groupid = 0;

    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_param_name == param) groupid = rs->m_groupid;
        node = node->GetNext();
    }

    node = m_radio_group.GetFirst();
    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_groupid == groupid && rs->m_param_name == param) rs->m_radiobox->SetValue(true);
        if (rs->m_groupid == groupid && rs->m_param_name != param) rs->m_radiobox->SetValue(false);
        node = node->GetNext();
    }
}

wxString PreferencesDialog::get_select_radio(int groupid)
{
    RadioSelectorList::Node *node = m_radio_group.GetFirst();
    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_groupid == groupid && rs->m_radiobox->GetValue()) { return rs->m_param_name; }
        node = node->GetNext();
    }

    return wxEmptyString;
}

void PreferencesDialog::OnSelectRadio(wxMouseEvent &event)
{
    RadioSelectorList::Node *node    = m_radio_group.GetFirst();
    auto                     groupid = 0;

    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_radiobox->GetId() == event.GetId()) groupid = rs->m_groupid;
        node = node->GetNext();
    }

    node = m_radio_group.GetFirst();
    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_groupid == groupid && rs->m_radiobox->GetId() == event.GetId()) rs->m_radiobox->SetValue(true);
        if (rs->m_groupid == groupid && rs->m_radiobox->GetId() != event.GetId()) rs->m_radiobox->SetValue(false);
        node = node->GetNext();
    }
}


}} // namespace Slic3r::GUI
