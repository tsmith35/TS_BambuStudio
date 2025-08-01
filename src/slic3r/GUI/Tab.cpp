// #include "libslic3r/GCodeSender.hpp"
//#include "slic3r/Utils/Serial.hpp"
#include "Tab.hpp"
#include "PresetHints.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

#include "Search.hpp"
#include "OG_CustomCtrl.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "wxExtensions.hpp"
#include "PresetComboBoxes.hpp"
#include <wx/wupdlock.h>

#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "SavePresetDialog.hpp"
#include "MsgDialog.hpp"
#include "Notebook.hpp"

#include "Widgets/Label.hpp"
#include "Widgets/TabCtrl.hpp"
#include "MarkdownTip.hpp"
#include "Search.hpp"
#include "BedShapeDialog.hpp"

#ifdef WIN32
	#include <commctrl.h>
#endif // WIN32

namespace Slic3r {

t_config_option_keys deep_diff(const ConfigBase &config_this, const ConfigBase &config_other, bool strict = true);

namespace GUI {

#define DISABLE_UNDO_SYS

static const std::vector<std::string> plate_keys = { "curr_bed_type", "first_layer_print_sequence", "first_layer_sequence_choice", "other_layers_print_sequence", "other_layers_sequence_choice", "print_sequence", "spiral_mode"};

static std::pair<std::string, std::string> extruder_variant_keys[]{
    {}, {"print_extruder_id", "print_extruder_variant"},     // Preset::TYPE_PRINT
    {}, {"", "filament_extruder_variant"},                   // Preset::TYPE_FILAMENT filament don't use id anymore
    {}, {"printer_extruder_id", "printer_extruder_variant"}, // Preset::TYPE_PRINTER
};

void Tab::Highlighter::set_timer_owner(wxEvtHandler* owner, int timerid/* = wxID_ANY*/)
{
    m_timer.SetOwner(owner, timerid);
}

void Tab::Highlighter::init(std::pair<OG_CustomCtrl*, bool*> params)
{
    if (m_timer.IsRunning())
        invalidate();
    if (!params.first || !params.second)
        return;

    m_timer.Start(300, false);

    m_custom_ctrl = params.first;
    m_show_blink_ptr = params.second;

    *m_show_blink_ptr = true;
    m_custom_ctrl->Refresh();
}

void Tab::Highlighter::invalidate()
{
    m_timer.Stop();

    if (m_custom_ctrl && m_show_blink_ptr) {
        *m_show_blink_ptr = false;
        m_custom_ctrl->Refresh();
        m_show_blink_ptr = nullptr;
        m_custom_ctrl = nullptr;
    }

    m_blink_counter = 0;
}

void Tab::Highlighter::blink()
{
    if (m_custom_ctrl && m_show_blink_ptr) {
        *m_show_blink_ptr = !*m_show_blink_ptr;
        m_custom_ctrl->Refresh();
    }
    else
        return;

    if ((++m_blink_counter) == 11)
        invalidate();
}

//BBS: GUI refactor
Tab::Tab(ParamsPanel* parent, const wxString& title, Preset::Type type) :
    m_parent(parent), m_title(title), m_type(type)
{
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL/*, name*/);
    this->SetFont(Slic3r::GUI::wxGetApp().normal_font());

    wxGetApp().UpdateDarkUI(this);
    SetBackgroundColour(*wxWHITE);

    m_compatible_printers.type			= Preset::TYPE_PRINTER;
    m_compatible_printers.key_list		= "compatible_printers";
    m_compatible_printers.key_condition	= "compatible_printers_condition";
    //m_compatible_printers.dialog_title  = _L("Compatible printers");
    //m_compatible_printers.dialog_label  = _L("Select the printers this profile is compatible with.");

    m_compatible_prints.type			= Preset::TYPE_PRINT;
    m_compatible_prints.key_list 		= "compatible_prints";
    m_compatible_prints.key_condition	= "compatible_prints_condition";
    //m_compatible_prints.dialog_title 	= _L("Compatible print profiles");
    //m_compatible_prints.dialog_label 	= _L("Select the print profiles this profile is compatible with.");

    wxGetApp().tabs_list.push_back(this);

    m_em_unit = em_unit(m_parent); //wxGetApp().em_unit();

    m_config_manipulation = get_config_manipulation();

    Bind(wxEVT_SIZE, ([](wxSizeEvent &evt) {
        //for (auto page : m_pages)
        //    if (! page.get()->IsShown())
        //        page->layout_valid = false;
        evt.Skip();
    }));

    m_highlighter.set_timer_owner(this, 0);
    this->Bind(wxEVT_TIMER, [this](wxTimerEvent&)
    {
        m_highlighter.blink();
    });
}

void Tab::set_type()
{
    if (m_name == PRESET_PRINT_NAME)              { m_type = Slic3r::Preset::TYPE_PRINT; }
    else if (m_name == "sla_print")     { m_type = Slic3r::Preset::TYPE_SLA_PRINT; }
    else if (m_name == PRESET_FILAMENT_NAME)      { m_type = Slic3r::Preset::TYPE_FILAMENT; }
    else if (m_name == "sla_material")  { m_type = Slic3r::Preset::TYPE_SLA_MATERIAL; }
    else if (m_name == PRESET_PRINTER_NAME)       { m_type = Slic3r::Preset::TYPE_PRINTER; }
    else                                { m_type = Slic3r::Preset::TYPE_INVALID; assert(false); }
}

// sub new
//BBS: GUI refactor, change tab to fit into ParamsPanel
void Tab::create_preset_tab()
{
//move to ParamsPanel
/*#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__*/
    auto panel = this;

    m_preset_bundle = wxGetApp().preset_bundle;

    // Vertical sizer to hold the choice menu and the rest of the page.
/*#ifdef __WXOSX__
    auto  *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->SetSizeHints(this);
    this->SetSizer(main_sizer);

    // Create additional panel to Fit() it from OnActivate()
    // It's needed for tooltip showing on OSX
    m_tmp_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    auto panel = m_tmp_panel;
    auto  sizer = new wxBoxSizer(wxVERTICAL);
    m_tmp_panel->SetSizer(sizer);
    m_tmp_panel->Layout();

    main_sizer->Add(m_tmp_panel, 1, wxEXPAND | wxALL, 0);
#else
    Tab *panel = this;
    auto  *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->SetSizeHints(panel);
    panel->SetSizer(sizer);
#endif //__WXOSX__*/

    // BBS: model config
    if (m_type < Preset::TYPE_COUNT) {
        // preset chooser
        m_presets_choice = new TabPresetComboBox(panel, m_type);
        // m_presets_choice->SetFont(Label::Body_10); // BBS
        m_presets_choice->set_selection_changed_function([this](int selection) {
            if (!m_presets_choice->selection_is_changed_according_to_physical_printers())
            {
                if (m_type == Preset::TYPE_PRINTER && !m_presets_choice->is_selected_physical_printer())
                    m_preset_bundle->physical_printers.unselect_printer();

                // select preset
                std::string preset_name = m_presets_choice->GetString(selection).ToUTF8().data();
                select_preset(Preset::remove_suffix_modified(preset_name));
            }
        });
    }

    auto color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);

    //buttons
    m_scaled_buttons.reserve(6);
    m_scaled_bitmaps.reserve(4);

    m_top_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    // BBS: open this tab by select first
    m_top_panel->SetBackgroundColour(*wxWHITE);
    m_top_panel->Bind(wxEVT_LEFT_UP, [this](auto & e) {
        restore_last_select_item();
    });

    //add_scaled_button(panel, &m_btn_compare_preset, "compare");
    add_scaled_button(m_top_panel, &m_btn_save_preset, "save");
    add_scaled_button(m_top_panel, &m_btn_delete_preset, "cross");
    //if (m_type == Preset::Type::TYPE_PRINTER)
    //    add_scaled_button(panel, &m_btn_edit_ph_printer, "cog");

    m_show_incompatible_presets = false;
    add_scaled_bitmap(this, m_bmp_show_incompatible_presets, "flag_red");
    add_scaled_bitmap(this, m_bmp_hide_incompatible_presets, "flag_green");

    //add_scaled_button(panel, &m_btn_hide_incompatible_presets, m_bmp_hide_incompatible_presets.name());

    //m_btn_compare_preset->SetToolTip(_L("Compare presets"));
    // TRN "Save current Settings"
    m_btn_save_preset->SetToolTip(wxString::Format(_L("Save current %s"), m_title));
    m_btn_delete_preset->SetToolTip(_(L("Delete this preset")));
    m_btn_delete_preset->Hide();

    /*add_scaled_button(panel, &m_question_btn, "question");
    m_question_btn->SetToolTip(_(L("Hover the cursor over buttons to find more information \n"
                                   "or click this button.")));

    add_scaled_button(panel, &m_search_btn, "search");
    m_search_btn->SetToolTip(format_wxstr(_L("Search in settings [%1%]"), "Ctrl+F"));*/

    // Bitmaps to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
    add_scaled_bitmap(this, m_bmp_value_lock  , "unlock_normal");
    add_scaled_bitmap(this, m_bmp_value_unlock, "lock_normal");
    m_bmp_non_system = &m_bmp_white_bullet;
    // Bitmaps to be shown on the "Undo user changes" button next to each input field.
    add_scaled_bitmap(this, m_bmp_value_revert, "undo");
    add_scaled_bitmap(this, m_bmp_white_bullet, "dot");

    set_tooltips_text();

    add_scaled_button(m_top_panel, &m_undo_btn,        m_bmp_white_bullet.name());
    add_scaled_button(m_top_panel, &m_undo_to_sys_btn, m_bmp_white_bullet.name());
    add_scaled_button(m_top_panel, &m_btn_search,      "search");
    m_btn_search->SetToolTip(_L("Search in preset"));

    //search input
    m_search_item = new StaticBox(m_top_panel);
    StateColor box_colour(std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    StateColor box_border_colour(std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal));

    m_search_item->SetBackgroundColor(box_colour);
    m_search_item->SetBorderColor(box_border_colour);
    m_search_item->SetCornerRadius(5);


    //StateColor::darkModeColorFor(wxColour(238, 238, 238)), wxDefaultPosition, wxSize(m_top_panel->GetSize().GetWidth(), 3 * wxGetApp().em_unit()), 8);
    auto search_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_search_input = new TextInput(m_search_item, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 | wxBORDER_NONE);
    m_search_input->SetBackgroundColour(wxColour(238, 238, 238));
    m_search_input->SetForegroundColour(wxColour(43, 52, 54));
    m_search_input->SetFont(wxGetApp().bold_font());

    search_sizer->Add(new wxWindow(m_search_item, wxID_ANY, wxDefaultPosition, wxSize(0, 0)), 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(6));
    search_sizer->Add(m_search_input, 1, wxEXPAND | wxALL, FromDIP(2));
    //bbl for linux
    //search_sizer->Add(new wxWindow(m_search_input, wxID_ANY, wxDefaultPosition, wxSize(0, 0)), 0, wxEXPAND | wxLEFT, 16);


     m_search_item->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        m_search_input->SetFocus();
    });

    m_search_input->Bind(wxCUSTOMEVT_EXIT_SEARCH, [this](wxCommandEvent &) {
         Freeze();
        if (m_presets_choice) m_presets_choice->Show();

        m_btn_save_preset->Show();
        m_btn_search->Show();
        m_search_item->Hide();

        m_search_item->Refresh();
        m_search_item->Update();
        m_search_item->Layout();

        this->GetParent()->Refresh();
        this->GetParent()->Update();
        this->GetParent()->Layout();
        Thaw();
    });

    m_search_item->SetSizer(search_sizer);
    m_search_item->Layout();
    search_sizer->Fit(m_search_item);

    m_search_item->Hide();
    //m_btn_search->SetId(wxID_FIND_PROCESS);

    m_btn_search->Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent &) {
         Freeze();
         if (m_presets_choice)
             m_presets_choice->Hide();

         m_btn_save_preset->Hide();
         m_btn_search->Hide();
         m_search_item->Show();

         this->GetParent()->Refresh();
         this->GetParent()->Update();
         this->GetParent()->Layout();

         wxGetApp().plater()->search(false, m_type, m_top_panel->GetParent(), m_search_input, m_btn_search);
         Thaw();

        });

    m_undo_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent) { on_roll_back_value(); }));
    m_undo_to_sys_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent) { on_roll_back_value(true); }));
    /* m_search_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent) { wxGetApp().plater()->search(false); });*/

    // Colors for ui "decoration"
    m_sys_label_clr			= wxGetApp().get_label_clr_sys();
    m_modified_label_clr	= wxGetApp().get_label_clr_modified();
    m_default_text_clr      = wxGetApp().get_label_clr_default();

    m_main_sizer = new wxBoxSizer( wxVERTICAL );
    m_top_sizer = new wxBoxSizer( wxHORIZONTAL );

    m_top_sizer->Add(m_undo_btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    // BBS: model config
    if (m_presets_choice) {
        m_presets_choice->Reparent(m_top_panel);
        m_top_sizer->Add(m_presets_choice, 1, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    } else {
        m_top_sizer->AddSpacer(FromDIP(10));
        m_top_sizer->AddStretchSpacer(1);
    }

    const float scale_factor = /*wxGetApp().*/em_unit(this)*0.1;// GetContentScaleFactor();
#ifndef DISABLE_UNDO_SYS
    m_top_sizer->Add( m_undo_to_sys_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_top_sizer->AddSpacer(8);
#endif
    m_top_sizer->Add( m_btn_save_preset, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
    m_top_sizer->Add( m_btn_delete_preset, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12) );
    m_top_sizer->Add( m_btn_search, 0, wxALIGN_CENTER_VERTICAL | wxLEFT , FromDIP(12) );
    m_top_sizer->Add(m_search_item, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, FromDIP(12));

    if (dynamic_cast<TabPrint*>(this) == nullptr) {
        m_static_title = new Label(m_top_panel, Label::Body_12, _L("Advance"));
        m_static_title->Wrap( -1 );
        // BBS: open this tab by select first
        m_static_title->Bind(wxEVT_LEFT_UP, [this](auto& e) {
            restore_last_select_item();
        });
        m_top_sizer->Add( m_static_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8 );
        m_mode_view = new SwitchButton(m_top_panel, wxID_ABOUT);
        m_top_sizer->AddSpacer(4);
        m_top_sizer->Add( m_mode_view, 0, wxALIGN_CENTER_VERTICAL);
    }

    m_top_sizer->AddSpacer(FromDIP(16));

    m_top_sizer->SetMinSize(-1, 3 * m_em_unit);
    m_top_panel->SetSizer(m_top_sizer);
    if (m_presets_choice)
        m_main_sizer->Add(m_top_panel, 0, wxEXPAND | wxUP | wxDOWN, m_em_unit);
    else
        m_top_panel->Hide();

#if 0
#ifdef _MSW_DARK_MODE
    // Sizer with buttons for mode changing
    if (wxGetApp().tabs_as_menu())
#endif
        m_mode_sizer = new ModeSizer(panel, int (0.5*em_unit(this)));

    const float scale_factor = /*wxGetApp().*/em_unit(this)*0.1;// GetContentScaleFactor();
    m_hsizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_hsizer, 0, wxEXPAND | wxBOTTOM, 3);
    m_hsizer->Add(m_presets_choice, 0, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
    m_hsizer->AddSpacer(int(4*scale_factor));
    m_hsizer->Add(m_btn_save_preset, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(4 * scale_factor));
    m_hsizer->Add(m_btn_delete_preset, 0, wxALIGN_CENTER_VERTICAL);
    if (m_btn_edit_ph_printer) {
        m_hsizer->AddSpacer(int(4 * scale_factor));
        m_hsizer->Add(m_btn_edit_ph_printer, 0, wxALIGN_CENTER_VERTICAL);
    }
    m_hsizer->AddSpacer(int(/*16*/8 * scale_factor));
    m_hsizer->Add(m_btn_hide_incompatible_presets, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(8 * scale_factor));
    m_hsizer->Add(m_question_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(32 * scale_factor));
    m_hsizer->Add(m_undo_to_sys_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->Add(m_undo_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(32 * scale_factor));
    m_hsizer->Add(m_search_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(8*scale_factor));
    m_hsizer->Add(m_btn_compare_preset, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(16*scale_factor));
    // m_hsizer->AddStretchSpacer(32);
    // StretchSpacer has a strange behavior under OSX, so
    // There is used just additional sizer for m_mode_sizer with right alignment
    if (m_mode_sizer) {
        auto mode_sizer = new wxBoxSizer(wxVERTICAL);
        // Don't set the 2nd parameter to 1, making the sizer rubbery scalable in Y axis may lead
        // to wrong vertical size assigned to wxBitmapComboBoxes, see GH issue #7176.
        mode_sizer->Add(m_mode_sizer, 0, wxALIGN_RIGHT);
        m_hsizer->Add(mode_sizer, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, wxOSX ? 15 : 10);
    }

    //Horizontal sizer to hold the tree and the selected page.
    m_hsizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_hsizer, 1, wxEXPAND, 0);

    //left vertical sizer
    m_left_sizer = new wxBoxSizer(wxVERTICAL);
    m_hsizer->Add(m_left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);
#endif
    // tree
    m_tabctrl = new TabCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(20 * m_em_unit, -1),
        wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_NONE | wxWANTS_CHARS | wxTR_FULL_ROW_HIGHLIGHT);
    m_tabctrl->Bind(wxEVT_RIGHT_DOWN, [this](auto &e) {}); // disable right select
    m_tabctrl->SetFont(Label::Body_14);
    //m_left_sizer->Add(m_tabctrl, 1, wxEXPAND);
    const int img_sz = int(32 * scale_factor + 0.5f);
    m_icons = new wxImageList(img_sz, img_sz, false, 1);
    // Index of the last icon inserted into $self->{icons}.
    m_icon_count = -1;
    m_tabctrl->AssignImageList(m_icons);
    wxGetApp().UpdateDarkUI(m_tabctrl);

    // Delay processing of the following handler until the message queue is flushed.
    // This helps to process all the cursor key events on Windows in the tree control,
    // so that the cursor jumps to the last item.
    // BBS: bold selection
    m_tabctrl->Bind(wxEVT_TAB_SEL_CHANGING, [this](wxCommandEvent& event) {
        if (m_disable_tree_sel_changed_event)
            return;
        const auto sel_item = m_tabctrl->GetSelection();
        //OutputDebugStringA("wxEVT_TAB_SEL_CHANGING ");
        //OutputDebugStringA(m_title.c_str());
        //const auto selection = sel_item >= 0 ? m_tabctrl->GetItemText(sel_item) : "";
        //OutputDebugString(selection);
        //OutputDebugStringA("\n");
        m_tabctrl->SetItemBold(sel_item, false);
        });
    m_tabctrl->Bind(wxEVT_TAB_SEL_CHANGED, [this](wxCommandEvent& event) {
#ifdef __linux__
        // Events queue is opposite On Linux. wxEVT_SET_FOCUS invokes after wxEVT_TAB_SEL_CHANGED,
        // and a result wxEVT_KILL_FOCUS doesn't invoke for the TextCtrls.
        // So, call SetFocus explicitly for this control before changing of the selection
        m_tabctrl->SetFocus();
#endif
            if (!m_disable_tree_sel_changed_event && !m_pages.empty()) {
                if (m_page_switch_running)
                    m_page_switch_planned = true;
                else {
                    m_page_switch_running = true;
                    do {
                        m_page_switch_planned = false;
                        m_tabctrl->Update();
                    } while (this->tree_sel_change_delayed(event));
                    m_page_switch_running = false;
                }
            }
        });

    m_tabctrl->Bind(wxEVT_KEY_DOWN, &Tab::OnKeyDown, this);

    m_main_sizer->Add(m_tabctrl, 0, wxEXPAND | wxALL, 0 );

    if (dynamic_cast<TabPrinter *>(this) || dynamic_cast<TabPrint *>(this)) {
        m_extruder_switch = new SwitchButton(panel);
        m_extruder_switch->SetMaxSize({em_unit(this) * 24, -1});
        m_extruder_switch->SetLabels(_L("Left"), _L("Right"));
        m_extruder_switch->Bind(wxEVT_TOGGLEBUTTON, [this] (auto & evt) {
            evt.Skip();
           switch_excluder(evt.GetInt());
        });
        m_extruder_sync = new ScalableButton(panel, wxID_ANY, "extruder_sync");
        m_extruder_sync->SetToolTip(_L("Synchronize the modification of parameters to the corresponding parameters of another extruder."));
        m_extruder_sync->Bind(wxEVT_BUTTON, [this](auto &evt) {
            evt.Skip();
            sync_excluder();
        });
        m_variant_sizer  = new wxBoxSizer(wxHORIZONTAL);
        auto right_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_variant_sizer->AddStretchSpacer(1);
        m_variant_sizer->Add(m_extruder_switch, 0, wxALIGN_CENTER, 0);
        m_variant_sizer->Add(right_sizer, 1, wxALIGN_CENTER);
        right_sizer->AddStretchSpacer(1);
        right_sizer->Add(m_extruder_sync, 0, wxALIGN_CENTER | wxRIGHT, m_em_unit);
        m_main_sizer->Add(m_variant_sizer, 0, wxEXPAND | wxTOP, m_em_unit);
    } else if (dynamic_cast<TabFilament *>(this)) {
        m_variant_combo = new ComboBox(panel, wxID_ANY, "", wxDefaultPosition, {20 * m_em_unit, -1}, 0, {}, wxCB_READONLY);
        m_variant_combo->Bind(wxEVT_COMBOBOX, [this](auto &evt) {
            evt.Skip();
            switch_excluder(evt.GetInt());
        });
        m_variant_sizer  = new wxBoxSizer(wxHORIZONTAL);
        m_variant_sizer->Add(m_variant_combo, 0, wxLEFT, m_em_unit);
        m_main_sizer->Add(m_variant_sizer, 0, wxEXPAND | wxTOP, m_em_unit);
    }

    this->SetSizer(m_main_sizer);
    //this->Layout();
    m_page_view = m_parent->get_paged_view();

    // Initialize the page.
/*#ifdef __WXOSX__
    auto page_parent = m_tmp_panel;
#else
    auto page_parent = this;
#endif

    m_page_view = new wxScrolledWindow(page_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_page_sizer = new wxBoxSizer(wxVERTICAL);
    m_page_view->SetSizer(m_page_sizer);
    m_page_view->SetScrollbars(1, 20, 1, 2);
    m_hsizer->Add(m_page_view, 1, wxEXPAND | wxLEFT, 5);*/

    //m_btn_compare_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { compare_preset(); }));
    m_btn_save_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { save_preset(); }));
    m_btn_delete_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { delete_preset(); }));
    /*m_btn_hide_incompatible_presets->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) {
        toggle_show_hide_incompatible();
    }));

    if (m_btn_edit_ph_printer)
        m_btn_edit_ph_printer->Bind(wxEVT_BUTTON, [this](wxCommandEvent e) {
            if (m_preset_bundle->physical_printers.has_selection())
                m_presets_choice->edit_physical_printer();
            else
                m_presets_choice->add_physical_printer();
        });*/

    // Initialize the DynamicPrintConfig by default keys/values.
    build();

    // ys_FIXME: Following should not be needed, the function will be called later
    // (update_mode->update_visibility->rebuild_page_tree). This does not work, during the
    // second call of rebuild_page_tree m_tabctrl->GetFirstVisibleItem(); returns zero
    // for some unknown reason (and the page is not refreshed until user does a selection).
    rebuild_page_tree();

    m_completed = true;
}

void Tab::add_scaled_button(wxWindow* parent,
                            ScalableButton** btn,
                            const std::string& icon_name,
                            const wxString& label/* = wxEmptyString*/,
                            long style /*= wxBU_EXACTFIT | wxNO_BORDER*/)
{
    *btn = new ScalableButton(parent, wxID_ANY, icon_name, label, wxDefaultSize, wxDefaultPosition, style, true);
    (*btn)->SetBackgroundColour(parent->GetBackgroundColour());
    m_scaled_buttons.push_back(*btn);
}

void Tab::add_scaled_bitmap(wxWindow* parent,
                            ScalableBitmap& bmp,
                            const std::string& icon_name)
{
    bmp = ScalableBitmap(parent, icon_name);
    m_scaled_bitmaps.push_back(&bmp);
}

void Tab::load_initial_data()
{
    m_config = &m_presets->get_edited_preset().config;
    bool has_parent = m_presets->get_selected_preset_parent() != nullptr;
    m_bmp_non_system = has_parent ? &m_bmp_value_unlock : &m_bmp_white_bullet;
    m_ttg_non_system = has_parent ? &m_ttg_value_unlock : &m_ttg_white_bullet_ns;
    m_tt_non_system  = has_parent ? &m_tt_value_unlock  : &m_ttg_white_bullet_ns;
}

Slic3r::GUI::PageShp Tab::add_options_page(const wxString& title, const std::string& icon, bool is_extruder_pages /*= false*/)
{
    // Index of icon in an icon list $self->{icons}.
    auto icon_idx = 0;
    if (!icon.empty()) {
        icon_idx = (m_icon_index.find(icon) == m_icon_index.end()) ? -1 : m_icon_index.at(icon);
        if (icon_idx == -1) {
            // Add a new icon to the icon list.
            m_scaled_icons_list.push_back(ScalableBitmap(this, icon, 32, false, true));
            //m_icons->Add(m_scaled_icons_list.back().bmp());
            icon_idx = ++m_icon_count;
            m_icon_index[icon] = icon_idx;
        }

        if (m_category_icon.find(title) == m_category_icon.end()) {
            // Add new category to the category_to_icon list.
            m_category_icon[title] = icon;
        }
    }
    // Initialize the page.
    //BBS: GUI refactor
    PageShp page(new Page(m_page_view, title, icon_idx, this));
//	page->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#ifdef __WINDOWS__
//	page->SetDoubleBuffered(true);
#endif //__WINDOWS__

    if (dynamic_cast<TabPrint*>(this)) {
        page->m_split_multi_line = true;
        page->m_option_label_at_right = true;
    }

    if (!is_extruder_pages)
        m_pages.push_back(page);

    page->set_config(m_config);
    return page;
}

// Names of categories is save in English always. We translate them only for UI.
// But category "Extruder n" can't be translated regularly (using _()), so
// just for this category we should splite the title and translate "Extruder" word separately
wxString Tab::translate_category(const wxString& title, Preset::Type preset_type)
{
    if (preset_type == Preset::TYPE_PRINTER && title.Contains("Extruder ")) {
        if (title == "Extruder 1") return _("Left Extruder");
        if (title == "Extruder 2") return _("Right Extruder");
        return _("Extruder") + title.SubString(8, title.Last());
    }
    return _(title);
}

void Tab::OnActivate()
{
    //BBS: GUI refactor
    //noUpdates seems not working
    //wxWindowUpdateLocker noUpdates(this);
/*#ifdef __WXOSX__
//    wxWindowUpdateLocker noUpdates(this);
    auto size = GetSizer()->GetSize();
    m_tmp_panel->GetSizer()->SetMinSize(size.x + m_size_move, size.y);
    Fit();
    m_size_move *= -1;
#endif // __WXOSX__*/

#ifdef __WXMSW__
    // Workaround for tooltips over Tree Controls displayed over excessively long
    // tree control items, stealing the window focus.
    //
    // In case the Tab was reparented from the MainFrame to the floating dialog,
    // the tooltip created by the Tree Control before reparenting is not reparented,
    // but it still points to the MainFrame. If the tooltip pops up, the MainFrame
    // is incorrectly focussed, stealing focus from the floating dialog.
    //
    // The workaround is to delete the tooltip control.
    // Vojtech tried to reparent the tooltip control, but it did not work,
    // and if the Tab was later reparented back to MainFrame, the tooltip was displayed
    // at an incorrect position, therefore it is safer to just discard the tooltip control
    // altogether.
    HWND hwnd_tt = TreeView_GetToolTips(m_tabctrl->GetHandle());
    if (hwnd_tt) {
	    HWND hwnd_toplevel 	= find_toplevel_parent(m_tabctrl)->GetHandle();
	    HWND hwnd_parent 	= ::GetParent(hwnd_tt);
	    if (hwnd_parent != hwnd_toplevel) {
	    	::DestroyWindow(hwnd_tt);
			TreeView_SetToolTips(m_tabctrl->GetHandle(), nullptr);
	    }
    }
#endif

    // BBS: select on first active
    if (!m_active_page)
        restore_last_select_item();

    //BBS: GUI refactor
    m_page_view->Freeze();

    // create controls on active page
    activate_selected_page([](){});
    //BBS: GUI refactor
    //m_main_sizer->Layout();
    m_parent->Layout();

#ifdef _MSW_DARK_MODE
    // Because of DarkMode we use our own Notebook (inherited from wxSiplebook) instead of wxNotebook
    // And it looks like first Layout of the page doesn't update a size of the m_presets_choice
    // So we have to set correct size explicitely
   /* if (wxSize ok_sz = wxSize(35 * m_em_unit, m_presets_choice->GetBestSize().y);
        ok_sz != m_presets_choice->GetSize()) {
        m_presets_choice->SetMinSize(ok_sz);
        m_presets_choice->SetSize(ok_sz);
        GetSizer()->GetItem(size_t(0))->GetSizer()->Layout();
        if (wxGetApp().tabs_as_menu())
            m_presets_choice->update();
    }*/
#endif // _MSW_DARK_MODE
    Refresh();

    //BBS: GUI refactor
    m_page_view->Thaw();
}

void Tab::update_label_colours()
{
    m_default_text_clr = wxGetApp().get_label_clr_default();
    if (m_sys_label_clr == wxGetApp().get_label_clr_sys() && m_modified_label_clr == wxGetApp().get_label_clr_modified())
        return;
    m_sys_label_clr = wxGetApp().get_label_clr_sys();
    m_modified_label_clr = wxGetApp().get_label_clr_modified();

    //update options "decoration"
    for (const auto& opt : m_options_list)
    {
        const wxColour *color = &m_sys_label_clr;

        // value isn't equal to system value
        if ((opt.second & osSystemValue) == 0) {
            // value is equal to last saved
            if ((opt.second & osInitValue) != 0)
                color = &m_default_text_clr;
            // value is modified
            else
                color = &m_modified_label_clr;
        }
        if (opt.first == "printable_area"            ||
            opt.first == "compatible_prints"    || opt.first == "compatible_printers"           ) {
            if (m_colored_Label_colors.find(opt.first) != m_colored_Label_colors.end())
                m_colored_Label_colors.at(opt.first) = *color;
            continue;
        }

        Field* field = get_field(opt.first);
        if (field == nullptr) continue;
        field->set_label_colour(color);
    }

    auto cur_item = m_tabctrl->GetFirstVisibleItem();
    if (cur_item < 0 || !m_tabctrl->IsVisible(cur_item))
        return;
    while (cur_item >= 0) {
        auto title = m_tabctrl->GetItemText(cur_item);
        for (auto page : m_pages)
        {
            if (translate_category(page->title(), m_type) != title)
                continue;

            const wxColor *clr = !page->m_is_nonsys_values ? &m_sys_label_clr :
                page->m_is_modified_values ? &m_modified_label_clr :
                (m_type < Preset::TYPE_COUNT ? &m_default_text_clr : &m_modified_label_clr);

            m_tabctrl->SetItemTextColour(cur_item, clr == &m_modified_label_clr ? *clr : StateColor(
                        std::make_pair(0x6B6B6C, (int) StateColor::NotChecked),
                        std::make_pair(*clr, (int) StateColor::Normal)));
            break;
        }
        cur_item = m_tabctrl->GetNextVisible(cur_item);
    }

    decorate();
}

void Tab::decorate()
{
    for (const auto& opt : m_options_list)
    {
        Field*      field = nullptr;
        wxColour*   colored_label_clr = nullptr;

        if (opt.first == "printable_area" ||
            opt.first == "compatible_prints" || opt.first == "compatible_printers")
            colored_label_clr = (m_colored_Label_colors.find(opt.first) == m_colored_Label_colors.end()) ? nullptr : &m_colored_Label_colors.at(opt.first);

        if (!colored_label_clr) {
            field = get_field(opt.first);
            if (!field)
                continue;
        }

        bool is_nonsys_value = false;
        bool is_modified_value = true;
        const ScalableBitmap* sys_icon  = &m_bmp_value_lock;
        const ScalableBitmap* icon      = &m_bmp_value_revert;

        const wxColour* color = m_is_default_preset ? &m_default_text_clr : &m_sys_label_clr;

        const wxString* sys_tt  = &m_tt_value_lock;
        const wxString* tt      = &m_tt_value_revert;

        // value isn't equal to system value
        if ((opt.second & osSystemValue) == 0) {
            is_nonsys_value = true;
            sys_icon = m_bmp_non_system;
            sys_tt = m_tt_non_system;
            // value is equal to last saved
            if ((opt.second & osInitValue) != 0)
                color = &m_default_text_clr;
            // value is modified
            else
                color = &m_modified_label_clr;
        }
        if ((opt.second & osInitValue) != 0)
        {
            is_modified_value = false;
            icon = &m_bmp_white_bullet;
            tt = &m_tt_white_bullet;
        }

        if (colored_label_clr) {
            *colored_label_clr = *color;
            continue;
        }

        field->m_is_nonsys_value = is_nonsys_value;
        field->m_is_modified_value = is_modified_value;
        field->set_undo_bitmap(icon);
        //BBS: GUI refactor
        field->set_undo_to_sys_bitmap(sys_icon);
        field->set_undo_tooltip(tt);
        field->set_undo_to_sys_tooltip(sys_tt);
        field->set_label_colour(color);
    }

    if (m_active_page)
        m_active_page->refresh();
}

void Tab::filter_diff_option(std::vector<std::string> &options)
{
    for (auto &opt : options) {
        auto n = opt.find_last_of('#');
        if (n == std::string::npos) continue;
        bool found = false;
        for (auto page : m_pages) {
            if (auto iter = page->m_opt_id_map.find(opt); iter != page->m_opt_id_map.end()) {
                opt = iter->second;
                found = true;
                break;
            }
        }
        if (!found) opt = opt.substr(0, n);
    }
    options.erase(std::remove(options.begin(), options.end(), ""), options.end());
}

// Update UI according to changes
void Tab::update_changed_ui()
{
    if (m_postpone_update_ui)
        return;

    const bool deep_compare   = (m_type == Preset::TYPE_PRINTER || m_type == Preset::TYPE_PRINT || m_type == Preset::TYPE_FILAMENT
            || m_type == Preset::TYPE_SLA_MATERIAL || m_type == Preset::TYPE_MODEL);
    auto dirty_options = m_presets->current_dirty_options(deep_compare);
    auto nonsys_options = m_presets->current_different_from_parent_options(deep_compare);
    if (m_type == Preset::TYPE_PRINTER && static_cast<TabPrinter*>(this)->m_printer_technology == ptFFF) {
        TabPrinter* tab = static_cast<TabPrinter*>(this);
        if (tab->m_initial_extruders_count != tab->m_extruders_count)
            dirty_options.emplace_back("extruders_count");
        if (tab->m_sys_extruders_count != tab->m_extruders_count)
            nonsys_options.emplace_back("extruders_count");
    }

    update_custom_dirty(dirty_options, nonsys_options);

    filter_diff_option(dirty_options);
    filter_diff_option(nonsys_options);

    for (auto& it : m_options_list)
        it.second = m_opt_status_value;

    for (auto opt_key : dirty_options) {
        auto iter = m_options_list.find(opt_key);
        if (iter != m_options_list.end())
            iter->second &= ~osInitValue;
    }
    for (auto opt_key : nonsys_options) {
        auto iter = m_options_list.find(opt_key);
        if (iter != m_options_list.end())
            iter->second &= ~osSystemValue;
    }

    decorate();

    wxTheApp->CallAfter([this]() {
        if (parent()) //To avoid a crash, parent should be exist for a moment of a tree updating
            update_changed_tree_ui();
    });
    // BBS:
    update_undo_buttons();
}

template<class T>
void add_correct_opts_to_options_list(const std::string &opt_key, std::map<std::string, int>& map, Tab *tab, const int& value)
{
    map.emplace(opt_key + "#0", value);
}

void Tab::init_options_list()
{
    if (!m_options_list.empty())
        m_options_list.clear();

    for (const std::string& opt_key : m_config->keys())
    {
        if (opt_key == "printable_area" || opt_key == "bed_exclude_area" || opt_key == "compatible_prints" || opt_key == "compatible_printers" || opt_key == "thumbnail_size") {
            m_options_list.emplace(opt_key, m_opt_status_value);
            continue;
        }
        if (m_config->option(opt_key)->is_vector())
            m_options_list.emplace(opt_key + "#0", m_opt_status_value);
        else
            m_options_list.emplace(opt_key, m_opt_status_value);
    }
}

void TabPrinter::init_options_list()
{
    Tab::init_options_list();
    if (m_printer_technology == ptFFF)
        m_options_list.emplace("extruders_count", m_opt_status_value);
    for (size_t i = 1; i < m_extruders_count; ++i) {
        auto extruder_page = m_pages[3 + i];
        for (auto group : extruder_page->m_optgroups) {
            for (auto & opt : group->opt_map())
                m_options_list.emplace(opt.first, m_opt_status_value);
        }
    }
}

void TabPrinter::msw_rescale()
{
    Tab::msw_rescale();

    if (m_reset_to_filament_color)
        m_reset_to_filament_color->msw_rescale();

    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();
}

void TabFilament::init_options_list()
{
    if (!m_options_list.empty())
        m_options_list.clear();

    for (const std::string &opt_key : m_config->keys()) {
        if (filament_options_with_variant.find(opt_key) == filament_options_with_variant.end())
            m_options_list.emplace(opt_key, m_opt_status_value);
        else
            m_options_list.emplace(opt_key + "#0", m_opt_status_value);
    }
}

void Tab::get_sys_and_mod_flags(const std::string& opt_key, bool& sys_page, bool& modified_page)
{
    auto opt = m_options_list.find(opt_key);
    if (opt == m_options_list.end())
        return;

    if (sys_page) sys_page = (opt->second & osSystemValue) != 0;
    modified_page |= (opt->second & osInitValue) == 0;
}

void Tab::update_changed_tree_ui()
{
    if (m_options_list.empty()) {
        if (m_type == Preset::Type::TYPE_PLATE) {
            for (auto page : m_pages) {
                page->m_is_nonsys_values = false;
            }
        }
        return;
    }
    auto cur_item = m_tabctrl->GetFirstVisibleItem();
    if (cur_item < 0 || !m_tabctrl->IsVisible(cur_item))
        return;

    auto selected_item = m_tabctrl->GetSelection();
    auto selection = selected_item >= 0 ? m_tabctrl->GetItemText(selected_item) : "";

    while (cur_item >= 0) {
        auto title = m_tabctrl->GetItemText(cur_item);
        for (auto page : m_pages)
        {
            if (translate_category(page->title(), m_type) != title)
                continue;
            bool sys_page = true;
            bool modified_page = false;
            if (page->title() == "General") {
                std::initializer_list<const char*> optional_keys{ "extruders_count", "printable_area" };
                for (auto &opt_key : optional_keys) {
                    get_sys_and_mod_flags(opt_key, sys_page, modified_page);
                }
            }
            if (page->title() == "Dependencies") {
                if (m_type == Slic3r::Preset::TYPE_PRINTER) {
                    sys_page = m_presets->get_selected_preset_parent() != nullptr;
                    modified_page = false;
                } else {
                    if (m_type == Slic3r::Preset::TYPE_FILAMENT || m_type == Slic3r::Preset::TYPE_SLA_MATERIAL)
                        get_sys_and_mod_flags("compatible_prints", sys_page, modified_page);
                    get_sys_and_mod_flags("compatible_printers", sys_page, modified_page);
                }
            }
            for (auto group : page->m_optgroups)
            {
                if (!sys_page && modified_page)
                    break;
                for (const auto &kvp : group->opt_map()) {
                    const std::string &opt_key = kvp.first;
                    get_sys_and_mod_flags(opt_key, sys_page, modified_page);
                }
            }

            const wxColor *clr = sys_page ? (m_is_default_preset ? &m_default_text_clr : &m_sys_label_clr) :
                                 (modified_page || m_type >= Preset::TYPE_COUNT) ? &m_modified_label_clr : &m_default_text_clr;

            if (page->set_item_colour(clr))
                m_tabctrl->SetItemTextColour(cur_item, clr == &m_modified_label_clr ? *clr : StateColor(
                        std::make_pair(0x6B6B6C, (int) StateColor::NotChecked),
                        std::make_pair(*clr, (int) StateColor::Normal)));

            page->m_is_nonsys_values = !sys_page;
            page->m_is_modified_values = modified_page;

            if (selection == title) {
                m_is_nonsys_values = page->m_is_nonsys_values;
                m_is_modified_values = page->m_is_modified_values;
            }
            break;
        }
        auto next_item = m_tabctrl->GetNextVisible(cur_item);
        cur_item = next_item;
    }
}

void Tab::update_undo_buttons()
{
    // BBS: restore all pages in preset
    m_undo_btn->        SetBitmap_(m_presets->get_edited_preset().is_dirty ? m_bmp_value_revert: m_bmp_white_bullet);
    m_undo_to_sys_btn-> SetBitmap_(m_is_nonsys_values   ? *m_bmp_non_system : m_bmp_value_lock);

    m_undo_btn->SetToolTip(m_presets->get_edited_preset().is_dirty ? _L("Click to reset all settings to the last saved preset.") : m_ttg_white_bullet);
    m_undo_to_sys_btn->SetToolTip(m_is_nonsys_values ? *m_ttg_non_system : m_ttg_value_lock);
}

void Tab::on_roll_back_value(const bool to_sys /*= true*/)
{
    // BBS: restore all pages in preset
    // if (!m_active_page) return;

    int os;
    if (to_sys)	{
        if (!m_is_nonsys_values) return;
        os = osSystemValue;
    }
    else {
        // BBS: restore all pages in preset
        if (!m_presets->get_edited_preset().is_dirty) return;
        os = osInitValue;
    }

    m_postpone_update_ui = true;

    // BBS: restore all preset
    for (auto page : m_pages)
    for (auto group : page->m_optgroups) {
        if (group->title == "Capabilities") {
            if ((m_options_list["extruders_count"] & os) == 0)
                to_sys ? group->back_to_sys_value("extruders_count") : group->back_to_initial_value("extruders_count");
        }
        if (group->title == "Size and coordinates") {
            if ((m_options_list["printable_area"] & os) == 0) {
                to_sys ? group->back_to_sys_value("printable_area") : group->back_to_initial_value("printable_area");
                load_key_value("printable_area", true/*some value*/, true);
            }
        }
        //if (group->title == "Profile dependencies") {
        //    // "compatible_printers" option doesn't exists in Printer Settimgs Tab
        //    if (m_type != Preset::TYPE_PRINTER && (m_options_list["compatible_printers"] & os) == 0) {
        //        to_sys ? group->back_to_sys_value("compatible_printers") : group->back_to_initial_value("compatible_printers");
        //        load_key_value("compatible_printers", true/*some value*/, true);

        //        bool is_empty = m_config->option<ConfigOptionStrings>("compatible_printers")->values.empty();
        //        m_compatible_printers.checkbox->SetValue(is_empty);
        //        is_empty ? m_compatible_printers.btn->Disable() : m_compatible_printers.btn->Enable();
        //    }
        //    // "compatible_prints" option exists only in Filament Settimgs and Materials Tabs
        //    if ((m_type == Preset::TYPE_FILAMENT || m_type == Preset::TYPE_SLA_MATERIAL) && (m_options_list["compatible_prints"] & os) == 0) {
        //        to_sys ? group->back_to_sys_value("compatible_prints") : group->back_to_initial_value("compatible_prints");
        //        load_key_value("compatible_prints", true/*some value*/, true);

        //        bool is_empty = m_config->option<ConfigOptionStrings>("compatible_prints")->values.empty();
        //        m_compatible_prints.checkbox->SetValue(is_empty);
        //        is_empty ? m_compatible_prints.btn->Disable() : m_compatible_prints.btn->Enable();
        //    }
        //}
        for (const auto &kvp : group->opt_map()) {
            const std::string &opt_key = kvp.first;
            auto iter = m_options_list.find(opt_key);
            if (iter != m_options_list.end() && (iter->second & os) == 0)
                to_sys ? group->back_to_sys_value(opt_key) : group->back_to_initial_value(opt_key);
        }
    }

    // BBS: restore all pages in preset
    m_presets->discard_current_changes();

    m_postpone_update_ui = false;

    // When all values are rolled, then we hane to update whole tab in respect to the reverted values
    update();

    // BBS: restore all pages in preset, update_dirty also update combobox
    update_dirty();
}

// Update the combo box label of the selected preset based on its "dirty" state,
// comparing the selected preset config with $self->{config}.
void Tab::update_dirty()
{
    if (m_postpone_update_ui)
        return;

    if (m_presets_choice) {
        m_presets_choice->update_dirty();
        on_presets_changed();
    } else {
        m_presets->update_dirty();
    }
    update_changed_ui();
}

void Tab::update_tab_ui(bool update_plater_presets)
{
    if (m_presets_choice) {
        m_presets_choice->update();
        if (update_plater_presets)
            on_presets_changed();
    }
}

// Load a provied DynamicConfig into the tab, modifying the active preset.
// This could be used for example by setting a Wipe Tower position by interactive manipulation in the 3D view.
void Tab::load_config(const DynamicPrintConfig& config)
{
    bool modified = 0;
    for(auto opt_key : m_config->diff(config)) {
        m_config->set_key_value(opt_key, config.option(opt_key)->clone());
        modified = 1;
    }
    if (modified) {
        update_dirty();
        //# Initialize UI components with the config values.
        reload_config();
        update();
    }
}

// Reload current $self->{config} (aka $self->{presets}->edited_preset->config) into the UI fields.
void Tab::reload_config()
{
    if (m_active_page)
        m_active_page->reload_config();
}

void Tab::update_mode()
{
    m_mode = wxGetApp().get_mode();

    //BBS: GUI refactor
    // update mode for ModeSizer
    //if (m_mode_sizer)
    //    m_mode_sizer->SetMode(m_mode);

    update_visibility();

    update_changed_tree_ui();
}

void Tab::update_visibility()
{
    Freeze(); // There is needed Freeze/Thaw to avoid a flashing after Show/Layout

    for (auto page : m_pages)
        page->update_visibility(m_mode, page.get() == m_active_page);
    rebuild_page_tree();

    if (m_type == Preset::TYPE_SLA_PRINT)
        update_description_lines();

    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();
    Thaw();
}

void Tab::msw_rescale()
{
    m_em_unit = em_unit(m_parent);

    m_top_sizer->SetMinSize(-1, 3 * m_em_unit);

    //BBS: GUI refactor
    //if (m_mode_sizer)
    //    m_mode_sizer->msw_rescale();
    if (m_presets_choice)
        m_presets_choice->msw_rescale();

    m_tabctrl->SetMinSize(wxSize(20 * m_em_unit, -1));

    // rescale buttons and cached bitmaps
    for (const auto btn : m_scaled_buttons)
        btn->msw_rescale();
    for (const auto bmp : m_scaled_bitmaps)
        bmp->msw_rescale();

    if (m_mode_view)
        m_mode_view->Rescale();
    if (m_extruder_switch)
        m_extruder_switch->Rescale();
    if (m_variant_combo)
        m_variant_combo->Rescale();

    if (m_detach_preset_btn)
        m_detach_preset_btn->msw_rescale();

    // rescale icons for tree_ctrl
    for (ScalableBitmap& bmp : m_scaled_icons_list)
        bmp.msw_rescale();
    // recreate and set new ImageList for tree_ctrl
    m_icons->RemoveAll();
    m_icons = new wxImageList(m_scaled_icons_list.front().bmp().GetWidth(), m_scaled_icons_list.front().bmp().GetHeight(), false);
    for (ScalableBitmap& bmp : m_scaled_icons_list)
        //m_icons->Add(bmp.bmp());
    m_tabctrl->AssignImageList(m_icons);

    // rescale options_groups
    if (m_active_page)
        m_active_page->msw_rescale();

    m_tabctrl->Rescale();

    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();
}

void Tab::sys_color_changed()
{
    if (m_presets_choice)
        m_presets_choice->sys_color_changed();

    // update buttons and cached bitmaps
    for (const auto btn : m_scaled_buttons)
        btn->msw_rescale();
    for (const auto bmp : m_scaled_bitmaps)
        bmp->msw_rescale();
    if (m_detach_preset_btn)
        m_detach_preset_btn->msw_rescale();
    if (m_extruder_sync)
        m_extruder_sync->msw_rescale();

    // update icons for tree_ctrl
    for (ScalableBitmap& bmp : m_scaled_icons_list)
        bmp.msw_rescale();
    // recreate and set new ImageList for tree_ctrl
    m_icons->RemoveAll();
    m_icons = new wxImageList(m_scaled_icons_list.front().bmp().GetWidth(), m_scaled_icons_list.front().bmp().GetHeight(), false);
    for (ScalableBitmap& bmp : m_scaled_icons_list)
        //m_icons->Add(bmp.bmp());
    m_tabctrl->AssignImageList(m_icons);

    // Colors for ui "decoration"
    update_label_colours();
#ifdef _WIN32
    wxWindowUpdateLocker noUpdates(this);
    //BBS: GUI refactor
    //if (m_mode_sizer)
    //    m_mode_sizer->msw_rescale();
    wxGetApp().UpdateDarkUI(this);
    wxGetApp().UpdateDarkUI(m_tabctrl);
#endif
    update_changed_tree_ui();

    // update options_groups
    if (m_active_page)
        m_active_page->sys_color_changed();
    if (m_extruder_switch)
        m_extruder_switch->Rescale();

    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();
}

Field* Tab::get_field(const t_config_option_key& opt_key, int opt_index/* = -1*/) const
{
    return m_active_page ? m_active_page->get_field(opt_key, opt_index) : nullptr;
}

std::pair<OG_CustomCtrl*, bool*> Tab::get_custom_ctrl_with_blinking_ptr(const t_config_option_key& opt_key, int opt_index/* = -1*/)
{
    if (!m_active_page)
        return {nullptr, nullptr};

    std::pair<OG_CustomCtrl*, bool*> ret = {nullptr, nullptr};

    for (auto opt_group : m_active_page->m_optgroups) {
        ret = opt_group->get_custom_ctrl_with_blinking_ptr(opt_key, opt_index);
        if (ret.first && ret.second)
            break;
    }
    return ret;
}

Field* Tab::get_field(const t_config_option_key& opt_key, Page** selected_page, int opt_index/* = -1*/)
{
    Field* field = nullptr;
    for (auto page : m_pages) {
        field = page->get_field(opt_key, opt_index);
        if (field != nullptr) {
            *selected_page = page.get();
            return field;
        }
    }
    return field;
}

void Tab::toggle_option(const std::string& opt_key, bool toggle, int opt_index/* = -1*/)
{
    if (!m_active_page)
        return;
    Field *field = m_active_page->get_field(opt_key, opt_index);
    if (field)
        field->toggle(toggle);
}

void Tab::toggle_line(const std::string &opt_key, bool toggle, int opt_index)
{
    if (!m_active_page) return;
    Line *line = m_active_page->get_line(opt_key, opt_index);
    if (line) line->toggle_visible = toggle;
};

// To be called by custom widgets, load a value into a config,
// update the preset selection boxes (the dirty flags)
// If value is saved before calling this function, put saved_value = true,
// and value can be some random value because in this case it will not been used
void Tab::load_key_value(const std::string& opt_key, const boost::any& value, bool saved_value /*= false*/)
{
    if (!saved_value) change_opt_value(*m_config, opt_key, value);
    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    if (opt_key == "compatible_printers" || opt_key == "compatible_prints") {
        // Don't select another profile if this profile happens to become incompatible.
        m_preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
    }
    if (m_presets_choice)
        m_presets_choice->update_dirty();
    on_presets_changed();
    update();
}

static wxString support_combo_value_for_config(const DynamicPrintConfig &config, bool is_fff)
{
    const std::string support         = is_fff ? "enable_support"                 : "supports_enable";
    const std::string buildplate_only = is_fff ? "support_on_build_plate_only" : "support_buildplate_only";

    // BBS
#if 0
    return
        ! config.opt_bool(support) ?
            _("None") :
            (is_fff && !config.opt_bool("support_material_auto")) ?
                _("For support enforcers only") :
                (config.opt_bool(buildplate_only) ? _("Support on build plate only") :
                                                    _("Everywhere"));
#else
    if (config.opt_bool(support)) {
         return (config.opt_bool(buildplate_only) ? _("Support on build plate only") : _("Everywhere"));
    } else {
        return _("For support enforcers only");
    }
#endif
}

static wxString pad_combo_value_for_config(const DynamicPrintConfig &config)
{
    return config.opt_bool("pad_enable") ? (config.opt_bool("pad_around_object") ? _("Around object") : _("Below object")) : _("None");
}

void Tab::on_value_change(const std::string& opt_key, const boost::any& value)
{
    if (wxGetApp().plater() == nullptr) {
        return;
    }

    const bool is_fff = supports_printer_technology(ptFFF);
    ConfigOptionsGroup* og_freq_chng_params = wxGetApp().sidebar().og_freq_chng_params(is_fff);
    //BBS: GUI refactor
    if (og_freq_chng_params) {
        if (opt_key == "sparse_infill_density" || opt_key == "pad_enable")
        {
            boost::any val = og_freq_chng_params->get_config_value(*m_config, opt_key);
            og_freq_chng_params->set_value(opt_key, val);
        }

        if (opt_key == "pad_around_object") {
            for (PageShp& pg : m_pages) {
                Field* fld = pg->get_field(opt_key); /// !!! ysFIXME ????
                if (fld) fld->set_value(value, false);
            }
        }

        if (is_fff ?
            (opt_key == "enable_support" || opt_key == "support_type" || opt_key == "support_on_build_plate_only") :
            (opt_key == "supports_enable" || opt_key == "support_buildplate_only"))
            og_freq_chng_params->set_value("support", support_combo_value_for_config(*m_config, is_fff));

        if (!is_fff && (opt_key == "pad_enable" || opt_key == "pad_around_object"))
            og_freq_chng_params->set_value("pad", pad_combo_value_for_config(*m_config));

        if (opt_key == "brim_width")
        {
            bool val = m_config->opt_float("brim_width") > 0.0 ? true : false;
            og_freq_chng_params->set_value("brim", val);
        }
    }

    if (opt_key == "sparse_infill_density") {
        ConfigOptionPercent density  = *m_config->option<ConfigOptionPercent>("sparse_infill_density");
        DynamicPrintConfig new_conf = *m_config;
        new_conf.set_key_value("skeleton_infill_density", new ConfigOptionPercent(density));
        new_conf.set_key_value("skin_infill_density", new ConfigOptionPercent(density));
        m_config_manipulation.apply(m_config, &new_conf);
    }

    if (opt_key == "sparse_infill_line_width") {
        ConfigOptionFloat  line_width  = *m_config->option<ConfigOptionFloat>("sparse_infill_line_width");
        DynamicPrintConfig  new_conf = *m_config;
        new_conf.set_key_value("skin_infill_line_width", new ConfigOptionFloat(line_width));
        new_conf.set_key_value("skeleton_infill_line_width", new ConfigOptionFloat(line_width));
        m_config_manipulation.apply(m_config, &new_conf);
    }

    if (opt_key == "single_extruder_multi_material" || opt_key == "extruders_count" )
        update_wiping_button_visibility();

    if (opt_key == "enable_prime_tower") {
        auto timelapse_type = m_config->option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
        bool timelapse_enabled = timelapse_type->value == TimelapseType::tlSmooth;
        if (!boost::any_cast<bool>(value) && timelapse_enabled) {
            MessageDialog dlg(wxGetApp().plater(), _L("Prime tower is required for smooth timeplase. There may be flaws on the model without prime tower. Are you sure you want to disable prime tower?"),
                              _L("Warning"), wxICON_WARNING | wxYES | wxNO);
            if (dlg.ShowModal() == wxID_NO) {
                DynamicPrintConfig new_conf = *m_config;
                new_conf.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
                m_config_manipulation.apply(m_config, &new_conf);
            }
            wxGetApp().plater()->update();
        }
        bool is_precise_z_height = m_config->option<ConfigOptionBool>("precise_z_height")->value;
        if (boost::any_cast<bool>(value) && is_precise_z_height) {
            MessageDialog dlg(wxGetApp().plater(), _L("Enabling both precise Z height and the prime tower may cause the size of prime tower to increase. Do you still want to enable?"),
                _L("Warning"), wxICON_WARNING | wxYES | wxNO);
            if (dlg.ShowModal() == wxID_NO) {
                DynamicPrintConfig new_conf = *m_config;
                new_conf.set_key_value("enable_prime_tower", new ConfigOptionBool(false));
                m_config_manipulation.apply(m_config, &new_conf);
            }
            wxGetApp().plater()->update();
        }
        update_wiping_button_visibility();
    }

    if (opt_key == "precise_z_height") {
        bool wipe_tower_enabled = m_config->option<ConfigOptionBool>("enable_prime_tower")->value;
        if (boost::any_cast<bool>(value) && wipe_tower_enabled) {
            MessageDialog dlg(wxGetApp().plater(), _L("Enabling both precise Z height and the prime tower may cause the size of prime tower to increase. Do you still want to enable?"),
                _L("Warning"), wxICON_WARNING | wxYES | wxNO);
            if (dlg.ShowModal() == wxID_NO) {
                DynamicPrintConfig new_conf = *m_config;
                new_conf.set_key_value("precise_z_height", new ConfigOptionBool(false));
                m_config_manipulation.apply(m_config, &new_conf);
            }
            wxGetApp().plater()->update();
        }
    }

    // reload scene to update timelapse wipe tower
    if (opt_key == "timelapse_type") {
        bool wipe_tower_enabled = m_config->option<ConfigOptionBool>("enable_prime_tower")->value;
        if (!wipe_tower_enabled && boost::any_cast<int>(value) == (int)TimelapseType::tlSmooth) {
            MessageDialog dlg(wxGetApp().plater(), _L("Prime tower is required for smooth timelapse. There may be flaws on the model without prime tower. Do you want to enable prime tower?"),
                              _L("Warning"), wxICON_WARNING | wxYES | wxNO);
            if (dlg.ShowModal() == wxID_YES) {
                DynamicPrintConfig new_conf = *m_config;
                new_conf.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
                m_config_manipulation.apply(m_config, &new_conf);
                wxGetApp().plater()->update();
            }
        } else {
            wxGetApp().plater()->update();
        }
    }

    if (opt_key == "print_sequence" && m_config->opt_enum<PrintSequence>("print_sequence") == PrintSequence::ByObject) {
        auto printer_structure_opt = m_preset_bundle->printers.get_edited_preset().config.option<ConfigOptionEnum<PrinterStructure>>("printer_structure");
        if (printer_structure_opt && printer_structure_opt->value == PrinterStructure::psI3) {
            wxString msg_text = _(L("The current printer does not support timelapse in Traditional Mode when printing By-Object."));
            msg_text += "\n\n" + _(L("Still print by object?"));

            MessageDialog dialog(wxGetApp().plater(), msg_text, "", wxICON_WARNING | wxYES | wxNO);
            auto          answer = dialog.ShowModal();
            if (answer == wxID_NO) {
                DynamicPrintConfig new_conf = *m_config;
                new_conf.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByLayer));
                m_config_manipulation.apply(m_config, &new_conf);
                wxGetApp().plater()->update();
            }
        }
    }

    // BBS set support style to default when support type changes
    if (opt_key == "support_type") {
        DynamicPrintConfig new_conf = *m_config;
        new_conf.set_key_value("support_style", new ConfigOptionEnum<SupportMaterialStyle>(smsDefault));
        m_config_manipulation.apply(m_config, &new_conf);
    }

    if (opt_key == "support_filament") {
        int filament_id           = m_config->opt_int("support_filament") - 1; // the displayed id is based from 1, while internal id is based from 0
        int interface_filament_id = m_config->opt_int("support_interface_filament") - 1;
        auto           &filament_presets      = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
        auto           &filaments             = Slic3r::GUI::wxGetApp().preset_bundle->filaments;
        bool            support_TPU           = false;
        if (filament_id >= 0 && filament_id < filament_presets.size()) {
            Slic3r::Preset *filament      = filaments.find_preset(filament_presets[filament_id]);
            if (filament) {
                std::string filament_type = filament->config.option<ConfigOptionStrings>("filament_type")->values[0];
                support_TPU               = filament_type == "PLA" && has_filaments({"TPU", "TPU-AMS"});
            }
        }
        if (is_support_filament(filament_id) && !is_soluble_filament(filament_id) && !has_filaments({"TPU", "TPU-AMS"})) {
            wxString           msg_text = _L("Non-soluble support materials are not recommended for support base. \n"
                                                       "Are you sure to use them for support base? \n");
            MessageDialog      dialog(wxGetApp().plater(), msg_text, "", wxICON_WARNING | wxYES | wxNO);
            DynamicPrintConfig new_conf = *m_config;
            if (dialog.ShowModal() == wxID_NO) {
                new_conf.set_key_value("support_filament", new ConfigOptionInt(0));
                m_config_manipulation.apply(m_config, &new_conf);
                on_value_change(opt_key, 0);
            }
            wxGetApp().plater()->update();
        }
        if ((is_soluble_filament(filament_id) || support_TPU) &&
            !(m_config->opt_float("support_top_z_distance") == 0 && m_config->opt_float("support_interface_spacing") == 0 &&
              m_config->opt_float("support_object_xy_distance") == 0 &&
              m_config->opt_enum<SupportMaterialInterfacePattern>("support_interface_pattern") == SupportMaterialInterfacePattern::smipRectilinearInterlaced &&
              filament_id == interface_filament_id)) {
            wxString msg_text;
            if (support_TPU)
                msg_text = _L("When using PLA to support TPU, We recommend the following settings:\n"
                              "0 top z distance, 0 interface spacing, 0 support/object xy distance, interlaced rectilinear pattern, disable \n"
                              "independent support layer height and use PLA for both support interface and support base");
            else
                msg_text = _L("When using soluble material for the support, We recommend the following settings:\n"
                                   "0 top z distance, 0 interface spacing, 0 support/object xy distance, interlaced rectilinear pattern, disable \n"
                                   "independent support layer height and use soluble materials for both support interface and support base");
            msg_text += "\n\n" + _L("Change these settings automatically? \n"
                                    "Yes - Change these settings automatically\n"
                                    "No  - Do not change these settings for me");
            MessageDialog      dialog(wxGetApp().plater(), msg_text, "Suggestion", wxICON_WARNING | wxYES | wxNO);
            DynamicPrintConfig new_conf = *m_config;
            if (dialog.ShowModal() == wxID_YES) {
                new_conf.set_key_value("support_top_z_distance", new ConfigOptionFloat(0));
                new_conf.set_key_value("support_interface_spacing", new ConfigOptionFloat(0));
                new_conf.set_key_value("support_object_xy_distance", new ConfigOptionFloat(0));
                new_conf.set_key_value("support_interface_pattern",
                                       new ConfigOptionEnum<SupportMaterialInterfacePattern>(SupportMaterialInterfacePattern::smipRectilinearInterlaced));
                new_conf.set_key_value("independent_support_layer_height", new ConfigOptionBool(false));
                new_conf.set_key_value("support_interface_filament", new ConfigOptionInt(filament_id + 1));
                m_config_manipulation.apply(m_config, &new_conf);
            }
            wxGetApp().plater()->update();
        }
    }

    // BBS popup a message to ask the user to set optimum parameters for support interface if support materials are used
    if (opt_key == "support_interface_filament") {
        int filament_id           = m_config->opt_int("support_filament") - 1;
        int interface_filament_id = m_config->opt_int("support_interface_filament") - 1; // the displayed id is based from 1, while internal id is based from 0
        auto           &filament_presets      = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
        auto           &filaments             = Slic3r::GUI::wxGetApp().preset_bundle->filaments;
        bool            support_TPU = false;
        if (interface_filament_id >= 0 && interface_filament_id < filament_presets.size()) {
            Slic3r::Preset *filament      = filaments.find_preset(filament_presets[interface_filament_id]);
            if (filament) {
                std::string filament_type = filament->config.option<ConfigOptionStrings>("filament_type")->values[0];
                support_TPU               = filament_type == "PLA" && has_filaments({"TPU", "TPU-AMS"});
            }
        }

        if ((is_support_filament(interface_filament_id) &&
             !(m_config->opt_float("support_top_z_distance") == 0 && m_config->opt_float("support_interface_spacing") == 0 &&
               m_config->opt_enum<SupportMaterialInterfacePattern>("support_interface_pattern") == SupportMaterialInterfacePattern::smipRectilinearInterlaced &&
               (support_TPU ? m_config->opt_float("support_object_xy_distance") == 0 : -1))) ||
            (is_soluble_filament(interface_filament_id) && !is_soluble_filament(filament_id))) {
            wxString msg_text;
            if (support_TPU) {
                msg_text = _L("When using PLA to support TPU, We recommend the following settings:\n"
                              "0 top z distance, 0 interface spacing, 0 support/object xy distance, interlaced rectilinear pattern, disable \n"
                              "independent support layer height and use PLA for both support interface and support base");
            }
            else if (!is_soluble_filament(interface_filament_id)) {
                msg_text = _L("When using support material for the support interface, We recommend the following settings:\n"
                              "0 top z distance, 0 interface spacing, interlaced rectilinear pattern and disable independent support layer height");
            }else {
                msg_text = _L("When using soluble material for the support interface, We recommend the following settings:\n"
                              "0 top z distance, 0 interface spacing, 0 support/object xy distance, interlaced rectilinear pattern, disable \n"
                              "independent support layer height and use soluble materials for both support interface and support base");
            }
            msg_text += "\n\n" + _L("Change these settings automatically? \n"
                                    "Yes - Change these settings automatically\n"
                                    "No  - Do not change these settings for me");
            MessageDialog      dialog(wxGetApp().plater(), msg_text, "Suggestion", wxICON_WARNING | wxYES | wxNO);
            DynamicPrintConfig new_conf = *m_config;
            if (dialog.ShowModal() == wxID_YES) {
                new_conf.set_key_value("support_top_z_distance", new ConfigOptionFloat(0));
                new_conf.set_key_value("support_interface_spacing", new ConfigOptionFloat(0));
                new_conf.set_key_value("support_interface_pattern",
                                       new ConfigOptionEnum<SupportMaterialInterfacePattern>(SupportMaterialInterfacePattern::smipRectilinearInterlaced));
                new_conf.set_key_value("independent_support_layer_height", new ConfigOptionBool(false));
                if (support_TPU || (is_soluble_filament(interface_filament_id) && !is_soluble_filament(filament_id))) {
                    new_conf.set_key_value("support_object_xy_distance", new ConfigOptionFloat(0));
                    new_conf.set_key_value("support_filament", new ConfigOptionInt(interface_filament_id + 1));
                }
                m_config_manipulation.apply(m_config, &new_conf);
            }
            wxGetApp().plater()->update();
        }
    }

    if(opt_key=="layer_height"){
        auto min_layer_height_from_nozzle=m_preset_bundle->full_config().option<ConfigOptionFloatsNullable>("min_layer_height")->values;
        auto max_layer_height_from_nozzle=m_preset_bundle->full_config().option<ConfigOptionFloatsNullable>("max_layer_height")->values;
        auto layer_height_floor = *std::min_element(min_layer_height_from_nozzle.begin(), min_layer_height_from_nozzle.end());
        auto layer_height_ceil  = *std::max_element(max_layer_height_from_nozzle.begin(), max_layer_height_from_nozzle.end());
        float layer_height = m_config->opt_float("layer_height");
        bool exceed_minimum_flag = layer_height < layer_height_floor;
        bool exceed_maximum_flag = layer_height > layer_height_ceil;

        if (exceed_maximum_flag || exceed_minimum_flag) {
            if(layer_height < EPSILON){
                wxString msg_text = _(L("Layer height is too small.\nIt will set to min_layer_height\n"));
                MessageDialog dialog(wxGetApp().plater(), msg_text, "", wxICON_WARNING | wxOK);
                dialog.SetButtonLabel(wxID_OK, _L("OK"));
                dialog.ShowModal();
                auto new_conf = *m_config;
                new_conf.set_key_value("layer_height", new ConfigOptionFloat(layer_height_floor));
                m_config_manipulation.apply(m_config, &new_conf);
            }
            else{
                wxString msg_text = _(L("Layer height exceeds the limit in Printer Settings -> Extruder -> Layer height limits ,this may cause printing quality issues."));
                msg_text += "\n\n" + _(L("Adjust to the set range automatically? \n"));
                MessageDialog dialog(wxGetApp().plater(), msg_text, "", wxICON_WARNING | wxYES | wxNO);
                dialog.SetButtonLabel(wxID_YES, _L("Adjust"));
                dialog.SetButtonLabel(wxID_NO, _L("Ignore"));
                auto answer = dialog.ShowModal();
                auto new_conf = *m_config;
                if (answer == wxID_YES) {
                    if (exceed_maximum_flag)
                        new_conf.set_key_value("layer_height", new ConfigOptionFloat(layer_height_ceil));
                    if (exceed_minimum_flag)
                        new_conf.set_key_value("layer_height",new ConfigOptionFloat(layer_height_floor));
                    m_config_manipulation.apply(m_config, &new_conf);
                }
                wxGetApp().plater()->update();
            }
        }
    }

    string opt_key_without_idx = opt_key.substr(0, opt_key.find('#'));

    if (opt_key_without_idx == "long_retractions_when_cut") {
        unsigned char activate = boost::any_cast<unsigned char>(value);
        if (activate == 1) {
            MessageDialog dialog(wxGetApp().plater(),
                _L("Experimental feature: Retracting and cutting off the filament at a greater distance during filament changes to minimize flush."
                    "Although it can notably reduce flush,  it may also elevate the risk of nozzle clogs or other printing complications."), "", wxICON_WARNING | wxOK);
            dialog.ShowModal();
        }
        if (wxGetApp().app_config->get("auto_calculate_flush") == "all"){
            wxGetApp().plater()->sidebar().auto_calc_flushing_volumes(-1);
        }
    }

    if (opt_key == "filament_long_retractions_when_cut"){
        unsigned char activate = boost::any_cast<unsigned char>(value);
        if (activate == 1) {
            MessageDialog dialog(wxGetApp().plater(),
            _L("Experimental feature: Retracting and cutting off the filament at a greater distance during filament changes to minimize flush."
            "Although it can notably reduce flush, it may also elevate the risk of nozzle clogs or other printing complications.Please use with the latest printer firmware."), "", wxICON_WARNING | wxOK);
            dialog.ShowModal();
        }
        if (wxGetApp().app_config->get("auto_calculate_flush") == "all"){
            wxGetApp().plater()->sidebar().auto_calc_flushing_volumes(-1);
        }
    }


    // BBS
#if 0
    if (opt_key == "extruders_count")
        wxGetApp().plater()->on_extruders_change(boost::any_cast<size_t>(value));
#endif

    if (opt_key.find("nozzle_volume_type") != std::string::npos) {
        int extruder_idx = std::atoi(opt_key.substr(opt_key.find_last_of('#') + 1).c_str());
        for (auto tab : wxGetApp().tabs_list) {
            tab->update_extruder_variants(extruder_idx);
        }
        if (auto tab = wxGetApp().plate_tab) {
            tab->update_extruder_variants(extruder_idx);
        }
        for (auto tab : wxGetApp().model_tabs_list) {
            tab->update_extruder_variants(extruder_idx);
        }
        if (wxGetApp().app_config->get("auto_calculate_flush") == "all") {
            wxGetApp().plater()->sidebar().auto_calc_flushing_volumes(-1,extruder_idx);
        }
    }

    if (m_preset_bundle->get_printer_extruder_count() > 1){
        int extruder_idx = std::atoi(opt_key.substr(opt_key.find_last_of('#') + 1).c_str());
        if (opt_key.find("min_layer_height") != std::string::npos) {
            auto min_layer_height_from_nozzle = m_preset_bundle->full_config().option<ConfigOptionFloatsNullable>("min_layer_height")->values;
            if (extruder_idx < min_layer_height_from_nozzle.size()) {
                double value = min_layer_height_from_nozzle[extruder_idx];
                std::fill(min_layer_height_from_nozzle.begin(), min_layer_height_from_nozzle.end(), value);
            }
            auto new_conf = *m_config;
            new_conf.set_key_value("min_layer_height", new ConfigOptionFloatsNullable(min_layer_height_from_nozzle));
            m_config_manipulation.apply(m_config, &new_conf);
        }
        else if (opt_key.find("max_layer_height") != std::string::npos) {
            auto max_layer_height_from_nozzle = m_preset_bundle->full_config().option<ConfigOptionFloatsNullable>("max_layer_height")->values;
            if (extruder_idx < max_layer_height_from_nozzle.size()) {
                double value = max_layer_height_from_nozzle[extruder_idx];
                std::fill(max_layer_height_from_nozzle.begin(), max_layer_height_from_nozzle.end(), value);
            }
            auto new_conf = *m_config;
            new_conf.set_key_value("max_layer_height", new ConfigOptionFloatsNullable(max_layer_height_from_nozzle));
            m_config_manipulation.apply(m_config, &new_conf);
        }
    }

    if (m_postpone_update_ui) {
        // It means that not all values are rolled to the system/last saved values jet.
        // And call of the update() can causes a redundant check of the config values,
        return;
    }

    update();
    if (m_active_page)
        m_active_page->update_visibility(m_mode, true);
    m_page_view->GetParent()->Layout();
}

void Tab::show_timelapse_warning_dialog() {
    if (!m_is_timelapse_wipe_tower_already_prompted) {
        wxString      msg_text = _(L("When recording timelapse without toolhead, it is recommended to add a \"Timelapse Wipe Tower\" \n"
                                "by right-click the empty position of build plate and choose \"Add Primitive\"->\"Timelapse Wipe Tower\"."));
        msg_text += "\n";
        MessageDialog dialog(nullptr, msg_text, "", wxICON_WARNING | wxOK);
        dialog.ShowModal();
        m_is_timelapse_wipe_tower_already_prompted = true;
    }
}

// Show/hide the 'purging volumes' button
void Tab::update_wiping_button_visibility() {
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME
    bool wipe_tower_enabled = dynamic_cast<ConfigOptionBool*>(  (m_preset_bundle->prints.get_edited_preset().config  ).option("enable_prime_tower"))->value;
    bool multiple_extruders = dynamic_cast<ConfigOptionFloatsNullable*>((m_preset_bundle->printers.get_edited_preset().config).option("nozzle_diameter"))->values.size() > 1;

    auto wiping_dialog_button = wxGetApp().sidebar().get_wiping_dialog_button();
    if (wiping_dialog_button) {
        wiping_dialog_button->Show(wipe_tower_enabled && multiple_extruders);
        wiping_dialog_button->GetParent()->Layout();
    }
}

void Tab::activate_option(const std::string& opt_key, const wxString& category)
{
    wxString page_title = translate_category(category, m_type);

    auto cur_item = m_tabctrl->GetFirstVisibleItem();
    if (cur_item < 0)
        return;

    // We should to activate a tab with searched option, if it doesn't.
    // And do it before finding of the cur_item to avoid a case when Tab isn't activated jet and all treeItems are invisible
    //BBS: GUI refactor
    //wxGetApp().mainframe->select_tab(this);
    wxGetApp().mainframe->select_tab((wxPanel*)m_parent);

    while (cur_item >= 0) {
        if (page_title.empty()) {
            bool has = false;
            for (auto &g : m_pages[cur_item]->m_optgroups) {
                for (auto &l : g->get_lines()) {
                    for (auto &o : l.get_options()) { if (o.opt.opt_key == opt_key) { has = true; break; } }
                    if (has) break;
                }
                if (has) break;
            }
            if (!has) {
                cur_item = m_tabctrl->GetNextVisible(cur_item);
                continue;
            }
        } else {
            auto title = m_tabctrl->GetItemText(cur_item);
            if (page_title != title) {
                cur_item = m_tabctrl->GetNextVisible(cur_item);
                continue;
            }
        }

        m_tabctrl->SelectItem(cur_item);
        break;
    }

    auto set_focus = [](wxWindow* win) {
        win->SetFocus();
#ifdef WIN32
        if (wxTextCtrl* text = dynamic_cast<wxTextCtrl*>(win))
            text->SetSelection(-1, -1);
        else if (wxSpinCtrl* spin = dynamic_cast<wxSpinCtrl*>(win))
            spin->SetSelection(-1, -1);
#endif // WIN32
    };

    Field* field = get_field(opt_key);

    // focused selected field
    if (field) {
        set_focus(field->getWindow());
        if (!field->getWindow()->HasFocus()) {
            wxScrollEvent evt(wxEVT_SCROLL_CHANGED);
            evt.SetEventObject(field->getWindow());
            wxPostEvent(m_page_view, evt);
        }
    }
    //else if (category == "Single extruder MM setup") {
    //    // When we show and hide "Single extruder MM setup" page,
    //    // related options are still in the search list
    //    // So, let's hightlighte a "single_extruder_multi_material" option,
    //    // as a "way" to show hidden page again
    //    field = get_field("single_extruder_multi_material");
    //    if (field)
    //        set_focus(field->getWindow());
    //}

    m_highlighter.init(get_custom_ctrl_with_blinking_ptr(opt_key));
}

void Tab::apply_searcher()
{
    wxGetApp().sidebar().get_searcher().apply(m_config, m_type, m_mode);
}

void Tab::cache_config_diff(const std::vector<std::string>& selected_options)
{
    m_cache_options = selected_options;
    m_cache_config.apply_only(m_presets->get_edited_preset().config, selected_options);
}

void Tab::apply_config_from_cache()
{
    bool was_applied = false;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<<boost::format(": enter");
    // check and apply extruders count for printer preset
    if (m_type == Preset::TYPE_PRINTER)
        was_applied = static_cast<TabPrinter*>(this)->apply_extruder_cnt_from_cache();

    if (!m_cache_config.empty()) {
        auto variants_key = extruder_variant_keys[m_type].second;
        if (m_cache_options.back() == variants_key) {
            m_cache_options.pop_back();
            ConfigOptionStrings *old_variants = dynamic_cast<ConfigOptionStrings *>(m_cache_config.option(variants_key));
            ConfigOptionStrings *new_variants = dynamic_cast<ConfigOptionStrings *>(m_config->option(variants_key));
            std::vector<std::string> variant_options;
            boost::split(variant_options, m_cache_options.back(), boost::is_any_of(";"));
            m_cache_options.pop_back();
            auto title = m_cache_options.back();
            m_cache_options.pop_back();
            if (!(*old_variants == *new_variants) && old_variants->size() == 1) {
                for (auto &opt : variant_options) {
                    auto copy = dynamic_cast<ConfigOptionVectorBase *>(m_cache_config.option(opt)->clone());
                    copy->resize(new_variants->size());
                    m_cache_config.set_key_value(opt, copy);
                }
                old_variants = new_variants;
            }
            if (*old_variants == *new_variants) {
                m_cache_options.insert(m_cache_options.end(), variant_options.begin(), variant_options.end());
            } else {
                auto msg = _L("Switching to a printer with different extruder types or numbers will discard or reset changes to extruder or multi-nozzle-related parameters.");
                MessageDialog(wxGetApp().plater(), msg, from_u8(title), wxOK | wxICON_WARNING).ShowModal();
            }
        }
        m_presets->get_edited_preset().config.apply_only(m_cache_config, m_cache_options);
        m_cache_config.clear();
        m_cache_options.clear();

        was_applied = true;
    }

    if (was_applied)
        update_dirty();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<<boost::format(": exit, was_applied=%1%")%was_applied;
}

// Call a callback to update the selection of presets on the plater:
// To update the content of the selection boxes,
// to update the filament colors of the selection boxes,
// to update the "dirty" flags of the selection boxes,
// to update number of "filament" selection boxes when the number of extruders change.
void Tab::on_presets_changed()
{
    if (wxGetApp().plater() == nullptr)
        return;

    // Instead of PostEvent (EVT_TAB_PRESETS_CHANGED) just call update_presets
    wxGetApp().plater()->sidebar().update_presets(m_type);

    bool is_bbl_vendor_preset = m_preset_bundle->printers.get_edited_preset().is_bbl_vendor_preset(wxGetApp().preset_bundle);
    if (is_bbl_vendor_preset) {
        wxGetApp().plater()->get_partplate_list().set_render_option(true, true);
        if (m_preset_bundle->printers.get_edited_preset().has_cali_lines(wxGetApp().preset_bundle)) {
            wxGetApp().plater()->get_partplate_list().set_render_cali(true);
        } else {
            wxGetApp().plater()->get_partplate_list().set_render_cali(false);
        }
    } else {
        wxGetApp().plater()->get_partplate_list().set_render_option(false, false);
        wxGetApp().plater()->get_partplate_list().set_render_cali(false);
    }

    // Printer selected at the Printer tab, update "compatible" marks at the print and filament selectors.
    for (auto t: m_dependent_tabs)
    {
        Tab* tab = wxGetApp().get_tab(t);
        // If the printer tells us that the print or filament/sla_material preset has been switched or invalidated,
        // refresh the print or filament/sla_material tab page.
        // But if there are options, moved from the previously selected preset, update them to edited preset
        tab->apply_config_from_cache();
        tab->load_current_preset();
    }
    // clear m_dependent_tabs after first update from select_preset()
    // to avoid needless preset loading from update() function
    m_dependent_tabs.clear();

    wxGetApp().plater()->update_project_dirty_from_presets();
}

void Tab::build_preset_description_line(ConfigOptionsGroup* optgroup)
{
    auto description_line = [this](wxWindow* parent) {
        return description_line_widget(parent, &m_parent_preset_description_line);
    };

    auto detach_preset_btn = [this](wxWindow* parent) {
        m_detach_preset_btn = new ScalableButton(parent, wxID_ANY, "lock_normal_sys", "",
                                                 wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT, true);
        ScalableButton* btn = m_detach_preset_btn;
        btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());

        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(btn);

        //btn->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent&)
        //{
        //	bool system = m_presets->get_edited_preset().is_system;
        //	bool dirty  = m_presets->get_edited_preset().is_dirty;
        //    wxString msg_text = system ?
        //    	_(L("A copy of the current system preset will be created, which will be detached from the system preset.")) :
        //        _(L("The current custom preset will be detached from the parent system preset."));
        //    if (dirty) {
	       //     msg_text += "\n\n";
        //    	msg_text += _(L("Modifications to the current profile will be saved."));
        //    }
        //    msg_text += "\n\n";
        //    msg_text += _(L("This action is not revertible.\nDo you want to proceed?"));

        //    //wxMessageDialog dialog(parent, msg_text, _(L("Detach preset")), wxICON_WARNING | wxYES_NO | wxCANCEL);
        //    MessageDialog dialog(parent, msg_text, _(L("Detach preset")), wxICON_WARNING | wxYES_NO | wxCANCEL);
        //    if (dialog.ShowModal() == wxID_YES)
        //        save_preset(m_presets->get_edited_preset().is_system ? std::string() : m_presets->get_edited_preset().name, true);
        //});

        btn->Hide();

        return sizer;
    };

    Line line = Line{ "", "" };
    line.full_width = 1;

    line.append_widget(description_line);
    line.append_widget(detach_preset_btn);
    optgroup->append_line(line);
}

void Tab::update_preset_description_line()
{
    const Preset* parent = m_presets->get_selected_preset_parent();
    const Preset& preset = m_presets->get_edited_preset();

    wxString description_line;

    //if (preset.is_default) {
    //    description_line = _(L("This is a default preset."));
    //} else if (preset.is_system) {
    //    description_line = _(L("This is a system preset."));
    //} else if (parent == nullptr) {
    //    description_line = _(L("Current preset is inherited from the default preset."));
    //} else {
    //    std::string name = parent->name;
    //    boost::replace_all(name, "&", "&&");
    //    description_line = _(L("Current preset is inherited from")) + ":\n\t" + from_u8(name);
    //}

    //if (preset.is_default || preset.is_system)
    //    description_line += "\n\t" + _(L("It can't be deleted or modified.")) +
    //                        "\n\t" + _(L("Any modifications should be saved as a new preset inherited from this one.")) +
    //                        "\n\t" + _(L("To do that please specify a new name for the preset."));

    //if (parent && parent->vendor)
    //{
    //    description_line += "\n\n" + _(L("Additional information:")) + "\n";
    //    description_line += "\t" + _(L("vendor")) + ": " + (m_type == Slic3r::Preset::TYPE_PRINTER ? "\n\t\t" : "") + parent->vendor->name +
    //                        ", ver: " + parent->vendor->config_version.to_string();
    //    if (m_type == Slic3r::Preset::TYPE_PRINTER) {
    //        const std::string &printer_model = preset.config.opt_string("printer_model");
    //        if (! printer_model.empty())
    //            description_line += "\n\n\t" + _(L("printer model")) + ": \n\t\t" + printer_model;
    //        switch (preset.printer_technology()) {
    //        case ptFFF:
    //        {
    //            //FIXME add prefered_sla_material_profile for SLA
    //            const std::string              &default_print_profile = preset.config.opt_string("default_print_profile");
    //            const std::vector<std::string> &default_filament_profiles = preset.config.option<ConfigOptionStrings>("default_filament_profile")->values;
    //            if (!default_print_profile.empty())
    //                description_line += "\n\n\t" + _(L("default print profile")) + ": \n\t\t" + default_print_profile;
    //            if (!default_filament_profiles.empty())
    //            {
    //                description_line += "\n\n\t" + _(L("default filament profile")) + ": \n\t\t";
    //                for (auto& profile : default_filament_profiles) {
    //                    if (&profile != &*default_filament_profiles.begin())
    //                        description_line += ", ";
    //                    description_line += profile;
    //                }
    //            }
    //            break;
    //        }
    //        case ptSLA:
    //        {
    //            //FIXME add prefered_sla_material_profile for SLA
    //            const std::string &default_sla_material_profile = preset.config.opt_string("default_sla_material_profile");
    //            if (!default_sla_material_profile.empty())
    //                description_line += "\n\n\t" + _(L("default SLA material profile")) + ": \n\t\t" + default_sla_material_profile;

    //            const std::string &default_sla_print_profile = preset.config.opt_string("default_sla_print_profile");
    //            if (!default_sla_print_profile.empty())
    //                description_line += "\n\n\t" + _(L("default SLA print profile")) + ": \n\t\t" + default_sla_print_profile;
    //            break;
    //        }
    //        default: break;
    //        }
    //    }
    //    else if (!preset.alias.empty())
    //    {
    //        description_line += "\n\n\t" + _(L("full profile name"))     + ": \n\t\t" + preset.name;
    //        description_line += "\n\t"   + _(L("symbolic profile name")) + ": \n\t\t" + preset.alias;
    //    }
    //}

    m_parent_preset_description_line->SetText(description_line, false);

    if (m_detach_preset_btn)
        m_detach_preset_btn->Show(parent && parent->is_system && !preset.is_default);
    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();
}

static void validate_custom_note_cb(Tab *tab, ConfigOptionsGroupShp opt_group, const t_config_option_key &opt_key, const boost::any &value)
{
    if (boost::any_cast<std::string>(value).size() > 40 * 1024) {
        MessageDialog dialog(static_cast<wxWindow *>(wxGetApp().mainframe), _L("The notes are too large, and may not be synchronized to the cloud. Please keep it within 40k."),
                             "", wxICON_WARNING | wxOK);
        dialog.ShowModal();
    }
}

void Tab::update_frequently_changed_parameters()
{
    const bool is_fff = supports_printer_technology(ptFFF);
    auto og_freq_chng_params = wxGetApp().sidebar().og_freq_chng_params(is_fff);
    if (!og_freq_chng_params) return;

    og_freq_chng_params->set_value("support", support_combo_value_for_config(*m_config, is_fff));
    if (! is_fff)
        og_freq_chng_params->set_value("pad", pad_combo_value_for_config(*m_config));

    const std::string updated_value_key = is_fff ? "sparse_infill_density" : "pad_enable";

    const boost::any val = og_freq_chng_params->get_config_value(*m_config, updated_value_key);
    og_freq_chng_params->set_value(updated_value_key, val);

    if (is_fff)
    {
        og_freq_chng_params->set_value("brim", bool(m_config->opt_float("brim_width") > 0.0));
        update_wiping_button_visibility();
    }
}

//BBS: BBS new parameter list
void TabPrint::build()
{
    if (m_presets == nullptr)
        m_presets = &m_preset_bundle->prints;
    load_initial_data();

    auto page = add_options_page(L("Quality"), "empty");
        auto optgroup = page->new_optgroup(L("Layer height"), L"param_layer_height");
    optgroup->append_single_option_line("layer_height", "layer-height");
        optgroup->append_single_option_line("initial_layer_print_height", "layer-height");

        optgroup = page->new_optgroup(L("Line width"), L"param_line_width");
        optgroup->append_single_option_line("line_width","parameter/line-width");
        optgroup->append_single_option_line("initial_layer_line_width","parameter/line-width");
        optgroup->append_single_option_line("outer_wall_line_width","parameter/line-width");
        optgroup->append_single_option_line("inner_wall_line_width","parameter/line-width");
        optgroup->append_single_option_line("top_surface_line_width","parameter/line-width");
        optgroup->append_single_option_line("sparse_infill_line_width","parameter/line-width");
        optgroup->append_single_option_line("internal_solid_infill_line_width","parameter/line-width");
        optgroup->append_single_option_line("support_line_width","parameter/line-width");

        optgroup = page->new_optgroup(L("Seam"), L"param_seam");
        optgroup->append_single_option_line("seam_position", "Seam");
        optgroup->append_single_option_line("seam_placement_away_from_overhangs", "Seam");
        optgroup->append_single_option_line("seam_gap", "Seam");
        optgroup->append_single_option_line("seam_slope_conditional", "Seam");
        optgroup->append_single_option_line("scarf_angle_threshold", "Seam");
        optgroup->append_single_option_line("seam_slope_entire_loop", "Seam");
        optgroup->append_single_option_line("seam_slope_steps", "Seam");
        optgroup->append_single_option_line("seam_slope_inner_walls", "Seam");
        optgroup->append_single_option_line("override_filament_scarf_seam_setting", "Seam");
        optgroup->append_single_option_line("seam_slope_type", "Seam");
        optgroup->append_single_option_line("seam_slope_start_height", "Seam");
        optgroup->append_single_option_line("seam_slope_gap", "Seam");
        optgroup->append_single_option_line("seam_slope_min_length", "Seam");
        optgroup->append_single_option_line("wipe_speed", "Seam");
        optgroup->append_single_option_line("role_base_wipe_speed", "Seam");

        optgroup = page->new_optgroup(L("Precision"), L"param_precision");
        optgroup->append_single_option_line("slice_closing_radius");
        optgroup->append_single_option_line("resolution","acr-move");
        optgroup->append_single_option_line("enable_arc_fitting", "acr-move");
        optgroup->append_single_option_line("xy_hole_compensation", "xy-hole-contour-compensation");
        optgroup->append_single_option_line("xy_contour_compensation", "xy-hole-contour-compensation");
        optgroup->append_single_option_line("enable_circle_compensation");
        optgroup->append_single_option_line("circle_compensation_manual_offset");

        optgroup->append_single_option_line("elefant_foot_compensation", "parameter/elephant-foot");
        optgroup->append_single_option_line("precise_outer_wall");
        optgroup->append_single_option_line("precise_z_height");

        optgroup = page->new_optgroup(L("Ironing"), L"param_ironing");
        optgroup->append_single_option_line("ironing_type", "parameter/ironing");
        optgroup->append_single_option_line("ironing_pattern");
        optgroup->append_single_option_line("ironing_speed");
        optgroup->append_single_option_line("ironing_flow");
        optgroup->append_single_option_line("ironing_spacing");
        optgroup->append_single_option_line("ironing_inset");
        optgroup->append_single_option_line("ironing_direction");

        optgroup = page->new_optgroup(L("Wall generator"), L"param_wall");
        optgroup->append_single_option_line("wall_generator", "wall-generator");
        optgroup->append_single_option_line("wall_transition_angle");
        optgroup->append_single_option_line("wall_transition_filter_deviation");
        optgroup->append_single_option_line("wall_transition_length");
        optgroup->append_single_option_line("wall_distribution_count");
        optgroup->append_single_option_line("min_bead_width");
        optgroup->append_single_option_line("min_feature_size");

        optgroup = page->new_optgroup(L("Advanced"), L"param_advanced");
        optgroup->append_single_option_line("wall_sequence","parameter/quality-advance-settings");
        optgroup->append_single_option_line("is_infill_first","parameter/quality-advance-settings");
        optgroup->append_single_option_line("bridge_flow","parameter/bridge");
        optgroup->append_single_option_line("thick_bridges","parameter/bridge");
        optgroup->append_single_option_line("print_flow_ratio");
        optgroup->append_single_option_line("top_solid_infill_flow_ratio","parameter/quality-advance-settings");
        optgroup->append_single_option_line("initial_layer_flow_ratio","parameter/quality-advance-settings");
        optgroup->append_single_option_line("top_one_wall_type","parameter/quality-advance-settings");
        optgroup->append_single_option_line("top_area_threshold","parameter/quality-advance-settings");
        optgroup->append_single_option_line("only_one_wall_first_layer","parameter/quality-advance-settings");
        optgroup->append_single_option_line("detect_overhang_wall","parameter/quality-advance-settings");
        optgroup->append_single_option_line("smooth_speed_discontinuity_area","parameter/quality-advance-settings");
        optgroup->append_single_option_line("smooth_coefficient","parameter/quality-advance-settings");
        optgroup->append_single_option_line("reduce_crossing_wall","parameter/quality-advance-settings");
        optgroup->append_single_option_line("max_travel_detour_distance","parameter/quality-advance-settings");
        optgroup->append_single_option_line("z_direction_outwall_speed_continuous", "parameter/quality-advance-settings");

    page = add_options_page(L("Strength"), "empty");
        optgroup = page->new_optgroup(L("Walls"), L"param_wall");
        optgroup->append_single_option_line("wall_loops","wall-generator");
        optgroup->append_single_option_line("detect_thin_wall","wall-generator");

        optgroup = page->new_optgroup(L("Top/bottom shells"), L"param_shell");
        optgroup->append_single_option_line("interface_shells");
        optgroup->append_single_option_line("top_surface_pattern", "fill-patterns#Infill of the top surface and bottom surface");
        optgroup->append_single_option_line("top_shell_layers");
        optgroup->append_single_option_line("top_shell_thickness");
        optgroup->append_single_option_line("top_color_penetration_layers");
        optgroup->append_single_option_line("bottom_surface_pattern", "fill-patterns#Infill of the top surface and bottom surface");
        optgroup->append_single_option_line("bottom_shell_layers");
        optgroup->append_single_option_line("bottom_shell_thickness");
        optgroup->append_single_option_line("bottom_color_penetration_layers");
        optgroup->append_single_option_line("internal_solid_infill_pattern");

        optgroup = page->new_optgroup(L("Sparse infill"), L"param_infill");
        optgroup->append_single_option_line("sparse_infill_density");
        optgroup->append_single_option_line("sparse_infill_pattern", "fill-patterns#infill types and their properties of sparse");
        optgroup->append_single_option_line("locked_skin_infill_pattern", "fill-patterns#infill types and their properties of sparse", -1, true);
        optgroup->append_single_option_line("skin_infill_density", "", -1, true);
        optgroup->append_single_option_line("locked_skeleton_infill_pattern", "fill-patterns#infill types and their properties of sparse", -1, true);
        optgroup->append_single_option_line("skeleton_infill_density", "", -1, true);
        optgroup->append_single_option_line("infill_lock_depth", "", -1, true);
        optgroup->append_single_option_line("skin_infill_depth", "", -1, true);
        optgroup->append_single_option_line("skin_infill_line_width", "parameter/line-width", -1, true);
        optgroup->append_single_option_line("skeleton_infill_line_width", "parameter/line-width", -1, true);

        optgroup->append_single_option_line("symmetric_infill_y_axis");
        optgroup->append_single_option_line("infill_shift_step");

        optgroup->append_single_option_line("infill_rotate_step");
        optgroup->append_single_option_line("sparse_infill_anchor");
        optgroup->append_single_option_line("sparse_infill_anchor_max");
        optgroup->append_single_option_line("filter_out_gap_fill");

        optgroup = page->new_optgroup(L("Advanced"), L"param_advanced");
        optgroup->append_single_option_line("infill_wall_overlap","parameter/strength-advance-settings");
        optgroup->append_single_option_line("infill_direction","parameter/strength-advance-settings");
        optgroup->append_single_option_line("bridge_angle","parameter/strength-advance-settings");
        optgroup->append_single_option_line("minimum_sparse_infill_area","parameter/strength-advance-settings");
        optgroup->append_single_option_line("infill_combination","parameter/strength-advance-settings");
        optgroup->append_single_option_line("detect_narrow_internal_solid_infill","parameter/strength-advance-settings");
        optgroup->append_single_option_line("ensure_vertical_shell_thickness","parameter/strength-advance-settings");
        optgroup->append_single_option_line("detect_floating_vertical_shell","parameter/strength-advance-settings");
        //optgroup->append_single_option_line("internal_bridge_support_thickness","parameter/strength-advance-settings");

    page = add_options_page(L("Speed"), "empty");
        optgroup = page->new_optgroup(L("Initial layer speed"), L"param_speed_first", 15);
        optgroup->append_single_option_line("initial_layer_speed", "", 0);
        optgroup->append_single_option_line("initial_layer_infill_speed", "", 0);
        optgroup = page->new_optgroup(L("Other layers speed"), L"param_speed", 15);
        optgroup->append_single_option_line("outer_wall_speed", "", 0);
        optgroup->append_single_option_line("inner_wall_speed", "", 0);
        optgroup->append_single_option_line("small_perimeter_speed", "", 0);
        optgroup->append_single_option_line("small_perimeter_threshold", "", 0);
        optgroup->append_single_option_line("sparse_infill_speed", "", 0);
        optgroup->append_single_option_line("internal_solid_infill_speed", "", 0);
        optgroup->append_single_option_line("vertical_shell_speed", "", 0);
        optgroup->append_single_option_line("top_surface_speed", "", 0);
        optgroup->append_single_option_line("enable_overhang_speed", "slow-down-for-overhang", 0);
        Line line = { L("Overhang speed"), L("This is the speed for various overhang degrees. Overhang degrees are expressed as a percentage of line width. 0 speed means no slowing down for the overhang degree range and wall speed is used") };
        line.label_path = "slow-down-for-overhang";
        line.append_option(optgroup->get_option("overhang_1_4_speed", 0));
        line.append_option(optgroup->get_option("overhang_2_4_speed", 0));
        line.append_option(optgroup->get_option("overhang_3_4_speed", 0));
        line.append_option(optgroup->get_option("overhang_4_4_speed", 0));
        line.append_option(optgroup->get_option("overhang_totally_speed", 0));
        optgroup->append_line(line);
        optgroup->append_single_option_line("enable_height_slowdown", "", 0);
        optgroup->append_single_option_line("slowdown_start_height", "", 0);
        optgroup->append_single_option_line("slowdown_start_speed", "", 0);
        optgroup->append_single_option_line("slowdown_start_acc", "", 0);
        optgroup->append_single_option_line("slowdown_end_height", "", 0);
        optgroup->append_single_option_line("slowdown_end_speed", "", 0);
        optgroup->append_single_option_line("slowdown_end_acc", "", 0);
        optgroup->append_single_option_line("bridge_speed", "", 0);
        optgroup->append_single_option_line("gap_infill_speed", "", 0);
        optgroup->append_single_option_line("support_speed", "", 0);
        optgroup->append_single_option_line("support_interface_speed", "", 0);

        optgroup = page->new_optgroup(L("Travel speed"), L"param_travel_speed", 15);
        optgroup->append_single_option_line("travel_speed", "", 0);

        optgroup = page->new_optgroup(L("Acceleration"), L"param_acceleration", 15);
        optgroup->append_single_option_line("default_acceleration", "", 0);
        optgroup->append_single_option_line("travel_acceleration", "", 0);
        optgroup->append_single_option_line("initial_layer_travel_acceleration", "", 0);
        optgroup->append_single_option_line("initial_layer_acceleration", "", 0);
        optgroup->append_single_option_line("outer_wall_acceleration", "", 0);
        optgroup->append_single_option_line("inner_wall_acceleration", "", 0);
        optgroup->append_single_option_line("top_surface_acceleration", "", 0);
        optgroup->append_single_option_line("sparse_infill_acceleration", "", 0);
        optgroup->append_single_option_line("accel_to_decel_enable", "");
        optgroup->append_single_option_line("accel_to_decel_factor", "");

        optgroup = page->new_optgroup(L("Jerk(XY)"), L"param_acceleration", 15);
        optgroup->append_single_option_line("default_jerk", "");
        optgroup->append_single_option_line("outer_wall_jerk", "");
        optgroup->append_single_option_line("inner_wall_jerk", "");
        optgroup->append_single_option_line("infill_jerk", "");
        optgroup->append_single_option_line("top_surface_jerk", "");
        optgroup->append_single_option_line("initial_layer_jerk", "");
        optgroup->append_single_option_line("travel_jerk", "");

#ifdef HAS_PRESSURE_EQUALIZER
        optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_positive");
        optgroup->append_single_option_line("max_volumetric_extrusion_rate_slope_negative");
#endif /* HAS_PRESSURE_EQUALIZER */

    page = add_options_page(L("Support"), "support");
        optgroup = page->new_optgroup(L("Support"), L"param_support");
        optgroup->append_single_option_line("enable_support", "support");
        optgroup->append_single_option_line("support_type", "support#support-types");
        optgroup->append_single_option_line("support_style", "support#support-styles");
        optgroup->append_single_option_line("support_threshold_angle", "support#threshold-angle");
        optgroup->append_single_option_line("support_on_build_plate_only");
        optgroup->append_single_option_line("support_critical_regions_only");
        optgroup->append_single_option_line("support_remove_small_overhang");
        //optgroup->append_single_option_line("enforce_support_layers");

        optgroup = page->new_optgroup(L("Raft"), L"param_raft");
        optgroup->append_single_option_line("raft_layers");
        optgroup->append_single_option_line("raft_contact_distance");

        optgroup = page->new_optgroup(L("Support filament"), L"param_support_filament");
        optgroup->append_single_option_line("support_filament", "support#support-filament");
        optgroup->append_single_option_line("support_interface_filament", "support#support-filament");
        optgroup->append_single_option_line("support_interface_not_for_body", "support#support-filament");

        //optgroup = page->new_optgroup(L("Options for support material and raft"));

        //BBS
        optgroup = page->new_optgroup(L("Advanced"), L"param_advanced");
        optgroup->append_single_option_line("raft_first_layer_density");   // not only for raft, but for support too
        optgroup->append_single_option_line("raft_first_layer_expansion"); // not only for raft, but for support too
        optgroup->append_single_option_line("tree_support_wall_count");
        optgroup->append_single_option_line("support_top_z_distance", "support#top-z-distance");
        optgroup->append_single_option_line("support_bottom_z_distance", "support#bottom-z-distance");
        optgroup->append_single_option_line("support_base_pattern", "support#base-pattern");
        optgroup->append_single_option_line("support_base_pattern_spacing", "support#base-pattern");
        optgroup->append_single_option_line("support_angle");
        optgroup->append_single_option_line("support_interface_top_layers", "support#base-pattern");
        optgroup->append_single_option_line("support_interface_bottom_layers", "support#base-pattern");
        optgroup->append_single_option_line("support_interface_pattern", "support#base-pattern");
        optgroup->append_single_option_line("support_interface_spacing", "support#base-pattern");
        optgroup->append_single_option_line("support_bottom_interface_spacing");
        optgroup->append_single_option_line("support_expansion", "support#base-pattern");
        //optgroup->append_single_option_line("support_interface_loop_pattern");

        optgroup->append_single_option_line("support_object_xy_distance", "support");
        optgroup->append_single_option_line("support_object_first_layer_gap", "support");
        optgroup->append_single_option_line("bridge_no_support", "support#base-pattern");
        optgroup->append_single_option_line("max_bridge_length", "support#tree-support-only-options");
        optgroup->append_single_option_line("independent_support_layer_height", "support");

        optgroup = page->new_optgroup(L("Tree Support"), L"param_advanced");
        optgroup->append_single_option_line("tree_support_branch_distance", "support#tree-support-only-options");
        optgroup->append_single_option_line("tree_support_branch_diameter", "support#tree-support-only-options");
        optgroup->append_single_option_line("tree_support_branch_angle", "support#tree-support-only-options");
        optgroup->append_single_option_line("tree_support_branch_diameter_angle", "support#tree-support-only-options");

    page = add_options_page(L("Others"), "advanced");
        optgroup = page->new_optgroup(L("Bed adhension"), L"param_adhension");
        optgroup->append_single_option_line("skirt_loops");
        optgroup->append_single_option_line("skirt_height");
        optgroup->append_single_option_line("skirt_distance");
        //optgroup->append_single_option_line("draft_shield");
        optgroup->append_single_option_line("brim_type", "auto-brim");
        optgroup->append_single_option_line("brim_width", "auto-brim#manual");
        optgroup->append_single_option_line("brim_object_gap", "auto-brim#brim-object-gap");

        optgroup = page->new_optgroup(L("Prime tower"), L"param_tower");
        optgroup->append_single_option_line("enable_prime_tower","parameter/prime-tower");
        optgroup->append_single_option_line("prime_tower_skip_points", "parameter/prime-tower");
        optgroup->append_single_option_line("prime_tower_enable_framework", "parameter/prime-tower#internal-ribs");
        optgroup->append_single_option_line("prime_tower_width","parameter/prime-tower");
        optgroup->append_single_option_line("prime_tower_max_speed","parameter/prime-tower#max-speed");
        optgroup->append_single_option_line("prime_tower_brim_width","parameter/prime-tower");
        optgroup->append_single_option_line("prime_tower_infill_gap","parameter/prime-tower#infill-gap");
        optgroup->append_single_option_line("prime_tower_rib_wall", "parameter/prime-tower#rib-wall");
        optgroup->append_single_option_line("prime_tower_extra_rib_length","parameter/prime-tower#rib-wall");
        optgroup->append_single_option_line("prime_tower_rib_width","parameter/prime-tower#rib-wall");
        optgroup->append_single_option_line("prime_tower_fillet_wall","parameter/prime-tower");

        optgroup = page->new_optgroup(L("Flush options"), L"param_flush");
        optgroup->append_single_option_line("flush_into_infill", "reduce-wasting-during-filament-change#wipe-into-infill");
        optgroup->append_single_option_line("flush_into_objects", "reduce-wasting-during-filament-change#wipe-into-object");
        optgroup->append_single_option_line("flush_into_support", "reduce-wasting-during-filament-change#wipe-into-support-enabled-by-default");

        optgroup = page->new_optgroup(L("Special mode"), L"param_special");
        optgroup->append_single_option_line("slicing_mode", "special-slicing-modes");
        optgroup->append_single_option_line("print_sequence", "sequent-print");
        optgroup->append_single_option_line("spiral_mode", "spiral-vase");
        optgroup->append_single_option_line("spiral_mode_smooth", "spiral-vase#smooth");
        optgroup->append_single_option_line("spiral_mode_max_xy_smoothing", "spiral-vase#max-xy-smoothing");
        optgroup->append_single_option_line("timelapse_type", "Timelapse");

        optgroup->append_single_option_line("fuzzy_skin", "parameter/fuzzy-skin");
        optgroup->append_single_option_line("fuzzy_skin_point_distance");
        optgroup->append_single_option_line("fuzzy_skin_thickness");

        optgroup = page->new_optgroup(L("Advanced"), L"advanced");
        optgroup->append_single_option_line("interlocking_beam");
        // optgroup->append_single_option_line("mmu_segmented_region_max_width");
        optgroup->append_single_option_line("mmu_segmented_region_interlocking_depth");
        optgroup->append_single_option_line("interlocking_beam_width");
        optgroup->append_single_option_line("interlocking_orientation");
        optgroup->append_single_option_line("interlocking_beam_layer_count");
        optgroup->append_single_option_line("interlocking_depth");
        optgroup->append_single_option_line("interlocking_boundary_avoidance");

        optgroup = page->new_optgroup(L("G-code output"), L"param_gcode");
        optgroup->append_single_option_line("reduce_infill_retraction");
        optgroup->append_single_option_line("gcode_add_line_number");
        optgroup->append_single_option_line("exclude_object");
        Option option = optgroup->get_option("filename_format");
        option.opt.full_width = true;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Post-processing scripts"), L"param_gcode", 0);
        option = optgroup->get_option("post_process");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = 15;
        optgroup->append_single_option_line(option);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key &opt_key, const boost::any &value) { validate_custom_note_cb(this, optgroup, opt_key, value); };

        optgroup = page->new_optgroup(L("Notes"),"note");
        optgroup->label_width = 0;
        option = optgroup->get_option("process_notes");
        option.opt.full_width = true;
        option.opt.height = 25;
        optgroup->append_single_option_line(option);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key &opt_key, const boost::any &value) { validate_custom_note_cb(this, optgroup, opt_key, value); };

#if 0
    //page = add_options_page(L("Dependencies"), "advanced.png");
    //    optgroup = page->new_optgroup(L("Profile dependencies"));

    //    create_line_with_widget(optgroup.get(), "compatible_printers", "", [this](wxWindow* parent) {
    //        return compatible_widget_create(parent, m_compatible_printers);
    //    });
    //
    //    option = optgroup->get_option("compatible_printers_condition");
    //    option.opt.full_width = true;
    //    optgroup->append_single_option_line(option);

    //    build_preset_description_line(optgroup.get());
#endif
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabPrint::reload_config()
{
    this->compatible_widget_reload(m_compatible_printers);
    Tab::reload_config();
}

void TabPrint::update_description_lines()
{
    Tab::update_description_lines();

    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return;

    if (m_active_page && m_active_page->title() == "Layers and perimeters" &&
        m_recommended_thin_wall_thickness_description_line && m_top_bottom_shell_thickness_explanation)
    {
        m_recommended_thin_wall_thickness_description_line->SetText(
            from_u8(PresetHints::recommended_thin_wall_thickness(*m_preset_bundle)));
        m_top_bottom_shell_thickness_explanation->SetText(
            from_u8(PresetHints::top_bottom_shell_thickness_explanation(*m_preset_bundle)));
    }

}

void TabPrint::toggle_options()
{
    if (!m_active_page) return;
    // BBS: whether the preset is Bambu Lab printer
    if (m_preset_bundle) {
        bool is_BBL_printer = m_preset_bundle->printers.get_edited_preset().is_bbl_vendor_preset(m_preset_bundle);
        m_config_manipulation.set_is_BBL_Printer(is_BBL_printer);
    }

    m_config_manipulation.toggle_print_fff_options(m_config, int(intptr_t(m_extruder_switch->GetClientData())), m_type < Preset::TYPE_COUNT);

    Field *field = m_active_page->get_field("support_style");
    auto   support_type = m_config->opt_enum<SupportType>("support_type");
    if (auto choice = dynamic_cast<Choice*>(field)) {
        auto def = print_config_def.get("support_style");
        std::vector<int> enum_set_normal = {smsDefault, smsGrid, smsSnug };
        std::vector<int> enum_set_tree   = { smsDefault, smsTreeSlim, smsTreeStrong, smsTreeHybrid, smsTreeOrganic };
        auto &           set             = is_tree(support_type) ? enum_set_tree : enum_set_normal;
        auto &           opt             = const_cast<ConfigOptionDef &>(field->m_opt);
        auto             cb              = dynamic_cast<ComboBox *>(choice->window);
        auto             n               = cb->GetValue();
        opt.enum_values.clear();
        opt.enum_labels.clear();
        cb->Clear();
        for (auto i : set) {
            opt.enum_values.push_back(def->enum_values[i]);
            opt.enum_labels.push_back(def->enum_labels[i]);
            cb->Append(_(def->enum_labels[i]));
        }
        cb->SetValue(n);
    }
}

void TabPrint::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME

    m_update_cnt++;

    // ysFIXME: It's temporary workaround and should be clewer reworked:
    // Note: This workaround works till "enable_support" and "overhangs" is exclusive sets of mutually no-exclusive parameters.
    // But it should be corrected when we will have more such sets.
    // Disable check of the compatibility of the "enable_support" and "overhangs" options for saved user profile
    // NOTE: Initialization of the support_material_overhangs_queried value have to be processed just ones
    if (!m_config_manipulation.is_initialized_support_material_overhangs_queried())
    {
        const Preset& selected_preset = m_preset_bundle->prints.get_selected_preset();
        bool is_user_and_saved_preset = !selected_preset.is_system && !selected_preset.is_dirty;
        bool support_material_overhangs_queried = m_config->opt_bool("enable_support") && !m_config->opt_bool("detect_overhang_wall");
        m_config_manipulation.initialize_support_material_overhangs_queried(is_user_and_saved_preset && support_material_overhangs_queried);
    }

    m_config_manipulation.update_print_fff_config(m_config, m_type < Preset::TYPE_COUNT, m_type == Preset::TYPE_PLATE);

    update_description_lines();
    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();

    m_update_cnt--;

    if (m_update_cnt==0) {
        toggle_options();

        // update() could be called during undo/redo execution
        // Update of objectList can cause a crash in this case (because m_objects doesn't match ObjectList)
        if (m_type != Preset::TYPE_MODEL && !wxGetApp().plater()->inside_snapshot_capture())
            wxGetApp().obj_list()->update_and_show_object_settings_item();

        wxGetApp().mainframe->on_config_changed(m_config);
    }
}

void TabPrint::clear_pages()
{
    Tab::clear_pages();

    m_recommended_thin_wall_thickness_description_line = nullptr;
    m_top_bottom_shell_thickness_explanation = nullptr;
}

//BBS: GUI refactor

static std::vector<std::string> intersect(std::vector<std::string> const& l, std::vector<std::string> const& r)
{
    std::vector<std::string> t;
    std::copy_if(r.begin(), r.end(), std::back_inserter(t), [&l](auto & e) { return std::find(l.begin(), l.end(), e) != l.end(); });
    return t;
}

static std::vector<std::string> concat(std::vector<std::string> const& l, std::vector<std::string> const& r)
{
    std::vector<std::string> t;
    std::set_union(l.begin(), l.end(), r.begin(), r.end(), std::back_inserter(t));
    return t;
}

static std::vector<std::string> substruct(std::vector<std::string> const& l, std::vector<std::string> const& r)
{
    std::vector<std::string> t;
    std::copy_if(l.begin(), l.end(), std::back_inserter(t), [&r](auto & e) { return std::find(r.begin(), r.end(), e) == r.end(); });
    return t;
}

TabPrintModel::TabPrintModel(ParamsPanel* parent, std::vector<std::string> const & keys)
    : TabPrint(parent, Preset::TYPE_MODEL)
    , m_keys(intersect(Preset::print_options(), keys))
    , m_prints(Preset::TYPE_MODEL, Preset::print_options(), static_cast<const PrintRegionConfig&>(FullPrintConfig::defaults()))
{
    m_opt_status_value = osInitValue | osSystemValue;
    m_is_default_preset = true;
}

void TabPrintModel::build()
{
    m_presets = &m_prints;
    TabPrint::build();
    init_options_list();

    auto page = add_options_page(L("Frequent"), "empty");
        auto optgroup = page->new_optgroup("");
            optgroup->append_single_option_line("layer_height");
            optgroup->append_single_option_line("sparse_infill_density");
            optgroup->append_single_option_line("wall_loops");
            optgroup->append_single_option_line("enable_support", "support");
    m_pages.pop_back();
    m_pages.insert(m_pages.begin(), page);

    for (auto p : m_pages) {
        for (auto g : p->m_optgroups) {
            g->remove_option_if([this](auto &key) { return !has_key(key); });
            g->have_sys_config = [this] { m_back_to_sys = true; return true; };
        }
        p->m_optgroups.erase(std::remove_if(p->m_optgroups.begin(), p->m_optgroups.end(), [](auto & g) {
            return g->get_lines().empty();
        }), p->m_optgroups.end());
    }
    m_pages.erase(std::remove_if(m_pages.begin(), m_pages.end(), [](auto & p) {
        return p->m_optgroups.empty();
    }), m_pages.end());
}

void TabPrintModel::set_model_config(std::map<ObjectBase *, ModelConfig *> const & object_configs)
{
    m_object_configs = object_configs;
    m_prints.get_selected_preset().config.apply(*m_parent_tab->m_config);
    update_model_config();
}

static std::vector<std::string> variant_keys(DynamicPrintConfig const & config)
{
    std::vector<std::string> t;
    for (const std::string &opt_key : config.keys()) {
        auto opt = config.option(opt_key);
        if (opt->type() & coVectorType) {
            auto vec = dynamic_cast<ConfigOptionVectorBase const *>(opt);
            for (size_t i = 0; i < vec->size(); i++)
                if (!vec->is_nil(i))
                    t.push_back(opt_key + "#" + std::to_string(i));
        } else {
            t.push_back(opt_key);
        }
    }
    return t;
}

void TabPrintModel::update_model_config()
{
    if (m_config_manipulation.is_applying()) {
        return;
    }
    m_config->apply(*m_parent_tab->m_config);
    if (m_type != Preset::TYPE_PLATE) {
        m_config->apply_only(*wxGetApp().plate_tab->get_config(), plate_keys);
    }
    m_null_keys.clear();
    if (!m_object_configs.empty()) {
        DynamicPrintConfig const & global_config= *m_config;
        DynamicPrintConfig const & local_config = m_object_configs.begin()->second->get();
        DynamicPrintConfig diff_config;
        std::vector<std::string> all_keys = variant_keys(local_config); // at least one has these keys
        std::vector<std::string> local_diffs; // all diff keys to first config
        if (m_object_configs.size() > 1) {
            std::vector<std::string> global_diffs; // all diff keys to global config
            for (auto & config : m_object_configs) {
                all_keys = concat(all_keys, variant_keys(config.second->get()));
                auto diffs = deep_diff(config.second->get(), global_config, false);
                global_diffs = concat(global_diffs, diffs);
                diff_config.apply_only(config.second->get(), diffs);
                if (&config.second->get() == &local_config) continue;
                local_diffs = concat(local_diffs, deep_diff(local_config, config.second->get()));
            }
            m_null_keys = intersect(global_diffs, local_diffs);
            m_config->apply(diff_config);
        }
        m_all_keys.clear();
        std::copy_if(all_keys.begin(), all_keys.end(), std::back_inserter(m_all_keys), [this](std::string & e) {
            auto iter = std::lower_bound(m_keys.begin(), m_keys.end(), e);
            if (auto n = e.find('#'); n == std::string::npos)
                return iter != m_keys.end() && e == *iter;
            else
                return iter != m_keys.begin() && e.compare(0, n, *--iter) == 0;
        });
        // except those than all equal on
        auto local_keys = substruct(m_all_keys, local_diffs);
        auto iter = std::partition(local_keys.begin(), local_keys.end(), [] (auto & e) {
            return e.find('#') == std::string::npos;
        });
        m_config->apply_only(local_config, std::vector<std::string>(local_keys.begin(), iter));
        for (; iter != local_keys.end(); ++iter) {
            int n = iter->find('#');
            auto opt_key = iter->substr(0, n);
            n = std::atoi(iter->c_str() + n + 1);
            auto vec = dynamic_cast<ConfigOptionVectorBase *>(m_config->option(opt_key));
            vec->set_at(local_config.option(opt_key), n, n);
        }
        m_config_manipulation.apply_null_fff_config(m_config, m_null_keys, m_object_configs);

        if (m_type == Preset::Type::TYPE_PLATE) {
            // Reset m_config manually because there's no corresponding config in m_parent_tab->m_config
            for (auto plate_item : m_object_configs) {
                const DynamicPrintConfig& plate_config = plate_item.second->get();
                BedType plate_bed_type = (BedType)0;
                PrintSequence plate_print_seq = (PrintSequence)0;
                if (!plate_config.has("curr_bed_type")) {
                    // same as global
                    DynamicConfig& global_cfg = m_preset_bundle->project_config;
                    if (global_cfg.has("curr_bed_type")) {
                        BedType global_bed_type = global_cfg.opt_enum<BedType>("curr_bed_type");
                        m_config->set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(global_bed_type));
                    }
                }
                if (!plate_config.has("first_layer_print_sequence")) {
                    m_config->set_key_value("first_layer_sequence_choice", new ConfigOptionEnum<LayerSeq>(flsAuto));
                }
                else {
                    replace(m_all_keys.begin(), m_all_keys.end(), std::string("first_layer_print_sequence"), std::string("first_layer_sequence_choice"));
                    m_config->set_key_value("first_layer_sequence_choice", new ConfigOptionEnum<LayerSeq>(flsCutomize));
                }
                if (!plate_config.has("other_layers_print_sequence")) {
                    m_config->set_key_value("other_layers_sequence_choice", new ConfigOptionEnum<LayerSeq>(flsAuto));
                }
                else {
                    replace(m_all_keys.begin(), m_all_keys.end(), std::string("other_layers_print_sequence"), std::string("other_layers_sequence_choice"));
                    m_config->set_key_value("other_layers_sequence_choice", new ConfigOptionEnum<LayerSeq>(flsCutomize));
                }
                notify_changed(plate_item.first);
            }
        }
    }

    toggle_options();
    if (m_active_page)
        m_active_page->update_visibility(m_mode, true); // for taggle line
    update_dirty();
    TabPrint::reload_config();
    //update();
    if (!m_null_keys.empty()) {
        if (m_active_page) {
            auto null_keys = m_null_keys;
            filter_diff_option(null_keys);
            for (auto k : null_keys) {
                auto f = m_active_page->get_field(k);
                if (f)
                    f->set_value(boost::any(), false);
            }
        }
    }
}

void TabPrintModel::reset_model_config()
{
    if (m_object_configs.empty()) return;
    wxGetApp().plater()->take_snapshot(std::string("Reset Options"));
    for (auto config : m_object_configs) {
        auto rmkeys = intersect(m_keys, config.second->keys());
        for (auto& k : rmkeys) {
            config.second->erase(k);
        }
        notify_changed(config.first);
    }
    update_model_config();
    wxGetApp().mainframe->on_config_changed(m_config);
}

bool TabPrintModel::has_key(std::string const& key)
{
    return std::find(m_keys.begin(), m_keys.end(), key) != m_keys.end();
}

void TabPrintModel::activate_selected_page(std::function<void()> throw_if_canceled)
{
    TabPrint::activate_selected_page(throw_if_canceled);
    if (m_active_page) {
        auto null_keys = m_null_keys;
        filter_diff_option(null_keys);
        for (auto k : null_keys) {
            auto f = m_active_page->get_field(k);
            if (f)
                f->set_value(boost::any(), false);
        }
    }
}

void TabPrintModel::on_value_change(const std::string& opt_id, const boost::any& value)
{
    // TODO: support opt_index, translate by OptionsGroup's m_opt_map
    if (m_config_manipulation.is_applying()) {
        TabPrint::on_value_change(opt_id, value);
        return;
    }
    auto opt_key   = opt_id;
    auto opt_id2   = opt_id;
    int opt_index = -1;
    if (auto n = opt_key.find('#'); n != std::string::npos) {
        opt_key = opt_key.substr(0, n);
        auto iter = m_active_page->m_opt_id_map.lower_bound(opt_key);
        assert(iter != m_active_page->m_opt_id_map.end() && iter->second == opt_id);
        opt_id2 = iter->first;
        opt_index = std::atoi(opt_id2.c_str() + n + 1);
    }
    if (!has_key(opt_key))
        return;
    if (!m_object_configs.empty())
        wxGetApp().plater()->take_snapshot((boost::format("Change Option %s") % opt_id2).str());
    auto inull = std::find(m_null_keys.begin(), m_null_keys.end(), opt_id2);
    // always add object config
    bool set   = true; // *m_config->option(k) != *m_prints.get_selected_preset().config.option(k) || inull != m_null_keys.end();
    auto tab_opt = dynamic_cast<ConfigOptionVectorBase *>(m_config->option(opt_key));
    static std::map<ConfigOptionType, ConfigOptionVectorBase const *> null_vecs {
        {coBools, new ConfigOptionBoolsNullable(std::initializer_list<unsigned char>{ConfigOptionBoolsNullable::nil_value()})},
        {coInts, new ConfigOptionIntsNullable(1, ConfigOptionIntsNullable::nil_value())},
        {coFloats, new ConfigOptionFloatsNullable(1, ConfigOptionFloatsNullable::nil_value())},
        {coPercents, new ConfigOptionPercentsNullable(1, ConfigOptionPercentsNullable::nil_value())},
        {coFloatsOrPercents, new ConfigOptionFloatsOrPercentsNullable(1, ConfigOptionFloatsOrPercentsNullable::nil_value())}
    };
    if (m_back_to_sys) {
        if (opt_key == opt_id) {
            for (auto config : m_object_configs)
                config.second->erase(opt_key);
        } else {
            for (auto config : m_object_configs) {
                auto opt  = config.second->option(opt_key);
                if (opt) {
                    auto opt2 = opt->clone();
                    dynamic_cast<ConfigOptionVectorBase *>(opt2)->resize(tab_opt->size());
                    dynamic_cast<ConfigOptionVectorBase *>(opt2)->set_at(null_vecs[tab_opt->type()], opt_index, 0);
                    if (opt2->is_nil()) {
                        delete opt2;
                        config.second->erase(opt_key);
                    } else {
                        config.second->set_key_value(opt_key, opt2);
                    }
                }
            }
        }
        m_all_keys.erase(std::remove(m_all_keys.begin(), m_all_keys.end(), opt_id2), m_all_keys.end());
    } else if (set) {
        if (opt_key == opt_id) {
            for (auto config : m_object_configs)
                config.second->apply_only(*m_config, {opt_key});
        } else {
            for (auto config : m_object_configs) {
                auto opt = config.second->option(opt_key);
                auto opt2 = opt ? opt->clone() : null_vecs[tab_opt->type()]->clone();
                dynamic_cast<ConfigOptionVectorBase *>(opt2)->resize(tab_opt->size());
                dynamic_cast<ConfigOptionVectorBase *>(opt2)->set_at(tab_opt, opt_index, opt_index);
                config.second->set_key_value(opt_key, opt2);
            }
        }
        m_all_keys = concat(m_all_keys, {opt_id2});
    }
    if (inull != m_null_keys.end())
        m_null_keys.erase(inull);
    if (m_back_to_sys || set) update_changed_ui();
    m_back_to_sys = false;
    TabPrint::on_value_change(opt_id, value);
    for (auto config : m_object_configs) {
        config.second->touch();
        notify_changed(config.first);
    }
    wxGetApp().params_panel()->notify_object_config_changed();
}

void TabPrintModel::reload_config()
{
    TabPrint::reload_config();
    if (m_active_page) {
        auto null_keys = m_null_keys;
        filter_diff_option(null_keys);
        for (auto k : null_keys) {
            auto f = m_active_page->get_field(k);
            if (f) f->set_value(boost::any(), false);
        }
    }
    auto keys          = m_config_manipulation.applying_keys();
    bool super_changed = false;
    for (auto & k : keys) {
        if (has_key(k)) {
            auto inull = std::find(m_null_keys.begin(), m_null_keys.end(), k);
            bool set   = *m_config->option(k) != *m_prints.get_selected_preset().config.option(k) || inull != m_null_keys.end();
            if (set) {
                for (auto config : m_object_configs)
                    config.second->apply_only(*m_config, {k});
                m_all_keys = concat(m_all_keys, {k});
            }
            if (inull != m_null_keys.end()) m_null_keys.erase(inull);
        } else {
            m_parent_tab->m_config->apply_only(*m_config, {k});
            super_changed = true;
        }
    }
    if (super_changed) {
        m_parent_tab->update_dirty();
        m_parent_tab->reload_config();
        m_parent_tab->update();
    }
}

void TabPrintModel::update_custom_dirty(std::vector<std::string> &dirty_options, std::vector<std::string> &nonsys_options)
{
    dirty_options = concat(dirty_options, m_null_keys);
    nonsys_options = concat(nonsys_options, m_null_keys);
    nonsys_options = concat(nonsys_options, m_all_keys);
}

//BBS: GUI refactor
TabPrintPlate::TabPrintPlate(ParamsPanel* parent) :
    TabPrintModel(parent, plate_keys)
{
    m_parent_tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
    m_type = Preset::TYPE_PLATE;
    m_keys = concat(m_keys, plate_keys);
}

void TabPrintPlate::build()
{
    m_presets = &m_prints;
    load_initial_data();

    m_config->option("curr_bed_type", true);
    if (m_preset_bundle->project_config.has("curr_bed_type")) {
        BedType global_bed_type = m_preset_bundle->project_config.opt_enum<BedType>("curr_bed_type");
        global_bed_type = BedType(global_bed_type - 1);
        m_config->set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(global_bed_type));
    }
    m_config->option("first_layer_sequence_choice", true);
    m_config->option("first_layer_print_sequence", true);
    m_config->option("other_layers_print_sequence", true);
    m_config->option("other_layers_sequence_choice", true);

    auto page = add_options_page(L("Plate Settings"), "empty");
    auto optgroup = page->new_optgroup("");
    optgroup->append_single_option_line("curr_bed_type");
    optgroup->append_single_option_line("print_sequence", "sequent-print");
    optgroup->append_single_option_line("spiral_mode", "spiral-vase");
    optgroup->append_single_option_line("first_layer_sequence_choice", "parameter/filament-sequence-for-different-layers");
    optgroup->append_single_option_line("other_layers_sequence_choice", "parameter/filament-sequence-for-different-layers");

    for (auto& line : const_cast<std::vector<Line>&>(optgroup->get_lines())) {
        line.undo_to_sys = true;
    }
    optgroup->have_sys_config = [this] { m_back_to_sys = true; return true; };
}

void TabPrintPlate::reset_model_config()
{
    if (m_object_configs.empty()) return;
    wxGetApp().plater()->take_snapshot(std::string("Reset Options"));
    for (auto plate_item : m_object_configs) {
        auto rmkeys = intersect(m_keys, plate_item.second->keys());
        for (auto& k : rmkeys) {
            plate_item.second->erase(k);
        }
        auto plate = dynamic_cast<PartPlate*>(plate_item.first);
        plate->reset_bed_type();
        plate->set_print_seq(PrintSequence::ByDefault);
        plate->set_first_layer_print_sequence({});
        plate->set_spiral_vase_mode(false, true);
        notify_changed(plate_item.first);
    }
    update_model_config();
    wxGetApp().mainframe->on_config_changed(m_config);
}

void TabPrintPlate::on_value_change(const std::string& opt_key, const boost::any& value)
{
    auto k = opt_key;
    if (m_config_manipulation.is_applying()) {
        return;
    }
    if (!has_key(k))
        return;
    if (!m_object_configs.empty())
        wxGetApp().plater()->take_snapshot((boost::format("Change Option %s") % k).str());
    bool set = true;
    if (m_back_to_sys) {
        for (auto plate_item : m_object_configs) {
            plate_item.second->erase(k);
            auto plate = dynamic_cast<PartPlate*>(plate_item.first);
            if (k == "curr_bed_type")
                plate->reset_bed_type();
            if (k == "print_sequence")
                plate->set_print_seq(PrintSequence::ByDefault);
            if (k == "first_layer_sequence_choice")
                plate->set_first_layer_print_sequence({});
            if (k == "other_layers_sequence_choice")
                plate->set_other_layers_print_sequence({});
            if (k == "spiral_mode")
                plate->set_spiral_vase_mode(false, true);
        }
        m_all_keys.erase(std::remove(m_all_keys.begin(), m_all_keys.end(), k), m_all_keys.end());
    }
    else if (set) {
        for (auto plate_item : m_object_configs) {
            plate_item.second->apply_only(*m_config, { k });
            auto plate = dynamic_cast<PartPlate*>(plate_item.first);
            BedType bed_type;
            PrintSequence print_seq;
            LayerSeq first_layer_seq_choice;
            LayerSeq other_layer_seq_choice;
            if (k == "curr_bed_type") {
                bed_type = m_config->opt_enum<BedType>("curr_bed_type");
                plate->set_bed_type(BedType(bed_type));
            }
            if (k == "print_sequence") {
                print_seq = m_config->opt_enum<PrintSequence>("print_sequence");
                plate->set_print_seq(print_seq);
            }
            if (k == "first_layer_sequence_choice") {
                first_layer_seq_choice = m_config->opt_enum<LayerSeq>("first_layer_sequence_choice");
                if (first_layer_seq_choice == LayerSeq::flsAuto) {
                    plate->set_first_layer_print_sequence({});
                }
                else if (first_layer_seq_choice == LayerSeq::flsCutomize) {
                    const DynamicPrintConfig& plate_config = plate_item.second->get();
                    if (!plate_config.has("first_layer_print_sequence")) {
                        std::vector<int> initial_sequence;
                        for (int i = 0; i < wxGetApp().filaments_cnt(); i++) {
                            initial_sequence.push_back(i + 1);
                        }
                        plate->set_first_layer_print_sequence(initial_sequence);
                    }
                    wxCommandEvent evt(EVT_OPEN_PLATESETTINGSDIALOG);
                    evt.SetInt(plate->get_index());
                    evt.SetString("only_layer_sequence");
                    evt.SetEventObject(wxGetApp().plater());
                    wxPostEvent(wxGetApp().plater(), evt);
                }
            }
            if (k == "other_layers_sequence_choice") {
                other_layer_seq_choice = m_config->opt_enum<LayerSeq>("other_layers_sequence_choice");
                if (other_layer_seq_choice == LayerSeq::flsAuto) {
                    plate->set_other_layers_print_sequence({});
                }
                else if (other_layer_seq_choice == LayerSeq::flsCutomize) {
                    const DynamicPrintConfig& plate_config = plate_item.second->get();
                    if (!plate_config.has("other_layers_print_sequence")) {
                        std::vector<int> initial_sequence;
                        for (int i = 0; i < wxGetApp().filaments_cnt(); i++) {
                            initial_sequence.push_back(i + 1);
                        }
                        std::vector<LayerPrintSequence> initial_layer_sequence{ std::make_pair(std::make_pair(2, INT_MAX), initial_sequence) };
                        plate->set_other_layers_print_sequence(initial_layer_sequence);
                    }
                    wxCommandEvent evt(EVT_OPEN_PLATESETTINGSDIALOG);
                    evt.SetInt(plate->get_index());
                    evt.SetString("only_layer_sequence");
                    evt.SetEventObject(wxGetApp().plater());
                    wxPostEvent(wxGetApp().plater(), evt);
                }
            }
            if (k == "spiral_mode") {
                plate->set_spiral_vase_mode(m_config->opt_bool("spiral_mode"), false);
            }
        }
        m_all_keys = concat(m_all_keys, { k });
    }
    if (m_back_to_sys || set) update_changed_ui();
    m_back_to_sys = false;
    for (auto plate_item : m_object_configs) {
        plate_item.second->touch();
        notify_changed(plate_item.first);
    }

    wxGetApp().params_panel()->notify_object_config_changed();
    update();
}

void TabPrintPlate::notify_changed(ObjectBase* object)
{
    auto plate = dynamic_cast<PartPlate*>(object);
    auto objects_list = wxGetApp().obj_list();
    wxDataViewItemArray items;
    objects_list->GetSelections(items);
    for (auto item : items) {
        if (objects_list->GetModel()->GetItemType(item) == itPlate) {
            ObjectDataViewModelNode* node = static_cast<ObjectDataViewModelNode*>(item.GetID());
            if (node)
                node->set_action_icon(!m_all_keys.empty());
        }
    }
}

void TabPrintPlate::update_custom_dirty(std::vector<std::string> &dirty_options, std::vector<std::string> &nonsys_options)
{
    TabPrintModel::update_custom_dirty(dirty_options, nonsys_options);
    for (auto k : m_all_keys) {
        if (k == "first_layer_sequence_choice" || k == "other_layers_sequence_choice") {
            if (m_config->opt_enum<LayerSeq>("first_layer_sequence_choice") != LayerSeq::flsAuto) {
                dirty_options.push_back(k);
            }
            if (m_config->opt_enum<LayerSeq>("other_layers_sequence_choice") != LayerSeq::flsAuto) {
                dirty_options.push_back(k);
            }
        }
        if (k == "curr_bed_type") {
            DynamicConfig& global_cfg = m_preset_bundle->project_config;
            if (global_cfg.has("curr_bed_type")) {
                BedType global_bed_type = global_cfg.opt_enum<BedType>("curr_bed_type");
                if (m_config->opt_enum<BedType>("curr_bed_type") != global_bed_type) {
                    dirty_options.push_back(k);
                }
            }
        }
    }
}

TabPrintObject::TabPrintObject(ParamsPanel* parent) :
    TabPrintModel(parent, concat(PrintObjectConfig().keys(), PrintRegionConfig().keys()))
{
    m_parent_tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
}

void TabPrintObject::notify_changed(ObjectBase * object)
{
    auto obj = dynamic_cast<ModelObject*>(object);
    wxGetApp().obj_list()->object_config_options_changed({obj, nullptr});
}

//BBS: GUI refactor

TabPrintPart::TabPrintPart(ParamsPanel* parent) :
    TabPrintModel(parent, PrintRegionConfig().keys())
{
    m_parent_tab = wxGetApp().get_model_tab();
}

void TabPrintPart::notify_changed(ObjectBase * object)
{
    auto vol = dynamic_cast<ModelVolume*>(object);
    wxGetApp().obj_list()->object_config_options_changed({vol->get_object(), vol});
}

static std::string layer_height = "layer_height";
TabPrintLayer::TabPrintLayer(ParamsPanel* parent) :
    TabPrintModel(parent, concat({ layer_height }, PrintRegionConfig().keys()))
{
    m_parent_tab = wxGetApp().get_model_tab();
}

void TabPrintLayer::notify_changed(ObjectBase * object)
{
    for (auto config : m_object_configs) {
        if (!config.second->has(layer_height)) {
            auto option = m_parent_tab->get_config()->option(layer_height);
            config.second->set_key_value(layer_height, option->clone());
        }
        auto objects_list = wxGetApp().obj_list();
        wxDataViewItemArray items;
        objects_list->GetSelections(items);
        for (auto item : items)
            objects_list->add_settings_item(item, &config.second->get());
    }
}

void TabPrintLayer::update_custom_dirty(std::vector<std::string> &dirty_options, std::vector<std::string> &nonsys_options)
{
    TabPrintModel::update_custom_dirty(dirty_options, nonsys_options);
    auto option = m_parent_tab->get_config()->option(layer_height);
    for (auto config : m_object_configs) {
        if (!config.second->has(layer_height)) {
            config.second->set_key_value(layer_height, option->clone());
            dirty_options.erase(std::remove(dirty_options.begin(), dirty_options.end(), layer_height), dirty_options.end());
            nonsys_options.erase(std::remove(nonsys_options.begin(), nonsys_options.end(), layer_height), nonsys_options.end());
        }
        else if (config.second->opt_float(layer_height) == option->getFloat()) {
            dirty_options.erase(std::remove(dirty_options.begin(), dirty_options.end(), layer_height), dirty_options.end());
            nonsys_options.erase(std::remove(nonsys_options.begin(), nonsys_options.end(), layer_height), nonsys_options.end());
        }
    }
}

bool Tab::validate_custom_gcode(const wxString& title, const std::string& gcode)
{
    std::vector<std::string> tags;
    bool invalid = GCodeProcessor::contains_reserved_tags(gcode, 5, tags);
    if (invalid) {
        std::string lines = ":\n";
        for (const std::string& keyword : tags)
            lines += ";" + keyword + "\n";
        wxString reports = format_wxstr(
            _L_PLURAL("Following line %s contains reserved keywords.\nPlease remove it, or will beat G-code visualization and printing time estimation.",
                      "Following lines %s contain reserved keywords.\nPlease remove them, or will beat G-code visualization and printing time estimation.",
                      tags.size()),
            lines);
        //wxMessageDialog dialog(wxGetApp().mainframe, reports, _L("Found reserved keywords in") + " " + _(title), wxICON_WARNING | wxOK);
        MessageDialog dialog(wxGetApp().mainframe, reports, _L("Reserved keywords found") + " " + _(title), wxICON_WARNING | wxOK);
        dialog.ShowModal();
    }
    return !invalid;
}

static void validate_custom_gcode_cb(Tab* tab, ConfigOptionsGroupShp opt_group, const t_config_option_key& opt_key, const boost::any& value) {
    tab->validate_custom_gcodes_was_shown = !Tab::validate_custom_gcode(opt_group->title, boost::any_cast<std::string>(value));
    tab->update_dirty();
    tab->on_value_change(opt_key, value);
    if (boost::any_cast<std::string>(value).size() > 40 * 1024) {
        MessageDialog dialog(static_cast<wxWindow *>(wxGetApp().mainframe), _L("Custom G-code files are too large, and may not be synchronized to the cloud. Please keep it within 40k."), "", wxICON_WARNING | wxOK);
        dialog.ShowModal();
    }
}

void TabFilament::add_filament_overrides_page()
{
    //BBS
    PageShp page = add_options_page(L("Setting Overrides"), "empty");
    ConfigOptionsGroupShp optgroup = page->new_optgroup(L("Retraction"), L"param_retraction");

    auto append_single_option_line = [optgroup, this](const std::string& opt_key, int opt_index)
    {
        Line line {"",""};
        //BBS
        line = optgroup->create_single_option_line(optgroup->get_option(opt_key, opt_index));

        line.near_label_widget = [this, optgroup, opt_key, opt_index](wxWindow* parent) {
            wxCheckBox* check_box = new wxCheckBox(parent, wxID_ANY, "");

            check_box->Bind(wxEVT_CHECKBOX, [optgroup, opt_key, opt_index](wxCommandEvent& evt) {
                const bool is_checked = evt.IsChecked();
                Field* field = optgroup->get_fieldc(opt_key, opt_index);
                if (field != nullptr) {
                    field->toggle(is_checked);
                    if (is_checked)
                        field->set_last_meaningful_value();
                    else
                        field->set_na_value();
                }
            }, check_box->GetId());

            m_overrides_options[opt_key] = check_box;
            return check_box;
        };

        optgroup->append_line(line);
    };

    const int extruder_idx = 0; // #ys_FIXME

    for (const std::string opt_key : {  "filament_retraction_length",
                                        "filament_z_hop",
                                        "filament_z_hop_types",
                                        "filament_retraction_speed",
                                        "filament_deretraction_speed",
                                        //"filament_retract_restart_extra",
                                        "filament_retraction_minimum_travel",
                                        "filament_retract_when_changing_layer",
                                        "filament_wipe",
                                        //BBS
                                        "filament_wipe_distance",
                                        "filament_retract_before_wipe",
                                        "filament_long_retractions_when_cut",
                                        "filament_retraction_distances_when_cut"
                                     })
        append_single_option_line(opt_key, extruder_idx);
}

void TabFilament::update_filament_overrides_page()
{
    if (!m_active_page || m_active_page->title() != "Setting Overrides")
        return;

    //BBS: GUI refactor
    if (m_overrides_options.size() <= 0)
        return;

    Page* page = m_active_page;

    const auto og_it = std::find_if(page->m_optgroups.begin(), page->m_optgroups.end(), [](const ConfigOptionsGroupShp og) { return og->title == "Retraction"; });
    if (og_it == page->m_optgroups.end())
        return;
    ConfigOptionsGroupShp optgroup = *og_it;

    std::vector<std::string> opt_keys = {   "filament_retraction_length",
                                            "filament_z_hop",
                                            "filament_z_hop_types",
                                            "filament_retraction_speed",
                                            "filament_deretraction_speed",
                                            //"filament_retract_restart_extra",
                                            "filament_retraction_minimum_travel",
                                            "filament_retract_when_changing_layer",
                                            "filament_wipe",
                                            //BBS
                                            "filament_wipe_distance",
                                            "filament_retract_before_wipe",
                                            "filament_long_retractions_when_cut",
                                            "filament_retraction_distances_when_cut"
                                        };

    const int selection = m_variant_combo->GetSelection();
    auto opt = dynamic_cast<ConfigOptionVectorBase *>(m_config->option("filament_retraction_length"));
    const int extruder_idx = selection < 0 || selection >= static_cast<int>(opt->size()) ? 0 : selection;

    const bool have_retract_length = dynamic_cast<ConfigOptionVectorBase *>(m_config->option("filament_retraction_length"))->is_nil(extruder_idx) ||
                                     m_config->opt_float_nullable("filament_retraction_length", extruder_idx) > 0;

    for (const std::string& opt_key : opt_keys)
    {
        bool is_checked = opt_key=="filament_retraction_length" ? true : have_retract_length;
        m_overrides_options[opt_key]->Enable(is_checked);

        is_checked &= !dynamic_cast<ConfigOptionVectorBase*>(m_config->option(opt_key))->is_nil(extruder_idx);
        m_overrides_options[opt_key]->SetValue(is_checked);

        Field* field = optgroup->get_fieldc(opt_key, 0);
        if (field != nullptr) {
            if (opt_key == "filament_long_retractions_when_cut") {
                int machine_enabled_level = m_preset_bundle->printers.get_edited_preset().config.option<ConfigOptionInt>("enable_long_retraction_when_cut")->value;
                bool machine_enabled = machine_enabled_level == LongRectrationLevel::EnableFilament;
                toggle_line(opt_key, machine_enabled, extruder_idx + 256);
                field->toggle(is_checked && machine_enabled);
            }
            else if (opt_key == "filament_retraction_distances_when_cut") {
                int machine_enabled_level = m_preset_bundle->printers.get_edited_preset().config.option<ConfigOptionInt>("enable_long_retraction_when_cut")->value;
                bool machine_enabled = machine_enabled_level == LongRectrationLevel::EnableFilament;
                bool filament_enabled = m_config->option<ConfigOptionBoolsNullable>("filament_long_retractions_when_cut")->values[extruder_idx] == 1;
                toggle_line(opt_key, filament_enabled && machine_enabled, extruder_idx + 256);
                field->toggle(is_checked && filament_enabled && machine_enabled);
            }
            else
                field->toggle(is_checked);
        }
    }
}

void TabFilament::build()
{
    m_presets = &m_preset_bundle->filaments;
    load_initial_data();

    auto page = add_options_page(L("Filament"), "spool");
        //BBS
        auto optgroup = page->new_optgroup(L("Basic information"), L"param_information");
        // Set size as all another fields for a better alignment
        Option option = optgroup->get_option("filament_type");
        option.opt.width = Field::def_width_wider();
        optgroup->append_single_option_line(option);
        optgroup->append_single_option_line("filament_vendor");
        optgroup->append_single_option_line("filament_soluble");
        // BBS
        optgroup->append_single_option_line("filament_is_support");
        optgroup->append_single_option_line("impact_strength_z");
        optgroup->append_single_option_line("filament_change_length", "parameter/prime-tower#ramming");

        optgroup->append_single_option_line("filament_prime_volume", "parameter/prime-tower#primevolume");
        //optgroup->append_single_option_line("filament_colour");
        optgroup->append_single_option_line("required_nozzle_HRC");
        optgroup->append_single_option_line("default_filament_colour");
        optgroup->append_single_option_line("filament_diameter");
        optgroup->append_single_option_line("filament_adhesiveness_category", "parameter/prime-tower#adhesiveness");
        optgroup->append_single_option_line("filament_flow_ratio", "", 0);
        optgroup->append_single_option_line("enable_pressure_advance");
        optgroup->append_single_option_line("pressure_advance");
        optgroup->append_single_option_line("filament_density");
        optgroup->append_single_option_line("filament_shrink");
        optgroup->append_single_option_line("filament_cost");

        //BBS
        optgroup->append_single_option_line("temperature_vitrification");
        optgroup->append_single_option_line("filament_ramming_travel_time", "",0);
        optgroup->append_single_option_line("filament_pre_cooling_temperature", "parameter/prime-tower#precooling", 0);
        Line line = { L("Recommended nozzle temperature"), L("Recommended nozzle temperature range of this filament. 0 means no set") };
        line.append_option(optgroup->get_option("nozzle_temperature_range_low"));
        line.append_option(optgroup->get_option("nozzle_temperature_range_high"));
        optgroup->append_line(line);

        optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value) {
            DynamicPrintConfig &filament_config = m_preset_bundle->filaments.get_edited_preset().config;

            update_dirty();
            if (!m_postpone_update_ui && (opt_key == "nozzle_temperature_range_low" || opt_key == "nozzle_temperature_range_high")) {
                m_config_manipulation.check_nozzle_recommended_temperature_range(&filament_config);
            }
            on_value_change(opt_key, value);
        };


        optgroup = page->new_optgroup(L("Print temperature"), L"param_temperature");
        optgroup->append_single_option_line("chamber_temperatures","chamber-temperature");


        line = {L("Bambu Cool Plate SuperTack"), L("Bed temperature when cool plate is installed. Value 0 means the filament does not support to print on the Bambu Cool Plate SuperTack")};
        line.append_option(optgroup->get_option("supertack_plate_temp_initial_layer"));
        line.append_option(optgroup->get_option("supertack_plate_temp"));
        optgroup->append_line(line);

        line = { L("Cool Plate"), L("Bed temperature when cool plate is installed. Value 0 means the filament does not support to print on the Cool Plate") };
        line.append_option(optgroup->get_option("cool_plate_temp_initial_layer"));
        line.append_option(optgroup->get_option("cool_plate_temp"));
        optgroup->append_line(line);

        line = { L("Engineering Plate"), L("Bed temperature when engineering plate is installed. Value 0 means the filament does not support to print on the Engineering Plate") };
        line.append_option(optgroup->get_option("eng_plate_temp_initial_layer"));
        line.append_option(optgroup->get_option("eng_plate_temp"));
        optgroup->append_line(line);

        line = {L("Smooth PEI Plate / High Temp Plate"), L("Bed temperature when Smooth PEI Plate/High temperature plate is installed. Value 0 means the filament does not support to print on the Smooth PEI Plate/High Temp Plate") };
        line.append_option(optgroup->get_option("hot_plate_temp_initial_layer"));
        line.append_option(optgroup->get_option("hot_plate_temp"));
        optgroup->append_line(line);

        line = {L("Textured PEI Plate"), L("Bed temperature when Textured PEI Plate is installed. Value 0 means the filament does not support to print on the Textured PEI Plate")};
        line.append_option(optgroup->get_option("textured_plate_temp_initial_layer"));
        line.append_option(optgroup->get_option("textured_plate_temp"));
        optgroup->append_line(line);

        optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value)
        {
            DynamicPrintConfig& filament_config = m_preset_bundle->filaments.get_edited_preset().config;

            update_dirty();
            /*if (opt_key == "cool_plate_temp" || opt_key == "cool_plate_temp_initial_layer") {
                m_config_manipulation.check_bed_temperature_difference(BedType::btPC, &filament_config);
            }
            else if (opt_key == "eng_plate_temp" || opt_key == "eng_plate_temp_initial_layer") {
                m_config_manipulation.check_bed_temperature_difference(BedType::btEP, &filament_config);
            }
            else if (opt_key == "hot_plate_temp" || opt_key == "hot_plate_temp_initial_layer") {
                m_config_manipulation.check_bed_temperature_difference(BedType::btPEI, &filament_config);
            }
            else if (opt_key == "textured_plate_temp" || opt_key == "textured_plate_temp_initial_layer") {
                m_config_manipulation.check_bed_temperature_difference(BedType::btPTE, &filament_config);
            }
            else */if (opt_key == "nozzle_temperature") {
                m_config_manipulation.check_nozzle_temperature_range(&filament_config);
            }
            else if (opt_key == "nozzle_temperature_initial_layer") {
                m_config_manipulation.check_nozzle_temperature_initial_layer_range(&filament_config);
            }
            else if (opt_key == "chamber_temperatures") {
                m_config_manipulation.check_chamber_temperature(&filament_config);
            }

            on_value_change(opt_key, value);
        };

        line = { L("Nozzle"), L("Nozzle temperature when printing") };
        line.append_option(optgroup->get_option("nozzle_temperature_initial_layer", 0));
        line.append_option(optgroup->get_option("nozzle_temperature", 0));
        optgroup->append_line(line);

        //BBS
        optgroup = page->new_optgroup(L("Volumetric speed limitation"), L"param_volumetric_speed");
        optgroup->append_single_option_line("filament_max_volumetric_speed", "", 0);
        optgroup->append_single_option_line("filament_ramming_volumetric_speed", "",0);

        // BBS
        optgroup = page->new_optgroup(L("Filament scarf seam settings"), L"param_volumetric_speed");
        optgroup->append_single_option_line("filament_scarf_seam_type");
        optgroup->append_single_option_line("filament_scarf_height");
        optgroup->append_single_option_line("filament_scarf_gap");
        optgroup->append_single_option_line("filament_scarf_length");

        //optgroup = page->new_optgroup(L("Filament circle compensation setting"), L"param_volumetric_speed");
        //optgroup->append_single_option_line("circle_compensation_speed");
        //optgroup->append_single_option_line("counter_coef_1");
        //optgroup->append_single_option_line("counter_coef_2");
        //optgroup->append_single_option_line("counter_coef_3");
        //optgroup->append_single_option_line("hole_coef_1");
        //optgroup->append_single_option_line("hole_coef_2");
        //optgroup->append_single_option_line("hole_coef_3");
        //optgroup->append_single_option_line("counter_limit_min");
        //optgroup->append_single_option_line("counter_limit_max");
        //optgroup->append_single_option_line("hole_limit_min");
        //optgroup->append_single_option_line("hole_limit_max");
        //optgroup->append_single_option_line("diameter_limit");
        //line = { "", "" };
        //line.full_width = 1;
        //line.widget = [this](wxWindow* parent) {
        //    return description_line_widget(parent, &m_volumetric_speed_description_line);
        //};
        //optgroup->append_line(line);

    page = add_options_page(L("Cooling"), "empty");

        //line = { "", "" };
        //line.full_width = 1;
        //line.widget = [this](wxWindow* parent) {
        //    return description_line_widget(parent, &m_cooling_description_line);
        //};
        //optgroup->append_line(line);
        optgroup = page->new_optgroup(L("Cooling for specific layer"), L"param_cooling");
    optgroup->append_single_option_line("close_fan_the_first_x_layers", "auto-cooling");
        //optgroup->append_single_option_line("full_fan_speed_layer");

        optgroup = page->new_optgroup(L("Part cooling fan"), L"param_cooling_fan");
        line = { L("Min fan speed threshold"), L("Part cooling fan speed will start to run at min speed when the estimated layer time is no longer than the layer time in setting. When layer time is shorter than threshold, fan speed is interpolated between the minimum and maximum fan speed according to layer printing time") };
        line.label_path = "auto-cooling";
        line.append_option(optgroup->get_option("fan_min_speed"));
        line.append_option(optgroup->get_option("fan_cooling_layer_time"));
        optgroup->append_line(line);
        line = { L("Max fan speed threshold"), L("Part cooling fan speed will be max when the estimated layer time is shorter than the setting value") };
        line.label_path = "auto-cooling";
        line.append_option(optgroup->get_option("fan_max_speed"));
        line.append_option(optgroup->get_option("slow_down_layer_time"));
        optgroup->append_line(line);
        optgroup->append_single_option_line("reduce_fan_stop_start_freq", "auto-cooling");
        optgroup->append_single_option_line("slow_down_for_layer_cooling", "auto-cooling");
        optgroup->append_single_option_line("slow_down_min_speed","auto-cooling");

        optgroup->append_single_option_line("enable_overhang_bridge_fan", "auto-cooling");
        optgroup->append_single_option_line("overhang_fan_threshold", "auto-cooling");
        optgroup->append_single_option_line("overhang_threshold_participating_cooling", "auto-cooling");
        optgroup->append_single_option_line("overhang_fan_speed", "auto-cooling");
        optgroup->append_single_option_line("pre_start_fan_time", "auto-cooling");

        optgroup = page->new_optgroup(L("Auxiliary part cooling fan"), L"param_cooling_fan");
        optgroup->append_single_option_line("additional_cooling_fan_speed");

        optgroup = page->new_optgroup(L("Exhaust fan"),L"param_cooling_fan");

        optgroup->append_single_option_line("activate_air_filtration");

        line = {L("During print"), ""};
        line.append_option(optgroup->get_option("during_print_exhaust_fan_speed"));
        optgroup->append_line(line);


        line = {L("Complete print"), ""};
        line.append_option(optgroup->get_option("complete_print_exhaust_fan_speed"));
        optgroup->append_line(line);
        //BBS
        add_filament_overrides_page();
#if 0
    //page = add_options_page(L("Advanced"), "advanced");
    //    optgroup = page->new_optgroup(L("Wipe tower parameters"));
    //    optgroup->append_single_option_line("filament_minimal_purge_on_wipe_tower");
#endif

        const int gcode_field_height = 15; // 150
        const int notes_field_height = 25; // 250

    page = add_options_page(L("Advanced"), "advanced");
        optgroup = page->new_optgroup(L("Filament start G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("filament_start_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;// 150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Filament end G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("filament_end_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;// 150;
        optgroup->append_single_option_line(option);

    page = add_options_page(L("Notes"), "note");
        optgroup = page->new_optgroup(L("Notes"),"note");
        optgroup->label_width = 0;
        option = optgroup->get_option("filament_notes");
        option.opt.full_width = true;
        option.opt.height = notes_field_height;
        optgroup->append_single_option_line(option);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key &opt_key, const boost::any &value) { validate_custom_note_cb(this, optgroup, opt_key, value); };


    page = add_options_page(L("Multi Filament"), "advanced");
        optgroup = page->new_optgroup(L("Multi Filament"));
        // optgroup->append_single_option_line("filament_flush_temp", "", 0);
        // optgroup->append_single_option_line("filament_flush_volumetric_speed", "", 0);
        optgroup->append_single_option_line("long_retractions_when_ec", "" , 0);
        optgroup->append_single_option_line("retraction_distances_when_ec", "" , 0);
        //BBS
#if 0
    //page = add_options_page(L("Dependencies"), "advanced");
    //    optgroup = page->new_optgroup(L("Profile dependencies"));
    //    create_line_with_widget(optgroup.get(), "compatible_printers", "", [this](wxWindow* parent) {
    //        return compatible_widget_create(parent, m_compatible_printers);
    //    });

    //    option = optgroup->get_option("compatible_printers_condition");
    //    option.opt.full_width = true;
    //    optgroup->append_single_option_line(option);

    //    create_line_with_widget(optgroup.get(), "compatible_prints", "", [this](wxWindow* parent) {
    //        return compatible_widget_create(parent, m_compatible_prints);
    //    });

    //    option = optgroup->get_option("compatible_prints_condition");
    //    option.opt.full_width = true;
    //    optgroup->append_single_option_line(option);

    //    build_preset_description_line(optgroup.get());
#endif
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabFilament::reload_config()
{
    this->compatible_widget_reload(m_compatible_printers);
    this->compatible_widget_reload(m_compatible_prints);
    Tab::reload_config();
}

//void TabFilament::update_volumetric_flow_preset_hints()
//{
//    wxString text;
//    try {
//        text = from_u8(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle));
//    } catch (std::exception &ex) {
//        text = _(L("Volumetric flow hints not available")) + "\n\n" + from_u8(ex.what());
//    }
//    m_volumetric_speed_description_line->SetText(text);
//}

void TabFilament::update_description_lines()
{
    Tab::update_description_lines();

    if (!m_active_page)
        return;

    if (m_active_page->title() == "Cooling" && m_cooling_description_line)
        m_cooling_description_line->SetText(from_u8(PresetHints::cooling_description(m_presets->get_edited_preset())));
    //BBS
    //if (m_active_page->title() == "Filament" && m_volumetric_speed_description_line)
    //    this->update_volumetric_flow_preset_hints();
}

void TabFilament::toggle_options()
{
    if (!m_active_page)
        return;

    bool is_BBL_printer = false;
    if (m_preset_bundle) {
        is_BBL_printer =
            m_preset_bundle->printers.get_edited_preset().is_bbl_vendor_preset(
                m_preset_bundle);
    }
    bool is_multi_extruder = m_preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->size() > 1;

    if (m_active_page->title() == "Cooling")
    {
        bool cooling = m_config->opt_bool("slow_down_for_layer_cooling", 0);
        toggle_option("slow_down_min_speed", cooling);

        bool has_enable_overhang_bridge_fan = m_config->opt_bool("enable_overhang_bridge_fan", 0);
        for (auto el : {"overhang_fan_speed", "pre_start_fan_time", "overhang_fan_threshold"})
            toggle_option(el, has_enable_overhang_bridge_fan);

        bool support_air_filtration = m_preset_bundle->printers.get_edited_preset().config.opt_bool("support_air_filtration");
        toggle_line("activate_air_filtration",is_BBL_printer && support_air_filtration);

        for (auto elem : { "during_print_exhaust_fan_speed","complete_print_exhaust_fan_speed" })
            toggle_line(elem, m_config->opt_bool("activate_air_filtration",0)&&support_air_filtration);

    }
    if (m_active_page->title() == "Filament")
    {
        toggle_option("filament_type", false);
        toggle_option("filament_vendor", false);
        toggle_option("impact_strength_z", false);
        //BBS: hide these useless option for bambu printer
        toggle_line("enable_pressure_advance", !is_BBL_printer);
        if (is_BBL_printer)
            toggle_line("pressure_advance", false);
        else {
            toggle_line("pressure_advance", true);
            toggle_option("pressure_advance", m_config->opt_bool("enable_pressure_advance", 0));
        }

        bool support_chamber_temp_control = this->m_preset_bundle->printers.get_edited_preset().config.opt_bool("support_chamber_temp_control");
        toggle_line("chamber_temperatures", support_chamber_temp_control);

        for (auto el : {"supertack_plate_temp", "supertack_plate_temp_initial_layer", "cool_plate_temp", "cool_plate_temp_initial_layer", "eng_plate_temp", "eng_plate_temp_initial_layer", "textured_plate_temp", "textured_plate_temp_initial_layer"})
            toggle_line(el, is_BBL_printer);
    }

    if (m_active_page->title() == "Multi Filament") {
        const int extruder_idx = m_variant_combo->GetSelection();
        toggle_line("long_retractions_when_ec", is_multi_extruder && is_BBL_printer, 256 + extruder_idx);
        toggle_line("retraction_distances_when_ec", is_multi_extruder && is_BBL_printer && m_config->opt_bool_nullable("long_retractions_when_ec", extruder_idx), 256 + extruder_idx);
    }

    if (m_active_page->title() == "Setting Overrides")
        update_filament_overrides_page();
}

void TabFilament::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME

    m_config_manipulation.check_filament_max_volumetric_speed(m_config);

    m_update_cnt++;

    update_description_lines();
    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();

    toggle_options();

    m_update_cnt--;

    if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabFilament::clear_pages()
{
    Tab::clear_pages();

    m_volumetric_speed_description_line = nullptr;
	m_cooling_description_line = nullptr;

    //BBS: GUI refactor
    m_overrides_options.clear();
}

wxSizer* Tab::description_line_widget(wxWindow* parent, ogStaticText* *StaticText, wxString text /*= wxEmptyString*/)
{
    *StaticText = new ogStaticText(parent, text);

//	auto font = (new wxSystemSettings)->GetFont(wxSYS_DEFAULT_GUI_FONT);
    (*StaticText)->SetFont(wxGetApp().normal_font());

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(*StaticText, 1, wxEXPAND|wxALL, 0);
    return sizer;
}

bool Tab::saved_preset_is_dirty() const { return m_presets->saved_is_dirty(); }
void Tab::update_saved_preset_from_current_preset() { m_presets->update_saved_preset_from_current_preset(); }
bool Tab::current_preset_is_dirty() const { return m_presets->current_is_dirty(); }

void TabPrinter::build()
{
    m_presets = &m_preset_bundle->printers;
    m_printer_technology = m_presets->get_selected_preset().printer_technology();

    // For DiffPresetDialog we use options list which is saved in Searcher class.
    // Options for the Searcher is added in the moment of pages creation.
    // So, build first of all printer pages for non-selected printer technology...
    //std::string def_preset_name = "- default " + std::string(m_printer_technology == ptSLA ? "FFF" : "SLA") + " -";
    std::string def_preset_name = "Default Printer";
    m_config = &m_presets->find_preset(def_preset_name)->config;
    m_printer_technology == ptSLA ? build_fff() : build_sla();
    if (m_printer_technology == ptSLA)
        m_extruders_count_old = 0;// revert this value

    // ... and than for selected printer technology
    load_initial_data();
    m_printer_technology == ptSLA ? build_sla() : build_fff();
}

void TabPrinter::build_fff()
{
    if (!m_pages.empty())
        m_pages.resize(0);
    // to avoid redundant memory allocation / deallocation during extruders count changing
    m_pages.reserve(30);

    auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloatsNullable*>(m_config->option("nozzle_diameter"));
    m_initial_extruders_count = m_extruders_count = nozzle_diameter->values.size();
    // BBS
    //wxGetApp().obj_list()->update_objects_list_filament_column(m_initial_extruders_count);

    const Preset* parent_preset = m_printer_technology == ptSLA ? nullptr // just for first build, if SLA printer preset is selected
                                  : m_presets->get_selected_preset_parent();
    m_sys_extruders_count = parent_preset == nullptr ? 0 :
            static_cast<const ConfigOptionFloatsNullable*>(parent_preset->config.option("nozzle_diameter"))->values.size();

    auto page = add_options_page(L("Basic information"), "printer");
        auto optgroup = page->new_optgroup(L("Printable space")/*, L"param_printable_space"*/);

        create_line_with_widget(optgroup.get(), "printable_area", "", [this](wxWindow* parent) {
            return 	create_bed_shape_widget(parent);
            });

        Option option = optgroup->get_option("bed_exclude_area");
        option.opt.full_width = true;
        optgroup->append_single_option_line(option);
        optgroup->append_single_option_line("printable_height");
        optgroup->append_single_option_line("best_object_pos");
        // todo: for multi_extruder test
        // BBS
#if 0
        //optgroup->append_single_option_line("z_offset");

        //optgroup = page->new_optgroup(L("Capabilities"));
        //ConfigOptionDef def;
        //    def.type =  coInt,
        //    def.set_default_value(new ConfigOptionInt(1));
        //    def.label = L("Extruders");
        //    def.tooltip = L("Number of extruders of the printer.");
        //    def.min = 1;
        //    def.max = 256;
        //    //BBS
        //    def.mode = comDevelop;
        //Option option(def, "extruders_count");
        //optgroup->append_single_option_line(option);
        //optgroup->append_single_option_line("single_extruder_multi_material");

        //optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value) {
        //    // optgroup->get_value() return int for def.type == coInt,
        //    // Thus, there should be boost::any_cast<int> !
        //    // Otherwise, boost::any_cast<size_t> causes an "unhandled unknown exception"
        //    size_t extruders_count = size_t(boost::any_cast<int>(optgroup->get_value("extruders_count")));
        //    wxTheApp->CallAfter([this, opt_key, value, extruders_count]() {
        //        if (opt_key == "extruders_count" || opt_key == "single_extruder_multi_material") {
        //            extruders_count_changed(extruders_count);
        //            init_options_list(); // m_options_list should be updated before UI updating
        //            update_dirty();
        //            if (opt_key == "single_extruder_multi_material") { // the single_extruder_multimaterial was added to force pages
        //                on_value_change(opt_key, value);                      // rebuild - let's make sure the on_value_change is not skipped

        //                if (boost::any_cast<bool>(value) && m_extruders_count > 1) {
        //                    SuppressBackgroundProcessingUpdate sbpu;
        //                    std::vector<double> nozzle_diameters = static_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values;
        //                    const double frst_diam = nozzle_diameters[0];

        //                    for (auto cur_diam : nozzle_diameters) {
        //                        // if value is differs from first nozzle diameter value
        //                        if (fabs(cur_diam - frst_diam) > EPSILON) {
        //                            const wxString msg_text = _(L("Single Extruder Multi Material is selected, \n"
        //                                                          "and all extruders must have the same diameter.\n"
        //                                                          "Do you want to change the diameter for all extruders to first extruder nozzle diameter value?"));
        //                            //wxMessageDialog dialog(parent(), msg_text, _(L("Nozzle diameter")), wxICON_WARNING | wxYES_NO);
        //                            MessageDialog dialog(parent(), msg_text, _(L("Nozzle diameter")), wxICON_WARNING | wxYES_NO);

        //                            DynamicPrintConfig new_conf = *m_config;
        //                            if (dialog.ShowModal() == wxID_YES) {
        //                                for (size_t i = 1; i < nozzle_diameters.size(); i++)
        //                                    nozzle_diameters[i] = frst_diam;

        //                                new_conf.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzle_diameters));
        //                            }
        //                            else
        //                                new_conf.set_key_value("single_extruder_multi_material", new ConfigOptionBool(false));

        //                            load_config(new_conf);
        //                            break;
        //                        }
        //                    }
        //                }
        //            }
        //        }
        //        else {
        //            update_dirty();
        //            on_value_change(opt_key, value);
        //        }
        //    });
        //};

        //optgroup = page->new_optgroup(L("Firmware"));

        //optgroup->append_single_option_line("silent_mode");

        //optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value) {
        //    wxTheApp->CallAfter([this, opt_key, value]() {
        //        if (opt_key == "silent_mode") {
        //            bool val = boost::any_cast<bool>(value);
        //            if (m_use_silent_mode != val) {
        //                m_rebuild_kinematics_page = true;
        //                m_use_silent_mode = val;
        //            }
        //        }
        //        if (opt_key == "gcode_flavor") {
        //            bool supports_travel_acceleration = (boost::any_cast<int>(value) == int(gcfMarlinFirmware));
        //            if (supports_travel_acceleration != m_supports_travel_acceleration) {
        //                m_rebuild_kinematics_page = true;
        //                m_supports_travel_acceleration = supports_travel_acceleration;
        //            }
        //        }
        //        build_unregular_pages();
        //        update_dirty();
        //        on_value_change(opt_key, value);
        //    });
        //};
#endif

        optgroup = page->new_optgroup(L("Advanced"), L"param_advanced");
        optgroup->append_single_option_line("printer_structure");
        optgroup->append_single_option_line("gcode_flavor");

        option =optgroup->get_option("thumbnail_size");
        option.opt.full_width=true;
        optgroup->append_single_option_line(option);

        optgroup->append_single_option_line("scan_first_layer");
        optgroup->append_single_option_line("use_relative_e_distances");
        optgroup->append_single_option_line("use_firmware_retraction");
        optgroup->append_single_option_line("bed_temperature_formula");
        // optgroup->append_single_option_line("spaghetti_detector");
        optgroup->append_single_option_line("machine_load_filament_time");
        optgroup->append_single_option_line("machine_unload_filament_time");
        optgroup->append_single_option_line("machine_switch_extruder_time");

        optgroup = page->new_optgroup(L("Extruder Clearance"));
        optgroup->append_single_option_line("extruder_clearance_max_radius");
        optgroup->append_single_option_line("extruder_clearance_dist_to_rod");
        optgroup->append_single_option_line("extruder_clearance_height_to_rod");
        optgroup->append_single_option_line("extruder_clearance_height_to_lid");

        optgroup = page->new_optgroup(L("Accessory") /*, L"param_accessory"*/);
        optgroup->append_single_option_line("nozzle_type");
        optgroup->append_single_option_line("auxiliary_fan");
        optgroup->append_single_option_line("support_chamber_temp_control");
        optgroup->append_single_option_line("support_air_filtration");

    const int gcode_field_height = 15; // 150
    const int notes_field_height = 25; // 250

    page = add_options_page(L("Machine gcode"), "cog");
        optgroup = page->new_optgroup(L("Machine start G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("machine_start_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Machine end G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("machine_end_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup              = page->new_optgroup(L("Printing by object G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key &opt_key, const boost::any &value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option                = optgroup->get_option("printing_by_object_gcode");
        option.opt.full_width = true;
        option.opt.is_code    = true;
        option.opt.height     = gcode_field_height; // 150;
        optgroup->append_single_option_line(option);
#if 0
        optgroup = page->new_optgroup(L("Before layer change G-code"), 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("before_layer_change_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);
#endif

        optgroup = page->new_optgroup(L("Layer change G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("layer_change_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Time lapse G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("time_lapse_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Change filament G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("change_filament_gcode");
        option.opt.full_width = true;
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Pause G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("machine_pause_gcode");
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

        optgroup = page->new_optgroup(L("Template Custom G-code"), L"param_gcode", 0);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key& opt_key, const boost::any& value) {
            validate_custom_gcode_cb(this, optgroup, opt_key, value);
        };
        option = optgroup->get_option("template_custom_gcode");
        option.opt.is_code = true;
        option.opt.height = gcode_field_height;//150;
        optgroup->append_single_option_line(option);

#if 0
    //page = add_options_page(L("Dependencies"), "advanced");
    //    optgroup = page->new_optgroup(L("Profile dependencies"));

    //    build_preset_description_line(optgroup.get());
#endif

    page = add_options_page(L("Notes"),"note");
        optgroup = page->new_optgroup(L("Notes"),"note");
        optgroup->label_width = 0;
        option = optgroup->get_option("printer_notes");
        option.opt.full_width = true;
        option.opt.height = notes_field_height;
        optgroup->append_single_option_line(option);
        optgroup->m_on_change = [this, optgroup](const t_config_option_key &opt_key, const boost::any &value) { validate_custom_note_cb(this, optgroup, opt_key, value); };

    build_unregular_pages(true);
}

void TabPrinter::build_sla()
{
    //if (!m_pages.empty())
    //    m_pages.resize(0);
    //auto page = add_options_page(L("General"), "printer");
    //auto optgroup = page->new_optgroup(L("Size and coordinates"));

    //create_line_with_widget(optgroup.get(), "printable_area", "custom-svg-and-png-bed-textures_124612", [this](wxWindow* parent) {
    //    return 	create_bed_shape_widget(parent);
    //});
    //optgroup->append_single_option_line("printable_height");

    //optgroup = page->new_optgroup(L("Display"));
    //optgroup->append_single_option_line("display_width");
    //optgroup->append_single_option_line("display_height");

    //auto option = optgroup->get_option("display_pixels_x");
    //Line line = { option.opt.full_label, "" };
    //line.append_option(option);
    //line.append_option(optgroup->get_option("display_pixels_y"));
    //optgroup->append_line(line);
    //optgroup->append_single_option_line("display_orientation");

    //// FIXME: This should be on one line in the UI
    //optgroup->append_single_option_line("display_mirror_x");
    //optgroup->append_single_option_line("display_mirror_y");

    //optgroup = page->new_optgroup(L("Tilt"));
    //line = { L("Tilt time"), "" };
    //line.append_option(optgroup->get_option("fast_tilt_time"));
    //line.append_option(optgroup->get_option("slow_tilt_time"));
    //optgroup->append_line(line);
    //optgroup->append_single_option_line("area_fill");

    //optgroup = page->new_optgroup(L("Corrections"));
    //line = Line{ m_config->def()->get("relative_correction")->full_label, "" };
    //for (auto& axis : { "X", "Y", "Z" }) {
    //    auto opt = optgroup->get_option(std::string("relative_correction_") + char(std::tolower(axis[0])));
    //    opt.opt.label = axis;
    //    line.append_option(opt);
    //}
    //optgroup->append_line(line);
    //optgroup->append_single_option_line("absolute_correction");
    //optgroup->append_single_option_line("elefant_foot_compensation");
    //optgroup->append_single_option_line("elefant_foot_min_width");
    //optgroup->append_single_option_line("gamma_correction");
    //
    //optgroup = page->new_optgroup(L("Exposure"));
    //optgroup->append_single_option_line("min_exposure_time");
    //optgroup->append_single_option_line("max_exposure_time");
    //optgroup->append_single_option_line("min_initial_exposure_time");
    //optgroup->append_single_option_line("max_initial_exposure_time");

    //page = add_options_page(L("Dependencies"), "wrench.png");
    //optgroup = page->new_optgroup(L("Profile dependencies"));

    //build_preset_description_line(optgroup.get());
}

void TabPrinter::extruders_count_changed(size_t extruders_count)
{
    bool is_count_changed = false;
    if (m_extruders_count != extruders_count) {
        m_extruders_count = extruders_count;
        m_preset_bundle->on_extruders_count_changed(extruders_count);
        is_count_changed = true;

        wxGetApp().plater()->get_partplate_list().on_extruder_count_changed((int)m_extruders_count);
    }
    // BBS
#if 1
    else if (m_extruders_count == 1 &&
             m_preset_bundle->project_config.option<ConfigOptionFloats>("flush_volumes_matrix")->values.size()>1)
        m_preset_bundle->update_multi_material_filament_presets();
#endif

    /* This function should be call in any case because of correct updating/rebuilding
     * of unregular pages of a Printer Settings
     */
    build_unregular_pages();

    if (is_count_changed) {
        on_value_change("extruders_count", extruders_count);
        // BBS
        //wxGetApp().obj_list()->update_objects_list_filament_column(extruders_count);
    }
}

void TabPrinter::append_option_line(ConfigOptionsGroupShp optgroup, const std::string opt_key)
{
    auto option = optgroup->get_option(opt_key, 0);
    auto line = Line{ option.opt.full_label, "" };
    line.append_option(option);
    if (m_use_silent_mode
        || m_printer_technology == ptSLA // just for first build, if SLA printer preset is selected
        )
        line.append_option(optgroup->get_option(opt_key, 1));
    optgroup->append_line(line);
}

PageShp TabPrinter::build_kinematics_page()
{
    auto page = add_options_page(L("Motion ability"), "cog", true);

    if (m_use_silent_mode) {
        // Legend for OptionsGroups
        auto optgroup = page->new_optgroup("");
        auto line = Line{ "", "" };

        ConfigOptionDef def;
        def.type = coString;
        def.width = Field::def_width();
        def.gui_type = ConfigOptionDef::GUIType::legend;
        def.mode = comDevelop;
        //def.tooltip = L("Values in this column are for Normal mode");
        def.set_default_value(new ConfigOptionString{ _(L("Normal")).ToUTF8().data() });

        auto option = Option(def, "full_power_legend");
        line.append_option(option);

        //def.tooltip = L("Values in this column are for Stealth mode");
        def.set_default_value(new ConfigOptionString{ _(L("Silent")).ToUTF8().data() });
        option = Option(def, "silent_legend");
        line.append_option(option);

        optgroup->append_line(line);
    }

    const std::vector<std::string> speed_axes{
        "machine_max_speed_x",
        "machine_max_speed_y",
        "machine_max_speed_z",
        "machine_max_speed_e"
    };
    auto optgroup = page->new_optgroup(L("Speed limitation"), "param_speed");
        for (const std::string &speed_axis : speed_axes)	{
            append_option_line(optgroup, speed_axis);
        }

    const std::vector<std::string> axes{ "x", "y", "z", "e" };
    optgroup = page->new_optgroup(L("Acceleration limitation"), "param_acceleration");
        for (const std::string &axis : axes)	{
            append_option_line(optgroup, "machine_max_acceleration_" + axis);
        }
        append_option_line(optgroup, "machine_max_acceleration_extruding");
        append_option_line(optgroup, "machine_max_acceleration_retracting");
        if (m_supports_travel_acceleration)
            append_option_line(optgroup, "machine_max_acceleration_travel");

    optgroup = page->new_optgroup(L("Jerk limitation"));
        for (const std::string &axis : axes)	{
            append_option_line(optgroup, "machine_max_jerk_" + axis);
        }

    //optgroup = page->new_optgroup(L("Minimum feedrates"));
    //    append_option_line(optgroup, "machine_min_extruding_rate");
    //    append_option_line(optgroup, "machine_min_travel_rate");

    return page;
}

/* Previous name build_extruder_pages().
 *
 * This function was renamed because of now it implements not just an extruder pages building,
 * but "Motion ability" and "Single extruder MM setup" too
 * (These pages can changes according to the another values of a current preset)
 * */
void TabPrinter::build_unregular_pages(bool from_initial_build/* = false*/)
{
    size_t		n_before_extruders = 2;			//	Count of pages before Extruder pages
    auto        flavor = m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
    bool		is_marlin_flavor = (flavor == gcfMarlinLegacy || flavor == gcfMarlinFirmware || flavor == gcfKlipper);

    /* ! Freeze/Thaw in this function is needed to avoid call OnPaint() for erased pages
     * and be cause of application crash, when try to change Preset in moment,
     * when one of unregular pages is selected.
     *  */
    Freeze();

    // Add/delete Kinematics page according to is_marlin_flavor
    size_t existed_page = 0;
    for (size_t i = n_before_extruders; i < m_pages.size(); ++i) // first make sure it's not there already
        if (m_pages[i]->title().find(L("Motion ability")) != std::string::npos) {
            if (!is_marlin_flavor || m_rebuild_kinematics_page)
                m_pages.erase(m_pages.begin() + i);
            else
                existed_page = i;
            break;
        }
    m_rebuild_kinematics_page = false;

    if (existed_page < n_before_extruders && (is_marlin_flavor || from_initial_build)) {
        auto page = build_kinematics_page();
        if (from_initial_build && !is_marlin_flavor)
            page->clear();
        else
            m_pages.insert(m_pages.begin() + n_before_extruders, page);
    }

    if (is_marlin_flavor)
        n_before_extruders++;
    size_t		n_after_single_extruder_MM = 2; //	Count of pages after single_extruder_multi_material page

#if 0
    if (m_extruders_count_old == m_extruders_count ||
        (m_has_single_extruder_MM_page && m_extruders_count == 1))
    {
        // if we have a single extruder MM setup, add a page with configuration options:
        for (size_t i = 0; i < m_pages.size(); ++i) // first make sure it's not there already
            if (m_pages[i]->title().find(L("Single extruder MM setup")) != std::string::npos) {
                m_pages.erase(m_pages.begin() + i);
                break;
            }
        m_has_single_extruder_MM_page = false;
    }

    //BBS: please add our single extruder multimaterial parameters here. Currently
    //comment this part because we have no such config in this page.
    if (from_initial_build ||
        (m_extruders_count > 1 && m_config->opt_bool("single_extruder_multi_material") && !m_has_single_extruder_MM_page)) {
        // create a page, but pretend it's an extruder page, so we can add it to m_pages ourselves
        auto page = add_options_page(L("Single extruder MM setup"), "printer", true);
        auto optgroup = page->new_optgroup(L("Single extruder multimaterial parameters"));

        if (from_initial_build)
            page->clear();
        else {
            m_pages.insert(m_pages.end() - n_after_single_extruder_MM, page);
            m_has_single_extruder_MM_page = true;
        }
    }
#endif

    // BBS. Just create one extruder page because BBL machine has only on physical extruder.
    // Build missed extruder pages
    for (auto extruder_idx = m_extruders_count_old; extruder_idx < m_extruders_count; ++extruder_idx) {
        const wxString& page_name = (m_extruders_count > 1) ? wxString::Format("Extruder %d", int(extruder_idx + 1)) : wxString::Format("Extruder");

        //# build page
        //const wxString& page_name = wxString::Format("Extruder %d", int(extruder_idx + 1));
        auto page = add_options_page(page_name, "empty", true);
        m_pages.insert(m_pages.begin() + n_before_extruders + extruder_idx, page);

            auto optgroup = page->new_optgroup(L("Basic information"), L"param_type", -1, true);
            optgroup->append_single_option_line("extruder_type", "", extruder_idx);
            optgroup->append_single_option_line("nozzle_diameter", "", extruder_idx);
            //optgroup->append_single_option_line("default_nozzle_volume_type", "", extruder_idx);

            optgroup->append_single_option_line("nozzle_volume", "", extruder_idx);
            optgroup->append_single_option_line("extruder_printable_height", "", extruder_idx);
            Option option         = optgroup->get_option("extruder_printable_area", extruder_idx);
            option.opt.full_width = true;
            optgroup->append_single_option_line(option);

            optgroup->m_on_change = [this, extruder_idx](const t_config_option_key& opt_key, boost::any value)
            {
                //if (m_config->opt_bool("single_extruder_multi_material") && m_extruders_count > 1 && opt_key.find("nozzle_diameter") != std::string::npos)
                //{
                //    SuppressBackgroundProcessingUpdate sbpu;
                //    const double new_nd = boost::any_cast<double>(value);
                //    std::vector<double> nozzle_diameters = static_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values;

                //    // if value was changed
                //    if (fabs(nozzle_diameters[extruder_idx == 0 ? 1 : 0] - new_nd) > EPSILON)
                //    {
                //        const wxString msg_text = _(L("This is a single extruder multimaterial printer, diameters of all extruders "
                //                                      "will be set to the new value. Do you want to proceed?"));
                //        //wxMessageDialog dialog(parent(), msg_text, _(L("Nozzle diameter")), wxICON_WARNING | wxYES_NO);
                //        MessageDialog dialog(parent(), msg_text, _(L("Nozzle diameter")), wxICON_WARNING | wxYES_NO);

                //        DynamicPrintConfig new_conf = *m_config;
                //        if (dialog.ShowModal() == wxID_YES) {
                //            for (size_t i = 0; i < nozzle_diameters.size(); i++) {
                //                if (i==extruder_idx)
                //                    continue;
                //                nozzle_diameters[i] = new_nd;
                //            }
                //        }
                //        else
                //            nozzle_diameters[extruder_idx] = nozzle_diameters[extruder_idx == 0 ? 1 : 0];

                //        new_conf.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzle_diameters));
                //        load_config(new_conf);
                //    }
                //}

                update_dirty();
                on_value_change(opt_key, value);
                update();
            };

            optgroup = page->new_optgroup(L("Layer height limits"), L"param_layer_height", -1, true);
            optgroup->append_single_option_line("min_layer_height", "adaptive-layer-height", extruder_idx);
            optgroup->append_single_option_line("max_layer_height", "adaptive-layer-height", extruder_idx);

            optgroup = page->new_optgroup(L("Position"), L"param_retraction", -1, true);
            optgroup->append_single_option_line("extruder_offset");

            //BBS: don't show retract related config menu in machine page
            optgroup = page->new_optgroup(L("Retraction"), L"param_retraction");
            optgroup->append_single_option_line("retraction_length", "parameter/retraction#length", extruder_idx);
            optgroup->append_single_option_line("z_hop", "parameter/retraction#z-hop-when-retracting", extruder_idx);
            optgroup->append_single_option_line("retract_lift_above", "parameter/retraction#z-hop-upper-and-lower-boundary", extruder_idx);
            optgroup->append_single_option_line("retract_lift_below", "parameter/retraction#z-hop-upper-and-lower-boundary", extruder_idx);
            optgroup->append_single_option_line("z_hop_types", "parameter/retraction#z-hop-type", extruder_idx);
            optgroup->append_single_option_line("retraction_speed", "parameter/retraction#retraction-speed", extruder_idx);
            optgroup->append_single_option_line("deretraction_speed", "parameter/retraction#deretraction-speed", extruder_idx);
            //optgroup->append_single_option_line("retract_restart_extra", "", extruder_idx);
            optgroup->append_single_option_line("retraction_minimum_travel", "parameter/retraction#travel-distance-threshold", extruder_idx);
            optgroup->append_single_option_line("retract_when_changing_layer", "parameter/retraction#retract-on-layer-change", extruder_idx);
            optgroup->append_single_option_line("wipe", "parameter/retraction#wipe-while-retracting", extruder_idx);
            optgroup->append_single_option_line("wipe_distance", "parameter/retraction#wipe-distance", extruder_idx);
            optgroup->append_single_option_line("retract_before_wipe", "parameter/retraction#retract-amount-before-wipe", extruder_idx);

            optgroup = page->new_optgroup(L("Retraction when switching material"), L"param_retraction", -1, true);
            optgroup->append_single_option_line("retract_length_toolchange", "", extruder_idx);
            optgroup->append_single_option_line("retract_restart_extra_toolchange", "", extruder_idx);
            // do not display this params now
            optgroup->append_single_option_line("long_retractions_when_cut", "", extruder_idx);
            optgroup->append_single_option_line("retraction_distances_when_cut", "", extruder_idx);
#if 0
            //optgroup = page->new_optgroup(L("Preview"), -1, true);

            //auto reset_to_filament_color = [this, extruder_idx](wxWindow* parent) {
            //    m_reset_to_filament_color = new ScalableButton(parent, wxID_ANY, "undo", _L("Reset to Filament Color"),
            //                                                   wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT, true);
            //    ScalableButton* btn = m_reset_to_filament_color;
            //    btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
            //    btn->SetSize(btn->GetBestSize());
            //    auto sizer = new wxBoxSizer(wxHORIZONTAL);
            //    sizer->Add(btn);

            //    btn->Bind(wxEVT_BUTTON, [this, extruder_idx](wxCommandEvent& e)
            //    {
            //        std::vector<std::string> colors = static_cast<const ConfigOptionStrings*>(m_config->option("extruder_colour"))->values;
            //        colors[extruder_idx] = "";

            //        DynamicPrintConfig new_conf = *m_config;
            //        new_conf.set_key_value("extruder_colour", new ConfigOptionStrings(colors));
            //        load_config(new_conf);

            //        update_dirty();
            //        update();
            //    });

            //    return sizer;
            //};
            ////BBS
            //Line line = optgroup->create_single_option_line("extruder_colour", "", extruder_idx);
            //line.append_widget(reset_to_filament_color);
            //optgroup->append_line(line);
#endif
    }
    // BBS. No extra extruder page for single physical extruder machine
    // # remove extra pages
#if 1
    auto &first_extruder_title = const_cast<wxString &>(m_pages[n_before_extruders]->title());
    if (m_extruders_count < m_extruders_count_old) {
        m_pages.erase(	m_pages.begin() + n_before_extruders + m_extruders_count,
                        m_pages.begin() + n_before_extruders + m_extruders_count_old);
        if (m_extruders_count == 1)
            first_extruder_title = wxString::Format("Extruder");
    } else if (m_extruders_count_old == 1) {
        first_extruder_title = wxString::Format("Extruder %d", 1);
    }
    auto & searcher = wxGetApp().sidebar().get_searcher();
    for (auto &group : m_pages[n_before_extruders]->m_optgroups) {
        group->set_config_category_and_type(first_extruder_title, m_type);
        for (auto &opt : group->opt_map())
            searcher.add_key(opt.first + "#0", m_type, group->title, first_extruder_title);
    }
#endif

    Thaw();

    m_extruders_count_old = m_extruders_count;

    if (from_initial_build && m_printer_technology == ptSLA)
        return; // next part of code is no needed to execute at this moment

    rebuild_page_tree();

    // Reload preset pages with current configuration values
    reload_config();

    // apply searcher with current configuration
    apply_searcher();
}

// this gets executed after preset is loaded and before GUI fields are updated
void TabPrinter::on_preset_loaded()
{
    // BBS
    //update nozzle_volume_type
    const Preset& current_printer = m_preset_bundle->printers.get_selected_preset();
    const Preset* base_printer = m_preset_bundle->printers.get_preset_base(current_printer);
    if (!base_printer)
        base_printer = &current_printer;
    std::string base_name = base_printer->name;
    // update the extruders count field
    auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloatsNullable*>(m_config->option("nozzle_diameter"));
    size_t extruders_count = nozzle_diameter->values.size();
    // update the GUI field according to the number of nozzle diameters supplied
    if (m_extruders_count != extruders_count)
        extruders_count_changed(extruders_count);

    if (base_name != m_base_preset_name) {
        bool use_default_nozzle_volume_type = true;
        m_base_preset_name = base_name;
        std::string prev_nozzle_volume_type = wxGetApp().app_config->get_nozzle_volume_types_from_config(base_name);
        if (!prev_nozzle_volume_type.empty()) {
            ConfigOptionEnumsGeneric* nozzle_volume_type_option = m_preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
            if (nozzle_volume_type_option->deserialize(prev_nozzle_volume_type)) {
                use_default_nozzle_volume_type = false;
            }
        }
        if (use_default_nozzle_volume_type) {
            m_preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values = current_printer.config.option<ConfigOptionEnumsGeneric>("default_nozzle_volume_type")->values;
        }
    }
}

void TabPrinter::update_pages()
{
    // update m_pages ONLY if printer technology is changed
    const PrinterTechnology new_printer_technology = m_presets->get_edited_preset().printer_technology();
    if (new_printer_technology == m_printer_technology)
        return;

    //clear all active pages before switching
    clear_pages();

    // set m_pages to m_pages_(technology before changing)
    m_printer_technology == ptFFF ? m_pages.swap(m_pages_fff) : m_pages.swap(m_pages_sla);

    // build Tab according to the technology, if it's not exist jet OR
    // set m_pages_(technology after changing) to m_pages
    // m_printer_technology will be set by Tab::load_current_preset()
    if (new_printer_technology == ptFFF)
    {
        if (m_pages_fff.empty())
        {
            build_fff();
            if (m_extruders_count > 1)
            {
                m_preset_bundle->update_multi_material_filament_presets();
                on_value_change("extruders_count", m_extruders_count);
            }
        }
        else
            m_pages.swap(m_pages_fff);

         wxGetApp().obj_list()->update_objects_list_filament_column(m_extruders_count);
    }
    else
        m_pages_sla.empty() ? build_sla() : m_pages.swap(m_pages_sla);

    rebuild_page_tree();
}

void TabPrinter::reload_config()
{
    Tab::reload_config();

    // "extruders_count" doesn't update from the update_config(),
    // so update it implicitly
    if (m_active_page && m_active_page->title() == "General")
        m_active_page->set_value("extruders_count", int(m_extruders_count));
}

void TabPrinter::activate_selected_page(std::function<void()> throw_if_canceled)
{
    Tab::activate_selected_page(throw_if_canceled);

    // "extruders_count" doesn't update from the update_config(),
    // so update it implicitly
    if (m_active_page && m_active_page->title() == "General")
        m_active_page->set_value("extruders_count", int(m_extruders_count));
}

void TabPrinter::clear_pages()
{
    Tab::clear_pages();
    m_reset_to_filament_color = nullptr;
}

void TabPrinter::toggle_options()
{
    if (!m_active_page || m_presets->get_edited_preset().printer_technology() == ptSLA)
        return;

    auto nozzle_volumes = m_preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    auto extruders      = m_config->option<ConfigOptionEnumsGeneric>("extruder_type");
        auto get_index_for_extruder =
            [this, &extruders, &nozzle_volumes](int extruder_id, int stride = 1) {
        return m_config->get_index_for_extruder(extruder_id + 1, "printer_extruder_id",
            ExtruderType(extruders->values[extruder_id]), NozzleVolumeType(nozzle_volumes->values[extruder_id]), "printer_extruder_variant", stride);
    };

    auto config_mode = wxGetApp().get_mode();
    //BBS: whether the preset is Bambu Lab printer
    bool is_BBL_printer = false;
    if (m_preset_bundle) {
       is_BBL_printer = m_preset_bundle->printers.get_edited_preset().is_bbl_vendor_preset(m_preset_bundle);
    }

    bool have_multiple_extruders = m_extruders_count > 1;
    //if (m_active_page->title() == "Custom G-code") {
    //    toggle_option("change_filament_gcode", have_multiple_extruders);
    //}
    if (m_active_page->title() == "Basic information") {
        //toggle_line("printable_area", !is_configed_by_BBL);//all printer can entry and view data
        toggle_option("single_extruder_multi_material", have_multiple_extruders);
        //BBS: gcode_flavore of BBL printer can't be edited and changed
        toggle_option("gcode_flavor", !is_BBL_printer);
        toggle_option("thumbnail_size",!is_BBL_printer);
        toggle_option("printer_structure", !is_BBL_printer);
        toggle_option("use_relative_e_distances", !is_BBL_printer);
        toggle_option("support_chamber_temp_control",!is_BBL_printer);
        toggle_option("use_firmware_retraction", !is_BBL_printer);
        toggle_option("support_air_filtration",is_BBL_printer);
        auto flavor = m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
        bool is_marlin_flavor = flavor == gcfMarlinLegacy || flavor == gcfMarlinFirmware;
        // Disable silent mode for non-marlin firmwares.
        toggle_option("silent_mode", is_marlin_flavor);
        //BBS: extruder clearance of BBL printer can't be edited.
        for (auto el : {"extruder_clearance_max_radius", "extruder_clearance_dist_to_rod", "extruder_clearance_height_to_rod", "extruder_clearance_height_to_lid"})
            toggle_option(el, !is_BBL_printer);
    }

    wxString extruder_number;
    long val = 1;
    if ( m_active_page->title().IsSameAs("Extruder") ||
        (m_active_page->title().StartsWith("Extruder ", &extruder_number) && extruder_number.ToLong(&val) &&
        val > 0 && (size_t)val <= m_extruders_count))
    {
        size_t i = size_t(val - 1);
        int variant_index = get_index_for_extruder(i);
        bool have_retract_length = m_config->opt_float_nullable("retraction_length", variant_index) > 0;

        //BBS
        for (auto el : { "extruder_type" , "nozzle_diameter"}) {
            toggle_option(el, !is_BBL_printer, i);
        }

        toggle_option("extruder_type", !is_BBL_printer, i);
        toggle_option("nozzle_diameter", !is_BBL_printer || config_mode == ConfigOptionMode::comDevelop, i);
        toggle_option("extruder_offset", !is_BBL_printer || config_mode == ConfigOptionMode::comDevelop, i);

        toggle_option("extruder_printable_area", false, i);          // disable
        toggle_line("extruder_printable_area", m_preset_bundle->get_printer_extruder_count() == 2, i);  //hide
        toggle_option("extruder_printable_height", false, i);
        toggle_line("extruder_printable_height", m_preset_bundle->get_printer_extruder_count() == 2, i);

        bool use_firmware_retraction = m_config->opt_bool("use_firmware_retraction");
        toggle_option("retract_length",!use_firmware_retraction, i);

        // user can customize travel length if we have retraction length or we"re using
        // firmware retraction
        toggle_option("retraction_minimum_travel", have_retract_length||use_firmware_retraction, i);

        // user can customize other retraction options if retraction is enabled
        //BBS
        bool retraction = have_retract_length || use_firmware_retraction;
        std::vector<std::string> vec = { "z_hop", "retract_when_changing_layer" };
        for (auto el : vec)
            toggle_option(el, retraction, i);

        // some options only apply when not using firmware retraction
        vec.resize(0);
        vec = { "retraction_speed", "deretraction_speed", "retract_before_wipe", "retract_restart_extra", "wipe", "wipe_distance" };
        for (auto el : vec)
            //BBS
            toggle_option(el, retraction && !use_firmware_retraction, i);

        bool wipe = retraction && m_config->opt_bool_nullable("wipe", i);
        toggle_option("retract_before_wipe", wipe, i);

        if (use_firmware_retraction && wipe) {
            //wxMessageDialog dialog(parent(),
            MessageDialog dialog(parent(),
                _(L("The Wipe option is not available when using the Firmware Retraction mode.\n"
                    "\nShall I disable it in order to enable Firmware Retraction?")),
                _(L("Firmware Retraction")), wxICON_WARNING | wxYES | wxNO);

            DynamicPrintConfig new_conf = *m_config;
            if (dialog.ShowModal() == wxID_YES) {
                auto wipe = static_cast<ConfigOptionBoolsNullable*>(m_config->option("wipe")->clone());
                for (size_t w = 0; w < wipe->values.size(); w++)
                    wipe->values[w] = false;
                new_conf.set_key_value("wipe", wipe);
            }
            else {
                new_conf.set_key_value("use_firmware_retraction", new ConfigOptionBool(false));
            }
            load_config(new_conf);
        }

        // BBS
        toggle_option("wipe_distance", wipe, i);

        toggle_option("retract_length_toolchange", have_multiple_extruders, i);

        bool toolchange_retraction = m_config->opt_float_nullable("retract_length_toolchange", variant_index) > 0;
        toggle_option("retract_restart_extra_toolchange", have_multiple_extruders && toolchange_retraction, i);

        toggle_option("long_retractions_when_cut", !use_firmware_retraction && m_config->opt_int("enable_long_retraction_when_cut"), i);
        toggle_line("retraction_distances_when_cut", m_config->opt_bool_nullable("long_retractions_when_cut", variant_index), i);
    }

    if (m_active_page->title() == "Motion ability") {
        assert(m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value == gcfMarlinLegacy
            || m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value == gcfMarlinFirmware
            || m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value == gcfKlipper);
        bool silent_mode = m_config->opt_bool("silent_mode");
        int  max_field = silent_mode ? 2 : 1;
        //BBS: limits of BBL printer can't be edited, except jerk.
        for (const std::string& opt : { "machine_max_acceleration_extruding", "machine_max_acceleration_retracting", "machine_max_acceleration_travel",
            "machine_max_acceleration_x", "machine_max_acceleration_y", "machine_max_acceleration_z", "machine_max_acceleration_e",
            "machine_max_speed_x", "machine_max_speed_y", "machine_max_speed_z", "machine_max_speed_e",
            "machine_min_extruding_rate", "machine_min_travel_rate" })
            for (int i = 0; i < max_field; ++ i)
	            toggle_option(opt, !is_BBL_printer, i);
    }
}

void TabPrinter::update()
{
    m_update_cnt++;
    m_presets->get_edited_preset().printer_technology() == ptFFF ? update_fff() : update_sla();
    m_update_cnt--;

    update_description_lines();
    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();

    if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabPrinter::update_fff()
{
    if (m_use_silent_mode != m_config->opt_bool("silent_mode"))	{
        m_rebuild_kinematics_page = true;
        m_use_silent_mode = m_config->opt_bool("silent_mode");
    }

    auto gcf_ = m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
    bool supports_travel_acceleration = (gcf_ == gcfMarlinFirmware || gcf_ == gcfMarlinLegacy || gcf_ == gcfKlipper);
    if (m_supports_travel_acceleration != supports_travel_acceleration) {
        m_rebuild_kinematics_page = true;
        m_supports_travel_acceleration = supports_travel_acceleration;
    }

    toggle_options();
}

void TabPrinter::update_sla()
{ ; }

void Tab::update_ui_items_related_on_parent_preset(const Preset* selected_preset_parent)
{
    m_is_default_preset = selected_preset_parent != nullptr && selected_preset_parent->is_default;

    m_bmp_non_system = selected_preset_parent ? &m_bmp_value_unlock : &m_bmp_white_bullet;
    m_ttg_non_system = selected_preset_parent ? &m_ttg_value_unlock : &m_ttg_white_bullet_ns;
    m_tt_non_system  = selected_preset_parent ? &m_tt_value_unlock  : &m_ttg_white_bullet_ns;
}

//BBS: reactive the preset combo box
void Tab::reactive_preset_combo_box()
{
    if (!m_presets_choice) return;
    //BBS: add workaround to fix the issue caused by wxwidget 3.15 upgrading
    m_presets_choice->Enable(false);
    m_presets_choice->Enable(true);
}

// Initialize the UI from the current preset
void Tab::load_current_preset()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<<boost::format(": enter, m_type %1%")%Preset::get_type_string(m_type);
    const Preset& preset = m_presets->get_edited_preset();
    int previous_extruder_count = 0;

    update_btns_enabling();

    if (m_type == Slic3r::Preset::TYPE_PRINTER) {
        // For the printer profile, generate the extruder pages.
        if (preset.printer_technology() == ptFFF) {
            previous_extruder_count = static_cast<TabPrinter*>(this)->m_extruders_count;
            on_preset_loaded();
        }
        else
            wxGetApp().obj_list()->update_objects_list_filament_column(1);
    }
    if (m_type == Preset::TYPE_PRINT) {
        if (auto tab = wxGetApp().plate_tab) {
            tab->update_extruder_variants();
        }
        for (auto tab : wxGetApp().model_tabs_list) {
            tab->update_extruder_variants();
        }
    }
    update();

    // Reload preset pages with the new configuration values.
    update_extruder_variants(-1, false);
    reload_config();

    update_ui_items_related_on_parent_preset(m_presets->get_selected_preset_parent());

//	m_undo_to_sys_btn->Enable(!preset.is_default);

#if 0
    // use CallAfter because some field triggers schedule on_change calls using CallAfter,
    // and we don't want them to be called after this update_dirty() as they would mark the
    // preset dirty again
    // (not sure this is true anymore now that update_dirty is idempotent)
    wxTheApp->CallAfter([this]
#endif
    {
        // checking out if this Tab exists till this moment
        if (!wxGetApp().checked_tab(this))
            return;
        update_tab_ui();

        // update show/hide tabs
        if (m_type == Slic3r::Preset::TYPE_PRINTER) {
            const PrinterTechnology printer_technology = m_presets->get_edited_preset().printer_technology();
            if (printer_technology != static_cast<TabPrinter*>(this)->m_printer_technology)
            {
                // The change of the technology requires to remove some of unrelated Tabs
                // During this action, wxNoteBook::RemovePage invoke wxEVT_NOTEBOOK_PAGE_CHANGED
                // and as a result a function select_active_page() is called fron Tab::OnActive()
                // But we don't need it. So, to avoid activation of the page, set m_active_page to NULL
                // till unusable Tabs will be deleted
                Page* tmp_page = m_active_page;
                m_active_page = nullptr;
                for (auto tab : wxGetApp().tabs_list) {
                    if (tab->type() == Preset::TYPE_PRINTER) { // Printer tab is shown every time
                        int cur_selection = wxGetApp().tab_panel()->GetSelection();
                        if (cur_selection != 0)
                            wxGetApp().tab_panel()->SetSelection(wxGetApp().tab_panel()->GetPageCount() - 1);
                        continue;
                    }
                    if (tab->supports_printer_technology(printer_technology))
                    {
#ifdef _MSW_DARK_MODE
                        if (!wxGetApp().tabs_as_menu()) {
                            std::string bmp_name = tab->type() == Slic3r::Preset::TYPE_FILAMENT      ? "spool" :
                                                   tab->type() == Slic3r::Preset::TYPE_SLA_MATERIAL  ? "" : "cog";
                            tab->Hide(); // #ys_WORKAROUND : Hide tab before inserting to avoid unwanted rendering of the tab
                            dynamic_cast<Notebook*>(wxGetApp().tab_panel())->InsertPage(wxGetApp().tab_panel()->FindPage(this), tab, tab->title(), bmp_name);
                        }
                        else
#endif
                            wxGetApp().tab_panel()->InsertPage(wxGetApp().tab_panel()->FindPage(this), tab, tab->title(), "");
                        #ifdef __linux__ // the tabs apparently need to be explicitly shown on Linux (pull request #1563)
                            int page_id = wxGetApp().tab_panel()->FindPage(tab);
                            wxGetApp().tab_panel()->GetPage(page_id)->Show(true);
                        #endif // __linux__
                    }
                    else {
                        int page_id = wxGetApp().tab_panel()->FindPage(tab);
                        wxGetApp().tab_panel()->GetPage(page_id)->Show(false);
                        wxGetApp().tab_panel()->RemovePage(page_id);
                    }
                }
                static_cast<TabPrinter*>(this)->m_printer_technology = printer_technology;
                m_active_page = tmp_page;
#ifdef _MSW_DARK_MODE
                if (!wxGetApp().tabs_as_menu())
                    dynamic_cast<Notebook*>(wxGetApp().tab_panel())->SetPageImage(wxGetApp().tab_panel()->FindPage(this), printer_technology == ptFFF ? "printer" : "sla_printer");
#endif
            }
            //update the object config due to extruder count change
            DynamicPrintConfig& new_print_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            int new_extruder_count = wxGetApp().preset_bundle->get_printer_extruder_count();
            if (previous_extruder_count != new_extruder_count)
            {
                //process the object params here
                Model& model = wxGetApp().plater()->model();
                size_t num_objects = model.objects.size();
                for (int i = 0; i < num_objects; ++i) {
                    ModelObject* object = model.objects[i];
                    DynamicPrintConfig object_config = object->config.get();
                    if (!object_config.empty()) {
                        if (previous_extruder_count < new_extruder_count)
                            object_config.update_values_from_single_to_multi_2(new_print_config, print_options_with_variant);
                        else
                            object_config.update_values_from_multi_to_single_2(print_options_with_variant);
                        object->config.assign_config(std::move(object_config));
                    }
                    for (ModelVolume* v : object->volumes) {
                        if (v->is_model_part() || v->is_modifier()) {
                            DynamicPrintConfig volume_config = v->config.get();
                            if (!volume_config.empty()) {
                                if (previous_extruder_count < new_extruder_count)
                                    volume_config.update_values_from_single_to_multi_2(new_print_config, print_options_with_variant);
                                else
                                    volume_config.update_values_from_multi_to_single_2(print_options_with_variant);
                                v->config.assign_config(std::move(volume_config));
                            }
                        }
                    }

                    for (auto &layer_config_it : object->layer_config_ranges) {
                        ModelConfig& layer_model_config = layer_config_it.second;
                        DynamicPrintConfig layer_config = layer_model_config.get();
                        if (!layer_config.empty()) {
                           if (previous_extruder_count < new_extruder_count)
                               layer_config.update_values_from_single_to_multi_2(new_print_config, print_options_with_variant);
                           else
                               layer_config.update_values_from_multi_to_single_2(print_options_with_variant);
                           layer_model_config.assign_config(std::move(layer_config));
                       }
                    }
                }
            }

            on_presets_changed();
            if (printer_technology == ptFFF) {
                static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<const ConfigOptionFloatsNullable*>(m_presets->get_selected_preset().config.option("nozzle_diameter"))->values.size(); //static_cast<TabPrinter*>(this)->m_extruders_count;
                const Preset* parent_preset = m_presets->get_selected_preset_parent();
                static_cast<TabPrinter*>(this)->m_sys_extruders_count = parent_preset == nullptr ? 0 :
                    static_cast<const ConfigOptionFloatsNullable*>(parent_preset->config.option("nozzle_diameter"))->values.size();
            }
        }
        else {
            on_presets_changed();
            if (m_type == Preset::TYPE_SLA_PRINT || m_type == Preset::TYPE_PRINT)
                update_frequently_changed_parameters();
        }
        m_opt_status_value = (m_presets->get_selected_preset_parent() ? osSystemValue : 0) | osInitValue;
        init_options_list();
        if (m_type == Preset::TYPE_PRINT) {
            for (auto tab : wxGetApp().model_tabs_list) {
                tab->init_options_list();
            }
        }
        update_visibility();
        update_changed_ui();
    }
#if 0
    );
#endif
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<<boost::format(": exit");
}

//Regerenerate content of the page tree.
void Tab::rebuild_page_tree()
{
    // get label of the currently selected item
    auto sel_item = m_tabctrl->GetSelection();
    // BBS: fix new layout, record last select
    if (sel_item < 0)
        sel_item = m_last_select_item;
    const auto selected = sel_item >= 0 ? m_tabctrl->GetItemText(sel_item) : "";

    int item = -1;

    // Delete/Append events invoke wxEVT_TAB_SEL_CHANGED event.
    // To avoid redundant clear/activate functions call
    // suppress activate page before page_tree rebuilding
    m_disable_tree_sel_changed_event = true;
    m_tabctrl->DeleteAllItems();

    for (auto p : m_pages)
    {
        if (!p->get_show())
            continue;
        auto itemId = m_tabctrl->AppendItem(translate_category(p->title(), m_type), p->iconID());
        m_tabctrl->SetItemTextColour(itemId, p->get_item_colour() == m_modified_label_clr ? p->get_item_colour() : StateColor(
                        std::make_pair(0x6B6B6C, (int) StateColor::NotChecked),
                        std::make_pair(p->get_item_colour(), (int) StateColor::Normal)));
        if (translate_category(p->title(), m_type) == selected)
            item = itemId;
    }
    // BBS: on mac, root is selected, this fix it
    m_tabctrl->Unselect();
    // BBS: not select on hide tab
    if (item == -1 && m_parent->is_active_and_shown_tab(this)) {
        // this is triggered on first load, so we don't disable the sel change event
        item = m_tabctrl->GetFirstVisibleItem();
    }
    // BBS: fix new layout, record last select
    if (sel_item == m_last_select_item)
        m_last_select_item = item;
    else
        m_last_select_item = NULL;

    // allow activate page before selection of a page_tree item
    m_disable_tree_sel_changed_event = false;
    //BBS: GUI refactor
    if (item >= 0)
    {
        bool ret = update_current_page_in_background(item);
        //if m_active_page is changed in update_current_page_in_background
        //will just update the selected item of the treectrl
         if (m_parent->is_active_and_shown_tab(this)) // FIX: modify state not update
            m_tabctrl->SelectItem(item);
    }
}

void Tab::update_btns_enabling()
{
    // we can delete any preset from the physical printer
    // and any user preset
    const Preset& preset = m_presets->get_edited_preset();
    m_btn_delete_preset->Show((m_type == Preset::TYPE_PRINTER && m_preset_bundle->physical_printers.has_selection())
                              || (!preset.is_default && !preset.is_system));

    //if (m_btn_edit_ph_printer)
    //    m_btn_edit_ph_printer->SetToolTip( m_preset_bundle->physical_printers.has_selection() ?
    //                                       _L("Edit physical printer") : _L("Add physical printer"));
}

void Tab::update_preset_choice()
{
    if (m_presets_choice)
        m_presets_choice->update();
    update_btns_enabling();
}

// Called by the UI combo box when the user switches profiles, and also to delete the current profile.
// Select a preset by a name.If !defined(name), then the default preset is selected.
// If the current profile is modified, user is asked to save the changes.
bool Tab::select_preset(
    std::string preset_name, bool delete_current /*=false*/, const std::string &last_selected_ph_printer_name /* =""*/, bool force_select, bool force_no_transfer)
{
    auto app_config = wxGetApp().app_config;
    bool auto_transfer = app_config->get("auto_transfer_when_switch_preset") == "true";
    ForceOption option = auto_transfer ? ForceOption::fopTransfer : ForceOption::fopNone;
    BOOST_LOG_TRIVIAL(info) << boost::format("select preset, name %1%, delete_current %2%")
        %preset_name %delete_current;
    if (preset_name.empty()) {
        if (delete_current) {
            // Find an alternate preset to be selected after the current preset is deleted.
            const std::deque<Preset> &presets 		= m_presets->get_presets();
            size_t    				  idx_current   = m_presets->get_idx_selected();
            // Find the next visible preset.
            preset_name = presets[idx_current].inherits();
            if (preset_name.empty()) {
                size_t 				      idx_new       = idx_current + 1;
                if (idx_new < presets.size())
                    for (; idx_new < presets.size() && ! presets[idx_new].is_visible; ++ idx_new) ;
                if (idx_new == presets.size())
                    for (idx_new = idx_current - 1; idx_new > 0 && ! presets[idx_new].is_visible; -- idx_new);
                preset_name = presets[idx_new].name;
                BOOST_LOG_TRIVIAL(info) << boost::format("cause by delete current ,choose the next visible, idx %1%, name %2%")
                                            %idx_new %preset_name;
            } else {
                BOOST_LOG_TRIVIAL(info) << boost::format("cause by delete current ,choose base, name %1%") % preset_name;
            }
        } else {
            //BBS select first visible item first
            const std::deque<Preset> &presets 		= this->m_presets->get_presets();
            size_t 				      idx_new = 0;
            if (idx_new < presets.size())
                for (; idx_new < presets.size() && ! presets[idx_new].is_visible; ++ idx_new) ;
            preset_name = presets[idx_new].name;
            if (idx_new == presets.size()) {
                // If no name is provided, select the "-- default --" preset.
                preset_name = m_presets->default_preset().name;
            }
            BOOST_LOG_TRIVIAL(info) << boost::format("not cause by delete current ,choose the first visible, idx %1%, name %2%")
                                        %idx_new %preset_name;
        }
    }
    //BBS: add project embedded preset logic and refine is_external
    assert(! delete_current || (m_presets->get_edited_preset().name != preset_name && (m_presets->get_edited_preset().is_user() || m_presets->get_edited_preset().is_project_embedded)));
    //assert(! delete_current || (m_presets->get_edited_preset().name != preset_name && m_presets->get_edited_preset().is_user()));
    bool current_dirty = ! delete_current && m_presets->current_is_dirty();
    bool print_tab     = m_presets->type() == Preset::TYPE_PRINT || m_presets->type() == Preset::TYPE_SLA_PRINT;
    bool printer_tab   = m_presets->type() == Preset::TYPE_PRINTER;
    bool canceled      = false;
    bool no_transfer = false;
    bool technology_changed = false;
    m_dependent_tabs.clear();
    if ((m_presets->type() == Preset::TYPE_FILAMENT) && !preset_name.empty())
    {
        Preset *to_be_selected = m_presets->find_preset(preset_name, false, true);
        if (to_be_selected) {
            std::string current_type, to_select_type;
            ConfigOptionStrings* cur_opt = dynamic_cast <ConfigOptionStrings *>(m_presets->get_edited_preset().config.option("filament_type"));
            ConfigOptionStrings* to_select_opt = dynamic_cast <ConfigOptionStrings *>(to_be_selected->config.option("filament_type"));
            if (cur_opt && (cur_opt->values.size() > 0)) {
                current_type =  cur_opt->values[0];
            }
            if (to_select_opt && (to_select_opt->values.size() > 0)) {
                to_select_type =  to_select_opt->values[0];
            }
            if (current_type != to_select_type)
                no_transfer = true;
        }
    }
    else if (printer_tab)
        no_transfer = true;

    if (force_no_transfer) {
        no_transfer = true;
    }
    ForceOption option_for_dirty_preset = ForceOption::fopNone;
    if (current_dirty && print_tab) {
        option_for_dirty_preset = option;
    }
    if (current_dirty && ! may_discard_current_dirty_preset(nullptr, preset_name, no_transfer, option_for_dirty_preset) && !force_select) {
        canceled = true;
        BOOST_LOG_TRIVIAL(info) << boost::format("current dirty and cancelled");
    } else if (print_tab) {
        // Before switching the print profile to a new one, verify, whether the currently active filament or SLA material
        // are compatible with the new print.
        // If it is not compatible and the current filament or SLA material are dirty, let user decide
        // whether to discard the changes or keep the current print selection.
        PresetWithVendorProfile printer_profile = m_preset_bundle->printers.get_edited_preset_with_vendor_profile();
        PrinterTechnology  printer_technology = printer_profile.preset.printer_technology();
        PresetCollection  &dependent = (printer_technology == ptFFF) ? m_preset_bundle->filaments : m_preset_bundle->sla_materials;
        bool 			   old_preset_dirty = dependent.current_is_dirty();
        bool 			   new_preset_compatible = is_compatible_with_print(dependent.get_edited_preset_with_vendor_profile(),
        	m_presets->get_preset_with_vendor_profile(*m_presets->find_preset(preset_name, true)), printer_profile);
        if (! canceled)
            canceled = old_preset_dirty && ! new_preset_compatible && ! may_discard_current_dirty_preset(&dependent, preset_name, false, option) && !force_select;
        if (! canceled) {
            // The preset will be switched to a different, compatible preset, or the '-- default --'.
            m_dependent_tabs.emplace_back((printer_technology == ptFFF) ? Preset::Type::TYPE_FILAMENT : Preset::Type::TYPE_SLA_MATERIAL);
            if (old_preset_dirty && ! new_preset_compatible)
                dependent.discard_current_changes();
        }
        BOOST_LOG_TRIVIAL(info) << boost::format("select process, new_preset_compatible %1%, old_preset_dirty %2%, cancelled %3%")
            %new_preset_compatible %old_preset_dirty % canceled;
    } else if (printer_tab) {
        // Before switching the printer to a new one, verify, whether the currently active print and filament
        // are compatible with the new printer.
        // If they are not compatible and the current print or filament are dirty, let user decide
        // whether to discard the changes or keep the current printer selection.
        //
        // With the introduction of the SLA printer types, we need to support switching between
        // the FFF and SLA printers.
        const Preset 		&new_printer_preset     = *m_presets->find_preset(preset_name, true);
		const PresetWithVendorProfile new_printer_preset_with_vendor_profile = m_presets->get_preset_with_vendor_profile(new_printer_preset);
        PrinterTechnology    old_printer_technology = m_presets->get_edited_preset().printer_technology();
        PrinterTechnology    new_printer_technology = new_printer_preset.printer_technology();
        if (new_printer_technology == ptSLA && old_printer_technology == ptFFF && !wxGetApp().may_switch_to_SLA_preset(_omitL("New printer preset selected")))
            canceled = true;
        else {
            struct PresetUpdate {
                Preset::Type         tab_type;
                PresetCollection 	*presets;
                PrinterTechnology    technology;
                bool    	         old_preset_dirty;
                bool         	     new_preset_compatible;
            };
            std::vector<PresetUpdate> updates = {
                { Preset::Type::TYPE_PRINT,         &m_preset_bundle->prints,       ptFFF },
                //{ Preset::Type::TYPE_SLA_PRINT,     &m_preset_bundle->sla_prints,   ptSLA },
                { Preset::Type::TYPE_FILAMENT,      &m_preset_bundle->filaments,    ptFFF },
                //{ Preset::Type::TYPE_SLA_MATERIAL,  &m_preset_bundle->sla_materials,ptSLA }
            };
            Preset *to_be_selected = m_presets->find_preset(preset_name, false, true);
            for (PresetUpdate &pu : updates) {
                pu.old_preset_dirty = (old_printer_technology == pu.technology) && pu.presets->current_is_dirty();
                pu.new_preset_compatible = (new_printer_technology == pu.technology) && is_compatible_with_printer(pu.presets->get_edited_preset_with_vendor_profile(), new_printer_preset_with_vendor_profile);
                if (!canceled)
                    canceled = pu.old_preset_dirty && !pu.new_preset_compatible && !may_discard_current_dirty_preset(pu.presets, preset_name, false, option) && !force_select;
            }
            if (!canceled) {
                for (PresetUpdate &pu : updates) {
                    // The preset will be switched to a different, compatible preset, or the '-- default --'.
                    if (pu.technology == new_printer_technology)
                        m_dependent_tabs.emplace_back(pu.tab_type);
                    if (pu.old_preset_dirty && !pu.new_preset_compatible)
                        pu.presets->discard_current_changes();
                }
            }
        }
        if (! canceled)
        	technology_changed = old_printer_technology != new_printer_technology;

        BOOST_LOG_TRIVIAL(info) << boost::format("select machine, technology_changed %1%, canceled %2%")
                %technology_changed  % canceled;
    }

    BOOST_LOG_TRIVIAL(info) << boost::format("before delete action, canceled %1%, delete_current %2%") %canceled %delete_current;
    bool        delete_third_printer = false;
    std::deque<Preset> filament_presets;
    std::deque<Preset> process_presets;
    if (! canceled && delete_current) {
        // Delete the file and select some other reasonable preset.
        // It does not matter which preset will be made active as the preset will be re-selected from the preset_name variable.
        // The 'external' presets will only be removed from the preset list, their files will not be deleted.
        try {
            //BBS delete preset
            Preset &current_preset = m_presets->get_selected_preset();

            // Obtain compatible filament and process presets for printers
            if (m_preset_bundle && m_presets->get_preset_base(current_preset) == &current_preset && printer_tab && !current_preset.is_system) {
                delete_third_printer = true;
                for (const Preset &preset : m_preset_bundle->filaments.get_presets()) {
                    if (preset.is_compatible && !preset.is_default) {
                        if (preset.inherits() != "")
                            filament_presets.push_front(preset);
                        else
                            filament_presets.push_back(preset);
                        if (!preset.setting_id.empty()) { m_preset_bundle->filaments.set_sync_info_and_save(preset.name, preset.setting_id, "delete", 0); }
                    }
                }
                for (const Preset &preset : m_preset_bundle->prints.get_presets()) {
                    if (preset.is_compatible && !preset.is_default) {
                        if (preset.inherits() != "")
                            process_presets.push_front(preset);
                        else
                            process_presets.push_back(preset);
                        if (!preset.setting_id.empty()) { m_preset_bundle->filaments.set_sync_info_and_save(preset.name, preset.setting_id, "delete", 0); }
                    }
                }
            }
            if (!current_preset.setting_id.empty()) {
                m_presets->set_sync_info_and_save(current_preset.name, current_preset.setting_id, "delete", 0);
                wxGetApp().delete_preset_from_cloud(current_preset.setting_id);
            }
            BOOST_LOG_TRIVIAL(info) << "delete preset = " << current_preset.name << ", setting_id = " << current_preset.setting_id;
            BOOST_LOG_TRIVIAL(info) << boost::format("will delete current preset...");
            m_presets->delete_current_preset();
        } catch (const std::exception & ex) {
            //FIXME add some error reporting!
            canceled = true;
            BOOST_LOG_TRIVIAL(info) << boost::format("found exception when delete: %1%") %ex.what();
        }
    }

    if (canceled) {
        BOOST_LOG_TRIVIAL(info) << boost::format("canceled delete, update ui...");
        if (m_type == Preset::TYPE_PRINTER) {
            if (!last_selected_ph_printer_name.empty() &&
                m_presets->get_edited_preset().name == PhysicalPrinter::get_preset_name(last_selected_ph_printer_name)) {
                // If preset selection was canceled and previously was selected physical printer, we should select it back
                m_preset_bundle->physical_printers.select_printer(last_selected_ph_printer_name);
            }
            if (m_preset_bundle->physical_printers.has_selection()) {
                // If preset selection was canceled and physical printer was selected
                // we must disable selection marker for the physical printers
                m_preset_bundle->physical_printers.unselect_printer();
            }
        }

        update_tab_ui();

        // Trigger the on_presets_changed event so that we also restore the previous value in the plater selector,
        // if this action was initiated from the plater.
        on_presets_changed();
    } else {
        BOOST_LOG_TRIVIAL(info) << boost::format("successfully delete, will update compatibility");
        if (current_dirty)
            m_presets->discard_current_changes();

        const bool is_selected = m_presets->select_preset_by_name(preset_name, false) || delete_current;
        assert(m_presets->get_edited_preset().name == preset_name || ! is_selected);
        // Mark the print & filament enabled if they are compatible with the currently selected preset.
        // The following method should not discard changes of current print or filament presets on change of a printer profile,
        // if they are compatible with the current printer.
        auto update_compatible_type = [delete_current](bool technology_changed, bool on_page, bool show_incompatible_presets) {
        	return (delete_current || technology_changed) ? PresetSelectCompatibleType::Always :
        	       on_page                                ? PresetSelectCompatibleType::Never  :
        	       show_incompatible_presets              ? PresetSelectCompatibleType::OnlyIfWasCompatible : PresetSelectCompatibleType::Always;
        };
        if (current_dirty || delete_current || print_tab || printer_tab)
            m_preset_bundle->update_compatible(
            	update_compatible_type(technology_changed, print_tab,   (print_tab ? this : wxGetApp().get_tab(Preset::TYPE_PRINT))->m_show_incompatible_presets),
            	update_compatible_type(technology_changed, false, 		wxGetApp().get_tab(Preset::TYPE_FILAMENT)->m_show_incompatible_presets));
        // Initialize the UI from the current preset.
        if (printer_tab)
            static_cast<TabPrinter*>(this)->update_pages();

        if (! is_selected && printer_tab)
        {
            /* There is a case, when :
             * after Config Wizard applying we try to select previously selected preset, but
             * in a current configuration this one:
             *  1. doesn't exist now,
             *  2. have another printer_technology
             * So, it is necessary to update list of dependent tabs
             * to the corresponding printer_technology
             */
            const PrinterTechnology printer_technology = m_presets->get_edited_preset().printer_technology();
            if (printer_technology == ptFFF && m_dependent_tabs.front() != Preset::Type::TYPE_PRINT)
                m_dependent_tabs = { Preset::Type::TYPE_PRINT, Preset::Type::TYPE_FILAMENT };
            else if (printer_technology == ptSLA && m_dependent_tabs.front() != Preset::Type::TYPE_SLA_PRINT)
                m_dependent_tabs = { Preset::Type::TYPE_SLA_PRINT, Preset::Type::TYPE_SLA_MATERIAL };
        }

        // check if there is something in the cache to move to the new selected preset
        apply_config_from_cache();

        load_current_preset();

        if (delete_third_printer) {
            wxGetApp().CallAfter([filament_presets, process_presets]() {
                PresetBundle *preset_bundle     = wxGetApp().preset_bundle;
                std::string   old_filament_name = preset_bundle->filaments.get_edited_preset().name;
                std::string   old_process_name  = preset_bundle->prints.get_edited_preset().name;

                for (const Preset &preset : filament_presets) {
                    if (!preset.setting_id.empty()) {
                        wxGetApp().delete_preset_from_cloud(preset.setting_id);
                    }
                    BOOST_LOG_TRIVIAL(info) << "delete filament preset = " << preset.name << ", setting_id = " << preset.setting_id;
                    preset_bundle->filaments.delete_preset(preset.name);
                }

                for (const Preset &preset : process_presets) {
                    if (!preset.setting_id.empty()) {
                        wxGetApp().delete_preset_from_cloud(preset.setting_id);
                    }
                    BOOST_LOG_TRIVIAL(info) << "delete print preset = " << preset.name << ", setting_id = " << preset.setting_id;
                    preset_bundle->prints.delete_preset(preset.name);
                }

                preset_bundle->update_compatible(PresetSelectCompatibleType::Always);
                preset_bundle->filaments.select_preset_by_name(old_filament_name, true);
                preset_bundle->prints.select_preset_by_name(old_process_name, true);
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " old filament name is:" << old_filament_name << " old process name is: " << old_process_name;

            });
        }

    }

    if (technology_changed)
        wxGetApp().mainframe->technology_changed();
    BOOST_LOG_TRIVIAL(info) << boost::format("select preset, exit");

    return !canceled;
}

// If the current preset is dirty, the user is asked whether the changes may be discarded.
// if the current preset was not dirty, or the user agreed to discard the changes, 1 is returned.
bool Tab::may_discard_current_dirty_preset(PresetCollection* presets /*= nullptr*/, const std::string& new_printer_name /*= ""*/, bool no_transfer, ForceOption force_op)
{
    if (presets == nullptr) presets = m_presets;

    UnsavedChangesDialog dlg(m_type, presets, new_printer_name, no_transfer);

    auto handle_save_action = [&dlg, this, presets]() {
        const std::vector<std::string>& unselected_options = dlg.get_unselected_options(presets->type());
        const std::string& name = dlg.get_preset_name();
        //BBS: add project embedded preset relate logic
        bool save_to_project = dlg.get_save_to_project_option();

        if (m_type == presets->type()) // save changes for the current preset from this tab
        {
            // revert unselected options to the old values
            presets->get_edited_preset().config.apply_only(presets->get_selected_preset().config, unselected_options);
            //BBS: add project embedded preset relate logic
            save_preset(name, false, save_to_project);
            //save_preset(name);
        }
        else
        {
            //BBS: add project embedded preset relate logic
            m_preset_bundle->save_changes_for_preset(name, presets->type(), unselected_options, save_to_project);
            //m_preset_bundle->save_changes_for_preset(name, presets->type(), unselected_options);

            // If filament preset is saved for multi-material printer preset,
            // there are cases when filament comboboxs are updated for old (non-modified) colors,
            // but in full_config a filament_colors option aren't.
            if (presets->type() == Preset::TYPE_FILAMENT && wxGetApp().extruders_edited_cnt() > 1)
                wxGetApp().plater()->force_filament_colors_update();
        }
        };

    auto handle_transfer_action = [&dlg, this, presets, no_transfer]() {
        std::vector<std::string> selected_options = dlg.get_selected_options();

        if (m_type == presets->type()) // move changes for the current preset from this tab
        {
            if (m_type == Preset::TYPE_PRINTER) {
                auto it = std::find(selected_options.begin(), selected_options.end(), "extruders_count");
                if (it != selected_options.end()) {
                    // erase "extruders_count" option from the list
                    selected_options.erase(it);
                    // cache the extruders count
                    static_cast<TabPrinter*>(this)->cache_extruder_cnt();
                }
            }

            // copy selected options to the cache from edited preset
            cache_config_diff(selected_options);
        }
        else
            wxGetApp().get_tab(presets->type())->cache_config_diff(selected_options);

        if (!no_transfer) {
            Preset::Type type = presets->type();
            Tab * tab = wxGetApp().get_tab(type);
            auto& options_list = tab->m_options_list;
            auto &cache_options = tab->m_cache_options;
            auto &cache_config = tab->m_cache_config;
            auto &edited_config = presets->get_edited_preset().config;
            std::vector<std::string> variant_options;
            for (auto &opt : cache_options) {
                if (auto n = opt.find('#'); n != std::string::npos) {
                    if (type == Preset::TYPE_FILAMENT && filament_options_with_variant.count(opt.substr(0, n)) == 0)
                        continue;
                    variant_options.push_back(opt.substr(0, n));
                    opt.clear();
                }
            }
            if (!variant_options.empty()) {
                cache_options.erase(std::remove(cache_options.begin(), cache_options.end(), std::string{}), cache_options.end());
                cache_options.push_back(into_u8(dlg.GetTitle()));
                cache_options.push_back(boost::join(variant_options, ";"));
                cache_options.push_back(extruder_variant_keys[type].second);
                variant_options.push_back(extruder_variant_keys[type].second);
                cache_config.apply_only(edited_config, variant_options);
            }
        }
    };

    if (force_op == ForceOption::fopSave) {
        handle_save_action();
        return true;
    }

    if (force_op == ForceOption::fopTransfer) {
        handle_transfer_action();
        return true;
    }

    if (dlg.ShowModal() == wxID_CANCEL)
        return false;

    if (dlg.save_preset()) {
        handle_save_action();
    }
    else if (dlg.transfer_changes()) {
        handle_transfer_action();
    }

    return true;
}

void Tab::clear_pages()
{
    // invalidated highlighter, if any exists
    m_highlighter.invalidate();
    // clear pages from the controlls
    for (auto p : m_pages)
        p->clear();
    //BBS: clear page in Parent
    //m_page_sizer->Clear(true);
    m_parent->clear_page();

    // nulling pointers
    m_parent_preset_description_line = nullptr;
    m_detach_preset_btn = nullptr;

    m_compatible_printers.checkbox  = nullptr;
    m_compatible_printers.btn       = nullptr;

    m_compatible_prints.checkbox    = nullptr;
    m_compatible_prints.btn         = nullptr;
}

//BBS: GUI refactor: unselect current item
void Tab::unselect_tree_item()
{
    // BBS: bold selection
    const auto sel_item = m_tabctrl->GetSelection();
    m_last_select_item = sel_item;
    m_tabctrl->SetItemBold(sel_item, false);
    m_tabctrl->Unselect();
    m_active_page = nullptr;
}

// BBS: open/close this tab
void Tab::set_expanded(bool value)
{
    if (value) {
        if (m_presets_choice)
            m_main_sizer->Show(m_presets_choice);
        m_main_sizer->Show(m_tabctrl);
    }
    else {
        m_active_page = NULL;
        if (m_presets_choice)
            m_main_sizer->Hide(m_presets_choice);
        m_main_sizer->Hide(m_tabctrl);
    }
}

// BBS: new layout
void Tab::restore_last_select_item()
{
    auto item = m_last_select_item;
    if (item == -1)
        item = m_tabctrl->GetFirstVisibleItem();
    m_tabctrl->SelectItem(item);
}

void Tab::update_description_lines()
{
    if (m_active_page && m_active_page->title() == "Dependencies" && m_parent_preset_description_line)
        update_preset_description_line();
}

void Tab::activate_selected_page(std::function<void()> throw_if_canceled)
{
    if (!m_active_page)
        return;

    m_active_page->activate(m_mode, throw_if_canceled);
    update_changed_ui();
    update_description_lines();
    toggle_options();
    m_active_page->update_visibility(m_mode, true); // for taggle line
}

//BBS: GUI refactor
bool Tab::update_current_page_in_background(int& item)
{
    Page* page = nullptr;

    const auto selection = item >= 0 ? m_tabctrl->GetItemText(item) : "";
    for (auto p : m_pages)
        if (translate_category(p->title(), m_type) == selection)
        {
            page = p.get();
            break;
        }

    if (page == nullptr || m_active_page == page)
        return false;

    bool active_tab = false;
    if (wxGetApp().mainframe != nullptr && wxGetApp().mainframe->is_active_and_shown_tab(m_parent))
        active_tab = true;

    if (!active_tab || (!m_parent->is_active_and_shown_tab((wxPanel*)this)))
    {
        m_is_nonsys_values = page->m_is_nonsys_values;
        m_is_modified_values = page->m_is_modified_values;
        // BBS: not need active
        // m_active_page = page;

        // invalidated highlighter, if any exists
        m_highlighter.invalidate();

        // clear pages from the controlls
        // BBS: fix after new layout, clear page in backgroud
        for (auto p : m_pages)
            p->clear();
        if (m_parent->is_active_and_shown_tab((wxPanel*)this))
            m_parent->clear_page();

        update_undo_buttons();

        // BBS: this is not used, because we not SelectItem in background
        //todo: update selected item of tree_ctrl
        // wxTreeItemData item_data;
        // m_tabctrl->SetItemData(item, &item_data);

        return false;
    }

    return true;
}

//BBS: GUI refactor
bool Tab::tree_sel_change_delayed(wxCommandEvent& event)
{
    // The issue apparently manifests when Show()ing a window with overlay scrollbars while the UI is frozen. For this reason,
    // we will Thaw the UI prematurely on Linux. This means destroing the no_updates object prematurely.
#ifdef __linux__
    std::unique_ptr<wxWindowUpdateLocker> no_updates(new wxWindowUpdateLocker(this));
#else
    /* On Windows we use DoubleBuffering during rendering,
     * so on Window is no needed to call a Freeze/Thaw functions.
     * But under OSX (builds compiled with MacOSX10.14.sdk) wxStaticBitmap rendering is broken without Freeze/Thaw call.
     */
//#ifdef __WXOSX__  // Use Freeze/Thaw to avoid flickering during clear/activate new page
//    wxWindowUpdateLocker noUpdates(this);
//#endif
#endif

    //BBS: GUI refactor
    Page* page = nullptr;
    const auto sel_item = m_tabctrl->GetSelection();
    // BBS: bold selection
    //OutputDebugStringA("tree_sel_change_delayed ");
    //OutputDebugStringA(m_title.c_str());
    m_tabctrl->SetItemBold(sel_item, true);
    const auto selection = sel_item >= 0 ? m_tabctrl->GetItemText(sel_item) : "";
    //OutputDebugString(selection);
    //OutputDebugStringA("\n");
    for (auto p : m_pages)
        if (translate_category(p->title(), m_type) == selection)
        {
            page = p.get();
            m_is_nonsys_values = page->m_is_nonsys_values;
            m_is_modified_values = page->m_is_modified_values;
            break;
        }

    //BBS: GUI refactor
    if (page == nullptr)
    {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("can not find page with current selection %1%\n") % selection;
        return false;
    }
    void* item_data = m_tabctrl->GetItemData(sel_item);
    if (item_data)
    {
        //from update_current_page_in_background in not active tab
        m_tabctrl->SetItemData(sel_item, NULL);
        return false;
    }

    if (!m_parent->is_active_and_shown_tab((wxPanel*)this))
    {
        Tab* current_tab = dynamic_cast<Tab*>(m_parent->get_current_tab());

        m_page_view->Freeze();

        if (current_tab)
        {
            current_tab->clear_pages();
            current_tab->unselect_tree_item();
        }
        m_active_page = page;
        // BBS: not changed
        // update_undo_buttons();
        this->OnActivate();
        m_parent->set_active_tab(this);
        if (m_variant_sizer) {
            wxWindow *variant_ctrl = m_extruder_switch ? (wxWindow *) m_extruder_switch : m_variant_combo;
            m_main_sizer->Show(m_variant_sizer, variant_ctrl->IsThisEnabled() && !m_active_page->m_opt_id_map.empty() && !m_active_page->title().StartsWith("Extruder "));
            if (m_extruder_sync) m_extruder_sync->Show(variant_ctrl->IsShown() && m_extruder_sync->IsThisEnabled());
            GetParent()->Layout();
        }

        m_page_view->Thaw();
        return false;
    }

    //process logic in the same tab when select treeCtrlItem
    if (m_active_page == page)
        return false;

    m_active_page = page;
    if (m_variant_sizer) {
        wxWindow *variant_ctrl = m_extruder_switch ? (wxWindow *) m_extruder_switch : m_variant_combo;
        m_main_sizer->Show(m_variant_sizer, variant_ctrl->IsThisEnabled() && !m_active_page->m_opt_id_map.empty() && !m_active_page->title().StartsWith("Extruder"));
        if (m_extruder_sync) m_extruder_sync->Show(variant_ctrl->IsShown() && m_extruder_sync->IsThisEnabled());
        GetParent()->Layout();
    }

    auto throw_if_canceled = std::function<void()>([this](){
#ifdef WIN32
            //BBS: GUI refactor
            //TODO: remove this call currently, after refactor, there is Paint event in the queue
            //this call will cause OnPaint immediately, which will cause crash
            //wxCheckForInterrupt(m_tabctrl);
            if (m_page_switch_planned)
                throw UIBuildCanceled();
#else // WIN32
            (void)this; // silence warning
#endif
        });

    try {
        m_page_view->Freeze();
        // clear pages from the controls
        clear_pages();
        throw_if_canceled();

        //BBS: GUI refactor
        if (wxGetApp().mainframe!=nullptr && wxGetApp().mainframe->is_active_and_shown_tab(m_parent))
            activate_selected_page(throw_if_canceled);

        #ifdef __linux__
            no_updates.reset(nullptr);
        #endif

        // BBS: not changed
        // update_undo_buttons();
        throw_if_canceled();

        //BBS: GUI refactor
        //m_hsizer->Layout();
        m_parent->Layout();
        throw_if_canceled();
        // Refresh();

        m_page_view->Thaw();
    } catch (const UIBuildCanceled&) {
	    if (m_active_page)
		    m_active_page->clear();
        m_page_view->Thaw();
        return true;
    }

    return false;
}

void Tab::OnKeyDown(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_TAB)
        m_tabctrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
    else
        event.Skip();
}

void Tab::compare_preset()
{
    wxGetApp().mainframe->diff_dialog.show(m_type);
}

// Save the current preset into file.
// This removes the "dirty" flag of the preset, possibly creates a new preset under a new name,
// and activates the new preset.
// Wizard calls save_preset with a name "My Settings", otherwise no name is provided and this method
// opens a Slic3r::GUI::SavePresetDialog dialog.
//BBS: add project embedded preset relate logic
void Tab::save_preset(std::string name /*= ""*/, bool detach, bool save_to_project, bool from_input, std::string input_name )
{
    // since buttons(and choices too) don't get focus on Mac, we set focus manually
    // to the treectrl so that the EVT_* events are fired for the input field having
    // focus currently.is there anything better than this ?
//!	m_tabctrl->OnSetFocus();
    if (from_input) {
        SavePresetDialog dlg(m_parent, m_type, detach ? _u8L("Detached") : "");
        dlg.Show(false);
        dlg.input_name_from_other(input_name);
        wxCommandEvent evt(wxEVT_TEXT, GetId());
        dlg.GetEventHandler()->ProcessEvent(evt);
        dlg.confirm_from_other();
        name = input_name;
    }

    if (name.empty()) {
        SavePresetDialog dlg(m_parent, m_type, detach ? _u8L("Detached") : "");
        if (!m_just_edit) {
            if (dlg.ShowModal() != wxID_OK)
                return;
        }
        name = dlg.get_name();
        //BBS: add project embedded preset relate logic
        save_to_project = dlg.get_save_to_project_selection(m_type);
    }

    //BBS record current preset name
    std::string curr_preset_name = m_presets->get_edited_preset().name;
    auto        curr_preset      = m_presets->get_edited_preset();
    std::map<std::string, std::string> extra_map;
    {
        bool is_configed_by_BBL = PresetUtils::system_printer_bed_model(curr_preset).size() > 0;
        if (is_configed_by_BBL) {//only record svg
            if (wxGetApp().app_config->has_section("user_bbl_svg_list")) {
                auto user_bbl_svg_list = wxGetApp().app_config->get_section("user_bbl_svg_list");
                if (user_bbl_svg_list.size() > 0 && user_bbl_svg_list[curr_preset_name].size() > 0) {
                    extra_map["bed_custom_texture"] = ConfigOptionString(user_bbl_svg_list[curr_preset_name]);
                }
            }
        }
        else {//for cutom machine
            auto bed_model_path = wxGetApp().plater()->get_partplate_list().get_bed3d()->get_model_filename();
            if (!bed_model_path.empty()) {
                extra_map["bed_custom_model"] = bed_model_path;
            }
            auto logo = wxGetApp().plater()->get_partplate_list().get_logo_texture_filename();
            if (!logo.empty()) {
                extra_map["bed_custom_texture"] = logo;
            }
        }
    }
    bool exist_preset = false;
    Preset* new_preset = m_presets->find_preset(name, false);
    if (new_preset) {
        exist_preset = true;
    }

    // Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
    m_presets->save_current_preset(name, detach, save_to_project, nullptr, &extra_map);

    //BBS create new settings
    new_preset = m_presets->find_preset(name, false, true);
    //Preset* preset = &m_presets.preset(it - m_presets.begin(), true);
    if (!new_preset) {
        BOOST_LOG_TRIVIAL(info) << "create new preset failed";
        return;
    }

    // set sync_info for sync service
    if (exist_preset) {
        new_preset->sync_info = "update";
        BOOST_LOG_TRIVIAL(info) << "sync_preset: update preset = " << new_preset->name;
    }
    else {
        new_preset->sync_info = "create";
        if (wxGetApp().is_user_login())
            new_preset->user_id = wxGetApp().getAgent()->get_user_id();
        BOOST_LOG_TRIVIAL(info) << "sync_preset: create preset = " << new_preset->name;
    }
    new_preset->save_info();

    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // If saving the preset changes compatibility with other presets, keep the now incompatible dependent presets selected, however with a "red flag" icon showing that they are no more compatible.
    m_preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
    // Add the new item into the UI component, remove dirty flags and activate the saved item.
    update_tab_ui();

    // Update the selection boxes at the plater.
    on_presets_changed();

    //BBS if create a new prset name, preset changed from preset name to new preset name
    if (!exist_preset) {
        wxGetApp().plater()->sidebar().update_presets_from_to(m_type, curr_preset_name, new_preset->name);
    }

    // If current profile is saved, "delete preset" button have to be enabled
    m_btn_delete_preset->Show();
    m_btn_delete_preset->GetParent()->Layout();

    if (m_type == Preset::TYPE_PRINTER)
        static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<TabPrinter*>(this)->m_extruders_count;

    // Parent preset is "default" after detaching, so we should to update UI values, related on parent preset
    if (detach)
        update_ui_items_related_on_parent_preset(m_presets->get_selected_preset_parent());

    update_changed_ui();

    /* If filament preset is saved for multi-material printer preset,
     * there are cases when filament comboboxs are updated for old (non-modified) colors,
     * but in full_config a filament_colors option aren't.*/
    if (m_type == Preset::TYPE_FILAMENT && wxGetApp().extruders_edited_cnt() > 1)
        wxGetApp().plater()->force_filament_colors_update();

    {
        // Profile compatiblity is updated first when the profile is saved.
        // Update profile selection combo boxes at the depending tabs to reflect modifications in profile compatibility.
        std::vector<Preset::Type> dependent;
        switch (m_type) {
        case Preset::TYPE_PRINT:
            dependent = { Preset::TYPE_FILAMENT };
            break;
        case Preset::TYPE_SLA_PRINT:
            dependent = { Preset::TYPE_SLA_MATERIAL };
            break;
        case Preset::TYPE_PRINTER:
            if (static_cast<const TabPrinter*>(this)->m_printer_technology == ptFFF)
                dependent = { Preset::TYPE_PRINT, Preset::TYPE_FILAMENT };
            else
                dependent = { Preset::TYPE_SLA_PRINT, Preset::TYPE_SLA_MATERIAL };
            break;
        default:
            break;
        }
        for (Preset::Type preset_type : dependent)
            wxGetApp().get_tab(preset_type)->update_tab_ui();
    }

    // update preset comboboxes in DiffPresetDlg
    wxGetApp().mainframe->diff_dialog.update_presets(m_type);
}

// Called for a currently selected preset.
void Tab::delete_preset()
{
    auto current_preset = m_presets->get_selected_preset();
    // Don't let the user delete the ' - default - ' configuration.
    //BBS: add project embedded preset logic and refine is_external
    std::string action =  _utf8(L("Delete"));
    //std::string action = current_preset.is_external ? _utf8(L("remove")) : _utf8(L("delete"));
    // TRN  remove/delete
    wxString msg;
    bool     confirm_delete_third_party_printer = false;
    bool     is_base_preset                 = false;
    if (m_presets->get_preset_base(current_preset) == &current_preset) { //root preset
        is_base_preset = true;
        if (current_preset.type == Preset::Type::TYPE_PRINTER && !current_preset.is_system) { //Customize third-party printers
            Preset &current_preset = m_presets->get_selected_preset();
            int filament_preset_num    = 0;
            int process_preset_num     = 0;
            for (const Preset &preset : m_preset_bundle->filaments.get_presets()) {
                if (preset.is_compatible && !preset.is_default) { filament_preset_num++; }
            }
            for (const Preset &preset : m_preset_bundle->prints.get_presets()) {
                if (preset.is_compatible && !preset.is_default) { process_preset_num++; }
            }

            DeleteConfirmDialog
                dlg(parent(), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Delete"),
                    wxString::Format(_L("%d Filament Preset and %d Process Preset is attached to this printer. Those presets would be deleted if the printer is deleted."),
                                     filament_preset_num, process_preset_num));
            int res = dlg.ShowModal();
            if (res != wxID_OK) return;
            confirm_delete_third_party_printer = true;
        }
        int count = 0;
        wxString presets;
        for (auto &preset2 : *m_presets)
            if (preset2.inherits() == current_preset.name) {
                ++count;
                presets += "\n - " + from_u8(preset2.name);
            }
        if (count > 0) {
            msg = _L("Presets inherited by other presets can not be deleted!");
            msg += "\n";
            msg += _L_PLURAL("The following presets inherit this preset.",
                            "The following preset inherits this preset.", count);
            wxString title = from_u8((boost::format(_utf8(L("%1% Preset"))) % action).str()); // action + _(L(" Preset"));
            MessageDialog(parent(), msg + presets, title, wxOK | wxICON_ERROR).ShowModal();
            return;
        }
    }

    BOOST_LOG_TRIVIAL(info) << boost::format("delete preset %1%, setting_id %2%, user_id %3%, base_id %4%, sync_info %5%, type %6%")
        %current_preset.name%current_preset.setting_id%current_preset.user_id%current_preset.base_id%current_preset.sync_info
        %Preset::get_type_string(m_type);
    PhysicalPrinterCollection& physical_printers = m_preset_bundle->physical_printers;

    if (m_type == Preset::TYPE_PRINTER && !physical_printers.empty())
    {
        // Check preset for delete in physical printers
        // Ask a customer about next action, if there is a printer with just one preset and this preset is equal to delete
        std::vector<std::string> ph_printers        = physical_printers.get_printers_with_preset(current_preset.name);
        std::vector<std::string> ph_printers_only   = physical_printers.get_printers_with_only_preset(current_preset.name);

        //if (!ph_printers.empty()) {
        //    msg += _L_PLURAL("The physical printer below is based on the preset, you are going to delete.",
        //                        "The physical printers below are based on the preset, you are going to delete.", ph_printers.size());
        //    for (const std::string& printer : ph_printers)
        //        msg += "\n    \"" + from_u8(printer) + "\",";
        //    msg.RemoveLast();
        //    msg += "\n" + _L_PLURAL("Note, that the selected preset will be deleted from this printer too.",
        //                            "Note, that the selected preset will be deleted from these printers too.", ph_printers.size()) + "\n\n";
        //}

        //if (!ph_printers_only.empty()) {
        //    msg += _L_PLURAL("The physical printer below is based only on the preset, you are going to delete.",
        //                        "The physical printers below are based only on the preset, you are going to delete.", ph_printers_only.size());
        //    for (const std::string& printer : ph_printers_only)
        //        msg += "\n    \"" + from_u8(printer) + "\",";
        //    msg.RemoveLast();
        //    msg += "\n" + _L_PLURAL("Note, that this printer will be deleted after deleting the selected preset.",
        //                            "Note, that these printers will be deleted after deleting the selected preset.", ph_printers_only.size()) + "\n\n";
        //}
        if (!ph_printers.empty() || !ph_printers_only.empty()) {
            msg += _L_PLURAL("Following preset will be deleted too.", "Following presets will be deleted too.", ph_printers.size() + ph_printers_only.size());
            for (const std::string &printer : ph_printers) msg += "\n    \"" + from_u8(printer) + "\",";
            for (const std::string &printer : ph_printers_only) msg += "\n    \"" + from_u8(printer) + "\",";
            msg.RemoveLast();
            // msg += "\n" + _L_PLURAL("Note, that the selected preset will be deleted from this printer too.",
            //                        "Note, that the selected preset will be deleted from these printers too.", ph_printers.size()) + "\n\n";
        }
    }

    if (is_base_preset && (current_preset.type == Preset::Type::TYPE_FILAMENT) && action == _utf8(L("Delete"))) {
        msg += from_u8(_u8L("Are you sure to delete the selected preset? \nIf the preset corresponds to a filament currently in use on your printer, please reset the filament information for that slot."));
    } else {
        msg += from_u8((boost::format(_u8L("Are you sure to %1% the selected preset?")) % action).str());
    }

    //BBS: add project embedded preset logic and refine is_external
    action =  _utf8(L("Delete"));
    //action = current_preset.is_external ? _utf8(L("Remove")) : _utf8(L("Delete"));
    // TRN  Remove/Delete
    wxString title = from_u8((boost::format(_utf8(L("%1% Preset"))) % action).str());  //action + _(L(" Preset"));
    if (current_preset.is_default || !(confirm_delete_third_party_printer ||
        //wxID_YES != wxMessageDialog(parent(), msg, title, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal())
        wxID_YES == MessageDialog(parent(), msg, title, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal()))
        return;
    auto delete_cur_bed_type_to_config = [this]() {
        PresetBundle &preset_bundle   = *wxGetApp().preset_bundle;
        auto          cur_preset_name = preset_bundle.printers.get_edited_preset().name;
        if (cur_preset_name.size() > 0 && wxGetApp().app_config->has_section("user_bed_type_list")) {
            auto data        = wxGetApp().app_config->get_section("user_bed_type_list");
            auto data_modify = const_cast<std::map<std::string, std::string> *>(&data);
            if ((*data_modify).find(cur_preset_name) != data_modify->end()) {
                data_modify->erase(cur_preset_name);
                wxGetApp().app_config->set_section("user_bed_type_list", *data_modify);
            }
        }
    };
    delete_cur_bed_type_to_config();
    // if we just delete preset from the physical printer
    if (m_presets_choice->is_selected_physical_printer()) {
        PhysicalPrinter& printer = physical_printers.get_selected_printer();

        // just delete this preset from the current physical printer
        printer.delete_preset(m_presets->get_edited_preset().name);
        // select first from the possible presets for this printer
        physical_printers.select_printer(printer);

        this->select_preset(physical_printers.get_selected_printer_preset_name());
        return;
    }

    // delete selected preset from printers and printer, if it's needed
    if (m_type == Preset::TYPE_PRINTER && !physical_printers.empty())
        physical_printers.delete_preset_from_printers(current_preset.name);

    // Select will handle of the preset dependencies, of saving & closing the depending profiles, and
    // finally of deleting the preset.
    this->select_preset("", true);

    BOOST_LOG_TRIVIAL(info) << boost::format("delete preset finished");
}

void Tab::toggle_show_hide_incompatible()
{
    m_show_incompatible_presets = !m_show_incompatible_presets;
    if (m_presets_choice)
        m_presets_choice->set_show_incompatible_presets(m_show_incompatible_presets);
    update_show_hide_incompatible_button();
    update_tab_ui();
}

void Tab::update_show_hide_incompatible_button()
{
    //BBS: GUI refactor
    /*m_btn_hide_incompatible_presets->SetBitmap_(m_show_incompatible_presets ?
        m_bmp_show_incompatible_presets : m_bmp_hide_incompatible_presets);
    m_btn_hide_incompatible_presets->SetToolTip(m_show_incompatible_presets ?
        "Both compatible an incompatible presets are shown. Click to hide presets not compatible with the current printer." :
        "Only compatible presets are shown. Click to show both the presets compatible and not compatible with the current printer.");*/
}

void Tab::update_ui_from_settings()
{
    // Show the 'show / hide presets' button only for the print and filament tabs, and only if enabled
    // in application preferences.
    m_show_btn_incompatible_presets = true;
    bool show = m_show_btn_incompatible_presets && m_type != Slic3r::Preset::TYPE_PRINTER;
    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();
    //show ? m_btn_hide_incompatible_presets->Show() :  m_btn_hide_incompatible_presets->Hide();
    // If the 'show / hide presets' button is hidden, hide the incompatible presets.
    if (show) {
        update_show_hide_incompatible_button();
    }
    else {
        if (m_show_incompatible_presets) {
            m_show_incompatible_presets = false;
            update_tab_ui();
        }
    }
}

void Tab::create_line_with_widget(ConfigOptionsGroup* optgroup, const std::string& opt_key, const std::string& path, widget_t widget)
{
    Line line = optgroup->create_single_option_line(opt_key);
    line.widget = widget;
    line.label_path = path;

    m_colored_Label_colors[opt_key] = m_default_text_clr;
    line.full_Label_color = &m_colored_Label_colors[opt_key];

    optgroup->append_line(line);
}

// Return a callback to create a Tab widget to mark the preferences as compatible / incompatible to the current printer.
wxSizer* Tab::compatible_widget_create(wxWindow* parent, PresetDependencies &deps)
{
    deps.checkbox = new wxCheckBox(parent, wxID_ANY, _(L("All")));
    deps.checkbox->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(deps.checkbox, false, true);
    deps.btn = new ScalableButton(parent, wxID_ANY, "printer", from_u8((boost::format(" %s %s") % _utf8(L("Set")) % std::string(dots.ToUTF8())).str()),
                                  wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT, true);
    deps.btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    deps.btn->SetSize(deps.btn->GetBestSize());

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add((deps.checkbox), 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add((deps.btn), 0, wxALIGN_CENTER_VERTICAL);

    deps.checkbox->Bind(wxEVT_CHECKBOX, ([this, &deps](wxCommandEvent e)
    {
        deps.btn->Enable(! deps.checkbox->GetValue());
        // All printers have been made compatible with this preset.
        if (deps.checkbox->GetValue())
            this->load_key_value(deps.key_list, std::vector<std::string> {});
        this->get_field(deps.key_condition)->toggle(deps.checkbox->GetValue());
        this->update_changed_ui();
    }) );

    deps.btn->Bind(wxEVT_BUTTON, ([this, parent, &deps](wxCommandEvent e)
    {
        // Collect names of non-default non-external profiles.
        PrinterTechnology printer_technology = m_preset_bundle->printers.get_edited_preset().printer_technology();
        PresetCollection &depending_presets  = (deps.type == Preset::TYPE_PRINTER) ? m_preset_bundle->printers :
                (printer_technology == ptFFF) ? m_preset_bundle->prints : m_preset_bundle->sla_prints;
        wxArrayString presets;
        for (size_t idx = 0; idx < depending_presets.size(); ++ idx)
        {
            Preset& preset = depending_presets.preset(idx);
            //BBS: add project embedded preset logic and refine is_external
            bool add = ! preset.is_default;
            //bool add = ! preset.is_default && ! preset.is_external;
            if (add && deps.type == Preset::TYPE_PRINTER)
                // Only add printers with the same technology as the active printer.
                add &= preset.printer_technology() == printer_technology;
            if (add)
                presets.Add(from_u8(preset.name));
        }

        wxMultiChoiceDialog dlg(parent, deps.dialog_title, deps.dialog_label, presets);
        wxGetApp().UpdateDlgDarkUI(&dlg);
        // Collect and set indices of depending_presets marked as compatible.
        wxArrayInt selections;
        auto *compatible_printers = dynamic_cast<const ConfigOptionStrings*>(m_config->option(deps.key_list));
        if (compatible_printers != nullptr || !compatible_printers->values.empty())
            for (auto preset_name : compatible_printers->values)
                for (size_t idx = 0; idx < presets.GetCount(); ++idx)
                    if (presets[idx] == preset_name) {
                        selections.Add(idx);
                        break;
                    }
        dlg.SetSelections(selections);
        std::vector<std::string> value;
        // Show the dialog.
        if (dlg.ShowModal() == wxID_OK) {
            selections.Clear();
            selections = dlg.GetSelections();
            for (auto idx : selections)
                value.push_back(presets[idx].ToUTF8().data());
            if (value.empty()) {
                deps.checkbox->SetValue(1);
                deps.btn->Disable();
            }
            // All depending_presets have been made compatible with this preset.
            this->load_key_value(deps.key_list, value);
            this->update_changed_ui();
        }
    }));

    return sizer;
}

void TabPrinter::set_extruder_volume_type(int extruder_id, NozzleVolumeType type)
{
    auto nozzle_volumes = m_preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    assert(nozzle_volumes->values.size() > (size_t)extruder_id);
    nozzle_volumes->values[extruder_id] = type;
    on_value_change((boost::format("nozzle_volume_type#%1%") % extruder_id).str(), int(type));
    update_dirty();

    //save to app config
    if (!m_base_preset_name.empty()) {
        ConfigOptionEnumsGeneric* nozzle_volume_type_option = m_preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
        std::string nozzle_volume_type_str = nozzle_volume_type_option->serialize();
        wxGetApp().app_config->save_nozzle_volume_types_to_config(m_base_preset_name, nozzle_volume_type_str);
    }

}

// Return a callback to create a TabPrinter widget to edit bed shape
wxSizer* TabPrinter::create_bed_shape_widget(wxWindow* parent)
{
    ScalableButton* btn = new ScalableButton(parent, wxID_ANY, "printer", " " + _(L("Set")) + " " + dots,
        wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT, true);
    btn->SetFont(wxGetApp().normal_font());
    btn->SetSize(btn->GetBestSize());

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);

    btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) {
            bool  is_configed_by_BBL = PresetUtils::system_printer_bed_model(m_preset_bundle->printers.get_edited_preset()).size() > 0;
            ConfigOptionString custom_texture     = *m_config->option<ConfigOptionString>("bed_custom_texture");
            PresetBundle &     preset_bundle      = *wxGetApp().preset_bundle;
            auto               cur_preset_name    = preset_bundle.printers.get_edited_preset().name;
            if (is_configed_by_BBL && wxGetApp().app_config->has_section("user_bbl_svg_list")) {
                auto user_bbl_svg_list = wxGetApp().app_config->get_section("user_bbl_svg_list");
                if (user_bbl_svg_list.size() > 0 && user_bbl_svg_list[cur_preset_name].size() > 0) {
                    custom_texture = ConfigOptionString(user_bbl_svg_list[cur_preset_name]);
                }
            }
            BedShapeDialog dlg(this);
            dlg.build_dialog(*m_config->option<ConfigOptionPoints>("printable_area"), custom_texture,
                             *m_config->option<ConfigOptionString>("bed_custom_model") , !is_configed_by_BBL);
            if (dlg.ShowModal() == wxID_OK) {
                if (dlg.get_valid()) {
                    std::string custom_texture = dlg.get_custom_texture();
                    if (is_configed_by_BBL) {
                        {//save to user_bbl_svg_list
                            if (!wxGetApp().app_config->has_section("user_bbl_svg_list")) {
                                std::map<std::string, std::string> data;
                                data[cur_preset_name] = custom_texture;
                                wxGetApp().app_config->set_section("user_bbl_svg_list", data);
                            } else {
                                auto data                       = wxGetApp().app_config->get_section("user_bbl_svg_list");
                                auto data_modify                = const_cast<std::map<std::string, std::string> *>(&data);
                                (*data_modify)[cur_preset_name] = custom_texture;
                                wxGetApp().app_config->set_section("user_bbl_svg_list", *data_modify);
                            }
                        }
                        load_key_value("bed_custom_texture", custom_texture);
                        update_changed_ui();
                    } else {
                        const std::vector<Vec2d> &shape          = dlg.get_shape();
                        const std::string &       custom_model   = dlg.get_custom_model();
                        if (!shape.empty()) {
                            load_key_value("printable_area", shape);
                            load_key_value("bed_custom_texture", custom_texture);
                            load_key_value("bed_custom_model", custom_model);
                            update_changed_ui();
                        }
                    }
                    if (custom_texture == "") {
                        wxGetApp().plater()->get_partplate_list().update_logo_texture_filename("");
                    }

                } else {
                    show_error(m_parent, _L("Invalid input."));
                }
            }
        }));

    {
        Search::OptionsSearcher& searcher = wxGetApp().sidebar().get_searcher();
        const Search::GroupAndCategory& gc = searcher.get_group_and_category("printable_area");
        searcher.add_key("bed_custom_texture", m_type, gc.group, gc.category);
        searcher.add_key("bed_custom_model", m_type, gc.group, gc.category);
    }

    return sizer;
}

void TabPrinter::cache_extruder_cnt()
{
    if (m_presets->get_edited_preset().printer_technology() == ptSLA)
        return;

    // BBS. Get extruder count from preset instead of m_extruders_count.
    m_cache_extruder_count = dynamic_cast<ConfigOptionFloatsNullable*>((m_presets->get_edited_preset().config).option("nozzle_diameter"))->values.size();
}

bool TabPrinter::apply_extruder_cnt_from_cache()
{
    if (m_presets->get_edited_preset().printer_technology() == ptSLA)
        return false;

    if (m_cache_extruder_count > 0) {
        m_presets->get_edited_preset().set_num_extruders(m_cache_extruder_count);
        m_cache_extruder_count = 0;
        return true;
    }
    return false;
}

bool Tab::validate_custom_gcodes()
{
    if (m_type != Preset::TYPE_FILAMENT &&
        (m_type != Preset::TYPE_PRINTER || static_cast<TabPrinter*>(this)->m_printer_technology != ptFFF))
        return true;
    if (m_active_page->title() != L("Custom G-code"))
        return true;

    // When we switch Settings tab after editing of the custom g-code, then warning message could ba already shown after KillFocus event
    // and then it's no need to show it again
    if (validate_custom_gcodes_was_shown) {
        validate_custom_gcodes_was_shown = false;
        return true;
    }

    bool valid = true;
    for (auto opt_group : m_active_page->m_optgroups) {
        assert(opt_group->opt_map().size() == 1);
        if (!opt_group->is_activated())
            break;
        std::string key = opt_group->opt_map().begin()->first;
        valid &= validate_custom_gcode(opt_group->title, boost::any_cast<std::string>(opt_group->get_value(key)));
        if (!valid)
            break;
    }
    return valid;
}

void Tab::set_just_edit(bool just_edit)
{
    m_just_edit = just_edit;
    if (just_edit) {
        m_presets_choice->Disable();
        m_btn_delete_preset->Disable();
    } else {
        m_presets_choice->Enable();
        m_btn_delete_preset->Enable();
    }
}

/// <summary>
///     Call from:
///         1: on_value_change "nozzle_volume_type"
///         2: on_preset_loaded (extruder_id = -1)
/// </summary>
/// <param name="extruder_id"></param>

void Tab::update_extruder_variants(int extruder_id, bool reload)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << extruder_id;
    if (m_extruder_switch) {
        auto    nozzle_volumes = m_preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
        int extruder_nums = m_preset_bundle->get_printer_extruder_count();
        nozzle_volumes->values.resize(extruder_nums);
        if (extruder_nums == 2) {
            auto     nozzle_volumes_def = m_preset_bundle->project_config.def()->get("nozzle_volume_type");
            wxString left, right;
            for (size_t i = 0; i < nozzle_volumes_def->enum_labels.size(); ++i) {
                if (nozzle_volumes->values[0] == i) left = _L(nozzle_volumes_def->enum_labels[i]);
                if (nozzle_volumes->values[1] == i) right = _L(nozzle_volumes_def->enum_labels[i]);
            }
            m_extruder_switch->SetLabels(wxString::Format(_L("Left: %s"), left), wxString::Format(_L("Right: %s"), right));
            m_extruder_switch->SetValue(extruder_id == 1);
            m_extruder_switch->Enable(true);
            m_extruder_sync->Enable(left == right);
        } else {
            m_extruder_switch->Enable(false);
            m_extruder_sync->Enable(false);
        }
    } else if (m_variant_combo) {
        if (extruder_id >= 0) // variant_combo did not depend on extruder
            return;
        auto variants = m_config->option<ConfigOptionStrings>("filament_extruder_variant");
        int  n        = m_variant_combo->GetSelection();
        m_variant_combo->Clear();
        for (auto &v : variants->values) {
            int n = v.find("Drive ");
            if (n != std::string::npos)
                m_variant_combo->Append(_L(v.substr(0, n + 5)) + " " + _L(v.substr(n + 6)));
            else
                m_variant_combo->Append(_L(v));
        }
        m_variant_combo->SetSelection(n < 0 || (unsigned int)n >= m_variant_combo->GetCount() ? 0 : n);
        m_variant_combo->Enable(m_variant_combo->GetCount() > 1);
    }
    switch_excluder(extruder_id, reload);
    if (m_variant_sizer) {
        wxWindow *variant_ctrl = m_extruder_switch ? (wxWindow *) m_extruder_switch : m_variant_combo;
        m_main_sizer->Show(m_variant_sizer, variant_ctrl->IsThisEnabled() && m_active_page && !m_active_page->m_opt_id_map.empty() && !m_active_page->title().StartsWith("Extruder "));
        if (m_extruder_sync) m_extruder_sync->Show(variant_ctrl->IsShown() && m_extruder_sync->IsThisEnabled());
        GetParent()->Layout();
    }
}

void Tab::switch_excluder(int extruder_id, bool reload)
{
    Preset & printer_preset = m_preset_bundle->printers.get_edited_preset();
    auto nozzle_volumes = m_preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    auto extruders      = printer_preset.config.option<ConfigOptionEnumsGeneric>("extruder_type");
    if (m_extruder_switch && m_type != Preset::TYPE_PRINTER) {
        int current_extruder = m_extruder_switch->IsThisEnabled() && m_extruder_switch->GetValue() ? 1 : 0;
        m_extruder_sync->Enable(m_extruder_switch->IsThisEnabled() && extruders->values[0] == extruders->values[1] &&
                                     nozzle_volumes->values[0] == nozzle_volumes->values[1]);
        m_extruder_sync->Show(m_extruder_sync->IsThisEnabled());
        if (extruder_id == -1)
            extruder_id = current_extruder;
        else if (extruder_id != current_extruder)
            return;
    } else if (m_variant_combo) {
        int current_variant = m_variant_combo->GetSelection();
        if (extruder_id == -1)
            extruder_id = current_variant;
        else if (extruder_id != current_variant)
            // assert(false)
            return;
    }
    auto get_index_for_extruder =
            [this, &extruders, &nozzle_volumes, variant_keys = extruder_variant_keys[m_type >= Preset::TYPE_COUNT ? Preset::TYPE_PRINT : m_type]](int extruder_id, int stride = 1) {
        return m_config->get_index_for_extruder(extruder_id + 1, variant_keys.first,
            ExtruderType(extruders->values[extruder_id]), NozzleVolumeType(nozzle_volumes->values[extruder_id]), variant_keys.second, stride);
    };
    auto index = m_variant_combo ? extruder_id : get_index_for_extruder(extruder_id == -1 ? 0 : extruder_id);
    if (index < 0)
        return;
    if (m_extruder_switch) m_extruder_switch->SetClientData(reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
    if (m_variant_combo) m_variant_combo->SetClientData(reinterpret_cast<void *>(static_cast<std::uintptr_t>(index)));
    wxWindow *variant_ctrl = m_extruder_switch ? (wxWindow *) m_extruder_switch : m_variant_combo;
    for (auto page : m_pages) {
        bool is_extruder = false;
        if (m_type == Preset::TYPE_PRINTER) {
            if (page->title().StartsWith("Extruder")) {
                int extruder_id2 = std::atoi(page->title().Mid(9).ToUTF8()) - 1;
                if (extruder_id >= 0 && extruder_id2 != extruder_id)
                    continue;
                if (extruder_id2 > 0)
                    index = get_index_for_extruder(extruder_id2);
                is_extruder = true;
            } else if (page->title().StartsWith("Speed limitation")) {
                index = get_index_for_extruder(extruder_id == -1 ? 0 : extruder_id, 2);
            }
        }
        page->m_opt_id_map.clear();
        for (auto group : page->m_optgroups) {
            for (auto &opt : group->opt_map()) {
                auto iter = std::find(printer_extruder_options.begin(), printer_extruder_options.end(), opt.second.first);
                if (iter != printer_extruder_options.end()) {
                    page->m_opt_id_map.insert({opt.first, opt.first});
                    continue;
                }

                if (opt.second.second >= 0) {
                    const_cast<int &>(opt.second.second) = index;
                    page->m_opt_id_map.insert({opt.second.first + "#" + std::to_string(index), opt.first});
                    group->draw_multi_extruder = !is_extruder && variant_ctrl->IsThisEnabled() && m_type == Preset::TYPE_FILAMENT;
                }
            }
        }
    }
    if (reload) {
        reload_config();
        update_changed_ui();
        toggle_options();
        if (m_active_page)
            m_active_page->update_visibility(m_mode, true);
        m_page_view->GetParent()->Layout();
    }
}

void Tab::sync_excluder()
{
    Preset & printer_preset = m_preset_bundle->printers.get_edited_preset();
    auto nozzle_volumes = m_preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    auto extruders      = printer_preset.config.option<ConfigOptionEnumsGeneric>("extruder_type");
    auto get_index_for_extruder =
            [this, &extruders, &nozzle_volumes, variant_keys = extruder_variant_keys[m_type >= Preset::TYPE_COUNT ? Preset::TYPE_PRINT : m_type]](int extruder_id) {
        return m_config->get_index_for_extruder(extruder_id + 1, variant_keys.first,
            ExtruderType(extruders->values[extruder_id]), NozzleVolumeType(nozzle_volumes->values[extruder_id]), variant_keys.second);
    };
    int active_index = m_extruder_switch->GetValue() ? 1 : 0;
    int from_index = get_index_for_extruder(active_index);
    int dest_index = get_index_for_extruder(1 - active_index);
    auto from_str = std::to_string(from_index);
    auto dest_str = std::to_string(dest_index);
    auto dirty_options = m_presets->current_dirty_options(true);
    DynamicConfig config_origin, config_to_apply;
    for (int i = 0; i < dirty_options.size(); ++i) {
        auto &opt = dirty_options[i];
        auto n= opt.find('#');
        if (n == std::string::npos)
            continue;
        auto field = m_active_page->get_field(opt.substr(0, n), from_index + 256);
        auto line  = m_active_page->get_line(opt.substr(0, n), from_index + 256);
        if (field == nullptr || line == nullptr)
            continue;
        ++n;
        bool dirty  = opt.substr(n) == from_str;
        while (i + 1 < dirty_options.size() && dirty_options[i + 1].compare(0, n, opt, 0, n) == 0) {
            dirty |= dirty_options[i + 1].substr(n) == from_str;
            ++i;
        }
        if (dirty) {
            auto key = opt.substr(0, n - 1);
            auto option = dynamic_cast<ConfigOptionVectorBase*>(m_config->option(key));
            auto option2 = dynamic_cast<ConfigOptionVectorBase*>(option->clone());
            option2->set_at(option, dest_index, from_index);
            if (*option == *option2) {
                delete option2;
                continue;
            }
            config_origin.set_key_value(key, option->clone());
            config_to_apply.set_key_value(key, option2);
        }
    }
    if (config_to_apply.empty()) {
        MessageDialog md(wxGetApp().plater(), _L("No modifications need to be copied."), _L("Copy paramters"), wxICON_INFORMATION | wxOK);
        md.ShowModal();
        return;
    }

    wxString title  = active_index == 0 ? _L("Modify paramters of right nozzle") : _L("Modify paramters of left nozzle");
    wxString header = active_index == 0 ? _L("Do you want to modify the following parameters of the right nozzle to that of the left nozzle?") :
                                          _L("Do you want to modify the following parameters of the left nozzle to that of the right nozzle?");
    UnsavedChangesDialog dlg(title, header, &config_origin, from_index, dest_index, active_index == 0);
    dlg.ShowModal();
    if (dlg.transfer_changes()) {
        m_config->apply(config_to_apply);
        auto &applying_keys = const_cast<t_config_option_keys&>(m_config_manipulation.applying_keys());
        if (m_type > Preset::TYPE_COUNT)
            applying_keys = config_to_apply.keys();
        reload_config();
        if (m_type > Preset::TYPE_COUNT)
            applying_keys.clear();
        update_changed_ui();
        update();
        if (m_active_page)
            m_active_page->update_visibility(m_mode, true);
        m_page_view->GetParent()->Layout();
    }
}

void Tab::compatible_widget_reload(PresetDependencies &deps)
{
    Field* field = this->get_field(deps.key_condition);
    if (!field)
        return;

    bool has_any = ! m_config->option<ConfigOptionStrings>(deps.key_list)->values.empty();
    has_any ? deps.btn->Enable() : deps.btn->Disable();
    deps.checkbox->SetValue(! has_any);

    field->toggle(! has_any);
}

void Tab::set_tooltips_text()
{
    // --- Tooltip text for reset buttons (for whole options group)
    // Text to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
    //m_ttg_value_lock =		_(L("LOCKED LOCK icon indicates that the settings are the same as the system (or default) values "
    //                            "for the current option group"));
    //m_ttg_value_unlock =	_(L("UNLOCKED LOCK icon indicates that some settings were changed and are not equal "
    //                            "to the system (or default) values for the current option group.\n"
    //                            "Click to reset all settings for current option group to the system (or default) values."));
    //m_ttg_white_bullet_ns =	_(L("WHITE BULLET icon indicates a non system (or non default) preset."));
    //m_ttg_non_system =		&m_ttg_white_bullet_ns;
    // Text to be shown on the "Undo user changes" button next to each input field.
    //m_ttg_white_bullet =	_(L("WHITE BULLET icon indicates that the settings are the same as in the last saved "
    //                            "preset for the current option group."));
    //m_ttg_value_revert =	_(L("BACK ARROW icon indicates that the settings were changed and are not equal to "
    //                            "the last saved preset for the current option group.\n"
    //                            "Click to reset all settings for the current option group to the last saved preset."));

    // --- Tooltip text for reset buttons (for each option in group)
    // Text to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
    //m_tt_value_lock =		_(L("LOCKED LOCK icon indicates that the value is the same as the system (or default) value."));
    m_tt_value_unlock =		_(L("Click to reset current value and attach to the global value."));
    // 	m_tt_white_bullet_ns=	_(L("WHITE BULLET icon indicates a non system preset."));
    //m_tt_non_system =		&m_ttg_white_bullet_ns;
    // Text to be shown on the "Undo user changes" button next to each input field.
    //m_tt_white_bullet =		_(L("WHITE BULLET icon indicates that the value is the same as in the last saved preset."));
    m_tt_value_revert =		_(L("Click to drop current modify and reset to saved value."));
}

//BBS: GUI refactor
Page::Page(wxWindow* parent, const wxString& title, int iconID, wxPanel* tab_owner) :
        m_tab_owner(tab_owner),
        m_parent(parent),
        m_title(title),
        m_iconID(iconID)
{
    m_vsizer = (wxBoxSizer*)parent->GetSizer();
    m_page_title = NULL;
    m_item_color = &wxGetApp().get_label_clr_default();
}

void Page::reload_config()
{
    for (auto group : m_optgroups)
        group->reload_config();
}

void Page::update_visibility(ConfigOptionMode mode, bool update_contolls_visibility)
{
    bool ret_val = false;
#if HIDE_FIRST_SPLIT_LINE
    // BBS: no line spliter for first group
    bool first = true;
#endif
    for (auto group : m_optgroups) {
        ret_val = (update_contolls_visibility     ?
                   group->update_visibility(mode) :  // update visibility for all controlls in group
                   group->is_visible(mode)           // just detect visibility for the group
                   ) || ret_val;
#if HIDE_FIRST_SPLIT_LINE
        // BBS: no line spliter for first group
        if (update_contolls_visibility && ret_val && first) {
            if (group->stb) group->stb->Hide();
            first = false;
        }
#endif
    }

    m_show = ret_val;
#ifdef __WXMSW__
    if (!m_show) return;
    // BBS: fix field control position
    wxTheApp->CallAfter([this]() {
        for (auto group : m_optgroups) {
            if (group->custom_ctrl) group->custom_ctrl->fixup_items_positions();
        }
    });
#endif
}

void Page::activate(ConfigOptionMode mode, std::function<void()> throw_if_canceled)
{
#if 0 // BBS: page title
    if (m_page_title == NULL) {
        m_page_title = new Label(Label::Head_18, _(m_title), m_parent);
        m_vsizer->AddSpacer(30);
        m_vsizer->Add(m_page_title, 0, wxALIGN_CENTER);
        m_vsizer->AddSpacer(20);
    }
#else
    //m_vsizer->AddSpacer(10);
#endif
#if HIDE_FIRST_SPLIT_LINE
    // BBS: no line spliter for first group
    bool first = true;
#endif
    for (auto group : m_optgroups) {
        if (!group->activate(throw_if_canceled))
            continue;
        m_vsizer->Add(group->sizer, 0, wxEXPAND | (group->is_legend_line() ? (wxLEFT|wxTOP) : wxALL), 10);
        group->update_visibility(mode);
#if HIDE_FIRST_SPLIT_LINE
        if (first) group->stb->Hide();
        first = false;
#endif
        group->reload_config();
        throw_if_canceled();
    }

#ifdef __WXMSW__
    // BBS: fix field control position
    wxTheApp->CallAfter([this]() {
        for (auto group : m_optgroups) {
            if (group->custom_ctrl)
                group->custom_ctrl->fixup_items_positions();
        }
    });
#endif
}

void Page::clear()
{
    for (auto group : m_optgroups)
        group->clear();
    m_page_title = NULL;
}

void Page::msw_rescale()
{
    for (auto group : m_optgroups)
        group->msw_rescale();
}

void Page::sys_color_changed()
{
    for (auto group : m_optgroups)
        group->sys_color_changed();
}

void Page::refresh()
{
    for (auto group : m_optgroups)
        group->refresh();
}

Field *Page::get_field(const t_config_option_key &opt_key, int opt_index /*= -1*/) const
{
    Field *field = nullptr;
    auto   opt_key2 = opt_key;
    if (opt_index >= 256) {
        auto iter = m_opt_id_map.find(opt_key + '#' + std::to_string(opt_index - 256));
        if (iter != m_opt_id_map.end())
            opt_key2 = iter->second;
    }
    for (auto opt : m_optgroups) {
        field = opt->get_fieldc(opt_key2, opt_index);
        if (field != nullptr) return field;
    }
    return field;
}

Line *Page::get_line(const t_config_option_key &opt_key, int opt_index)
{
    Line *line = nullptr;
    auto   opt_key2 = opt_key;
    if (opt_index >= 256) {
        auto iter = m_opt_id_map.find(opt_key + '#' + std::to_string(opt_index - 256));
        if (iter != m_opt_id_map.end())
            opt_key2 = iter->second;
    } else if (opt_index >= 0) {
        assert(opt_key.find('#') == std::string::npos);
        opt_key2 = opt_key + '#' + std::to_string(opt_index);
    }
    for (auto opt : m_optgroups) {
        line = opt->get_line(opt_key2);
        if (line != nullptr) return line;
    }
    return line;
}

bool Page::set_value(const t_config_option_key &opt_key, const boost::any &value)
{
    bool changed = false;
    for(auto optgroup: m_optgroups) {
        if (optgroup->set_value(opt_key, value))
            changed = true ;
    }
    return changed;
}

// package Slic3r::GUI::Tab::Page;
ConfigOptionsGroupShp Page::new_optgroup(const wxString &title, const wxString &icon, int noncommon_label_width /*= -1*/, bool is_extruder_og /* false */)
{
    //! config_ have to be "right"
    ConfigOptionsGroupShp optgroup = is_extruder_og ? std::make_shared<ExtruderOptionsGroup>(m_parent, title, m_config, true)
        : std::make_shared<ConfigOptionsGroup>(m_parent, title, icon, m_config, true);
    optgroup->split_multi_line     = this->m_split_multi_line;
    optgroup->option_label_at_right = this->m_option_label_at_right;
    if (noncommon_label_width >= 0)
        optgroup->label_width = noncommon_label_width;

//BBS: GUI refactor
/*#ifdef __WXOSX__
    auto tab = parent()->GetParent()->GetParent();// GetParent()->GetParent();
#else
    auto tab = parent()->GetParent();// GetParent();
#endif*/
    auto tab = m_tab_owner;
    optgroup->set_config_category_and_type(m_title, static_cast<Tab*>(tab)->type());
    optgroup->m_on_change = [tab](t_config_option_key opt_key, boost::any value) {
        //! This function will be called from OptionGroup.
        //! Using of CallAfter is redundant.
        //! And in some cases it causes update() function to be recalled again
//!        wxTheApp->CallAfter([this, opt_key, value]() {
            static_cast<Tab*>(tab)->update_dirty();
            static_cast<Tab*>(tab)->on_value_change(opt_key, value);
//!        });
    };

    optgroup->m_get_initial_config = [tab]() {
        DynamicPrintConfig config = static_cast<Tab*>(tab)->m_presets->get_selected_preset().config;
        return config;
    };

    optgroup->m_get_sys_config = [tab]() {
        DynamicPrintConfig config = static_cast<Tab*>(tab)->m_presets->get_selected_preset_parent()->config;
        return config;
    };

    optgroup->have_sys_config = [tab]() {
        return static_cast<Tab*>(tab)->m_presets->get_selected_preset_parent() != nullptr;
    };

    optgroup->rescale_extra_column_item = [](wxWindow* win) {
        auto *ctrl = dynamic_cast<wxStaticBitmap*>(win);
        if (ctrl == nullptr)
            return;

        ctrl->SetBitmap(reinterpret_cast<ScalableBitmap*>(ctrl->GetClientData())->bmp());
    };

    m_optgroups.push_back(optgroup);

    return optgroup;
}

const ConfigOptionsGroupShp Page::get_optgroup(const wxString& title) const
{
    for (ConfigOptionsGroupShp optgroup : m_optgroups) {
        if (optgroup->title == title)
            return optgroup;
    }

    return nullptr;
}

void TabSLAMaterial::build()
{
    //m_presets = &m_preset_bundle->sla_materials;
    //load_initial_data();

    //auto page = add_options_page(L("Material"), "");

    //auto optgroup = page->new_optgroup(L("Material"));
    //optgroup->append_single_option_line("material_colour");
    //optgroup->append_single_option_line("bottle_cost");
    //optgroup->append_single_option_line("bottle_volume");
    //optgroup->append_single_option_line("bottle_weight");
    //optgroup->append_single_option_line("material_density");

    //optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value)
    //{
    //    if (opt_key == "material_colour") {
    //        update_dirty();
    //        on_value_change(opt_key, value);
    //        return;
    //    }

    //    DynamicPrintConfig new_conf = *m_config;

    //    if (opt_key == "bottle_volume") {
    //        double new_bottle_weight =  boost::any_cast<double>(value)*(new_conf.option("material_density")->getFloat() / 1000);
    //        new_conf.set_key_value("bottle_weight", new ConfigOptionFloat(new_bottle_weight));
    //    }
    //    if (opt_key == "bottle_weight") {
    //        double new_bottle_volume =  boost::any_cast<double>(value)/new_conf.option("material_density")->getFloat() * 1000;
    //        new_conf.set_key_value("bottle_volume", new ConfigOptionFloat(new_bottle_volume));
    //    }
    //    if (opt_key == "material_density") {
    //        double new_bottle_volume = new_conf.option("bottle_weight")->getFloat() / boost::any_cast<double>(value) * 1000;
    //        new_conf.set_key_value("bottle_volume", new ConfigOptionFloat(new_bottle_volume));
    //    }

    //    load_config(new_conf);

    //    update_dirty();

    //    // BBS
    //    // Change of any from those options influences for an update of "Sliced Info"
    //    //wxGetApp().sidebar().Layout();
    //};

    //optgroup = page->new_optgroup(L("Layers"));
    //optgroup->append_single_option_line("initial_layer_height");

    //optgroup = page->new_optgroup(L("Exposure"));
    //optgroup->append_single_option_line("exposure_time");
    //optgroup->append_single_option_line("initial_exposure_time");

    //optgroup = page->new_optgroup(L("Corrections"));
    //auto line = Line{ m_config->def()->get("material_correction")->full_label, "" };
    //for (auto& axis : { "X", "Y", "Z" }) {
    //    auto opt = optgroup->get_option(std::string("material_correction_") + char(std::tolower(axis[0])));
    //    opt.opt.label = axis;
    //    line.append_option(opt);
    //}

    //optgroup->append_line(line);

    //page = add_options_page(L("Dependencies"), "wrench.png");
    //optgroup = page->new_optgroup(L("Profile dependencies"));

    //create_line_with_widget(optgroup.get(), "compatible_printers", "", [this](wxWindow* parent) {
    //    return compatible_widget_create(parent, m_compatible_printers);
    //});
    //
    //Option option = optgroup->get_option("compatible_printers_condition");
    //option.opt.full_width = true;
    //optgroup->append_single_option_line(option);

    //create_line_with_widget(optgroup.get(), "compatible_prints", "", [this](wxWindow* parent) {
    //    return compatible_widget_create(parent, m_compatible_prints);
    //});

    //option = optgroup->get_option("compatible_prints_condition");
    //option.opt.full_width = true;
    //optgroup->append_single_option_line(option);

    //build_preset_description_line(optgroup.get());

    //page = add_options_page(L("Material printing profile"), "printer.png");
    //optgroup = page->new_optgroup(L("Material printing profile"));
    //option = optgroup->get_option("material_print_speed");
    //optgroup->append_single_option_line(option);
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabSLAMaterial::reload_config()
{
    this->compatible_widget_reload(m_compatible_printers);
    this->compatible_widget_reload(m_compatible_prints);
    Tab::reload_config();
}

void TabSLAMaterial::toggle_options()
{
    const Preset &current_printer = m_preset_bundle->printers.get_edited_preset();
    std::string model = current_printer.config.opt_string("printer_model");
    m_config_manipulation.toggle_field("material_print_speed", model != "SL1");
}

void TabSLAMaterial::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF)
        return;

    update_description_lines();
    Layout();

// #ys_FIXME. Just a template for this function
//     m_update_cnt++;
//     ! something to update
//     m_update_cnt--;
//
//     if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabSLAPrint::build()
{
    m_presets = &m_preset_bundle->sla_prints;
    load_initial_data();

//    auto page = add_options_page(L("Layers and perimeters"), "layers");
//
//    auto optgroup = page->new_optgroup(L("Layers"));
//    optgroup->append_single_option_line("layer_height");
//    optgroup->append_single_option_line("faded_layers");
//
//    page = add_options_page(L("Supports"), "support"/*"sla_supports"*/);
//    optgroup = page->new_optgroup(L("Supports"));
//    optgroup->append_single_option_line("supports_enable");
//
//    optgroup = page->new_optgroup(L("Support head"));
//    optgroup->append_single_option_line("support_head_front_diameter");
//    optgroup->append_single_option_line("support_head_penetration");
//    optgroup->append_single_option_line("support_head_width");
//
//    optgroup = page->new_optgroup(L("Support pillar"));
//    optgroup->append_single_option_line("support_pillar_diameter");
//    optgroup->append_single_option_line("support_small_pillar_diameter_percent");
//    optgroup->append_single_option_line("support_max_bridges_on_pillar");
//
//    optgroup->append_single_option_line("support_pillar_connection_mode");
//    optgroup->append_single_option_line("support_buildplate_only");
//    // TODO: This parameter is not used at the moment.
//    // optgroup->append_single_option_line("support_pillar_widening_factor");
//    optgroup->append_single_option_line("support_base_diameter");
//    optgroup->append_single_option_line("support_base_height");
//    optgroup->append_single_option_line("support_base_safety_distance");
//
//    // Mirrored parameter from Pad page for toggling elevation on the same page
//    optgroup->append_single_option_line("support_object_elevation");
//
//    Line line{ "", "" };
//    line.full_width = 1;
//    line.widget = [this](wxWindow* parent) {
//        return description_line_widget(parent, &m_support_object_elevation_description_line);
//    };
//    optgroup->append_line(line);
//
//    optgroup = page->new_optgroup(L("Connection of the support sticks and junctions"));
//    optgroup->append_single_option_line("support_critical_angle");
//    optgroup->append_single_option_line("support_max_bridge_length");
//    optgroup->append_single_option_line("support_max_pillar_link_distance");
//
//    optgroup = page->new_optgroup(L("Automatic generation"));
//    optgroup->append_single_option_line("support_points_density_relative");
//    optgroup->append_single_option_line("support_points_minimal_distance");
//
//    page = add_options_page(L("Pad"), "");
//    optgroup = page->new_optgroup(L("Pad"));
//    optgroup->append_single_option_line("pad_enable");
//    optgroup->append_single_option_line("pad_wall_thickness");
//    optgroup->append_single_option_line("pad_wall_height");
//    optgroup->append_single_option_line("pad_brim_size");
//    optgroup->append_single_option_line("pad_max_merge_distance");
//    // TODO: Disabling this parameter for the beta release
////    optgroup->append_single_option_line("pad_edge_radius");
//    optgroup->append_single_option_line("pad_wall_slope");
//
//    optgroup->append_single_option_line("pad_around_object");
//    optgroup->append_single_option_line("pad_around_object_everywhere");
//    optgroup->append_single_option_line("pad_object_gap");
//    optgroup->append_single_option_line("pad_object_connector_stride");
//    optgroup->append_single_option_line("pad_object_connector_width");
//    optgroup->append_single_option_line("pad_object_connector_penetration");
//
//    page = add_options_page(L("Hollowing"), "hollowing");
//    optgroup = page->new_optgroup(L("Hollowing"));
//    optgroup->append_single_option_line("hollowing_enable");
//    optgroup->append_single_option_line("hollowing_min_thickness");
//    optgroup->append_single_option_line("hollowing_quality");
//    optgroup->append_single_option_line("hollowing_closing_distance");
//
//    page = add_options_page(L("Advanced"), "advanced");
//    optgroup = page->new_optgroup(L("Slicing"));
//    optgroup->append_single_option_line("slice_closing_radius");
//    optgroup->append_single_option_line("slicing_mode");
//
//    page = add_options_page(L("Output options"), "output+page_white");
//    optgroup = page->new_optgroup(L("Output file"));
//    Option option = optgroup->get_option("filename_format");
//    option.opt.full_width = true;
//    optgroup->append_single_option_line(option);
//
//    page = add_options_page(L("Dependencies"), "advanced");
//    optgroup = page->new_optgroup(L("Profile dependencies"));
//
//    create_line_with_widget(optgroup.get(), "compatible_printers", "", [this](wxWindow* parent) {
//        return compatible_widget_create(parent, m_compatible_printers);
//    });
//
//    option = optgroup->get_option("compatible_printers_condition");
//    option.opt.full_width = true;
//    optgroup->append_single_option_line(option);
//
//    build_preset_description_line(optgroup.get());
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabSLAPrint::reload_config()
{
    this->compatible_widget_reload(m_compatible_printers);
    Tab::reload_config();
}

void TabSLAPrint::update_description_lines()
{
    Tab::update_description_lines();

    //if (m_active_page && m_active_page->title() == "Supports")
    //{
    //    bool is_visible = m_config->def()->get("support_object_elevation")->mode <= m_mode;
    //    if (m_support_object_elevation_description_line)
    //    {
    //        m_support_object_elevation_description_line->Show(is_visible);
    //        if (is_visible)
    //        {
    //            bool elev = !m_config->opt_bool("pad_enable") || !m_config->opt_bool("pad_around_object");
    //            m_support_object_elevation_description_line->SetText(elev ? "" :
    //                from_u8((boost::format(_u8L("\"%1%\" is disabled because \"%2%\" is on in \"%3%\" category.\n"
    //                    "To enable \"%1%\", please switch off \"%2%\""))
    //                    % _L("Object elevation") % _L("Pad around object") % _L("Pad")).str()));
    //        }
    //    }
    //}
}

void TabSLAPrint::toggle_options()
{
    if (m_active_page)
        m_config_manipulation.toggle_print_sla_options(m_config);
}

void TabSLAPrint::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF)
        return;

    m_update_cnt++;

    m_config_manipulation.update_print_sla_config(m_config, true);

    update_description_lines();
    //BBS: GUI refactor
    //Layout();
    m_parent->Layout();

    m_update_cnt--;

    if (m_update_cnt == 0) {
        toggle_options();

        // update() could be called during undo/redo execution
        // Update of objectList can cause a crash in this case (because m_objects doesn't match ObjectList)
        if (!wxGetApp().plater()->inside_snapshot_capture())
            wxGetApp().obj_list()->update_and_show_object_settings_item();

        wxGetApp().mainframe->on_config_changed(m_config);
    }
}

void TabSLAPrint::clear_pages()
{
    Tab::clear_pages();

    m_support_object_elevation_description_line = nullptr;
}

ConfigManipulation Tab::get_config_manipulation()
{
    auto load_config = [this]()
    {
        update_dirty();
        // Initialize UI components with the config values.
        reload_config();
        update();
    };

    auto cb_toggle_field = [this](const t_config_option_key& opt_key, bool toggle, int opt_index) {
        return toggle_option(opt_key, toggle, opt_index >= 0 ? opt_index + 256 : opt_index);
    };

    auto cb_toggle_line = [this](const t_config_option_key &opt_key, bool toggle, int opt_index) {
        return toggle_line(opt_key, toggle, opt_index >= 0 ? opt_index + 256 : opt_index);
    };

    auto cb_value_change = [this](const std::string& opt_key, const boost::any& value) {
        return on_value_change(opt_key, value);
    };

    return ConfigManipulation(load_config, cb_toggle_field, cb_toggle_line, cb_value_change, nullptr, this);
}


} // GUI
} // Slic3r
