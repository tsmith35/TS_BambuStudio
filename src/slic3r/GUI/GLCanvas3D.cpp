#include "libslic3r/libslic3r.h"
#include "GLCanvas3D.hpp"

#include <igl/unproject.h>

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Technologies.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/GLShader.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/GUI_Preview.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp"
#include "slic3r/GUI/BitmapCache.hpp"
#include "slic3r/Utils/MacDarkMode.hpp"

#include "GLToolbar.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Colors.hpp"
#include "Mouse3DController.hpp"
#include "I18N.hpp"
#include "NotificationManager.hpp"
#include "format.hpp"
#include "DailyTips.hpp"
#include "FilamentMapDialog.hpp"
#include "../Utils/CpuMemory.hpp"
#if ENABLE_RETINA_GL
#include "slic3r/Utils/RetinaHelper.hpp"
#endif
#ifdef __APPLE__
#include "libslic3r/MacUtils.hpp"
#endif
#include <GL/glew.h>

#include <wx/glcanvas.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/settings.h>
#include <wx/tooltip.h>
#include <wx/debug.h>
#include <wx/fontutil.h>
// Print now includes tbb, and tbb includes Windows. This breaks compilation of wxWidgets if included before wx.
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "wxExtensions.hpp"

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <iostream>
#include <float.h>
#include <algorithm>
#include <cmath>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include <imguizmo/ImGuizmo.h>
static constexpr const float TRACKBALLSIZE = 0.8f;

static const float SLIDER_DEFAULT_RIGHT_MARGIN  = 10.0f;
static const float SLIDER_DEFAULT_BOTTOM_MARGIN = 10.0f;
static const float SLIDER_RIGHT_MARGIN          = 124.0f;
static const float SLIDER_BOTTOM_MARGIN         = 64.0f;

float GLCanvas3D::DEFAULT_BG_LIGHT_COLOR[3] = { 0.906f, 0.906f, 0.906f };
float GLCanvas3D::DEFAULT_BG_LIGHT_COLOR_DARK[3] = { 0.329f, 0.329f, 0.353f };
float GLCanvas3D::ERROR_BG_LIGHT_COLOR[3] = { 0.753f, 0.192f, 0.039f };
float GLCanvas3D::ERROR_BG_LIGHT_COLOR_DARK[3] = { 0.753f, 0.192f, 0.039f };

void GLCanvas3D::update_render_colors()
{
    GLCanvas3D::DEFAULT_BG_LIGHT_COLOR[0] = RenderColor::colors[RenderCol_3D_Background].x;
    GLCanvas3D::DEFAULT_BG_LIGHT_COLOR[1] = RenderColor::colors[RenderCol_3D_Background].y;
    GLCanvas3D::DEFAULT_BG_LIGHT_COLOR[2] = RenderColor::colors[RenderCol_3D_Background].z;
}

void GLCanvas3D::load_render_colors()
{
    RenderColor::colors[RenderCol_3D_Background] = ImVec4(GLCanvas3D::DEFAULT_BG_LIGHT_COLOR[0],
                                                          GLCanvas3D::DEFAULT_BG_LIGHT_COLOR[1],
                                                          GLCanvas3D::DEFAULT_BG_LIGHT_COLOR[2],
                                                          1.0f);
}

//static constexpr const float AXES_COLOR[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };

// Number of floats
static constexpr const size_t MAX_VERTEX_BUFFER_SIZE     = 131072 * 6; // 3.15MB
// Reserve size in number of floats.
static constexpr const size_t VERTEX_BUFFER_RESERVE_SIZE = 131072 * 2; // 1.05MB
// Reserve size in number of floats, maximum sum of all preallocated buffers.
//static constexpr const size_t VERTEX_BUFFER_RESERVE_SIZE_SUM_MAX = 1024 * 1024 * 128 / 4; // 128MB

namespace Slic3r {
namespace GUI {

#ifdef __WXGTK3__
// wxGTK3 seems to simulate OSX behavior in regard to HiDPI scaling support.
RetinaHelper::RetinaHelper(wxWindow* window) : m_window(window), m_self(nullptr) {}
RetinaHelper::~RetinaHelper() {}
float RetinaHelper::get_scale_factor() { return float(m_window->GetContentScaleFactor()); }
#endif // __WXGTK3__

// Fixed the collision between BuildVolume::Type::Convex and macro Convex defined inside /usr/include/X11/X.h that is included by WxWidgets 3.0.
#if defined(__linux__) && defined(Convex)
#undef Convex
#endif


std::string get_object_limited_text() {
    std::string object_limited_text = _u8L("An object is placed in the left/right nozzle-only area or exceeds the printable height of the left nozzle.\n"
            "Please ensure the filaments used by this object are not arranged to other nozzles.");
    return object_limited_text;
}

// serve as a text container, not the real text
std::string& get_object_clashed_text() {
    static std::string object_clashed_text = _u8L("An object is laid over the boundary of plate or exceeds the height limit.\n"
            "Please solve the problem by moving it totally on or off the plate, and confirming that the height is within the build volume.");
    return object_clashed_text;
}

std::string& get_assembly_too_far_text() {
    static std::string assembly_warning_too_far{};
    return assembly_warning_too_far;
}

std::string& get_left_extruder_unprintable_text() {
    static std::string left_unprintable_text;
    return left_unprintable_text;
}

std::string& get_right_extruder_unprintable_text() {
    static std::string right_unprintable_text;
    return right_unprintable_text;
}

std::string& get_nozzle_filament_incompatible_text() {
    static std::string nozzle_filament_incompatible_text;
    return nozzle_filament_incompatible_text;
}

static std::string format_number(float value)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision((static_cast<int>(value * 10) % 10 == 0) ? 0 : 1) << value;
    return oss.str();
}

wxString filament_printable_error_msg;

GLModel GLCanvas3D::s_full_screen_mesh;

GLCanvas3D::LayersEditing::~LayersEditing()
{
    if (m_z_texture_id != 0) {
        glsafe(::glDeleteTextures(1, &m_z_texture_id));
        m_z_texture_id = 0;
    }
    delete m_slicing_parameters;

    m_background.reset();
    m_baseline.reset();
    m_profile_curve.reset();
}

const float GLCanvas3D::LayersEditing::THICKNESS_BAR_WIDTH = 70.0f;

void GLCanvas3D::LayersEditing::init()
{
    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    const auto& gl_info = p_ogl_manager->get_gl_info();
    const auto formated_gl_version = gl_info.get_formated_gl_version();
    glsafe(::glGenTextures(1, (GLuint*)&m_z_texture_id));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_z_texture_id));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, formated_gl_version >= 30 ? GL_CLAMP_TO_EDGE : GL_CLAMP));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, formated_gl_version >= 30 ? GL_CLAMP_TO_EDGE : GL_CLAMP));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
}

void GLCanvas3D::LayersEditing::set_config(const DynamicPrintConfig* config)
{
    m_config = config;
    delete m_slicing_parameters;
    m_slicing_parameters = nullptr;
    m_layers_texture.valid = false;
    m_layer_height_profile.clear();
}

void GLCanvas3D::LayersEditing::select_object(const Model& model, int object_id)
{
    const ModelObject* model_object_new = (object_id >= 0) ? model.objects[object_id] : nullptr;
    // Maximum height of an object changes when the object gets rotated or scaled.
    // Changing maximum height of an object will invalidate the layer heigth editing profile.
    // m_model_object->bounding_box() is cached, therefore it is cheap even if this method is called frequently.
    const float new_max_z = (model_object_new == nullptr) ? 0.0f : static_cast<float>(model_object_new->bounding_box().max.z());
    if (m_model_object != model_object_new || this->last_object_id != object_id || m_object_max_z != new_max_z ||
        (model_object_new != nullptr && m_model_object->id() != model_object_new->id())) {
        m_layer_height_profile.clear();
        delete m_slicing_parameters;
        m_slicing_parameters = nullptr;
        m_layers_texture.valid = false;
        this->last_object_id = object_id;
        m_model_object = model_object_new;
        m_object_max_z = new_max_z;
        m_profile_dirty = true;
    }
}

bool GLCanvas3D::LayersEditing::is_allowed() const
{
    return wxGetApp().get_shader("variable_layer_height") != nullptr && m_z_texture_id > 0;
}

bool GLCanvas3D::LayersEditing::is_enabled() const
{
    return m_enabled;
}

void GLCanvas3D::LayersEditing::set_enabled(bool enabled)
{
    m_enabled = is_allowed() && enabled;
}

float GLCanvas3D::LayersEditing::s_overlay_window_width;
void GLCanvas3D::LayersEditing::show_tooltip_information(const GLCanvas3D& canvas, std::map<wxString, wxString> captions_texts, float x, float y)
{
    ImTextureID normal_id = canvas.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id = canvas.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    float caption_max = 0.f;
    for (auto caption_text : captions_texts) {
        caption_max = std::max(imgui.calc_text_size(caption_text.first).x, caption_max);
    }
    caption_max += GImGui->Style.WindowPadding.x + imgui.scaled(1);

    float font_size = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size * 1.8, font_size * 1.3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0.0f, GImGui->Style.FramePadding.y});
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &caption_max, &imgui](const wxString& caption, const wxString& text) {
            imgui.text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            imgui.text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };

        for (const auto& caption_text : captions_texts) draw_text_with_caption(caption_text.first, caption_text.second);

        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
}

void GLCanvas3D::LayersEditing::render_variable_layer_height_dialog(const GLCanvas3D& canvas) {
    if (!m_enabled)
        return;

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    const Size& cnv_size = canvas.get_canvas_size();
    float         zoom   = (float) canvas.get_active_camera().get_zoom();
    const auto& p_main_toolbar = canvas.get_main_toolbar();
    float left_pos = 0.0f;
    float main_toolbar_height = 0.0f;
    if (p_main_toolbar) {
        left_pos = p_main_toolbar->get_item("layersediting")->render_rect[0];
        main_toolbar_height = p_main_toolbar->get_height();
    }
    float x = 0.5 * cnv_size.get_width() + left_pos * zoom;

    const auto canvas_width = cnv_size.get_width();
    if (abs(GLCanvas3D::LayersEditing::s_overlay_window_width) > 0.01f) {
        if (x + GLCanvas3D::LayersEditing::s_overlay_window_width > canvas_width) {
            if (GLCanvas3D::LayersEditing::s_overlay_window_width > canvas_width)
                x = 0;
            else
                x = canvas_width - GLCanvas3D::LayersEditing::s_overlay_window_width;
        }
    }
    x = std::max(x, 0.0f);

    imgui.set_next_window_pos(x, main_toolbar_height, ImGuiCond_Always, 0.0f, 0.0f);

    imgui.push_toolbar_style(canvas.get_scale());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * canvas.get_scale(), 4.0f * canvas.get_scale()));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * canvas.get_scale(), 10.0f * canvas.get_scale()));
    imgui.begin(_L("Variable layer height"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    const float sliders_width = imgui.scaled(7.0f);
    const float input_box_width = 1.5 * imgui.get_slider_icon_size().x;

    if (imgui.button(_L("Adaptive")))
        wxPostEvent((wxEvtHandler*)canvas.get_wxglcanvas(), Event<float>(EVT_GLCANVAS_ADAPTIVE_LAYER_HEIGHT_PROFILE, m_adaptive_quality));
    ImGui::SameLine();
    static float text_align = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    text_align = std::max(text_align, ImGui::GetCursorPosX());
    ImGui::SetCursorPosX(text_align);
    imgui.text(_L("Quality / Speed"));
    if (ImGui::IsItemHovered()) {
        //ImGui::BeginTooltip();
        //ImGui::TextUnformatted(_L("Higher print quality versus higher print speed.").ToUTF8());
        //ImGui::EndTooltip();
    }
    ImGui::SameLine();
    static float slider_align = ImGui::GetCursorPosX();
    ImGui::PushItemWidth(sliders_width);
    m_adaptive_quality = std::clamp(m_adaptive_quality, 0.0f, 1.f);
    slider_align = std::max(slider_align, ImGui::GetCursorPosX());
    ImGui::SetCursorPosX(slider_align);
    imgui.bbl_slider_float_style("##adaptive_slider", &m_adaptive_quality, 0.0f, 1.f, "%.2f");
    ImGui::SameLine();
    static float input_align = ImGui::GetCursorPosX();
    ImGui::PushItemWidth(input_box_width);
    input_align = std::max(input_align, ImGui::GetCursorPosX());
    ImGui::SetCursorPosX(input_align);
    ImGui::BBLDragFloat("##adaptive_input", &m_adaptive_quality, 0.05f, 0.0f, 0.0f, "%.2f");

    if (imgui.button(_L("Smooth")))
        wxPostEvent((wxEvtHandler*)canvas.get_wxglcanvas(), HeightProfileSmoothEvent(EVT_GLCANVAS_SMOOTH_LAYER_HEIGHT_PROFILE, m_smooth_params));
    ImGui::SameLine();
    text_align = std::max(text_align, ImGui::GetCursorPosX());
    ImGui::SetCursorPosX(text_align);
    ImGui::AlignTextToFramePadding();
    imgui.text(_L("Radius"));
    ImGui::SameLine();
    slider_align = std::max(slider_align, ImGui::GetCursorPosX());
    ImGui::SetCursorPosX(slider_align);
    ImGui::PushItemWidth(sliders_width);
    int radius = (int)m_smooth_params.radius;
    int v_min = 1, v_max = 10;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(238 / 255.0f, 238 / 255.0f, 238 / 255.0f, 0.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(238 / 255.0f, 238 / 255.0f, 238 / 255.0f, 0.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.81f, 0.81f, 0.81f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
    if(ImGui::BBLSliderScalar("##radius_slider", ImGuiDataType_S32, &radius, &v_min, &v_max)){
        radius = std::clamp(radius, 1, 10);
        m_smooth_params.radius = (unsigned int)radius;
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::SameLine();
    input_align = std::max(input_align, ImGui::GetCursorPosX());
    ImGui::SetCursorPosX(input_align);
    ImGui::PushItemWidth(input_box_width);
    ImGui::PushStyleColor(ImGuiCol_BorderActive, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.00f, 0.68f, 0.26f, 0.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.00f, 0.68f, 0.26f, 0.00f));
    if (ImGui::BBLDragScalar("##radius_input", ImGuiDataType_S32, &radius, 1, &v_min, &v_max)) {
        radius = std::clamp(radius, 1, 10);
        m_smooth_params.radius = (unsigned int)radius;
    }
    ImGui::PopStyleColor(3);

    imgui.bbl_checkbox("##keep_min", m_smooth_params.keep_min);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    imgui.text(_L("Keep min"));

    ImGui::Separator();

    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight();
    if (p_main_toolbar) {
        get_cur_y += p_main_toolbar->get_height();
    }
    std::map<wxString, wxString> captions_texts = {
        {_L("Left mouse button:") ,_L("Add detail")},
        {_L("Right mouse button:"), _L("Remove detail")},
        {_L("Shift + Left mouse button:"),_L("Reset to base")},
        {_L("Shift + Right mouse button:"), _L("Smoothing")},
        {_L("Mouse wheel:"), _L("Increase/decrease edit area")}
    };
    show_tooltip_information(canvas, captions_texts, x, get_cur_y);
    ImGui::SameLine();
    if (imgui.button(_L("Reset")))
        wxPostEvent((wxEvtHandler*)canvas.get_wxglcanvas(), SimpleEvent(EVT_GLCANVAS_RESET_LAYER_HEIGHT_PROFILE));

    GLCanvas3D::LayersEditing::s_overlay_window_width = ImGui::GetWindowSize().x;
    imgui.end();
    ImGui::PopStyleVar(2);
    imgui.pop_toolbar_style();
}

void GLCanvas3D::LayersEditing::render_overlay(const GLCanvas3D& canvas)
{
    render_variable_layer_height_dialog(canvas);
    render_background_texture(canvas);
    const Rect& bar_rect = get_bar_rect_viewport(canvas);
    render_curve(bar_rect);
}

float GLCanvas3D::LayersEditing::get_cursor_z_relative(const GLCanvas3D& canvas)
{
    const Vec2d mouse_pos = canvas.get_local_mouse_position();
    const Rect& rect = get_bar_rect_screen(canvas);
    float x = (float)mouse_pos.x();
    float y = (float)mouse_pos.y();
    float t = rect.get_top();
    float b = rect.get_bottom();

    return (rect.get_left() <= x && x <= rect.get_right() && t <= y && y <= b) ?
        // Inside the bar.
        (b - y - 1.0f) / (b - t - 1.0f) :
        // Outside the bar.
        -1000.0f;
}

bool GLCanvas3D::LayersEditing::bar_rect_contains(const GLCanvas3D& canvas, float x, float y)
{
    const Rect& rect = get_bar_rect_screen(canvas);
    return rect.get_left() <= x && x <= rect.get_right() && rect.get_top() <= y && y <= rect.get_bottom();
}

Rect GLCanvas3D::LayersEditing::get_bar_rect_screen(const GLCanvas3D& canvas)
{
    const Size& cnv_size = canvas.get_canvas_size();
    float w = (float)cnv_size.get_width();
    float h = (float)cnv_size.get_height();

    return { w - thickness_bar_width(canvas), 0.0f, w, h };
}

Rect GLCanvas3D::LayersEditing::get_bar_rect_viewport(const GLCanvas3D& canvas)
{
    const Size& cnv_size = canvas.get_canvas_size();
    float half_w = 0.5f * (float)cnv_size.get_width();
    float half_h = 0.5f * (float)cnv_size.get_height();
    float inv_zoom = (float) canvas.get_active_camera().get_inv_zoom();
    return { (half_w - thickness_bar_width(canvas)) * inv_zoom, half_h * inv_zoom, half_w * inv_zoom, -half_h * inv_zoom };
}

bool GLCanvas3D::LayersEditing::is_initialized() const
{
    return wxGetApp().get_shader("variable_layer_height") != nullptr;
}

std::string GLCanvas3D::LayersEditing::get_tooltip(const GLCanvas3D& canvas) const
{
    std::string ret;
    if (m_enabled && m_layer_height_profile.size() >= 4) {
        float z = get_cursor_z_relative(canvas);
        if (z != -1000.0f) {
            z *= m_object_max_z;

            float h = 0.0f;
            for (size_t i = m_layer_height_profile.size() - 2; i >= 2; i -= 2) {
                const float zi = static_cast<float>(m_layer_height_profile[i]);
                const float zi_1 = static_cast<float>(m_layer_height_profile[i - 2]);
                if (zi_1 <= z && z <= zi) {
                    float dz = zi - zi_1;
                    h = (dz != 0.0f) ? static_cast<float>(lerp(m_layer_height_profile[i - 1], m_layer_height_profile[i + 1], (z - zi_1) / dz)) :
                        static_cast<float>(m_layer_height_profile[i + 1]);
                    break;
                }
            }
            if (h > 0.0f)
                ret = std::to_string(h);
        }
    }
    return ret;
}

void GLCanvas3D::LayersEditing::render_background_texture(const GLCanvas3D& canvas)
{
    if (!m_enabled)
        return;

    const Size cnv_size = canvas.get_canvas_size();
    const int cnv_width = cnv_size.get_width();
    const int cnv_height = cnv_size.get_height();
    if (cnv_width == 0 || cnv_height == 0)
        return;

    const float cnv_inv_width = 1.0f / static_cast<float>(cnv_width);

    const auto& shader = wxGetApp().get_shader("variable_layer_height");
    if (shader == nullptr)
        return;

    wxGetApp().bind_shader(shader);

    shader->set_uniform("z_to_texture_row", float(m_layers_texture.cells - 1) / (float(m_layers_texture.width) * m_object_max_z));
    shader->set_uniform("z_texture_row_to_normalized", 1.0f / (float)m_layers_texture.height);
    shader->set_uniform("z_cursor", m_object_max_z * this->get_cursor_z_relative(canvas));
    shader->set_uniform("z_cursor_band_width", band_width);
    shader->set_uniform("object_max_z", m_object_max_z);

    const auto thickness = thickness_bar_width(canvas) * cnv_inv_width;
    if (abs(thickness - m_cached_background_thickness) > 1e-6f)
    {
        const float l = 1.0f - 2.0f * thickness;
        const float r = 1.0f;
        const float t = 1.0f;
        const float b = -1.0f;

        const float center_x = (l + r) * 0.5f;
        const float center_y = (t + b) * 0.5f;
        const float center_z = 0.0f;
        const float scale_x = r - l;
        const float scale_y = t - b;
        const float scale_z = 0.0f;

        m_model_matrix_for_background = Transform3d::Identity();
        m_model_matrix_for_background.data()[3 * 4 + 0] = center_x;
        m_model_matrix_for_background.data()[3 * 4 + 1] = center_y;
        m_model_matrix_for_background.data()[3 * 4 + 2] = center_z;

        m_model_matrix_for_background.data()[0 * 4 + 0] = scale_x;
        m_model_matrix_for_background.data()[1 * 4 + 1] = scale_y;
        m_model_matrix_for_background.data()[2 * 4 + 2] = scale_z;

        m_normal_matrix_for_background = (Matrix3d)m_model_matrix_for_background.matrix().block(0, 0, 3, 3).inverse().transpose();

        m_cached_background_thickness = thickness;
    }

    shader->set_uniform("view_model_matrix", m_model_matrix_for_background);
    shader->set_uniform("projection_matrix", Transform3d::Identity());
    shader->set_uniform("normal_matrix", m_normal_matrix_for_background);

    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_z_texture_id));

    // Render the color bar
    if (!m_background.is_initialized()) {
        GLModel::Geometry init_data;
        init_data.format = { GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3T2 };
        init_data.reserve_vertices(4);
        init_data.reserve_indices(6);

        // vertices
        init_data.add_vertex(Vec3f(-0.5f, -0.5f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f), Vec2f(0.0f, 0.0f));
        init_data.add_vertex(Vec3f(0.5f, -0.5f, 0.0f),  Vec3f(0.0f, 0.0f, 1.0f), Vec2f(1.0f, 0.0f));
        init_data.add_vertex(Vec3f(0.5f, 0.5f, 0.0f),   Vec3f(0.0f, 0.0f, 1.0f), Vec2f(1.0f, 1.0f));
        init_data.add_vertex(Vec3f(-0.5f, 0.5f, 0.0f),  Vec3f(0.0f, 0.0f, 1.0f), Vec2f(0.0f, 1.0f));

        // indices
        init_data.add_triangle(0, 1, 2);
        init_data.add_triangle(2, 3, 0);

        m_background.init_from(std::move(init_data));
    }

    m_background.render_geometry();

    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    wxGetApp().unbind_shader();
}

void GLCanvas3D::LayersEditing::render_curve(const Rect & bar_rect)
{
    if (!m_enabled)
        return;

    //FIXME show some kind of legend.
    if (!m_slicing_parameters)
        return;

    const auto& shader = wxGetApp().get_shader("flat");
    if (!shader) {
        return;
    }

    wxGetApp().bind_shader(shader);
    // Make the vertical bar a bit wider so the layer height curve does not touch the edge of the bar region.
    const float scale_x = bar_rect.get_width() / float(1.12 * m_slicing_parameters->max_layer_height);
    const float scale_y = bar_rect.get_height() / m_object_max_z;
    const float x = bar_rect.get_left() + float(m_slicing_parameters->layer_height) * scale_x;

    const Camera &camera    = m_canvas.get_active_camera();
    Transform3d view_matrix = camera.get_view_matrix_for_billboard();

    Transform3d model_matrix{ Transform3d::Identity() };
    model_matrix.data()[3 * 4 + 0] = x;
    model_matrix.data()[3 * 4 + 1] = (bar_rect.get_top() + bar_rect.get_bottom()) * 0.5f;
    model_matrix.data()[0 * 4 + 0] = 0.0f;
    model_matrix.data()[1 * 4 + 1] = (bar_rect.get_top() - bar_rect.get_bottom());

    const auto& projection_matrix = camera.get_projection_matrix();
    shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
    shader->set_uniform("projection_matrix", projection_matrix);

    if (!m_baseline.is_initialized()) {
        GLModel::Geometry init_data;
        init_data.format = { GLModel::PrimitiveType::LineStrip, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        init_data.add_vertex(Vec3f(0.0f, -0.5f, 0.0f));
        init_data.add_vertex(Vec3f(0.0f, 0.5f, 0.0f));

        // indices
        init_data.add_line(0, 1);

        m_baseline.init_from(std::move(init_data));
    }
    m_baseline.set_color({ 0.0f, 0.0f, 0.0f , 1.0f});
    m_baseline.render_geometry();

    // Curve

    Transform3d model_matrix_curve{ Transform3d::Identity() };
    model_matrix_curve.data()[3 * 4 + 0] = bar_rect.get_left();
    model_matrix_curve.data()[3 * 4 + 1] = bar_rect.get_bottom();
    model_matrix_curve.data()[0 * 4 + 0] = scale_x;
    model_matrix_curve.data()[1 * 4 + 1] = scale_y;
    shader->set_uniform("view_model_matrix", view_matrix * model_matrix_curve);

    if (!m_profile_curve.is_initialized() || m_profile_dirty) {
        m_profile_curve.reset();
        GLModel::Geometry init_data;
        init_data.format = { GLModel::PrimitiveType::LineStrip, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(m_layer_height_profile.size() / 2);
        init_data.reserve_indices(m_layer_height_profile.size() / 2);

        // vertices
        for (unsigned int i = 0; i < m_layer_height_profile.size(); i += 2) {
            init_data.add_vertex(Vec3f((float)m_layer_height_profile[i + 1], (float)m_layer_height_profile[i], 0.0f));
            init_data.add_index(i / 2);
        }

        m_profile_curve.init_from(std::move(init_data));

        m_profile_dirty = false;
    }
    m_profile_curve.set_color({ 0.0f, 0.0f, 1.0f , 1.0f });
    m_profile_curve.render_geometry();

    wxGetApp().unbind_shader();
}

void GLCanvas3D::LayersEditing::render_volumes(const GLCanvas3D & canvas, const GLVolumeCollection & volumes)//render volume and layer height texture (has mapping relation with each other)
{
    assert(this->is_allowed());
    assert(this->last_object_id != -1);

    const auto& current_shader = wxGetApp().get_current_shader();
    ScopeGuard guard([current_shader]() { if (current_shader != nullptr) wxGetApp().bind_shader(current_shader); });
    if (current_shader != nullptr)
        wxGetApp().unbind_shader();

    const auto& shader = wxGetApp().get_shader("variable_layer_height");
    if (shader == nullptr)
        return;

    wxGetApp().bind_shader(shader);

    generate_layer_height_texture();

    // Uniforms were resolved, go ahead using the layer editing shader.
    shader->set_uniform("z_to_texture_row", float(m_layers_texture.cells - 1) / (float(m_layers_texture.width) * float(m_object_max_z)));
    shader->set_uniform("z_texture_row_to_normalized", 1.0f / float(m_layers_texture.height));
    shader->set_uniform("z_cursor", float(m_object_max_z) * float(this->get_cursor_z_relative(canvas)));
    shader->set_uniform("z_cursor_band_width", float(this->band_width));

    const Camera &camera    = m_canvas.get_active_camera();
    const auto& view_matrix = camera.get_view_matrix();
    const auto& projection_matrix = camera.get_projection_matrix();
    shader->set_uniform("projection_matrix", projection_matrix);

    // Initialize the layer height texture mapping.
    const GLsizei w = (GLsizei)m_layers_texture.width;
    const GLsizei h = (GLsizei)m_layers_texture.height;
    const GLsizei half_w = w / 2;
    const GLsizei half_h = h / 2;
    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_z_texture_id));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, half_w, half_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
    glsafe(::glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, m_layers_texture.data.data()));
    glsafe(::glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, half_w, half_h, GL_RGBA, GL_UNSIGNED_BYTE, m_layers_texture.data.data() + m_layers_texture.width * m_layers_texture.height * 4));
    std::vector<std::array<float, 4>> colors = m_canvas.get_active_colors();
    for (GLVolume* glvolume : volumes.volumes) {
        // Render the object using the layer editing shader and texture.
        if (!glvolume->is_active || glvolume->composite_id.object_id != this->last_object_id || glvolume->is_modifier)
            continue;

        shader->set_uniform("volume_world_matrix", glvolume->world_matrix());
        shader->set_uniform("object_max_z", 0.0f);

        const Transform3d view_model_matrix = view_matrix * glvolume->world_matrix();
        shader->set_uniform("view_model_matrix", view_model_matrix);
        shader->set_uniform("normal_matrix", (Matrix3d)view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());

        glvolume->render(camera, colors, m_canvas.get_ref_model());
    }
    // Revert back to the previous shader.
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GLCanvas3D::LayersEditing::adjust_layer_height_profile()
{
    this->update_slicing_parameters();
    PrintObject::update_layer_height_profile(*m_model_object, *m_slicing_parameters, m_layer_height_profile);
    Slic3r::adjust_layer_height_profile(*m_slicing_parameters, m_layer_height_profile, this->last_z, this->strength, this->band_width, this->last_action);
    m_layers_texture.valid = false;
    m_profile_dirty = true;
}

void GLCanvas3D::LayersEditing::reset_layer_height_profile(GLCanvas3D & canvas)
{
    const_cast<ModelObject*>(m_model_object)->layer_height_profile.clear();
    m_layer_height_profile.clear();
    m_layers_texture.valid = false;
    canvas.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    wxGetApp().obj_list()->update_info_items(last_object_id);
    m_profile_dirty = true;
}

void GLCanvas3D::LayersEditing::adaptive_layer_height_profile(GLCanvas3D & canvas, float quality_factor)
{
    this->update_slicing_parameters();
    m_layer_height_profile = layer_height_profile_adaptive(*m_slicing_parameters, *m_model_object, quality_factor);
    const_cast<ModelObject*>(m_model_object)->layer_height_profile.set(m_layer_height_profile);
    m_layers_texture.valid = false;
    canvas.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    wxGetApp().obj_list()->update_info_items(last_object_id);
    m_profile_dirty = true;
}

void GLCanvas3D::LayersEditing::smooth_layer_height_profile(GLCanvas3D & canvas, const HeightProfileSmoothingParams & smoothing_params)
{
    this->update_slicing_parameters();
    m_layer_height_profile = smooth_height_profile(m_layer_height_profile, *m_slicing_parameters, smoothing_params);
    const_cast<ModelObject*>(m_model_object)->layer_height_profile.set(m_layer_height_profile);
    m_layers_texture.valid = false;
    canvas.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    wxGetApp().obj_list()->update_info_items(last_object_id);
    m_profile_dirty = true;
}

void GLCanvas3D::LayersEditing::generate_layer_height_texture()
{
    this->update_slicing_parameters();
    // Always try to update the layer height profile.
    bool update = !m_layers_texture.valid;
    if (PrintObject::update_layer_height_profile(*m_model_object, *m_slicing_parameters, m_layer_height_profile)) {
        // Initialized to the default value.
        update = true;
    }
    // Update if the layer height profile was changed, or when the texture is not valid.
    if (!update && !m_layers_texture.data.empty() && m_layers_texture.cells > 0)
        // Texture is valid, don't update.
        return;

    if (m_layers_texture.data.empty()) {
        m_layers_texture.width = 1024;
        m_layers_texture.height = 1024;
        m_layers_texture.levels = 2;
        m_layers_texture.data.assign(m_layers_texture.width * m_layers_texture.height * 5, 0);
    }

    bool level_of_detail_2nd_level = true;
    m_layers_texture.cells = Slic3r::generate_layer_height_texture(
        *m_slicing_parameters,
        Slic3r::generate_object_layers(*m_slicing_parameters, m_layer_height_profile, false),
        m_layers_texture.data.data(), m_layers_texture.height, m_layers_texture.width, level_of_detail_2nd_level);
    m_layers_texture.valid = true;
}

void GLCanvas3D::LayersEditing::accept_changes(GLCanvas3D & canvas)
{
    if (last_object_id >= 0) {
        wxGetApp().plater()->take_snapshot("Variable layer height - Manual edit");
        const_cast<ModelObject*>(m_model_object)->layer_height_profile.set(m_layer_height_profile);
        canvas.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
        wxGetApp().obj_list()->update_info_items(last_object_id);
    }
}

void GLCanvas3D::LayersEditing::update_slicing_parameters()
{
    if (m_slicing_parameters == nullptr) {
        m_slicing_parameters = new SlicingParameters();
        *m_slicing_parameters = PrintObject::slicing_parameters(*m_config, *m_model_object, m_object_max_z);
    }
}

float GLCanvas3D::LayersEditing::thickness_bar_width(const GLCanvas3D & canvas)
{
    return
#if ENABLE_RETINA_GL
        canvas.get_canvas_size().get_scale_factor()
#else
        canvas.get_wxglcanvas()->GetContentScaleFactor()
#endif
        * THICKNESS_BAR_WIDTH;
}

Size::Size()
    : m_width(0)
    , m_height(0)
{
}

Size::Size(int width, int height, float scale_factor)
    : m_width(width)
    , m_height(height)
    , m_scale_factor(scale_factor)
{
}

int Size::get_width() const
{
    return m_width;
}

void Size::set_width(int width)
{
    m_width = width;
}

int Size::get_height() const
{
    return m_height;
}

void Size::set_height(int height)
{
    m_height = height;
}

int Size::get_scale_factor() const
{
    return m_scale_factor;
}

void Size::set_scale_factor(int scale_factor)
{
    m_scale_factor = scale_factor;
}

const Point GLCanvas3D::Mouse::Drag::Invalid_2D_Point(INT_MAX, INT_MAX);
const Vec3d GLCanvas3D::Mouse::Drag::Invalid_3D_Point(DBL_MAX, DBL_MAX, DBL_MAX);
const int GLCanvas3D::Mouse::Drag::MoveThresholdPx = 5;

GLCanvas3D::Mouse::Drag::Drag()
    : start_position_2D(Invalid_2D_Point)
    , start_position_3D(Invalid_3D_Point)
    , move_volume_idx(-1)
    , move_requires_threshold(false)
    , move_start_threshold_position_2D(Invalid_2D_Point)
{
}

GLCanvas3D::Mouse::Mouse()
    : dragging(false)
    , position(DBL_MAX, DBL_MAX)
    , scene_position(DBL_MAX, DBL_MAX, DBL_MAX)
    , ignore_left_up(false)
    , ignore_right_up(false)
{
}

void GLCanvas3D::Labels::render(const std::vector<const ModelInstance*>& sorted_instances) const
{
    if (!m_enabled || !is_shown() || m_canvas.get_gizmos_manager().is_running())
        return;

    const Camera &camera = m_canvas.get_active_camera();
    const Model* model = m_canvas.get_model();
    if (model == nullptr)
        return;

    Transform3d world_to_eye = camera.get_view_matrix();
    const std::array<int, 4>& viewport = camera.get_viewport();
    struct Owner
    {
        int obj_idx;
        int inst_idx;
        size_t model_instance_id;
        BoundingBoxf3 world_box;
        double eye_center_z;
        std::string title;
        std::string label;
        std::string print_order;
        bool selected;
    };

    // collect owners world bounding boxes and data from volumes
    std::vector<Owner> owners;
    const GLVolumeCollection& volumes = m_canvas.get_volumes();
    PartPlate* cur_plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
    for (const GLVolume* volume : volumes.volumes) {
        int obj_idx = volume->object_idx();
        if (0 <= obj_idx && obj_idx < (int)model->objects.size()) {
            int inst_idx = volume->instance_idx();
            //only show current plate's label
            if (!cur_plate->contain_instance(obj_idx, inst_idx))
                continue;
            std::vector<Owner>::iterator it = std::find_if(owners.begin(), owners.end(), [obj_idx, inst_idx](const Owner& owner) {
                return (owner.obj_idx == obj_idx) && (owner.inst_idx == inst_idx);
                });
            if (it != owners.end()) {
                it->world_box.merge(volume->transformed_bounding_box());
                it->selected &= volume->selected;
            } else {
                const ModelObject* model_object = model->objects[obj_idx];
                Owner owner;
                owner.obj_idx = obj_idx;
                owner.inst_idx = inst_idx;
                owner.model_instance_id = model_object->instances[inst_idx]->id().id;
                owner.world_box = volume->transformed_bounding_box();
                owner.title = "object" + std::to_string(obj_idx) + "_inst##" + std::to_string(inst_idx);
                owner.label = model_object->name;
                if (model_object->instances.size() > 1)
                    owner.label += " (" + std::to_string(inst_idx + 1) + ")";
                owner.selected = volume->selected;
                owners.emplace_back(owner);
            }
        }
    }

    // updates print order strings
    if (sorted_instances.size() > 0) {
        for (size_t i = 0; i < sorted_instances.size(); ++i) {
            size_t id = sorted_instances[i]->id().id;
            std::vector<Owner>::iterator it = std::find_if(owners.begin(), owners.end(), [id](const Owner& owner) {
                return owner.model_instance_id == id;
                });
            if (it != owners.end())
                //it->print_order = std::string((_(L("Sequence"))).ToUTF8()) + "#: " + std::to_string(i + 1);
                it->print_order = std::string((_(L("Sequence"))).ToUTF8()) + "#: " + std::to_string(sorted_instances[i]->arrange_order);
        }
    }

    // calculate eye bounding boxes center zs
    for (Owner& owner : owners) {
        owner.eye_center_z = (world_to_eye * owner.world_box.center())(2);
    }

    // sort owners by center eye zs and selection
    std::sort(owners.begin(), owners.end(), [](const Owner& owner1, const Owner& owner2) {
        if (!owner1.selected && owner2.selected)
            return true;
        else if (owner1.selected && !owner2.selected)
            return false;
        else
            return (owner1.eye_center_z < owner2.eye_center_z);
        });

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    // render info windows
    Matrix4d world_to_screen = camera.get_projection_matrix().matrix() * world_to_eye.matrix();
    for (const Owner& owner : owners) {
        Vec4d temp_center(owner.world_box.center().x(), owner.world_box.center().y(), owner.world_box.center().z(), 1.0);
        Vec4d temp_ndc = world_to_screen * temp_center;
        Vec3d screen_box_center = Vec3d(temp_ndc.x(), temp_ndc.y(), temp_ndc.z()) / temp_ndc.w();

        float x = 0.5f * (1 + screen_box_center(0)) * viewport[2];
        float y  = 0.5f * (1 - screen_box_center(1)) * viewport[3];

        if (x < 0.0f || viewport[2] < x || y < 0.0f || viewport[3] < y)
            continue;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, owner.selected ? 3.0f : 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, owner.selected ? ImVec4(0.757f, 0.404f, 0.216f, 1.0f) : ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        imgui.set_next_window_pos(x, y, ImGuiCond_Always, 0.5f, 0.5f);
        imgui.begin(owner.title, ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        float win_w = ImGui::GetWindowWidth();
        ImGui::AlignTextToFramePadding();
        imgui.text(owner.label);

        if (!owner.print_order.empty()) {
            ImGui::Separator();
            float po_len = imgui.calc_text_size(owner.print_order).x;
            ImGui::SetCursorPosX(0.5f * (win_w - po_len));
            ImGui::AlignTextToFramePadding();
            imgui.text(owner.print_order);
        }

        // force re-render while the windows gets to its final size (it takes several frames)
        if (ImGui::GetWindowContentRegionWidth() + 2.0f * ImGui::GetStyle().WindowPadding.x != ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow()).x)
            imgui.set_requires_extra_frame();

        imgui.end();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
}

void GLCanvas3D::Tooltip::set_text(const std::string& text)
{
    // If the mouse is inside an ImGUI dialog, then the tooltip is suppressed.
    m_text = m_in_imgui ? std::string() : text;
}

void GLCanvas3D::Tooltip::render(const Vec2d& mouse_position, GLCanvas3D& canvas)
{
    static ImVec2 size(0.0f, 0.0f);

    auto validate_position = [](const Vec2d& position, const GLCanvas3D& canvas, const ImVec2& wnd_size) {
        const Size cnv_size = canvas.get_canvas_size();
        const float x = std::clamp((float)position.x(), 0.0f, (float)cnv_size.get_width() - wnd_size.x);
        const float y = std::clamp((float)position.y() + 16.0f, 0.0f, (float)cnv_size.get_height() - wnd_size.y);
        return Vec2f(x, y);
    };

    if (m_text.empty()) {
        m_start_time = std::chrono::steady_clock::now();
        return;
    }

    // draw the tooltip as hidden until the delay is expired
    // use a value of alpha slightly different from 0.0f because newer imgui does not calculate properly the window size if alpha == 0.0f
    const float alpha = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start_time).count() < 500) ? 0.01f : 1.0f;

    const Vec2f position = validate_position(mouse_position, canvas, size);

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    imgui.set_next_window_pos(position.x(), position.y(), ImGuiCond_Always, 0.0f, 0.0f);

    imgui.begin(wxString("canvas_tooltip"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::TextUnformatted(m_text.c_str());

    // force re-render while the windows gets to its final size (it may take several frames) or while hidden
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    if (alpha < 1.0f || ImGui::GetWindowContentRegionWidth() + 2.0f * ImGui::GetStyle().WindowPadding.x != ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow()).x)
        imgui.set_requires_extra_frame();
#else
    if (alpha < 1.0f || ImGui::GetWindowContentRegionWidth() + 2.0f * ImGui::GetStyle().WindowPadding.x != ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow()).x)
        canvas.request_extra_frame();
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

    size = ImGui::GetWindowSize();

    imgui.end();
    ImGui::PopStyleVar(2);
}

//BBS: add height limit logic

GLCanvas3D::SequentialPrintClearance::~SequentialPrintClearance()
{
    reset();
}

void GLCanvas3D::SequentialPrintClearance::set_polygons(const Polygons& polygons, const std::vector<std::pair<Polygon, float>>& height_polygons)
{
    //BBS: add height limit logic
    m_height_limit.reset();
    m_perimeter.reset();
    m_fill.reset();
    if (!polygons.empty()) {
        size_t triangles_count = 0;
        for (const Polygon &poly : polygons) { triangles_count += poly.points.size() - 2; }
        const size_t vertices_count = 3 * triangles_count;

        if (m_render_fill) {
            GLModel::Geometry fill_data;
            fill_data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3};
            fill_data.color  = {0.8f, 0.8f, 1.0f, 0.5f};

            // vertices + indices
            const ExPolygons polygons_union   = union_ex(polygons);
            unsigned int     vertices_counter = 0;
            for (const ExPolygon &poly : polygons_union) {
                const std::vector<Vec3d> triangulation = triangulate_expolygon_3d(poly);
                fill_data.reserve_vertices(fill_data.vertices_count() + triangulation.size());
                fill_data.reserve_indices(fill_data.indices_count() + triangulation.size());
                for (const Vec3d &v : triangulation) {
                    fill_data.add_vertex((Vec3f) (v.cast<float>() + 0.0125f * Vec3f::UnitZ())); // add a small positive z to avoid z-fighting
                    ++vertices_counter;
                    if (vertices_counter % 3 == 0)
                        fill_data.add_triangle(vertices_counter - 3, vertices_counter - 2, vertices_counter - 1);
                }
            }
            m_fill.init_from(std::move(fill_data));

        }

        m_perimeter.init_model_from_polygon(polygons, 0.025f);
    }

    //BBS: add the height limit compute logic
    if (!height_polygons.empty()) {
        size_t height_triangles_count = 0;
        for (const auto &poly : height_polygons) { height_triangles_count += poly.first.points.size() - 2; }
        const size_t height_vertices_count = 3 * height_triangles_count;

        GLModel::InitializationData         height_fill_data;
        GLModel::InitializationData::Entity height_entity;
        height_entity.type  = GLModel::PrimitiveType::Triangles;
        height_entity.color = {0.8f, 0.8f, 1.0f, 0.5f};
        height_entity.positions.reserve(height_vertices_count);
        height_entity.normals.reserve(height_vertices_count);
        height_entity.indices.reserve(height_vertices_count);

        for (const auto &poly : height_polygons) {
            ExPolygon                ex_poly(poly.first);
            const std::vector<Vec3d> height_triangulation = triangulate_expolygon_3d(ex_poly);
            for (const Vec3d &v : height_triangulation) {
                Vec3d point{v.x(), v.y(), poly.second};
                height_entity.positions.emplace_back(point.cast<float>());
                height_entity.normals.emplace_back(Vec3f::UnitZ());
                const size_t height_positions_count = height_entity.positions.size();
                if (height_positions_count % 3 == 0) {
                    height_entity.indices.emplace_back(height_positions_count - 3);
                    height_entity.indices.emplace_back(height_positions_count - 2);
                    height_entity.indices.emplace_back(height_positions_count - 1);
                }
            }
        }

        height_fill_data.entities.emplace_back(height_entity);
        m_height_limit.init_from(height_fill_data);
    }
}

void GLCanvas3D::SequentialPrintClearance::render()
{
    std::array<float, 4> FILL_COLOR = { 0.7f, 0.7f, 1.0f, 0.5f };
    std::array<float, 4> NO_FILL_COLOR = { 0.75f, 0.75f, 0.75f, 0.75f };

    const auto& shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    wxGetApp().bind_shader(shader);

    const Camera &camera = m_canvas.get_active_camera();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    m_perimeter.set_color(-1, m_render_fill ? FILL_COLOR : NO_FILL_COLOR);
    m_perimeter.render_geometry();
    m_fill.render_geometry();
    //BBS: add height limit
    m_height_limit.set_color(-1, m_render_fill ? FILL_COLOR : NO_FILL_COLOR);
    m_height_limit.render_geometry();

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_DEPTH_TEST));

    wxGetApp().unbind_shader();
}

void GLCanvas3D::SequentialPrintClearance::reset()
{
    m_height_limit.reset();
    m_fill.reset();
    m_perimeter.reset();
}

wxDEFINE_EVENT(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_OBJECT_SELECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_PLATE_NAME_CHANGE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_PLATE_SELECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RIGHT_CLICK, RBtnEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_PLATE_RIGHT_CLICK, RBtnPlateEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_REMOVE_OBJECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ARRANGE, SimpleEvent);
//BBS: add arrange and orient event
wxDEFINE_EVENT(EVT_GLCANVAS_ARRANGE_PARTPLATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ARRANGE_OUTPLATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ORIENT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ORIENT_PARTPLATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SELECT_CURR_PLATE_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SELECT_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_QUESTION_MARK, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INCREASE_INSTANCES, Event<int>);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_MOVED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_ROTATED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_SCALED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_FORCE_UPDATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, Event<bool>);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE_GEOMETRY, Vec3dsEvent<2>);
wxDEFINE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE_BED_SHAPE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_TAB, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RESETGIZMOS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_MOVE_SLIDERS, wxKeyEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_EDIT_COLOR_CHANGE, wxKeyEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_JUMP_TO, wxKeyEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_UNDO, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_REDO, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SWITCH_TO_OBJECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SWITCH_TO_GLOBAL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_COLLAPSE_SIDEBAR, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RELOAD_FROM_DISK, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RENDER_TIMER, wxTimerEvent/*RenderTimerEvent*/);
wxDEFINE_EVENT(EVT_GLCANVAS_TOOLBAR_HIGHLIGHTER_TIMER, wxTimerEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_GIZMO_HIGHLIGHTER_TIMER, wxTimerEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE, SimpleEvent);
wxDEFINE_EVENT(EVT_CUSTOMEVT_TICKSCHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RESET_LAYER_HEIGHT_PROFILE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ADAPTIVE_LAYER_HEIGHT_PROFILE, Event<float>);
wxDEFINE_EVENT(EVT_GLCANVAS_SMOOTH_LAYER_HEIGHT_PROFILE, HeightProfileSmoothEvent);

const double GLCanvas3D::DefaultCameraZoomToBoxMarginFactor = 1.25;
const double GLCanvas3D::DefaultCameraZoomToBedMarginFactor = 2.00;
const double GLCanvas3D::DefaultCameraZoomToPlateMarginFactor = 1.25;

void GLCanvas3D::load_arrange_settings()
{
    std::string dist_fff_str =
        wxGetApp().app_config->get("arrange", "min_object_distance");

    std::string dist_fff_seq_print_str =
        wxGetApp().app_config->get("arrange", "min_object_distance_seq_print");

    std::string dist_sla_str =
        wxGetApp().app_config->get("arrange", "min_object_distance_sla");

    std::string en_rot_fff_str =
        wxGetApp().app_config->get("arrange", "enable_rotation");

    std::string en_rot_fff_seqp_str =
        wxGetApp().app_config->get("arrange", "enable_rotation_seq_print");

    std::string en_rot_sla_str =
        wxGetApp().app_config->get("arrange", "enable_rotation_sla");

    if (!dist_fff_str.empty())
        m_arrange_settings_fff.distance = std::stof(dist_fff_str);

    if (!dist_fff_seq_print_str.empty())
        m_arrange_settings_fff_seq_print.distance = std::stof(dist_fff_seq_print_str);

    if (!dist_sla_str.empty())
        m_arrange_settings_sla.distance = std::stof(dist_sla_str);

    if (!en_rot_fff_str.empty())
        m_arrange_settings_fff.enable_rotation = (en_rot_fff_str == "1" || en_rot_fff_str == "yes");

    if (!en_rot_fff_seqp_str.empty())
        m_arrange_settings_fff_seq_print.enable_rotation = (en_rot_fff_seqp_str == "1" || en_rot_fff_seqp_str == "yes");

    if (!en_rot_sla_str.empty())
        m_arrange_settings_sla.enable_rotation = (en_rot_sla_str == "1" || en_rot_sla_str == "yes");

    //BBS: add specific arrange settings
    m_arrange_settings_fff_seq_print.is_seq_print = true;
}

GLCanvas3D::ArrangeSettings& GLCanvas3D::get_arrange_settings()
{
    PrinterTechnology ptech = current_printer_technology();

    auto* ptr = &m_arrange_settings_fff;

    if (ptech == ptSLA) {
        ptr = &m_arrange_settings_sla;
        ptr->postfix = "_sla";
    }
    else if (ptech == ptFFF) {
        if (wxGetApp().global_print_sequence() == PrintSequence::ByObject) {
            ptr     = &m_arrange_settings_fff_seq_print;
            ptr->postfix = "_fff_seq_print";
        } else {
            ptr     = &m_arrange_settings_fff;
            ptr->postfix = "_fff";
        }
    }

    return *ptr;
}

int GLCanvas3D::GetHoverId()
{
    if (m_hover_plate_idxs.size() == 0) {
        return -1; }
    return m_hover_plate_idxs.front();

}

PrinterTechnology GLCanvas3D::current_printer_technology() const {
    return m_process->current_printer_technology();
}

GLCanvas3D::GLCanvas3D(wxGLCanvas* canvas, Bed3D &bed)
    : m_canvas(canvas)
    , m_context(nullptr)
    , m_bed(bed)
#if ENABLE_RETINA_GL
    , m_retina_helper(nullptr)
#endif
    , m_in_render(false)
    , m_return_toolbar()
    , m_canvas_type(ECanvasType::CanvasView3D)
    , m_gizmos(*this)
    , m_use_clipping_planes(false)
    , m_sidebar_field("")
    , m_extra_frame_requested(false)
    , m_config(nullptr)
    , m_process(nullptr)
    , m_model(nullptr)
    , m_dirty(true)
    , m_initialized(false)
    , m_apply_zoom_to_volumes_filter(false)
    , m_picking_enabled(false)
    , m_moving_enabled(false)
    , m_dynamic_background_enabled(false)
    , m_multisample_allowed(false)
    , m_moving(false)
    , m_tab_down(false)
    , m_cursor_type(Standard)
    , m_color_by("volume")
    , m_reload_delayed(false)
#if ENABLE_RENDER_PICKING_PASS
    , m_show_picking_texture(false)
#endif // ENABLE_RENDER_PICKING_PASS
    , m_render_sla_auxiliaries(true)
    , m_layers_editing(*this)
    , m_labels(*this)
    , m_slope(m_volumes)
    , m_sequential_print_clearance(*this)
{
    if (m_canvas != nullptr) {
        m_timer.SetOwner(m_canvas);
        m_render_timer.SetOwner(m_canvas);
#if ENABLE_RETINA_GL
        m_retina_helper.reset(new RetinaHelper(canvas));
#endif // ENABLE_RETINA_GL
    }
    m_timer_set_color.Bind(wxEVT_TIMER, &GLCanvas3D::on_set_color_timer, this);
    load_arrange_settings();

    m_selection.set_volumes(&m_volumes.volumes);

    const wxString alt                               = GUI::shortkey_alt_prefix();
    m_assembly_view_desc["object_selection_caption"] = _L("Left mouse button");
    m_assembly_view_desc["object_selection"]         = _L("object selection");
    m_assembly_view_desc["part_selection_caption"]   = alt  + _L("Left mouse button");
    m_assembly_view_desc["part_selection"]         = _L("part selection");
    m_assembly_view_desc["number_key_caption"]       = "1~16 " + _L("number keys");
    m_assembly_view_desc["number_key"]       = _L("number keys can quickly change the color of objects");

    m_render_pipeline_stage_stack.push(ERenderPipelineStage::Normal);
}

GLCanvas3D::~GLCanvas3D()
{
    reset_volumes(false);

    m_sel_plate_toolbar.del_all_item();
    m_sel_plate_toolbar.del_stats_item();

    s_full_screen_mesh.reset();
    m_unit_cube.reset();
#if ENABLE_SHOW_CAMERA_TARGET
    m_camera_target_mark.reset();
#endif // ENABLE_SHOW_CAMERA_TARGET

    m_sequential_print_clearance.reset();
}

void GLCanvas3D::set_context(wxGLContext* context)
{
    if (context != m_context) {
        m_context = context;
        m_dirty_context = true;
    }
}

void GLCanvas3D::set_type(ECanvasType type)
{
    if (type != m_canvas_type) {
        m_canvas_type = type;
    }
}

void GLCanvas3D::post_event(wxEvent &&event)
{
    event.SetEventObject(m_canvas);
    wxPostEvent(m_canvas, event);
}

bool GLCanvas3D::init()
{
    if (m_initialized)
        return true;

    if (m_canvas == nullptr || m_context == nullptr)
        return false;

    // init dark mode status
    on_change_color_mode(wxGetApp().app_config->get("dark_color_mode") == "1", false);

    BOOST_LOG_TRIVIAL(info) <<__FUNCTION__<< " enter";
    glsafe(::glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
    glsafe(::glClearDepth(1.0f));

    glsafe(::glDepthFunc(GL_LESS));

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    // Set antialiasing / multisampling
    glsafe(::glDisable(GL_LINE_SMOOTH));
    glsafe(::glDisable(GL_POLYGON_SMOOTH));

    if (m_multisample_allowed)
        glsafe(::glEnable(GL_MULTISAMPLE));

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": before m_layers_editing init";
    if (m_canvas_type == ECanvasType::CanvasView3D)
        m_layers_editing.init();

    // on linux the gl context is not valid until the canvas is not shown on screen
    // we defer the geometry finalization of volumes until the first call to render()
    m_volumes.finalize_geometry(true);

    BOOST_LOG_TRIVIAL(info) <<__FUNCTION__<< ": before gizmo init";
    if (m_gizmos.is_enabled() && !m_gizmos.init())
        std::cout << "Unable to initialize gizmos: please, check that all the required textures are available" << std::endl;

    BOOST_LOG_TRIVIAL(info) <<__FUNCTION__<< ": before _init_toolbars";
    if (!_init_toolbars())
        return false;

    BOOST_LOG_TRIVIAL(info) <<__FUNCTION__<< ": finish _init_toolbars";
    if (m_selection.is_enabled() && !m_selection.init())
        return false;

    BOOST_LOG_TRIVIAL(info) <<__FUNCTION__<< ": finish m_selection";

#if ENABLE_IMGUI_STYLE_EDITOR
    //BBS load render color for style editor
    GLVolume::load_render_colors();
    PartPlate::load_render_colors();
    GLGizmoBase::load_render_colors();
    GLCanvas3D::load_render_colors();
    Bed3D::load_render_colors();
#endif
    //if (!wxGetApp().is_gl_version_greater_or_equal_to(3, 0))
    //    wxGetApp().plater()->enable_wireframe(false);
    m_initialized = true;

    return true;
}

void GLCanvas3D::on_change_color_mode(bool is_dark, bool reinit) {
    m_is_dark = is_dark;
    // Bed color
    m_bed.on_change_color_mode(is_dark);
    // GcodeViewer color
    m_gcode_viewer.on_change_color_mode(is_dark);
    // ImGui Style
    wxGetApp().imgui()->on_change_color_mode(is_dark);
    // Notification
    wxGetApp().plater()->get_notification_manager()->on_change_color_mode(is_dark);
    // DailyTips Window
    wxGetApp().plater()->get_dailytips()->on_change_color_mode(is_dark);
    // Preview Slider
    IMSlider* m_layers_slider = get_gcode_viewer().get_layers_slider();
    IMSlider* m_moves_slider = get_gcode_viewer().get_moves_slider();
    m_layers_slider->on_change_color_mode(is_dark);
    m_moves_slider->on_change_color_mode(is_dark);
    // Partplate
    wxGetApp().plater()->get_partplate_list().on_change_color_mode(is_dark);

    // Toolbar
    if (m_canvas_type == CanvasView3D) {
        m_gizmos.on_change_color_mode(is_dark);
        if (reinit) {
            // reset svg
            _switch_toolbars_icon_filename();
            const auto& p_main_toolbar = get_main_toolbar();
            if (p_main_toolbar) {
                p_main_toolbar->set_icon_dirty();
            }
            wxGetApp().plater()->get_collapse_toolbar().set_icon_dirty();
        }
    }
    if (m_canvas_type == CanvasAssembleView) {
        m_gizmos.on_change_color_mode(is_dark);
    }
}

void GLCanvas3D::set_as_dirty()
{
    m_dirty = true;
}

const float GLCanvas3D::get_scale() const
{
#if ENABLE_RETINA_GL
    return m_retina_helper->get_scale_factor();
#else
    return 1.0f;
#endif
}

unsigned int GLCanvas3D::get_volumes_count() const
{
    return (unsigned int)m_volumes.volumes.size();
}

void GLCanvas3D::reset_volumes(bool set_notice)
{
    if (!m_initialized)
        return;

    if (m_volumes.empty())
        return;

    _set_current(true);

    m_selection.clear();
    m_volumes.clear();
    m_dirty = true;
    if (set_notice) { _set_warning_notification(EWarning::ObjectOutside, false); }
}

//BBS: get current plater's bounding box
BoundingBoxf3 GLCanvas3D::_get_current_partplate_print_volume()
{
    BoundingBoxf3 test_volume;
    if (m_process && m_config)
    {
        BoundingBoxf3 plate_bb = m_process->get_current_plate()->get_bounding_box(false);
        BoundingBoxf3 print_volume({ plate_bb.min(0), plate_bb.min(1), 0.0 }, { plate_bb.max(0), plate_bb.max(1), m_config->opt_float("printable_height") });
        // Allow the objects to protrude below the print bed
        print_volume.min(2) = -1e10;
        print_volume.min(0) -= Slic3r::BuildVolume::BedEpsilon;
        print_volume.min(1) -= Slic3r::BuildVolume::BedEpsilon;
        print_volume.max(0) += Slic3r::BuildVolume::BedEpsilon;
        print_volume.max(1) += Slic3r::BuildVolume::BedEpsilon;
        test_volume = print_volume;
    }
    else
        test_volume = BoundingBoxf3();

    return test_volume;
}

static bool construct_error_string(ObjectFilamentResults& object_result, std::string& error_string)
{
    error_string.clear();
    if (!object_result.partly_outside_objects.empty()) {
        error_string += _u8L("Following objects are laid over the boundary of plate or exceeds the height limit:\n");
        for(auto& object: object_result.partly_outside_objects)
        {
            error_string += object->name;
            error_string += "\n";
        }
        error_string += _u8L("Please solve the problem by moving it totally on or off the plate, and confirming that the height is within the build volume.\n");
        return true;
    }
    return false;
}

static bool construct_assembly_warning_string(std::vector<std::string>& object_result, std::string& error_string)
{
    error_string.clear();
    if (!object_result.size()) {
        return false;
    }
    bool imperial_units = wxGetApp().app_config->get("use_inches") == "1";
    double koef = imperial_units ? GizmoObjectManipulation::mm_to_in : 1.0f;
    float distance_limit = 10000.0f;
    if (imperial_units) {
        distance_limit *= koef;
    }
    if (imperial_units) {
        error_string += (boost::format(_utf8(L("Assembly's bounding box is too large ( max size >= %1% in ) which may cause rendering issues.\n"))) % distance_limit).str();
    }
    else {
        error_string += (boost::format(_utf8(L("Assembly's bounding box is too large ( max size >= %1% mm ) which may cause rendering issues.\n"))) % distance_limit).str();
    }
    if (!object_result.empty()) {
        if (imperial_units) {
            error_string += (boost::format(_utf8(L("Following objects are too far ( distance >= %1% in ) from the original of the world coordinate system:\n"))) % distance_limit).str();
        }
        else {
            error_string += (boost::format(_utf8(L("Following objects are too far ( distance >= %1% mm ) from the original of the world coordinate system:\n"))) % distance_limit).str();
        }
        for (const auto& t_name : object_result)
        {
            error_string += t_name;
            error_string += "\n";
        }
    }
    return true;
}

static std::pair<bool, bool> construct_extruder_unprintable_error(ObjectFilamentResults& object_result, std::string& left_extruder_unprintable_text, std::string& right_extruder_unprintable_text)
{
    left_extruder_unprintable_text.clear();
    right_extruder_unprintable_text.clear();
    if (object_result.filaments.empty())
        return {false,false};

    const std::vector<std::string> nozzle_name_list = { _u8L("left nozzle"), _u8L("right nozzle") };

    std::vector<ObjectFilamentInfo> left_unprintable_objects;
    std::vector<ObjectFilamentInfo> right_unprintable_objects;

    std::vector<int> conflicted_filaments = object_result.filaments;

    auto mode = object_result.mode;

    for (auto& obj_filament : object_result.object_filaments) {
        if (mode == FilamentMapMode::fmmManual) {
            for (auto& elem : obj_filament.manual_filaments) {
                bool found_left = false, found_right = false;
                int filamnet_id = elem.first;
                int extruder_id = elem.second;
                if (extruder_id == 1 && !found_left) {
                    found_left = true;
                    left_unprintable_objects.emplace_back(obj_filament);
                }
                if (extruder_id == 2 && !found_right) {
                    found_right = true;
                    right_unprintable_objects.emplace_back(obj_filament);
                }
            }
        }
        else {
            if (!obj_filament.auto_filaments.empty()) {
                left_unprintable_objects.emplace_back(obj_filament);
                right_unprintable_objects.emplace_back(obj_filament);
            }
        }
    }

    Preset &preset = GUI::wxGetApp().preset_bundle->printers.get_edited_preset();
    float   left_x_min = 0, left_x_max = 0, left_y_min = 0, left_y_max = 0, left_z_min = 0, left_z_max = 0;
    float   right_x_min = 0, right_x_max = 0, right_y_min = 0, right_y_max = 0, right_z_min = 0, right_z_max = 0;
    auto printable_height_option = preset.config.option<ConfigOptionFloatsNullable>("extruder_printable_height");
    if (printable_height_option && printable_height_option->values.size() == 2) {
        left_z_max = (float) printable_height_option->values[0];
        right_z_max = (float) printable_height_option->values[1];
    }
    std::vector<Pointfs> printable_areas = preset.config.option<ConfigOptionPointsGroups>("extruder_printable_area")->values;
    if (printable_areas.size() == 2 && printable_areas[0].size() == 4) {
        left_x_min = printable_areas[0][0][0];
        left_y_min = printable_areas[0][0][1];
        left_x_max = printable_areas[0][2][0];
        left_y_max = printable_areas[0][2][1];

        right_x_min = printable_areas[1][0][0];
        right_y_min = printable_areas[1][0][1];
        right_x_max = printable_areas[1][2][0];
        right_y_max = printable_areas[1][2][1];
    }

    std::vector<std::string> tips(2);
    for (size_t idx = 0; idx < tips.size(); ++idx) {
        const auto& unprintable_objs = idx == 0 ? left_unprintable_objects : right_unprintable_objects;
        auto& output_text = idx == 0 ? left_extruder_unprintable_text : right_extruder_unprintable_text;
        if (unprintable_objs.empty())
            continue;
        std::string nozzle_name = nozzle_name_list[idx];
        std::string model_prefix;
        if ((idx == 0 && left_unprintable_objects.size() > 1) || (idx == 1 && right_unprintable_objects.size() > 1))
            model_prefix = (boost::format(_u8L("The position or size of some models exceeds the %s's printable range.")) % nozzle_name).str();
        else
            model_prefix = (boost::format(_u8L("The position or size of the model %s exceeds the %s's printable range."))
                           %object_result.object_filaments.front().object->name % nozzle_name).str();
        tips[idx] += model_prefix;

        tips[idx] += (boost::format(_u8L(" Please check and adjust the part's position or size to fit the printable range:\n"))).str();
        if (idx == 0)
            tips[idx] += (boost::format(_u8L("Left nozzle: X:%1%-%2%, Y:%3%-%4%, Z:%5%-%6%\n"))
                         % format_number(left_x_min) % format_number(left_x_max) % format_number(left_y_min)
                         % format_number(left_y_max) % format_number(left_z_min) % format_number(left_z_max)).str();
        else
            tips[idx] += (boost::format(_u8L("Right nozzle: X:%1%-%2%, Y:%3%-%4%, Z:%5%-%6%"))
                         %format_number(right_x_min) %format_number(right_x_max) %format_number(right_y_min)
                         %format_number(right_y_max) %format_number(right_z_min) %format_number(right_z_max)).str();
        output_text = tips[idx];
    }

    return { !left_unprintable_objects.empty(),!right_unprintable_objects.empty() };
}

ModelInstanceEPrintVolumeState GLCanvas3D::check_volumes_outside_state(ObjectFilamentResults* object_results) const
{
    //BBS: if not initialized, return inside directly insteadof assert
    if (!m_initialized) {
        return ModelInstancePVS_Inside;
    }
    //assert(m_initialized);

    ModelInstanceEPrintVolumeState state;
    m_volumes.check_outside_state(m_bed.build_volume(), &state, object_results,*m_model);

    construct_error_string(*object_results, get_object_clashed_text());
    construct_extruder_unprintable_error(*object_results, get_left_extruder_unprintable_text(), get_right_extruder_unprintable_text());
    return state;
}

bool GLCanvas3D::is_volumes_selected_and_sinking() const {
    for (GLVolume *volume : m_volumes.volumes) {
        if (volume->selected && volume->is_sinking() && !volume->is_below_printbed()) {
            return true;
        }
    }
    return false;
}

void GLCanvas3D::toggle_selected_volume_visibility(bool selected_visible)
{
    m_render_sla_auxiliaries = !selected_visible;
    if (selected_visible) {
        const Selection::IndicesList &idxs = m_selection.get_volume_idxs();
        if (idxs.size() > 0) {
            for (GLVolume *vol : m_volumes.volumes) {
                if (vol->composite_id.object_id >= 1000 && vol->composite_id.object_id < 1000 + wxGetApp().plater()->get_partplate_list().get_plate_count())
                    continue; // the wipe tower
                if (vol->composite_id.volume_id >= 0) {
                    vol->is_active = false;
                }
            }
            for (unsigned int idx : idxs) {
                GLVolume *v  = const_cast<GLVolume *>(m_selection.get_volume(idx));
                v->is_active = true;
            }
        }
    } else { // show all
        for (GLVolume *vol : m_volumes.volumes) {
            if (vol->composite_id.object_id >= 1000 && vol->composite_id.object_id < 1000 + wxGetApp().plater()->get_partplate_list().get_plate_count())
                continue; // the wipe tower
            if (vol->composite_id.volume_id >= 0) {
                vol->is_active = true;
            }
        }
    }
}

void GLCanvas3D::toggle_sla_auxiliaries_visibility(bool visible, const ModelObject *mo, int instance_idx)
{
    m_render_sla_auxiliaries = visible;

    for (GLVolume* vol : m_volumes.volumes) {
        if (vol->composite_id.object_id >= 1000 &&
            vol->composite_id.object_id < 1000 + wxGetApp().plater()->get_partplate_list().get_plate_count())
            continue; // the wipe tower
        if ((mo == nullptr || m_model->objects[vol->composite_id.object_id] == mo)
        && (instance_idx == -1 || vol->composite_id.instance_id == instance_idx)
        && vol->composite_id.volume_id < 0)
            vol->is_active = visible;
    }
}

void GLCanvas3D::toggle_model_objects_visibility(bool visible, const ModelObject* mo, int instance_idx, const ModelVolume* mv)
{
    for (GLVolume* vol : m_volumes.volumes) {
        // BBS: add partplate logic
        if (vol->composite_id.object_id >= 1000 &&
            vol->composite_id.object_id < 1000 + wxGetApp().plater()->get_partplate_list().get_plate_count()) { // wipe tower
            vol->is_active = (visible && mo == nullptr);
        }
        else {
            if ((mo == nullptr || m_model->objects[vol->composite_id.object_id] == mo)
            && (instance_idx == -1 || vol->composite_id.instance_id == instance_idx)
            && (mv == nullptr || m_model->objects[vol->composite_id.object_id]->volumes[vol->composite_id.volume_id] == mv)) {
                vol->is_active = visible;

                if (instance_idx == -1) {
                    vol->force_native_color = false;
                    vol->force_neutral_color = false;
                } else {
                    auto  gizmo_type = m_gizmos.get_current_type();
                    if (m_gizmos.is_paint_gizmo()
                        && ! vol->is_modifier)
                        vol->force_neutral_color = true;
                    else if (gizmo_type == GLGizmosManager::BrimEars)
                        vol->force_neutral_color = false;
                    else if (gizmo_type == GLGizmosManager::MmuSegmentation)
                        vol->is_active = false;
                    else if (gizmo_type == GLGizmosManager::Text || gizmo_type == GLGizmosManager::MeshBoolean) {
                        vol->force_native_color  = false;
                        vol->force_neutral_color = false;
                    }
                    else
                        vol->force_native_color = true;
                }
            }
        }
    }
    if (visible && !mo)
        toggle_sla_auxiliaries_visibility(true, mo, instance_idx);

    if (!mo && !visible && !m_model->objects.empty() && (m_model->objects.size() > 1 || m_model->objects.front()->instances.size() > 1))
        _set_warning_notification(EWarning::SomethingNotShown, true);

    if (!mo && visible)
        _set_warning_notification(EWarning::SomethingNotShown, false);
}

void GLCanvas3D::update_instance_printable_state_for_object(const size_t obj_idx)
{
    ModelObject* model_object = m_model->objects[obj_idx];
    for (int inst_idx = 0; inst_idx < (int)model_object->instances.size(); ++inst_idx) {
        ModelInstance* instance = model_object->instances[inst_idx];

        for (GLVolume* volume : m_volumes.volumes) {
            if ((volume->object_idx() == (int)obj_idx) && (volume->instance_idx() == inst_idx))
                volume->printable = instance->printable;
                if (!volume->printable) {
                    volume->render_color = GLVolume::UNPRINTABLE_COLOR;
                }
        }
    }
}

void GLCanvas3D::update_instance_printable_state_for_objects(const std::vector<size_t>& object_idxs)
{
    for (size_t obj_idx : object_idxs)
        update_instance_printable_state_for_object(obj_idx);
}

void GLCanvas3D::set_config(const DynamicPrintConfig* config)
{
    m_config = config;
    m_layers_editing.set_config(config);
}

void GLCanvas3D::set_process(BackgroundSlicingProcess *process)
{
    m_process = process;
}

void GLCanvas3D::set_model(Model* model)
{
    m_model = model;
    m_selection.set_model(m_model);
}

const Selection& GLCanvas3D::get_selection() const
{
    return m_selection;
}

Selection& GLCanvas3D::get_selection()
{
    return m_selection;
}

void GLCanvas3D::bed_shape_changed()
{
    refresh_camera_scene_box();
    get_active_camera().requires_zoom_to_bed = true;
    m_dirty = true;
}

void GLCanvas3D::plates_count_changed()
{
    refresh_camera_scene_box();
    m_dirty = true;
}

void GLCanvas3D::set_use_clipping_planes(bool use)
{
    if (m_gizmos.get_current_type() == GLGizmosManager::EType::Text) {
        m_use_clipping_planes = false;
    }
    else{
        m_use_clipping_planes = use;
    }
}

void GLCanvas3D::set_color_by(const std::string &value) {
    m_color_by = value;
}

void GLCanvas3D::refresh_camera_scene_box()
{
    get_active_camera().set_scene_box(scene_bounding_box());
}

BoundingBoxf3 GLCanvas3D::assembly_view_cur_bounding_box() const {
    return m_model->bounding_box_in_assembly_view();
}

BoundingBoxf3 GLCanvas3D::volumes_bounding_box(bool limit_to_expand_plate) const
{
    BoundingBoxf3 bb;
    BoundingBoxf3 expand_part_plate_list_box;
    bool          is_limit = limit_to_expand_plate;
    if (is_limit) {
        auto        plate_list_box    = wxGetApp().plater()->get_partplate_list().get_bounding_box();
        auto        horizontal_radius = 0.5 * sqrt(std::pow(plate_list_box.min[0] - plate_list_box.max[0], 2) + std::pow(plate_list_box.min[1] - plate_list_box.max[1], 2));
        const float scale             = 2;
        expand_part_plate_list_box.merge(plate_list_box.min - scale * Vec3d(horizontal_radius, horizontal_radius, 0));
        expand_part_plate_list_box.merge(plate_list_box.max + scale * Vec3d(horizontal_radius, horizontal_radius, 0));
    }
    for (const GLVolume *volume : m_volumes.volumes) {
        if (!m_apply_zoom_to_volumes_filter || ((volume != nullptr) && volume->zoom_to_volumes)) {
            const auto v_bb     = volume->transformed_bounding_box();
            if (is_limit && !expand_part_plate_list_box.overlap(v_bb))
                continue;
            if (v_bb.max_size() > 100000) {//unit::mm more than 100m
                continue;
            }
            bb.merge(v_bb);
        }
    }
    return bb;
}

bool GLCanvas3D::is_volumes_limit_to_expand_plate() const {
    return m_canvas_type != ECanvasType::CanvasAssembleView;
}

BoundingBoxf3 GLCanvas3D::scene_bounding_box() const
{
    BoundingBoxf3 bb = volumes_bounding_box(is_volumes_limit_to_expand_plate());
    bb.merge(m_bed.extended_bounding_box());
    double h = m_bed.build_volume().printable_height();
    //FIXME why -h?
    bb.min.z() = std::min(bb.min.z(), -h);
    bb.max.z() = std::max(bb.max.z(), h);

    //BBS merge plate scene bounding box
    if (m_canvas_type == ECanvasType::CanvasView3D) {
        PartPlateList& plate = wxGetApp().plater()->get_partplate_list();
        bb.merge(plate.get_bounding_box());
    }

    return bb;
}

BoundingBoxf3 GLCanvas3D::plate_scene_bounding_box(int plate_idx) const
{
    PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_plate(plate_idx);

    BoundingBoxf3 bb = plate->get_bounding_box(true);
    if (m_config != nullptr) {
        double h = m_config->opt_float("printable_height");
        bb.min(2) = std::min(bb.min(2), -h);
        bb.max(2) = std::max(bb.max(2), h);
    }

    return bb;
}

bool GLCanvas3D::is_layers_editing_enabled() const
{
    return m_layers_editing.is_enabled();
}

bool GLCanvas3D::is_layers_editing_allowed() const
{
    return m_layers_editing.is_allowed();
}

void GLCanvas3D::reset_layer_height_profile()
{
    wxGetApp().plater()->take_snapshot("Variable layer height - Reset");
    m_layers_editing.reset_layer_height_profile(*this);
    m_layers_editing.state = LayersEditing::Completed;
    m_dirty = true;
}

void GLCanvas3D::adaptive_layer_height_profile(float quality_factor)
{
    wxGetApp().plater()->take_snapshot("Variable layer height - Adaptive");
    m_layers_editing.adaptive_layer_height_profile(*this, quality_factor);
    m_layers_editing.state = LayersEditing::Completed;
    m_dirty = true;
}

void GLCanvas3D::smooth_layer_height_profile(const HeightProfileSmoothingParams& smoothing_params)
{
    wxGetApp().plater()->take_snapshot("Variable layer height - Smooth all");
    m_layers_editing.smooth_layer_height_profile(*this, smoothing_params);
    m_layers_editing.state = LayersEditing::Completed;
    m_dirty = true;
}

bool GLCanvas3D::is_reload_delayed() const
{
    return m_reload_delayed;
}

void GLCanvas3D::enable_layers_editing(bool enable)
{
    m_layers_editing.set_enabled(enable);
    set_as_dirty();
}

void GLCanvas3D::enable_legend_texture(bool enable)
{
    m_gcode_viewer.enable_legend(enable);
}

void GLCanvas3D::enable_picking(bool enable)
{
    m_picking_enabled = enable;
    m_selection.set_mode(Selection::Instance);
}

void GLCanvas3D::enable_moving(bool enable)
{
    m_moving_enabled = enable;
}

void GLCanvas3D::enable_gizmos(bool enable)
{
    m_gizmos.set_enabled(enable);
}

void GLCanvas3D::enable_selection(bool enable)
{
    m_selection.set_enabled(enable);
}

void GLCanvas3D::enable_main_toolbar(bool enable)
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return;
    }
    p_main_toolbar->set_enabled(enable);
}

void GLCanvas3D::reset_select_plate_toolbar_selection() {
    if (m_sel_plate_toolbar.m_all_plates_stats_item)
        m_sel_plate_toolbar.m_all_plates_stats_item->selected = false;
    if (wxGetApp().mainframe)
        wxGetApp().mainframe->update_slice_print_status(MainFrame::eEventSliceUpdate, true, true);
}

void GLCanvas3D::enable_select_plate_toolbar(bool enable)
{
    m_sel_plate_toolbar.set_enabled(enable);
}

void GLCanvas3D::enable_return_toolbar(bool enable)
{
    m_return_toolbar.set_enabled(enable);
}

void GLCanvas3D::enable_dynamic_background(bool enable)
{
    m_dynamic_background_enabled = enable;
}

void GLCanvas3D::allow_multisample(bool allow)
{
    m_multisample_allowed = allow;
}

void GLCanvas3D::zoom_to_bed()
{
    BoundingBoxf3 box = m_bed.build_volume().bounding_volume();
    box.min.z() = 0.0;
    box.max.z() = 0.0;
    _zoom_to_box(box, DefaultCameraZoomToBedMarginFactor);
}

void GLCanvas3D::zoom_to_volumes()
{
    m_apply_zoom_to_volumes_filter = true;
    _zoom_to_box(volumes_bounding_box(is_volumes_limit_to_expand_plate()));
    m_apply_zoom_to_volumes_filter = false;
}

void GLCanvas3D::zoom_to_selection()
{
    if (!m_selection.is_empty())
        _zoom_to_box(m_selection.get_bounding_box());
}

void GLCanvas3D::zoom_to_gcode()
{
    _zoom_to_box(m_gcode_viewer.get_paths_bounding_box(), 1.05);
}

void GLCanvas3D::zoom_to_plate(int plate_idx)
{
    BoundingBoxf3 box;
    if (plate_idx == REQUIRES_ZOOM_TO_ALL_PLATE) {
        box = wxGetApp().plater()->get_partplate_list().get_bounding_box();
        box.min.z() = 0.0;
        box.max.z() = 0.0;
        _zoom_to_box(box, DefaultCameraZoomToPlateMarginFactor);
    } else {
        PartPlate* plate = nullptr;
        if (plate_idx == REQUIRES_ZOOM_TO_CUR_PLATE) {
            plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
        }else {
            assert(plate_idx >= 0 && plate_idx < wxGetApp().plater()->get_partplate_list().get_plate_count());
            plate = wxGetApp().plater()->get_partplate_list().get_plate(plate_idx);
        }
        box = plate->get_bounding_box(true);
        box.min.z() = 0.0;
        box.max.z() = 0.0;
        _zoom_to_box(box, DefaultCameraZoomToPlateMarginFactor);
    }
}

void GLCanvas3D::select_view(const std::string& direction)
{
    get_active_camera().select_view(direction);
    if (m_canvas != nullptr)
        m_canvas->Refresh();
}

void GLCanvas3D::select_plate()
{
    wxGetApp().plater()->get_partplate_list().select_plate_view();
    if (m_canvas != nullptr)
        m_canvas->Refresh();
}

void GLCanvas3D::update_volumes_colors_by_extruder()
{
    if (m_config != nullptr)
        m_volumes.update_colors_by_extruder(m_config);
}

int GLCanvas3D::get_main_toolbar_offset() const
{
    const float cnv_width              = get_canvas_size().get_width();
    const float collapse_toolbar_width = get_collapse_toolbar_width() * 2;

    float main_toolbar_width = 0.0f;
    const auto& p_main_toolbar = get_main_toolbar();
    if (p_main_toolbar) {
        main_toolbar_width = p_main_toolbar->get_width();
    }
    const float toolbar_total_width = main_toolbar_width + collapse_toolbar_width;

    if (cnv_width < toolbar_total_width) {
        return is_collapse_toolbar_on_left() ? collapse_toolbar_width : 0;
    } else {
        const float offset = (cnv_width - toolbar_total_width) / 2.f;
        return is_collapse_toolbar_on_left() ? offset + collapse_toolbar_width : offset;
    }
}

float GLCanvas3D::get_main_toolbar_left(int cnv_width,float inv_zoom) const {
    return (-0.5f * cnv_width + get_main_toolbar_offset()) * inv_zoom;
}

int GLCanvas3D::get_main_toolbar_height() const
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return 0;
    }
    return p_main_toolbar->get_height();
}

int GLCanvas3D::get_main_toolbar_width() const
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return 0;
    }
    return p_main_toolbar->get_width();
}

float GLCanvas3D::get_main_toolbar_scale() const
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return 1.0f;
    }
    return p_main_toolbar->get_scale();
}

bool GLCanvas3D::is_collapse_toolbar_on_left() const
{
    auto state = wxGetApp().plater()->get_sidebar_docking_state();
    return state == Sidebar::Left;
}

float GLCanvas3D::get_collapse_toolbar_width() const
{
    GLToolbar &collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();
    const auto state            = wxGetApp().plater()->get_sidebar_docking_state();

    return state != Sidebar::None ? collapse_toolbar.get_width() : 0;
}

float GLCanvas3D::get_collapse_toolbar_height() const
{
    GLToolbar &collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();
    const auto state            = wxGetApp().plater()->get_sidebar_docking_state();

    return state != Sidebar::None ? collapse_toolbar.get_height() : 0;
}

bool GLCanvas3D::make_current_for_postinit() {
    return _set_current(true);
}

void GLCanvas3D::mark_context_dirty()
{
    m_dirty_context = true;
}

void GLCanvas3D::render(bool only_init)
{
    if (m_in_render) {
        // if called recursively, return
        m_dirty = true;
        return;
    }

    m_in_render = true;
    Slic3r::ScopeGuard in_render_guard([this]() { m_in_render = false; });
    (void)in_render_guard;

    if (m_canvas == nullptr)
        return;

    //BBS: add enable_render
    if (!m_enable_render)
        return;

    // ensures this canvas is current and initialized
    if (!_is_shown_on_screen() || !_set_current(true) || !wxGetApp().init_opengl())
        return;

    if (!is_initialized() && !init())
        return;
    if (m_canvas_type == ECanvasType::CanvasView3D  && m_gizmos.get_current_type() == GLGizmosManager::Undefined) {
        enable_return_toolbar(false);
    }
    if (m_canvas_type == ECanvasType::CanvasPreview)
        m_gcode_viewer.init(wxGetApp().get_mode(), wxGetApp().preset_bundle);

    if (! m_bed.build_volume().valid()) {
        // this happens at startup when no data is still saved under <>\AppData\Roaming\Slic3rPE
        post_event(SimpleEvent(EVT_GLCANVAS_UPDATE_BED_SHAPE));
        return;
    }

    wxGetApp().set_picking_effect(EPickingEffect::Silhouette);
    ColorRGB picking_color;
    if (m_canvas_type == ECanvasType::CanvasAssembleView) {
        picking_color.set(0, 1.0f);
        picking_color.set(1, 1.0f);
        picking_color.set(2, 0.0f);
    }
    else {
        picking_color.set(0, 1.0f);
        picking_color.set(1, 1.0f);
        picking_color.set(2, 1.0f);
    }
    wxGetApp().set_picking_color(picking_color);

    if (only_init)
        return;

#if ENABLE_ENVIRONMENT_MAP
    if (wxGetApp().is_editor())
        wxGetApp().plater()->init_environment_texture();
#endif // ENABLE_ENVIRONMENT_MAP

    const Size& cnv_size = get_canvas_size();
    // Probably due to different order of events on Linux/GTK2, when one switched from 3D scene
    // to preview, this was called before canvas had its final size. It reported zero width
    // and the viewport was set incorrectly, leading to tripping glAsserts further down
    // the road (in apply_projection). That's why the minimum size is forced to 10.
    Camera& camera = get_active_camera();
    camera.apply_viewport(0, 0, std::max(10u, (unsigned int)cnv_size.get_width()), std::max(10u, (unsigned int)cnv_size.get_height()));

    if (camera.requires_zoom_to_bed) {
        zoom_to_bed();
        _resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());
        camera.requires_zoom_to_bed = false;
    }

    if (camera.requires_zoom_to_plate > REQUIRES_ZOOM_TO_PLATE_IDLE) {
        zoom_to_plate(camera.requires_zoom_to_plate);
        _resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());
        camera.requires_zoom_to_plate = REQUIRES_ZOOM_TO_PLATE_IDLE;
    }

    if (camera.requires_zoom_to_volumes) {
        zoom_to_volumes();
        _resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());
        camera.requires_zoom_to_volumes = false;
    }

    camera.apply_projection(_max_bounding_box(true, true, true,is_volumes_limit_to_expand_plate()));
    camera.update_frustum();

    m_frame_callback_list.clear();

    const std::array<int, 4>& viewport = camera.get_viewport();
    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    if (!p_ogl_manager) {
        return;
    }

    if (m_main_toolbar) {
        if (m_canvas_type == ECanvasType::CanvasView3D) {
            const auto toolbar_style = p_ogl_manager->get_toolbar_rendering_style();
            m_main_toolbar->set_rendering_mode(static_cast<GLToolbar::EToolbarRenderingMode>(toolbar_style));
        }
    }

    auto& ogl_manager = *p_ogl_manager;
    ogl_manager.set_viewport_size(viewport[2], viewport[3]);

    ogl_manager.bind_vao();

    wxGetApp().imgui()->new_frame();

    if (m_picking_enabled) {
        if (m_rectangle_selection.is_dragging())
            // picking pass using rectangle selection
            _rectangular_selection_picking_pass();
        //BBS: enable picking when no volumes for partplate logic
        //else if (!m_volumes.empty())
        else
            // regular picking pass
            _picking_pass();
    }
    glsafe(::glViewport(viewport[0], viewport[1], viewport[2], viewport[3]));
    EPickingEffect picking_effect = wxGetApp().get_picking_effect();
    if (EPickingEffect::Disabled != picking_effect) {
        if (!ogl_manager.are_framebuffers_supported()) {
            picking_effect = EPickingEffect::StencilOutline; // use stencil outline as framebuffer not supported yet.
        }
        if (!m_gizmos.is_allow_show_volume_highlight_outline()) {
            picking_effect = EPickingEffect::Disabled;
        }
    }

    const bool off_screen_rendering_enabled = ogl_manager.is_fxaa_enabled();

    if (m_picking_enabled && EPickingEffect::Silhouette == picking_effect) {
        _render_silhouette_effect();
    }

    std::string write_to_framebuffer_name{};
    if (off_screen_rendering_enabled) {
        write_to_framebuffer_name = "mainframe";
        OpenGLManager::FrameBufferModifier main_frame(ogl_manager, write_to_framebuffer_name, ogl_manager.get_msaa_type());
    }
    else {
        write_to_framebuffer_name = OpenGLManager::s_back_frame;
        OpenGLManager::FrameBufferModifier main_frame(ogl_manager, write_to_framebuffer_name);
    }

#if ENABLE_RENDER_PICKING_PASS
    if (!m_picking_enabled || !m_show_picking_texture) {
#endif // ENABLE_RENDER_PICKING_PASS
    // draw scene
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    _render_background();

    //BBS add partplater rendering logic
    bool only_current = false, only_body = false, show_axes = true, no_partplate = false;
    bool show_grid = true;
    GLGizmosManager::EType gizmo_type = m_gizmos.get_current_type();
    if (m_canvas_type != CanvasView3D || m_gizmos.is_show_only_active_plate()) {
        //only_body = true;
        if (m_gizmos.get_object_located_outside_plate()) {
            no_partplate = true;
        }
        else {
            only_current = true;
        }
    } else if (m_gizmos.get_current_type() == GLGizmosManager::EType::Text) {
        if (m_gizmos.is_only_text_volume()) {
            only_current = false;
        } else {
            only_current = true;
        }
    } else if (m_gizmos.is_paint_gizmo())
        no_partplate = true;
    else if (gizmo_type == GLGizmosManager::BrimEars && !camera.is_looking_downward())
        show_grid = false;

    /* view3D render*/
    int hover_id = (m_hover_plate_idxs.size() > 0) ? m_hover_plate_idxs.front() : -1;
    bool b_with_stencil_outline = !m_gizmos.is_running() && (EPickingEffect::StencilOutline == picking_effect);
    if (m_canvas_type == ECanvasType::CanvasView3D) {
        //BBS: add outline logic
        _render_objects(m_volumes,GLVolumeCollection::ERenderType::Opaque, b_with_stencil_outline);
        if (!m_paint_outline_volumes.empty()) {
            _render_objects(m_paint_outline_volumes, GLVolumeCollection::ERenderType::Opaque, b_with_stencil_outline,true);
        }
        _render_sla_slices();
        _render_selection();
        if (!no_partplate)
            _render_bed(!camera.is_looking_downward(), show_axes);
        if (!no_partplate) //BBS: add outline logic
            _render_platelist(!camera.is_looking_downward(), only_current, only_body, hover_id, true, show_grid);
        _render_objects(m_volumes, GLVolumeCollection::ERenderType::Transparent, b_with_stencil_outline);
        _render_objects(m_paint_outline_volumes, GLVolumeCollection::ERenderType::Transparent, b_with_stencil_outline, true);
    }
    /* preview render */
    else if (m_canvas_type == ECanvasType::CanvasPreview && m_render_preview) {
        _render_objects(m_volumes, GLVolumeCollection::ERenderType::Opaque, b_with_stencil_outline);
        _render_sla_slices();
        _render_selection();
        _render_bed(!camera.is_looking_downward(), show_axes);
        _render_platelist(!camera.is_looking_downward(), only_current, true, hover_id);
        // BBS: GUI refactor: add canvas size as parameters
        _render_gcode(cnv_size.get_width(), cnv_size.get_height());
    }
    /* assemble render*/
    else if (m_canvas_type == ECanvasType::CanvasAssembleView) {
        //BBS: add outline logic
        if (m_show_world_axes) {
            m_axes.render();
        }
        _render_objects(m_volumes, GLVolumeCollection::ERenderType::Opaque, b_with_stencil_outline);
        //_render_bed(!camera.is_looking_downward(), show_axes);
        _render_plane();
        //BBS: add outline logic insteadof selection under assemble view
        //_render_selection();
        // BBS: add outline logic
        _render_objects(m_volumes, GLVolumeCollection::ERenderType::Transparent, b_with_stencil_outline);
    }

     if (m_picking_enabled && EPickingEffect::Silhouette == picking_effect) {
         _composite_silhouette_effect();
     }

    _render_sequential_clearance();
#if ENABLE_RENDER_SELECTION_CENTER
    _render_selection_center();
#endif // ENABLE_RENDER_SELECTION_CENTER

    // sidebar hints need to be rendered before the gizmos because the depth buffer
    // could be invalidated by the following gizmo render methods
    _render_selection_sidebar_hints();
    _render_current_gizmo();

    _rebuild_postprocessing_pipeline(p_ogl_manager, write_to_framebuffer_name, OpenGLManager::s_back_frame, viewport[2], viewport[3]);

#if ENABLE_RENDER_PICKING_PASS
    }
#endif // ENABLE_RENDER_PICKING_PASS

#if ENABLE_SHOW_CAMERA_TARGET
    _render_camera_target();
#endif // ENABLE_SHOW_CAMERA_TARGET
    if (m_picking_enabled && m_rectangle_selection.is_dragging())
        m_rectangle_selection.render(*this);

    // draw overlays
    _render_overlays();

    if (wxGetApp().plater()->is_render_statistic_dialog_visible()) {
        ImGui::ShowMetricsWindow();

        ImGuiWrapper& imgui = *wxGetApp().imgui();
        imgui.begin(std::string("Render statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        imgui.text("FPS (SwapBuffers() calls per second):");
        ImGui::SameLine();
        imgui.text(std::to_string(m_render_stats.get_fps_and_reset_if_needed()));
        ImGui::Separator();
        imgui.text("Compressed textures:");
        ImGui::SameLine();
        imgui.text(OpenGLManager::are_compressed_textures_supported() ? "supported" : "not supported");
        imgui.text("Max texture size:");
        ImGui::SameLine();
        imgui.text(std::to_string(OpenGLManager::get_gl_info().get_max_tex_size()));
        imgui.end();
    }

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    if (wxGetApp().is_editor() && wxGetApp().plater()->is_view3D_shown())
        wxGetApp().plater()->render_project_state_debug_window();
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

    if (wxGetApp().plater()->is_render_statistic_dialog_visible()) {
        camera.debug_render();
        camera.debug_frustum();
    }

#if ENABLE_IMGUI_STYLE_EDITOR
    if (wxGetApp().get_mode() == ConfigOptionMode::comDevelop)
        _render_style_editor();
#endif


    std::string tooltip;

	// Negative coordinate means out of the window, likely because the window was deactivated.
	// In that case the tooltip should be hidden.
    if (m_mouse.position.x() >= 0. && m_mouse.position.y() >= 0.) {
        if (tooltip.empty())
            tooltip = m_layers_editing.get_tooltip(*this);

	    if (tooltip.empty())
	        tooltip = m_gizmos.get_tooltip();

        if (tooltip.empty()) {
            const auto& p_main_toolbar = get_main_toolbar();
            if (p_main_toolbar) {
                tooltip = p_main_toolbar->get_tooltip();
            }
        }

	    if (tooltip.empty())
            tooltip = wxGetApp().plater()->get_collapse_toolbar().get_tooltip();

        // BBS
#if 0
	    if (tooltip.empty())
            tooltip = wxGetApp().plater()->get_view_toolbar().get_tooltip();
#endif
    }

    set_tooltip(tooltip);

    if (m_tooltip_enabled)
        m_tooltip.render(m_mouse.position, *this);

    wxGetApp().plater()->get_mouse3d_controller().render_settings_dialog(*this);

    float right_margin = SLIDER_DEFAULT_RIGHT_MARGIN;
    float bottom_margin = SLIDER_DEFAULT_BOTTOM_MARGIN;
    if (m_canvas_type == ECanvasType::CanvasPreview) {
        right_margin = SLIDER_RIGHT_MARGIN;
        bottom_margin = SLIDER_BOTTOM_MARGIN;
    }
    wxGetApp().plater()->get_notification_manager()->render_notifications(*this, get_overlay_window_width(), bottom_margin, right_margin);
    if (m_canvas_type != ECanvasType::CanvasAssembleView) {
        wxGetApp().plater()->get_dailytips()->render();
    }

    wxGetApp().imgui()->render();
    ogl_manager.unbind_vao();
    ogl_manager.clear_dirty();
    m_canvas->SwapBuffers();

    for (const auto& cb : m_frame_callback_list) {
        cb();
    }

    m_render_stats.increment_fps_counter();
}

void GLCanvas3D::render_thumbnail(ThumbnailData &         thumbnail_data,
                                  unsigned int            w,
                                  unsigned int            h,
                                  const ThumbnailsParams &thumbnail_params,
                                  Camera::EType           camera_type,
                                  Camera::ViewAngleType   camera_view_angle_type,
                                  bool                    for_picking,
                                  bool                    ban_light)
{
    ModelObjectPtrs &model_objects = GUI::wxGetApp().model().objects;
    std::vector<std::array<float, 4>> colors = wxGetApp().plater()->get_extruders_colors();
    render_thumbnail(thumbnail_data, colors, w, h, thumbnail_params, model_objects, m_volumes, camera_type, camera_view_angle_type, for_picking, ban_light);
}

void GLCanvas3D::render_thumbnail(ThumbnailData &                    thumbnail_data,
                                  std::vector<std::array<float, 4>> &extruder_colors,
                                  unsigned int                       w,
                                  unsigned int                       h,
                                  const ThumbnailsParams &           thumbnail_params,
                                  ModelObjectPtrs &                  model_objects,
                                  const GLVolumeCollection &         volumes,
                                  Camera::EType                      camera_type,
                                  Camera::ViewAngleType              camera_view_angle_type,
                                  bool                               for_picking,
                                  bool                               ban_light)
{
    const auto& shader = wxGetApp().get_shader("thumbnail");
    if (!shader) {
        return;
    }

    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    p_ogl_manager->bind_vao();
    p_ogl_manager->bind_shader(shader);

    render_thumbnail_framebuffer(p_ogl_manager, thumbnail_data, w, h, thumbnail_params, wxGetApp().plater()->get_partplate_list(), model_objects, volumes, extruder_colors, shader,
        camera_type, camera_view_angle_type, for_picking, ban_light);

    p_ogl_manager->unbind_shader();
    p_ogl_manager->unbind_vao();
}

void GLCanvas3D::render_calibration_thumbnail(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params)
{
    //load current plate gcode
    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    if (!p_ogl_manager) {
        return;
    }
    m_gcode_viewer.render_calibration_thumbnail(thumbnail_data, w, h, thumbnail_params,
        wxGetApp().plater()->get_partplate_list(), *p_ogl_manager);
}

//BBS
void GLCanvas3D::select_curr_plate_all()
{
    m_selection.add_curr_plate();
    m_dirty = true;
}

void GLCanvas3D::select_object_from_idx(std::vector<int>& object_idxs) {
    m_selection.add_object_from_idx(object_idxs);
    m_dirty = true;
}

//BBS
void GLCanvas3D::remove_curr_plate_all()
{
    m_selection.remove_curr_plate();
    m_dirty = true;
}

void GLCanvas3D::select_all()
{
    if (!m_gizmos.is_allow_select_all()) {
        return;
    }
    m_selection.add_all();
    m_dirty = true;
}

void GLCanvas3D::deselect_all()
{
    m_selection.remove_all();
    // BBS
    //wxGetApp().obj_manipul()->set_dirty();
    m_gizmos.reset_all_states();
    m_gizmos.update_data();
    post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
}

void GLCanvas3D::exit_gizmo() {
    if (m_gizmos.get_current_type() != GLGizmosManager::Undefined) {
        m_gizmos.reset_all_states();
        m_gizmos.update_data();
    }
}

void GLCanvas3D::set_selected_visible(bool visible)
{
    for (unsigned int i : m_selection.get_volume_idxs()) {
        GLVolume* volume = const_cast<GLVolume*>(m_selection.get_volume(i));
        volume->visible = visible;
        volume->color[3] = visible ? 1.f : GLVolume::MODEL_HIDDEN_COL[3];
        volume->render_color[3] = volume->color[3];
        volume->force_native_color = !visible;
    }
    m_dirty = true;
}

void GLCanvas3D::delete_selected()
{
    m_selection.erase();
}

void GLCanvas3D::ensure_on_bed(unsigned int object_idx, bool allow_negative_z)
{
    //BBS if asseble view canvas
    if (m_canvas_type == ECanvasType::CanvasAssembleView) {
        return;
    }

    if (allow_negative_z)
        return;

    typedef std::map<std::pair<int, int>, double> InstancesToZMap;
    InstancesToZMap instances_min_z;

    for (GLVolume* volume : m_volumes.volumes) {
        if (volume->object_idx() == (int)object_idx && !volume->is_modifier) {
            double min_z = volume->transformed_convex_hull_bounding_box().min.z();
            std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
            InstancesToZMap::iterator it = instances_min_z.find(instance);
            if (it == instances_min_z.end())
                it = instances_min_z.insert(InstancesToZMap::value_type(instance, DBL_MAX)).first;

            it->second = std::min(it->second, min_z);
        }
    }

    for (GLVolume* volume : m_volumes.volumes) {
        std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
        InstancesToZMap::iterator it = instances_min_z.find(instance);
        if (it != instances_min_z.end())
            volume->set_instance_offset(Z, volume->get_instance_offset(Z) - it->second);
    }
}


const std::vector<double>& GLCanvas3D::get_gcode_layers_zs() const
{
    return m_gcode_viewer.get_layers_zs();
}

std::vector<double> GLCanvas3D::get_volumes_print_zs(bool active_only) const
{
    return m_volumes.get_current_print_zs(active_only);
}

void GLCanvas3D::set_gcode_options_visibility_from_flags(unsigned int flags)
{
    m_gcode_viewer.set_options_visibility_from_flags(flags);
}

void GLCanvas3D::set_volumes_z_range(const std::array<double, 2>& range)
{
    m_volumes.set_range(range[0] - 1e-6, range[1] + 1e-6);
}

std::vector<int> GLCanvas3D::load_object(const ModelObject& model_object, int obj_idx, std::vector<int> instance_idxs, bool lod_enabled)
{
    if (instance_idxs.empty()) {
        for (unsigned int i = 0; i < model_object.instances.size(); ++i) {
            instance_idxs.emplace_back(i);
        }
    }
    return m_volumes.load_object(&model_object, obj_idx, instance_idxs, m_color_by, m_initialized, lod_enabled);
}

std::vector<int> GLCanvas3D::load_object(const Model& model, int obj_idx, bool lod_enabled)
{
    if (0 <= obj_idx && obj_idx < (int)model.objects.size()) {
        const ModelObject* model_object = model.objects[obj_idx];
        if (model_object != nullptr)
            return load_object(*model_object, obj_idx, std::vector<int>(), lod_enabled);
    }

    return std::vector<int>();
}

void GLCanvas3D::mirror_selection(Axis axis)
{
    TransformationType transformation_type;
    //transformation_type.set_world();
    transformation_type.set_relative();
    m_selection.setup_cache();
    m_selection.mirror(axis, transformation_type);
    do_mirror(L("Mirror Object"));
    // BBS
    //wxGetApp().obj_manipul()->set_dirty();
}

// Reload the 3D scene of
// 1) Model / ModelObjects / ModelInstances / ModelVolumes
// 2) Print bed
// 3) SLA support meshes for their respective ModelObjects / ModelInstances
// 4) Wipe tower preview
// 5) Out of bed collision status & message overlay (texture)
void GLCanvas3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas == nullptr || m_config == nullptr || m_model == nullptr)
        return;

    if (!m_initialized)
        return;

    _set_current(true);

    m_hover_volume_idxs.clear();

    struct ModelVolumeState {
        ModelVolumeState(const GLVolume* volume) :
            model_volume(nullptr), geometry_id(volume->geometry_id), volume_idx(-1) {}
        ModelVolumeState(const ModelVolume* model_volume, const ObjectID& instance_id, const GLVolume::CompositeID& composite_id) :
            model_volume(model_volume), geometry_id(std::make_pair(model_volume->id().id, instance_id.id)), composite_id(composite_id), volume_idx(-1) {}
        ModelVolumeState(const ObjectID& volume_id, const ObjectID& instance_id) :
            model_volume(nullptr), geometry_id(std::make_pair(volume_id.id, instance_id.id)), volume_idx(-1) {}
        bool new_geometry() const { return this->volume_idx == size_t(-1); }
        const ModelVolume* model_volume;
        // ObjectID of ModelVolume + ObjectID of ModelInstance
        // or timestamp of an SLAPrintObjectStep + ObjectID of ModelInstance
        std::pair<size_t, size_t>   geometry_id;
        GLVolume::CompositeID       composite_id;
        // Volume index in the new GLVolume vector.
        size_t                      volume_idx;
    };
    std::vector<ModelVolumeState> model_volume_state;
    std::vector<ModelVolumeState> aux_volume_state;

    struct GLVolumeState {
        GLVolumeState() :
            volume_idx(size_t(-1)) {}
        GLVolumeState(const GLVolume* volume, unsigned int volume_idx) :
            composite_id(volume->composite_id), volume_idx(volume_idx) {}
        GLVolumeState(const GLVolume::CompositeID &composite_id) :
            composite_id(composite_id), volume_idx(size_t(-1)) {}

        GLVolume::CompositeID       composite_id;
        // Volume index in the old GLVolume vector.
        size_t                      volume_idx;
    };

    // SLA steps to pull the preview meshes for.
	typedef std::array<SLAPrintObjectStep, 3> SLASteps;
    SLASteps sla_steps = { slaposDrillHoles, slaposSupportTree, slaposPad };
    struct SLASupportState {
        std::array<PrintStateBase::StateWithTimeStamp, std::tuple_size<SLASteps>::value> step;
    };
    // State of the sla_steps for all SLAPrintObjects.
    std::vector<SLASupportState>   sla_support_state;

    std::vector<size_t> instance_ids_selected;
    std::vector<size_t> map_glvolume_old_to_new(m_volumes.volumes.size(), size_t(-1));
    std::vector<GLVolumeState> deleted_volumes;
    // BBS
    std::vector<GLVolumeState> deleted_wipe_towers;
    std::vector<GLVolume*> glvolumes_new;
    glvolumes_new.reserve(m_volumes.volumes.size());
    auto model_volume_state_lower = [](const ModelVolumeState& m1, const ModelVolumeState& m2) { return m1.geometry_id < m2.geometry_id; };

    m_reload_delayed = !m_canvas->IsShown() && !refresh_immediately && !force_full_scene_refresh;

    PrinterTechnology printer_technology = current_printer_technology();

    // BBS: support wipe tower for multi-plates
    PartPlateList& ppl = wxGetApp().plater()->get_partplate_list();
    int n_plates = ppl.get_plate_count();
    std::vector<int> volume_idxs_wipe_tower_old(n_plates, -1);

    // Release invalidated volumes to conserve GPU memory in case of delayed refresh (see m_reload_delayed).
    // First initialize model_volumes_new_sorted & model_instances_new_sorted.
    for (int object_idx = 0; object_idx < (int)m_model->objects.size(); ++object_idx) {
        const ModelObject* model_object = m_model->objects[object_idx];
        for (int instance_idx = 0; instance_idx < (int)model_object->instances.size(); ++instance_idx) {
            const ModelInstance* model_instance = model_object->instances[instance_idx];
            for (int volume_idx = 0; volume_idx < (int)model_object->volumes.size(); ++volume_idx) {
                const ModelVolume* model_volume = model_object->volumes[volume_idx];
                if (m_canvas_type == ECanvasType::CanvasAssembleView) {
                    if (model_volume->is_model_part())
                        model_volume_state.emplace_back(model_volume, model_instance->id(), GLVolume::CompositeID(object_idx, volume_idx, instance_idx));
                }
                else {
                    model_volume_state.emplace_back(model_volume, model_instance->id(), GLVolume::CompositeID(object_idx, volume_idx, instance_idx));
                }
            }
        }
    }
    if (printer_technology == ptSLA) {
        const SLAPrint* sla_print = this->sla_print();
#ifndef NDEBUG
        // Verify that the SLAPrint object is synchronized with m_model.
        check_model_ids_equal(*m_model, sla_print->model());
#endif /* NDEBUG */
        sla_support_state.reserve(sla_print->objects().size());
        for (const SLAPrintObject* print_object : sla_print->objects()) {
            SLASupportState state;
            for (size_t istep = 0; istep < sla_steps.size(); ++istep) {
                state.step[istep] = print_object->step_state_with_timestamp(sla_steps[istep]);
                if (state.step[istep].state == PrintStateBase::DONE) {
                    if (!print_object->has_mesh(sla_steps[istep]))
                        // Consider the DONE step without a valid mesh as invalid for the purpose
                        // of mesh visualization.
                        state.step[istep].state = PrintStateBase::INVALID;
                    else if (sla_steps[istep] != slaposDrillHoles)
                        for (const ModelInstance* model_instance : print_object->model_object()->instances)
                            // Only the instances, which are currently printable, will have the SLA support structures kept.
                            // The instances outside the print bed will have the GLVolumes of their support structures released.
                            if (model_instance->is_printable())
                                aux_volume_state.emplace_back(state.step[istep].timestamp, model_instance->id());
                }
            }
            sla_support_state.emplace_back(state);
        }
    }
    std::sort(model_volume_state.begin(), model_volume_state.end(), model_volume_state_lower);
    std::sort(aux_volume_state.begin(), aux_volume_state.end(), model_volume_state_lower);

    // BBS: normalize painting data with current filament count
    for (unsigned int obj_idx = 0; obj_idx < (unsigned int)m_model->objects.size(); ++obj_idx) {
        const ModelObject& model_object = *m_model->objects[obj_idx];
        for (int volume_idx = 0; volume_idx < (int)model_object.volumes.size(); ++volume_idx) {
            ModelVolume& model_volume = *model_object.volumes[volume_idx];
            if (!model_volume.is_model_part())
                continue;

            unsigned int filaments_count = (unsigned int)dynamic_cast<const ConfigOptionStrings*>(m_config->option("filament_colour"))->values.size();
            model_volume.update_extruder_count(filaments_count);
        }
    }

    // Release all ModelVolume based GLVolumes not found in the current Model. Find the GLVolume of a hollowed mesh.
    for (size_t volume_id = 0; volume_id < m_volumes.volumes.size(); ++volume_id) {
        GLVolume* volume = m_volumes.volumes[volume_id];
        ModelVolumeState  key(volume);
        ModelVolumeState* mvs = nullptr;
        if (volume->volume_idx() < 0) {
            auto it = std::lower_bound(aux_volume_state.begin(), aux_volume_state.end(), key, model_volume_state_lower);
            if (it != aux_volume_state.end() && it->geometry_id == key.geometry_id)
                // This can be an SLA support structure that should not be rendered (in case someone used undo
                // to revert to before it was generated). We only reuse the volume if that's not the case.
                if (m_model->objects[volume->composite_id.object_id]->sla_points_status != sla::PointsStatus::NoPoints)
                    mvs = &(*it);
        }
        else {
            auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key, model_volume_state_lower);
            if (it != model_volume_state.end() && it->geometry_id == key.geometry_id)
                mvs = &(*it);
        }
        // Emplace instance ID of the volume. Both the aux volumes and model volumes share the same instance ID.
        // The wipe tower has its own wipe_tower_instance_id().
        if (m_selection.contains_volume(volume_id)) {
            if (m_canvas_type == ECanvasType::CanvasAssembleView) {
                if (!volume->is_modifier)
                    instance_ids_selected.emplace_back(volume->geometry_id.second);
            }
            else {
                instance_ids_selected.emplace_back(volume->geometry_id.second);
            }
        }
        if (mvs == nullptr || force_full_scene_refresh) {
            // This GLVolume will be released.
            if (volume->is_wipe_tower) {
                // There is only one wipe tower.
                //assert(volume_idx_wipe_tower_old == -1);
                int plate_id = volume->composite_id.object_id - 1000;
                if (plate_id < n_plates)
                    volume_idxs_wipe_tower_old[plate_id] = (int)volume_id;
            }
            if (!m_reload_delayed) {
                deleted_volumes.emplace_back(volume, volume_id);
                // BBS
                if (volume->is_wipe_tower)
                    deleted_wipe_towers.emplace_back(volume, volume_id);
                m_volumes.release_volume(volume);
                delete volume;
            }
        }
        else {
            // This GLVolume will be reused.
            volume->set_sla_shift_z(0.0);
            map_glvolume_old_to_new[volume_id] = glvolumes_new.size();
            mvs->volume_idx = glvolumes_new.size();
            glvolumes_new.emplace_back(volume);
            // Update color of the volume based on the current extruder.
            if (mvs->model_volume != nullptr) {
                int extruder_id = mvs->model_volume->extruder_id();
                if (extruder_id != -1)
                    volume->extruder_id = extruder_id;

                volume->is_modifier = !mvs->model_volume->is_model_part();
                volume->set_color(color_from_model_volume(*mvs->model_volume));

                // updates volumes transformations
                if (m_canvas_type != ECanvasType::CanvasAssembleView) {
                    volume->set_instance_transformation(mvs->model_volume->get_object()->instances[mvs->composite_id.instance_id]->get_transformation());
                    volume->set_volume_transformation(mvs->model_volume->get_transformation());
                    // updates volumes convex hull
                    if (mvs->model_volume->is_model_part() && ! volume->convex_hull())
                        // Model volume was likely changed from modifier or support blocker / enforcer to a model part.
                        // Only model parts require convex hulls.
                        volume->set_convex_hull(mvs->model_volume->get_convex_hull_shared_ptr());
                    volume->set_offset_to_assembly(Vec3d(0, 0, 0));
                }
                else {
                    volume->set_instance_transformation(mvs->model_volume->get_object()->instances[mvs->composite_id.instance_id]->get_assemble_transformation());
                    volume->set_volume_transformation(mvs->model_volume->get_transformation());
                    // updates volumes convex hull
                    if (mvs->model_volume->is_model_part() && ! volume->convex_hull())
                        // Model volume was likely changed from modifier or support blocker / enforcer to a model part.
                        // Only model parts require convex hulls.
                        volume->set_convex_hull(mvs->model_volume->get_convex_hull_shared_ptr());
                    volume->set_offset_to_assembly(mvs->model_volume->get_object()->instances[mvs->composite_id.instance_id]->get_offset_to_assembly());
                }
            }
        }
    }
    sort_remove_duplicates(instance_ids_selected);
    auto deleted_volumes_lower = [](const GLVolumeState &v1, const GLVolumeState &v2) { return v1.composite_id < v2.composite_id; };
    std::sort(deleted_volumes.begin(), deleted_volumes.end(), deleted_volumes_lower);

    //BBS clean hover_volume_idxs
    m_hover_volume_idxs.clear();

    if (m_reload_delayed)
        return;

    // BBS: do not check wipe tower changes
    bool update_object_list = false;
    if (deleted_volumes.size() != deleted_wipe_towers.size())
        update_object_list = true;

    if (m_volumes.volumes != glvolumes_new && !update_object_list) {
        int vol_idx = 0;
        for (; vol_idx < std::min(m_volumes.volumes.size(), glvolumes_new.size()); vol_idx++) {
            if (m_volumes.volumes[vol_idx] != glvolumes_new[vol_idx]) {
                update_object_list = true;
                break;
            }
        }
        for (int temp_idx = vol_idx; temp_idx < m_volumes.volumes.size() && !update_object_list; temp_idx++) {
            // Volumes in m_volumes might not exist anymore, so we cannot
            // directly check if they are is_wipe_towers, for which we do
            // not want to update the object list.  Instead, we do a kind of
            // slow thing of seeing if they were in the deleted list, and if
            // so, if they were a wipe tower.
            bool was_deleted_wipe_tower = false;
            for (int del_idx = 0; del_idx < deleted_wipe_towers.size(); del_idx++) {
                if (deleted_wipe_towers[del_idx].volume_idx == temp_idx) {
                    was_deleted_wipe_tower = true;
                    break;
                }
            }
            if (!was_deleted_wipe_tower) {
                update_object_list = true;
            }
        }
        for (int temp_idx = vol_idx; temp_idx < glvolumes_new.size() && !update_object_list; temp_idx++) {
            if (!glvolumes_new[temp_idx]->is_wipe_tower)
                update_object_list = true;
        }
    }
    m_volumes.volumes = std::move(glvolumes_new);
    bool enable_lod   = GUI::wxGetApp().app_config->get_bool("enable_lod") ;
    for (unsigned int obj_idx = 0; obj_idx < (unsigned int)m_model->objects.size(); ++ obj_idx) {
        const ModelObject &model_object = *m_model->objects[obj_idx];
        for (int volume_idx = 0; volume_idx < (int)model_object.volumes.size(); ++ volume_idx) {
			const ModelVolume &model_volume = *model_object.volumes[volume_idx];
            if (m_canvas_type == ECanvasType::CanvasAssembleView && !model_volume.is_model_part())
                continue;
            for (int instance_idx = 0; instance_idx < (int)model_object.instances.size(); ++ instance_idx) {
				const ModelInstance &model_instance = *model_object.instances[instance_idx];
				ModelVolumeState key(model_volume.id(), model_instance.id());
				auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key, model_volume_state_lower);
				assert(it != model_volume_state.end() && it->geometry_id == key.geometry_id);
                if (it->new_geometry()) {
                    // New volume.
                    auto it_old_volume = std::lower_bound(deleted_volumes.begin(), deleted_volumes.end(), GLVolumeState(it->composite_id), deleted_volumes_lower);
                    if (it_old_volume != deleted_volumes.end() && it_old_volume->composite_id == it->composite_id)
                        // If a volume changed its ObjectID, but it reuses a GLVolume's CompositeID, maintain its selection.
                        map_glvolume_old_to_new[it_old_volume->volume_idx] = m_volumes.volumes.size();
                    // Note the index of the loaded volume, so that we can reload the main model GLVolume with the hollowed mesh
                    // later in this function.
                    it->volume_idx = m_volumes.volumes.size();
                    if (enable_lod && CpuMemory::cur_free_memory_less_than_specify_size_gb(LOD_FREE_MEMORY_SIZE)) {
                        enable_lod = false;
                    }
#ifdef __APPLE__
                    if (Slic3r::is_mac_version_15()) {
                        enable_lod = false;
                    }
#endif
                    m_volumes.load_object_volume(&model_object, obj_idx, volume_idx, instance_idx, m_color_by, m_initialized, m_canvas_type == ECanvasType::CanvasAssembleView, false, enable_lod);
                    m_volumes.volumes.back()->geometry_id = key.geometry_id;
                    update_object_list = true;
                } else {
					// Recycling an old GLVolume.
					GLVolume &existing_volume = *m_volumes.volumes[it->volume_idx];
                    assert(existing_volume.geometry_id == key.geometry_id);
					// Update the Object/Volume/Instance indices into the current Model.
					if (existing_volume.composite_id != it->composite_id) {
						existing_volume.composite_id = it->composite_id;
						update_object_list = true;
					}
                }
            }
        }
    }
    if (printer_technology == ptSLA) {
        size_t idx = 0;
        const SLAPrint *sla_print = this->sla_print();
		std::vector<double> shift_zs(m_model->objects.size(), 0);
        double relative_correction_z = sla_print->relative_correction().z();
        if (relative_correction_z <= EPSILON)
            relative_correction_z = 1.;
		for (const SLAPrintObject *print_object : sla_print->objects()) {
            SLASupportState   &state        = sla_support_state[idx ++];
            const ModelObject *model_object = print_object->model_object();
            // Find an index of the ModelObject
            int object_idx;
            // There may be new SLA volumes added to the scene for this print_object.
            // Find the object index of this print_object in the Model::objects list.
            auto it = std::find(sla_print->model().objects.begin(), sla_print->model().objects.end(), model_object);
            assert(it != sla_print->model().objects.end());
			object_idx = it - sla_print->model().objects.begin();
			// Cache the Z offset to be applied to all volumes with this object_idx.
			shift_zs[object_idx] = print_object->get_current_elevation() / relative_correction_z;
            // Collect indices of this print_object's instances, for which the SLA support meshes are to be added to the scene.
            // pairs of <instance_idx, print_instance_idx>
			std::vector<std::pair<size_t, size_t>> instances[std::tuple_size<SLASteps>::value];
            for (size_t print_instance_idx = 0; print_instance_idx < print_object->instances().size(); ++ print_instance_idx) {
                const SLAPrintObject::Instance &instance = print_object->instances()[print_instance_idx];
                // Find index of ModelInstance corresponding to this SLAPrintObject::Instance.
				auto it = std::find_if(model_object->instances.begin(), model_object->instances.end(),
                    [&instance](const ModelInstance *mi) { return mi->id() == instance.instance_id; });
                assert(it != model_object->instances.end());
                int instance_idx = it - model_object->instances.begin();
                for (size_t istep = 0; istep < sla_steps.size(); ++ istep)
                    if (sla_steps[istep] == slaposDrillHoles) {
                    	// Hollowing is a special case, where the mesh from the backend is being loaded into the 1st volume of an instance,
                    	// not into its own GLVolume.
                        // There shall always be such a GLVolume allocated.
                        ModelVolumeState key(model_object->volumes.front()->id(), instance.instance_id);
                        auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key, model_volume_state_lower);
                        assert(it != model_volume_state.end() && it->geometry_id == key.geometry_id);
                        assert(!it->new_geometry());
                        GLVolume &volume = *m_volumes.volumes[it->volume_idx];
                        if (! volume.offsets.empty() && state.step[istep].timestamp != volume.offsets.front()) {
                        	// The backend either produced a new hollowed mesh, or it invalidated the one that the front end has seen.
                            volume.indexed_vertex_array->release_geometry();
                        	if (state.step[istep].state == PrintStateBase::DONE) {
                                TriangleMesh mesh = print_object->get_mesh(slaposDrillHoles);
	                            assert(! mesh.empty());
                                mesh.transform(sla_print->sla_trafo(*m_model->objects[volume.object_idx()]).inverse());
#if ENABLE_SMOOTH_NORMALS
                                volume.indexed_vertex_array->load_mesh(mesh, true);
#else
                                volume.indexed_vertex_array->load_mesh(mesh);
#endif // ENABLE_SMOOTH_NORMALS
                            } else {
	                        	// Reload the original volume.
#if ENABLE_SMOOTH_NORMALS
                                volume.indexed_vertex_array->load_mesh(m_model->objects[volume.object_idx()]->volumes[volume.volume_idx()]->mesh(), true);
#else
                                volume.indexed_vertex_array->load_mesh(m_model->objects[volume.object_idx()]->volumes[volume.volume_idx()]->mesh());
#endif // ENABLE_SMOOTH_NORMALS
                            }
                            volume.finalize_geometry(true);
	                    }
                    	//FIXME it is an ugly hack to write the timestamp into the "offsets" field to not have to add another member variable
                    	// to the GLVolume. We should refactor GLVolume significantly, so that the GLVolume will not contain member variables
                    	// of various concenrs (model vs. 3D print path).
                    	volume.offsets = { state.step[istep].timestamp };
                    } else if (state.step[istep].state == PrintStateBase::DONE) {
                        // Check whether there is an existing auxiliary volume to be updated, or a new auxiliary volume to be created.
						ModelVolumeState key(state.step[istep].timestamp, instance.instance_id.id);
						auto it = std::lower_bound(aux_volume_state.begin(), aux_volume_state.end(), key, model_volume_state_lower);
						assert(it != aux_volume_state.end() && it->geometry_id == key.geometry_id);
                    	if (it->new_geometry()) {
                            // This can be an SLA support structure that should not be rendered (in case someone used undo
                            // to revert to before it was generated). If that's the case, we should not generate anything.
                            if (model_object->sla_points_status != sla::PointsStatus::NoPoints)
                                instances[istep].emplace_back(std::pair<size_t, size_t>(instance_idx, print_instance_idx));
                            else
                                shift_zs[object_idx] = 0.;
                        } else {
                            // Recycling an old GLVolume. Update the Object/Instance indices into the current Model.
                            m_volumes.volumes[it->volume_idx]->composite_id = GLVolume::CompositeID(object_idx, m_volumes.volumes[it->volume_idx]->volume_idx(), instance_idx);
                            m_volumes.volumes[it->volume_idx]->set_instance_transformation(model_object->instances[instance_idx]->get_transformation());
                        }
                    }
            }

            for (size_t istep = 0; istep < sla_steps.size(); ++istep)
                if (!instances[istep].empty())
                    m_volumes.load_object_auxiliary(print_object, object_idx, instances[istep], sla_steps[istep], state.step[istep].timestamp, m_initialized);
        }

		// Shift-up all volumes of the object so that it has the right elevation with respect to the print bed
		for (GLVolume* volume : m_volumes.volumes)
			if (volume->object_idx() < (int)m_model->objects.size() && m_model->objects[volume->object_idx()]->instances[volume->instance_idx()]->is_printable())
				volume->set_sla_shift_z(shift_zs[volume->object_idx()]);
    }

    // BBS
    if (printer_technology == ptFFF && m_config->has("filament_colour") && (m_canvas_type != ECanvasType::CanvasAssembleView)) {
        // Should the wipe tower be visualized ?
        unsigned int filaments_count = (unsigned int)dynamic_cast<const ConfigOptionStrings*>(m_config->option("filament_colour"))->values.size();

        bool wt = dynamic_cast<const ConfigOptionBool*>(m_config->option("enable_prime_tower"))->value;
        auto co = dynamic_cast<const ConfigOptionEnum<PrintSequence>*>(m_config->option<ConfigOptionEnum<PrintSequence>>("print_sequence"));

        const DynamicPrintConfig &dconfig           = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        auto timelapse_type = dconfig.option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
        bool timelapse_enabled = timelapse_type ? (timelapse_type->value == TimelapseType::tlSmooth) : false;

        if (wt && (timelapse_enabled || filaments_count > 1) && !wxGetApp().plater()->only_gcode_mode() && !wxGetApp().plater()->is_gcode_3mf()) {
            for (int plate_id = 0; plate_id < n_plates; plate_id++) {
                // If print ByObject and there is only one object in the plate, the wipe tower is allowed to be generated.
                PartPlate* part_plate = ppl.get_plate(plate_id);
                if (part_plate->get_print_seq() == PrintSequence::ByObject ||
                    (part_plate->get_print_seq() == PrintSequence::ByDefault && co != nullptr && co->value == PrintSequence::ByObject)) {
                    if (ppl.get_plate(plate_id)->printable_instance_size() != 1)
                        continue;
                }

                DynamicPrintConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
                float x = dynamic_cast<const ConfigOptionFloats*>(proj_cfg.option("wipe_tower_x"))->get_at(plate_id);
                float y = dynamic_cast<const ConfigOptionFloats*>(proj_cfg.option("wipe_tower_y"))->get_at(plate_id);
                float w = dynamic_cast<const ConfigOptionFloat*>(m_config->option("prime_tower_width"))->value;
                float a = dynamic_cast<const ConfigOptionFloat*>(proj_cfg.option("wipe_tower_rotation_angle"))->value;
                // BBS
                std::vector<double> v = dynamic_cast<const ConfigOptionFloats*>(m_config->option("filament_prime_volume"))->values;
                Vec3d plate_origin = ppl.get_plate(plate_id)->get_origin();

                const Print* print = m_process->fff_print();
                const Print* current_print = part_plate->fff_print();
                if (!timelapse_enabled && part_plate->get_extruders(true).size() < 2) continue;
                if (part_plate->get_objects_on_this_plate().empty()) continue;

                float brim_width = print->wipe_tower_data(filaments_count).brim_width;
                const DynamicPrintConfig &print_cfg   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
                double wipe_vol = get_max_element(v);
                int nozzle_nums = wxGetApp().preset_bundle->get_printer_extruder_count();
                Vec3d wipe_tower_size = ppl.get_plate(plate_id)->estimate_wipe_tower_size(print_cfg, w, wipe_vol, nozzle_nums);

                {
                    const float                 margin     = WIPE_TOWER_MARGIN;
                    BoundingBoxf3               plate_bbox = part_plate->get_bounding_box();
                    BoundingBoxf                plate_bbox_2d(Vec2d(plate_bbox.min(0), plate_bbox.min(1)), Vec2d(plate_bbox.max(0), plate_bbox.max(1)));
                    const std::vector<Pointfs> &extruder_areas = part_plate->get_extruder_areas();
                    for (Pointfs points : extruder_areas) {
                        BoundingBoxf bboxf(points);
                        plate_bbox_2d.min = plate_bbox_2d.min(0) >= bboxf.min(0) ? plate_bbox_2d.min : bboxf.min;
                        plate_bbox_2d.max = plate_bbox_2d.max(0) <= bboxf.max(0) ? plate_bbox_2d.max : bboxf.max;
                    }

                    coordf_t plate_bbox_x_min_local_coord = plate_bbox_2d.min(0) - plate_origin(0);
                    coordf_t plate_bbox_x_max_local_coord = plate_bbox_2d.max(0) - plate_origin(0);
                    coordf_t plate_bbox_y_max_local_coord = plate_bbox_2d.max(1) - plate_origin(1);

                    if (!current_print->is_step_done(psWipeTower) || !current_print->wipe_tower_data().wipe_tower_mesh_data) {
                        // update for wipe tower position
                        {
                            int volume_idx_wipe_tower_new = m_volumes.load_wipe_tower_preview(1000 + plate_id, x + plate_origin(0), y + plate_origin(1),
                                                                                              (float) wipe_tower_size(0), (float) wipe_tower_size(1), (float) wipe_tower_size(2),
                                                                                              a,
                                                                                              /*!print->is_step_done(psWipeTower)*/ true, brim_width, m_initialized);
                            int volume_idx_wipe_tower_old = volume_idxs_wipe_tower_old[plate_id];
                            if (volume_idx_wipe_tower_old != -1) map_glvolume_old_to_new[volume_idx_wipe_tower_old] = volume_idx_wipe_tower_new;
                        }
                    } else {
                        auto        tower_bottom = current_print->wipe_tower_data().wipe_tower_mesh_data->bottom;
                        tower_bottom.translate(scaled(Vec2d{x, y}));
                        tower_bottom.translate(scaled(Vec2d{plate_origin[0], plate_origin[1]}));
                        auto tower_bottom_bbox = get_extents(tower_bottom);
                        BoundingBoxf3 plate_bbox        = wxGetApp().plater()->get_partplate_list().get_plate(plate_id)->get_build_volume(true);
                        BoundingBox   plate_bbox2d      = BoundingBox(scaled(Vec2f(plate_bbox.min[0], plate_bbox.min[1])), scaled(Vec2f(plate_bbox.max[0], plate_bbox.max[1])));
                        Vec2f         offset            = WipeTower::move_box_inside_box(tower_bottom_bbox, plate_bbox2d, scaled(margin));
                        int volume_idx_wipe_tower_new = m_volumes.load_real_wipe_tower_preview(1000 + plate_id, x + plate_origin(0), y + plate_origin(1),
                                                                                               current_print->wipe_tower_data().wipe_tower_mesh_data->real_wipe_tower_mesh,
                                                                                               current_print->wipe_tower_data().wipe_tower_mesh_data->real_brim_mesh,
                                                                                            true,a,/*!print->is_step_done(psWipeTower)*/ true, m_initialized);
                        int volume_idx_wipe_tower_old = volume_idxs_wipe_tower_old[plate_id];
                        if (volume_idx_wipe_tower_old != -1) map_glvolume_old_to_new[volume_idx_wipe_tower_old] = volume_idx_wipe_tower_new;
                    }
                }
            }
        }
    }

    update_volumes_colors_by_extruder();
    if (m_selection.is_enabled()) {
        // Update selection indices based on the old/new GLVolumeCollection.
        if (m_selection.get_mode() == Selection::Instance)
            m_selection.instances_changed(instance_ids_selected);
        else
            m_selection.volumes_changed(map_glvolume_old_to_new);
    }
    if (m_gizmos.is_enabled()) {
        m_gizmos.update_data();
        m_gizmos.update_assemble_view_data();
        m_gizmos.refresh_on_off_state();
    }

    // Update the toolbar
    //BBS: notify the PartPlateList to reload all objects
    if (update_object_list)
    {
        post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
    }

    //BBS:exclude the assmble view
    if (m_canvas_type != ECanvasType::CanvasAssembleView) {
        _update_slice_error_status();
        // checks for geometry outside the print volume to render it accordingly
        if (!m_volumes.empty()) {
            ModelInstanceEPrintVolumeState state;
            ObjectFilamentResults object_results;
            const bool contained_min_one = m_volumes.check_outside_state(m_bed.build_volume(), &state, &object_results, *m_model);
            const bool partlyOut = (state == ModelInstanceEPrintVolumeState::ModelInstancePVS_Partly_Outside);
            const bool fullyOut = (state == ModelInstanceEPrintVolumeState::ModelInstancePVS_Fully_Outside);
           // const bool objectLimited = (state == ModelInstanceEPrintVolumeState::ModelInstancePVS_Limited);

            PartPlate *cur_plate  = wxGetApp().plater()->get_partplate_list().get_curr_plate();
            bool show_read_wipe_tower = cur_plate->fff_print()->is_step_done(psWipeTower);
            bool       wipe_tower_outside   = m_volumes.check_wipe_tower_outside_state(m_bed.build_volume(), wxGetApp().plater()->get_partplate_list().get_curr_plate_index());
            bool show_wipe_tower_outside_error = show_read_wipe_tower ? !wipe_tower_outside : false;
            _set_warning_notification(EWarning::PrimeTowerOutside, show_wipe_tower_outside_error);

            auto clash_flag = construct_error_string(object_results, get_object_clashed_text());
            auto unprintable_flag= construct_extruder_unprintable_error(object_results, get_left_extruder_unprintable_text(), get_right_extruder_unprintable_text());

            _set_warning_notification(EWarning::ObjectClashed, clash_flag);
            _set_warning_notification(EWarning::LeftExtruderPrintableError, unprintable_flag.first);
            _set_warning_notification(EWarning::RightExtruderPrintableError, unprintable_flag.second);
            //_set_warning_notification(EWarning::ObjectLimited, objectLimited);
            //BBS: turn off the warning when fully outside
            //_set_warning_notification(EWarning::ObjectOutside, fullyOut);
            //if (printer_technology != ptSLA || !contained_min_one)
            //    _set_warning_notification(EWarning::SlaSupportsOutside, false);

            auto full_config_temp = wxGetApp().preset_bundle->full_config();
            bool tpu_valid = cur_plate->check_tpu_printable_status(full_config_temp, wxGetApp().preset_bundle->get_used_tpu_filaments(cur_plate->get_extruders(true)));
            _set_warning_notification(EWarning::TPUPrintableError, !tpu_valid);

            bool filament_printable = cur_plate->check_filament_printable(full_config_temp, filament_printable_error_msg);
            _set_warning_notification(EWarning::FilamentPrintableError, !filament_printable);

            bool mix_pla_and_petg = cur_plate->check_mixture_of_pla_and_petg(full_config_temp);
            _set_warning_notification(EWarning::MixUsePLAAndPETG, !mix_pla_and_petg);

            bool filament_nozzle_compatible = cur_plate->check_compatible_of_nozzle_and_filament(full_config_temp, wxGetApp().preset_bundle->filament_presets, get_nozzle_filament_incompatible_text());
            _set_warning_notification(EWarning::NozzleFilamentIncompatible, !filament_nozzle_compatible);

            bool model_fits = contained_min_one && !m_model->objects.empty() && !partlyOut && object_results.filaments.empty() && tpu_valid && filament_printable;
            post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, model_fits));
            ppl.get_curr_plate()->update_slice_ready_status(model_fits);
        }
        else {
            _set_warning_notification(EWarning::ObjectOutside, false);
            _set_warning_notification(EWarning::ObjectClashed, false);
            _set_warning_notification(EWarning::LeftExtruderPrintableError, false);
            _set_warning_notification(EWarning::RightExtruderPrintableError, false);
            //_set_warning_notification(EWarning::ObjectLimited, false);
            //_set_warning_notification(EWarning::SlaSupportsOutside, false);
           _set_warning_notification(EWarning::TPUPrintableError, false);
           _set_warning_notification(EWarning::FilamentPrintableError, false);
           _set_warning_notification(EWarning::MixUsePLAAndPETG, false);
           _set_warning_notification(EWarning::PrimeTowerOutside, false);
           _set_warning_notification(EWarning::MultiExtruderPrintableError,false);
           _set_warning_notification(EWarning::MultiExtruderHeightOutside,false);
           _set_warning_notification(EWarning::NozzleFilamentIncompatible,false);

           post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, false));
        }
    }
    else
    {
        bool flag = false;
        if (!m_volumes.empty()) {
            std::vector<std::string> object_results;
            object_results.reserve(10);
            struct TempVolumeData
            {
                BoundingBoxf3 m_aabb;
                GLVolume* m_p_volume{ nullptr };
            };
            std::vector<TempVolumeData> temp_volume_data_list;
            temp_volume_data_list.reserve(m_volumes.volumes.size());
            BoundingBoxf3 assembly_bb;
            for (GLVolume* volume : m_volumes.volumes) {
                if (!m_apply_zoom_to_volumes_filter || ((volume != nullptr) && volume->zoom_to_volumes)) {
                    const auto v_bb = volume->transformed_bounding_box();
                    assembly_bb.merge(v_bb);

                    TempVolumeData t_volume_data;
                    t_volume_data.m_aabb = v_bb;
                    t_volume_data.m_p_volume = volume;
                    temp_volume_data_list.emplace_back(t_volume_data);
                }
            }

            if (assembly_bb.max_size() >= 1e4f) { // 10m
                for (const auto& t_volume_data : temp_volume_data_list) {
                    if (!t_volume_data.m_p_volume) {
                        continue;
                    }
                    const auto t_length = t_volume_data.m_aabb.center().norm();
                    if (t_length >= 1e4f) {
                        const auto& p_object = (*m_model).objects[t_volume_data.m_p_volume->object_idx()];
                        if (p_object) {
                            object_results.emplace_back(p_object->name);
                        }
                    }
                }
                flag = construct_assembly_warning_string(object_results, get_assembly_too_far_text());
            }
        }
        else {
            flag = false;
        }
        _set_warning_notification(EWarning::AsemblyInvalid, flag);
    }

    refresh_camera_scene_box();

    if (m_gizmos.is_enabled() && m_selection.is_empty()) {
        // If no object is selected, deactivate the active gizmo, if any
        // Otherwise it may be shown after cleaning the scene (if it was active while the objects were deleted)
        m_gizmos.reset_all_states();
        // BBS
#if 0
        // If no object is selected, reset the objects manipulator on the sidebar
        // to force a reset of its cache
        auto manip = wxGetApp().obj_manipul();
        if (manip != nullptr)
            manip->set_dirty();
#endif
    }

    // and force this canvas to be redrawn.
    m_dirty = true;
}

static void reserve_new_volume_finalize_old_volume(GLVolume& vol_new, GLVolume& vol_old, bool gl_initialized, size_t prealloc_size = VERTEX_BUFFER_RESERVE_SIZE)
{
	// Assign the large pre-allocated buffers to the new GLVolume.
	*(vol_new.indexed_vertex_array) = std::move(*(vol_old.indexed_vertex_array));
	// Copy the content back to the old GLVolume.
	*(vol_old.indexed_vertex_array) = *(vol_new.indexed_vertex_array);
	// Clear the buffers, but keep them pre-allocated.
	vol_new.indexed_vertex_array->clear();
	// Just make sure that clear did not clear the reserved memory.
	// Reserving number of vertices (3x position + 3x color)
	vol_new.indexed_vertex_array->reserve(prealloc_size / 6);
	// Finalize the old geometry, possibly move data to the graphics card.
	vol_old.finalize_geometry(gl_initialized);
}

//BBS: always load shell at preview
void GLCanvas3D::load_shells(const Print& print, bool force_previewing)
{
    if (m_initialized)
    {
        m_gcode_viewer.load_shells(print, m_initialized, force_previewing);
        m_gcode_viewer.update_shells_color_by_extruder(m_config);
    }
}

//BBS: add only gcode mode
void GLCanvas3D::load_gcode_preview(const GCodeProcessorResult& gcode_result, const std::vector<std::string>& str_tool_colors, bool only_gcode)
{
    PartPlateList& partplate_list = wxGetApp().plater()->get_partplate_list();
    PartPlate* plate = partplate_list.get_curr_plate();
    const std::vector<BoundingBoxf3>& exclude_bounding_box = plate->get_exclude_areas();

    //BBS: init is called in GLCanvas3D.render()
    //when load gcode directly, it is too late
    m_gcode_viewer.init(wxGetApp().get_mode(), wxGetApp().preset_bundle);
    m_gcode_viewer.load(gcode_result, *this->fff_print(), wxGetApp().plater()->build_volume(), exclude_bounding_box,
        m_initialized, wxGetApp().get_mode(), only_gcode);

    if (wxGetApp().is_editor()) {
        //BBS: always load shell at preview, do this in load_shells
        _update_slice_error_status();
    }

    m_gcode_viewer.refresh(gcode_result, str_tool_colors);
    set_as_dirty();
    request_extra_frame();
}

void GLCanvas3D::refresh_gcode_preview_render_paths()
{
    m_gcode_viewer.refresh_render_paths();
    set_as_dirty();
    request_extra_frame();
}

void GLCanvas3D::load_sla_preview()
{
    const SLAPrint* print = sla_print();
    if (m_canvas != nullptr && print != nullptr) {
        _set_current(true);
	    // Release OpenGL data before generating new data.
	    reset_volumes();
        _load_sla_shells();
        _update_sla_shells_outside_state();
        _set_warning_notification_if_needed(EWarning::SlaSupportsOutside);
    }
}

/*void GLCanvas3D::load_preview(const std::vector<std::string>& str_tool_colors, const std::vector<CustomGCode::Item>& color_print_values)
{
    const Print *print = this->fff_print();
    if (print == nullptr)
        return;

    _set_current();

    // Release OpenGL data before generating new data.
    this->reset_volumes();

    const BuildVolume &build_volume = m_bed.build_volume();
    _load_print_toolpaths(build_volume);
    _load_wipe_tower_toolpaths(build_volume, str_tool_colors);
    for (const PrintObject* object : print->objects())
        _load_print_object_toolpaths(*object, build_volume, str_tool_colors, color_print_values);

    _set_warning_notification_if_needed(EWarning::ToolpathOutside);
}*/

void GLCanvas3D::bind_event_handlers()
{
    if (m_canvas != nullptr) {
        m_canvas->Bind(wxEVT_SIZE, &GLCanvas3D::on_size, this);
        m_canvas->Bind(wxEVT_IDLE, &GLCanvas3D::on_idle, this);
        m_canvas->Bind(wxEVT_CHAR, &GLCanvas3D::on_char, this);
        m_canvas->Bind(wxEVT_KEY_DOWN, &GLCanvas3D::on_key, this);
        m_canvas->Bind(wxEVT_KEY_UP, &GLCanvas3D::on_key, this);
        m_canvas->Bind(wxEVT_MOUSEWHEEL, &GLCanvas3D::on_mouse_wheel, this);
        m_canvas->Bind(wxEVT_TIMER, &GLCanvas3D::on_timer, this);
        m_canvas->Bind(EVT_GLCANVAS_RENDER_TIMER, &GLCanvas3D::on_render_timer, this);
        m_toolbar_highlighter.set_timer_owner(m_canvas, 0);
        m_canvas->Bind(EVT_GLCANVAS_TOOLBAR_HIGHLIGHTER_TIMER, [this](wxTimerEvent&) { m_toolbar_highlighter.blink(); });
        m_gizmo_highlighter.set_timer_owner(m_canvas, 0);
        m_canvas->Bind(EVT_GLCANVAS_GIZMO_HIGHLIGHTER_TIMER, [this](wxTimerEvent&) { m_gizmo_highlighter.blink(); });
        m_canvas->Bind(wxEVT_LEFT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEFT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MOTION, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_ENTER_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEAVE_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEFT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_PAINT, &GLCanvas3D::on_paint, this);
        m_canvas->Bind(wxEVT_SET_FOCUS, &GLCanvas3D::on_set_focus, this);
        m_canvas->Bind(wxEVT_KILL_FOCUS, &GLCanvas3D::on_kill_focus, this);
        m_event_handlers_bound = true;

        m_canvas->Bind(wxEVT_GESTURE_PAN, &GLCanvas3D::on_gesture, this);
        m_canvas->Bind(wxEVT_GESTURE_ZOOM, &GLCanvas3D::on_gesture, this);
        m_canvas->Bind(wxEVT_GESTURE_ROTATE, &GLCanvas3D::on_gesture, this);
        m_canvas->EnableTouchEvents(wxTOUCH_ZOOM_GESTURE | wxTOUCH_ROTATE_GESTURE);
#if __WXOSX__
        initGestures(m_canvas->GetHandle(), m_canvas); // for UIPanGestureRecognizer allowedScrollTypesMask
#endif
    }
}

void GLCanvas3D::unbind_event_handlers()
{
    if (m_canvas != nullptr && m_event_handlers_bound) {
        m_canvas->Unbind(wxEVT_SIZE, &GLCanvas3D::on_size, this);
        m_canvas->Unbind(wxEVT_IDLE, &GLCanvas3D::on_idle, this);
        m_canvas->Unbind(wxEVT_CHAR, &GLCanvas3D::on_char, this);
        m_canvas->Unbind(wxEVT_KEY_DOWN, &GLCanvas3D::on_key, this);
        m_canvas->Unbind(wxEVT_KEY_UP, &GLCanvas3D::on_key, this);
        m_canvas->Unbind(wxEVT_MOUSEWHEEL, &GLCanvas3D::on_mouse_wheel, this);
        m_canvas->Unbind(wxEVT_TIMER, &GLCanvas3D::on_timer, this);
        m_canvas->Unbind(EVT_GLCANVAS_RENDER_TIMER, &GLCanvas3D::on_render_timer, this);
        m_canvas->Unbind(wxEVT_LEFT_DOWN, &GLCanvas3D::on_mouse, this);
		m_canvas->Unbind(wxEVT_LEFT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MOTION, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_ENTER_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEAVE_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEFT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_PAINT, &GLCanvas3D::on_paint, this);
        m_canvas->Unbind(wxEVT_SET_FOCUS, &GLCanvas3D::on_set_focus, this);
        m_event_handlers_bound = false;

        m_canvas->Unbind(wxEVT_GESTURE_PAN, &GLCanvas3D::on_gesture, this);
        m_canvas->Unbind(wxEVT_GESTURE_ZOOM, &GLCanvas3D::on_gesture, this);
        m_canvas->Unbind(wxEVT_GESTURE_ROTATE, &GLCanvas3D::on_gesture, this);
    }
}

void GLCanvas3D::on_size(wxSizeEvent& evt)
{
    m_dirty = true;
}

void GLCanvas3D::on_idle(wxIdleEvent& evt)
{
    if (!m_initialized)
        return;

    const auto& p_main_toolbar = get_main_toolbar();
    if (p_main_toolbar) {
        m_dirty |= p_main_toolbar->update_items_state();
    }
    // BBS
    //m_dirty |= wxGetApp().plater()->get_view_toolbar().update_items_state();
    m_dirty |= wxGetApp().plater()->sidebar().get_update_3d_state();
    if (wxGetApp().plater()->sidebar().get_update_3d_state()) {
        wxGetApp().plater()->sidebar().cancel_update_3d_state();
    }
    m_dirty |= wxGetApp().plater()->get_collapse_toolbar().update_items_state();
    bool mouse3d_controller_applied = wxGetApp().plater()->get_mouse3d_controller().apply(get_active_camera());
    m_dirty |= mouse3d_controller_applied;
    m_dirty |= wxGetApp().plater()->get_notification_manager()->update_notifications(*this);
    auto gizmo = wxGetApp().plater()->get_view3D_canvas3D()->get_gizmos_manager().get_current();
    if (gizmo != nullptr) m_dirty |= gizmo->update_items_state();
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    // ImGuiWrapper::m_requires_extra_frame may have been set by a render made outside of the OnIdle mechanism
    bool imgui_requires_extra_frame = wxGetApp().imgui()->requires_extra_frame();
    m_dirty |= imgui_requires_extra_frame;
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

    if (!m_dirty)
        return;

#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    // this needs to be done here.
    // during the render launched by the refresh the value may be set again
    wxGetApp().imgui()->reset_requires_extra_frame();
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

    _refresh_if_shown_on_screen();

#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    if (m_extra_frame_requested || mouse3d_controller_applied || imgui_requires_extra_frame || wxGetApp().imgui()->requires_extra_frame()) {
#else
    if (m_extra_frame_requested || mouse3d_controller_applied) {
        m_dirty = true;
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        m_extra_frame_requested = false;
        evt.RequestMore();
    }
    else
        m_dirty = false;
}

void GLCanvas3D::on_char(wxKeyEvent& evt)
{
    if (!m_initialized)
        return;

    // see include/wx/defs.h enum wxKeyCode
    int keyCode = evt.GetKeyCode();
    int ctrlMask = wxMOD_CONTROL;
    int shiftMask = wxMOD_SHIFT;
    int altMask   = wxMOD_ALT;

    auto imgui = wxGetApp().imgui();
    if (imgui->update_key_data(evt)) {
        render();
        return;
    }

    bool is_in_painting_mode = false;
    GLGizmoPainterBase *current_gizmo_painter = dynamic_cast<GLGizmoPainterBase *>(get_gizmos_manager().get_current());
    if (current_gizmo_painter != nullptr) {
        is_in_painting_mode = true;
    }

    //BBS: add orient deactivate logic
    if (keyCode == WXK_ESCAPE
        && (_deactivate_arrange_menu() || _deactivate_orient_menu() || _deactivate_layersediting_menu()))
        return;

    if (m_gizmos.on_char(evt))
        return;

    if ((evt.GetModifiers() & ctrlMask) != 0) {
        if ((evt.GetModifiers() & shiftMask) != 0) {
            switch (keyCode) {
            case 'd':
            case 'D':
            case WXK_CONTROL_D: {
                post_event(SimpleEvent(EVT_GLTOOLBAR_DELETE_ALL));
                return;
            }
            case 'z':
            case 'Z':
            case WXK_CONTROL_Z: {
                if (m_canvas_type == CanvasView3D || m_canvas_type == CanvasAssembleView) {
                    post_event(SimpleEvent(EVT_GLCANVAS_REDO));
                }
                return;
            }
            }
        }
        // CTRL is pressed
        switch (keyCode) {
#ifdef __APPLE__
        case 'a':
        case 'A':
#else /* __APPLE__ */
        case WXK_CONTROL_A:
#endif /* __APPLE__ */
            if (!is_in_painting_mode && !m_layers_editing.is_enabled())
                post_event(SimpleEvent(EVT_GLCANVAS_SELECT_ALL));
        break;
#ifdef __APPLE__
        case 'c':
        case 'C':
#else /* __APPLE__ */
        case WXK_CONTROL_C:
#endif /* __APPLE__ */
            if (!is_in_painting_mode)
                post_event(SimpleEvent(EVT_GLTOOLBAR_COPY));
        break;
#ifdef __APPLE__
        case 'm':
        case 'M':
#else  /* __APPLE__ */
        case WXK_CONTROL_M:
#endif /* __APPLE__ */
        {
#ifdef _WIN32
            if (wxGetApp().app_config->get("use_legacy_3DConnexion") == "true") {
#endif //_WIN32
#ifdef __APPLE__
                // On OSX use Cmd+Shift+M to "Show/Hide 3Dconnexion devices settings dialog"
                if ((evt.GetModifiers() & shiftMask) != 0) {
#endif // __APPLE__
                    Mouse3DController &controller = wxGetApp().plater()->get_mouse3d_controller();
                    controller.show_settings_dialog(!controller.is_settings_dialog_shown());
                    m_dirty = true;
#ifdef __APPLE__
                } else
                    // and Cmd+M to minimize application
                    wxGetApp().mainframe->Iconize();
#endif // __APPLE__
#ifdef _WIN32
            }
#endif //_WIN32
            break;
        }
#ifdef __APPLE__
        case 'v':
        case 'V':
#else /* __APPLE__ */
        case WXK_CONTROL_V:
#endif /* __APPLE__ */
            if (!is_in_painting_mode)
                post_event(SimpleEvent(EVT_GLTOOLBAR_PASTE));
        break;

#ifdef __APPLE__
        case 'x':
        case 'X':
#else /* __APPLE__ */
        case WXK_CONTROL_X:
#endif /* __APPLE__ */
            if (!is_in_painting_mode)
                post_event(SimpleEvent(EVT_GLTOOLBAR_CUT));
        break;

#ifdef __APPLE__
        case 'f':
        case 'F':
#else /* __APPLE__ */
        case WXK_CONTROL_F:
#endif /* __APPLE__ */
            break;


#ifdef __APPLE__
        case 'y':
        case 'Y':
#else /* __APPLE__ */
        case WXK_CONTROL_Y:
#endif /* __APPLE__ */
            if (m_canvas_type == CanvasView3D || m_canvas_type == CanvasAssembleView) {
                post_event(SimpleEvent(EVT_GLCANVAS_REDO));
            }
        break;
#ifdef __APPLE__
        case 'z':
        case 'Z':
#else /* __APPLE__ */
        case WXK_CONTROL_Z:
#endif /* __APPLE__ */
            // only support redu/undo in CanvasView3D
            if (m_canvas_type == CanvasView3D || m_canvas_type == CanvasAssembleView) {
                post_event(SimpleEvent(EVT_GLCANVAS_UNDO));
            }
        break;

        // BBS
#ifdef __APPLE__
        case 'E':
        case 'e':
#else /* __APPLE__ */
        case WXK_CONTROL_E:
#endif /* __APPLE__ */
        { m_labels.show(!m_labels.is_shown()); m_dirty = true; break; }
#ifdef __APPLE__
        case 'W':
        case 'w':
#else  /* __APPLE__ */
        case WXK_CONTROL_W:
#endif /* __APPLE__ */
        {
            wxGetApp().plater()->reset_window_layout();
            break;
        }
        case '0': {
            select_view("plate");
            zoom_to_bed();
            break; }
        case '1': { select_view("top"); break; }
        case '2': { select_view("bottom"); break; }
        case '3': { select_view("front"); break; }
        case '4': { select_view("rear"); break; }
        case '5': { select_view("left"); break; }
        case '6': { select_view("right"); break; }
        case '7': { select_view("iso");break; }
#ifdef __APPLE__
        case 'k':
        case 'K':
#else /* __APPLE__ */
        case WXK_CONTROL_K:
#endif /* __APPLE__ */
            post_event(SimpleEvent(EVT_GLTOOLBAR_CLONE));
            break;
        default:            evt.Skip();
        }
    } else {
        auto obj_list = wxGetApp().obj_list();
        switch (keyCode)
        {
        //case WXK_BACK:
        case WXK_DELETE: { post_event(SimpleEvent(EVT_GLTOOLBAR_DELETE)); break; }
        // BBS
#ifdef __APPLE__
        case WXK_BACK: { post_event(SimpleEvent(EVT_GLTOOLBAR_DELETE)); break; }
#endif
        case WXK_ESCAPE: { deselect_all(); break; }
        //case WXK_F5: {
        //    if ((wxGetApp().is_editor() && !wxGetApp().plater()->model().objects.empty()) ||
        //        (wxGetApp().is_gcode_viewer() && !wxGetApp().plater()->get_last_loaded_gcode().empty()))
        //        post_event(SimpleEvent(EVT_GLCANVAS_RELOAD_FROM_DISK));
        //    break;
        //}

        // BBS: use keypad to change extruder
        case '1': {
            if (!m_timer_set_color.IsRunning()) {
                m_timer_set_color.StartOnce(500);
                break;
            }
        }
        case '0':   //Color logic for material 10
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            if (m_timer_set_color.IsRunning()) {
                if (keyCode < '7')  keyCode += 10;
                m_timer_set_color.Stop();
            }
            if (m_gizmos.get_current_type() != GLGizmosManager::MmuSegmentation)
                obj_list->set_extruder_for_selected_items(keyCode - '0');
            break;
        }

        //case '+': {
        //    if (dynamic_cast<Preview*>(m_canvas->GetParent()) != nullptr)
        //        post_event(wxKeyEvent(EVT_GLCANVAS_EDIT_COLOR_CHANGE, evt));
        //    else
        //        post_event(Event<int>(EVT_GLCANVAS_INCREASE_INSTANCES, +1));
        //    break;
        //}
        //case '-': {
        //    if (dynamic_cast<Preview*>(m_canvas->GetParent()) != nullptr)
        //        post_event(wxKeyEvent(EVT_GLCANVAS_EDIT_COLOR_CHANGE, evt));
        //    else
        //        post_event(Event<int>(EVT_GLCANVAS_INCREASE_INSTANCES, -1));
        //    break;
        //}
        case '?': { post_event(SimpleEvent(EVT_GLCANVAS_QUESTION_MARK)); break; }
        case 'A':
        case 'a':
            {
                if ((evt.GetModifiers() & shiftMask) != 0)
                    post_event(SimpleEvent(EVT_GLCANVAS_ARRANGE_PARTPLATE));
                else if ((evt.GetModifiers() & altMask) != 0)
                    post_event(SimpleEvent(EVT_GLCANVAS_ARRANGE_OUTPLATE));
                else
                    post_event(SimpleEvent(EVT_GLCANVAS_ARRANGE));
                break;
            }
        case 'r':
        case 'R':
            {
                if ((evt.GetModifiers() & shiftMask) != 0)
                    post_event(SimpleEvent(EVT_GLCANVAS_ORIENT_PARTPLATE));
                else
                    post_event(SimpleEvent(EVT_GLCANVAS_ORIENT));
                break;
            }
#if !BBL_RELEASE_TO_PUBLIC
        case 'C':
        case 'c': { m_gcode_viewer.toggle_gcode_window_visibility(); m_dirty = true; request_extra_frame(); break; }
#endif
        //case 'G':
        //case 'g': {
        //    if ((evt.GetModifiers() & shiftMask) != 0) {
        //        if (dynamic_cast<Preview*>(m_canvas->GetParent()) != nullptr)
        //            post_event(wxKeyEvent(EVT_GLCANVAS_JUMP_TO, evt));
        //    }
        //    break;
        //}
        //case 'I':
        //case 'i': { _update_camera_zoom(1.0); break; }

        //case 'L':
        //case 'l': {
            //if (!m_main_toolbar.is_enabled()) {
            //    m_gcode_viewer.enable_legend(!m_gcode_viewer.is_legend_enabled());
            //    m_dirty = true;
            //    wxGetApp().plater()->update_preview_bottom_toolbar();
            //}
            //break;
        //}
        //case 'O':
        //case 'o': { _update_camera_zoom(-1.0); break; }
#if ENABLE_RENDER_PICKING_PASS
        case 'T':
        case 't': {
            m_show_picking_texture = !m_show_picking_texture;
            m_dirty = true;
            break;
        }
#endif // ENABLE_RENDER_PICKING_PASS
        //case 'Z':
        //case 'z': {
        //    if (!m_selection.is_empty())
        //        zoom_to_selection();
        //    else {
        //        if (!m_volumes.empty())
        //            zoom_to_volumes();
        //        else
        //            _zoom_to_box(m_gcode_viewer.get_paths_bounding_box());
        //    }
        //    break;
        //}
        default:  { evt.Skip(); break; }
        }
    }
}

class TranslationProcessor
{
    using UpAction = std::function<void(void)>;
    using DownAction = std::function<void(const Vec3d&, bool, bool)>;

    UpAction m_up_action{ nullptr };
    DownAction m_down_action{ nullptr };

    bool m_running{ false };
    Vec3d m_direction{ Vec3d::UnitX() };

public:
    TranslationProcessor(UpAction up_action, DownAction down_action)
        : m_up_action(up_action), m_down_action(down_action)
    {
    }

    void process(wxKeyEvent& evt)
    {
        const int keyCode = evt.GetKeyCode();
        wxEventType type = evt.GetEventType();
        if (type == wxEVT_KEY_UP) {
            switch (keyCode)
            {
            case WXK_NUMPAD_LEFT:  case WXK_LEFT:
            case WXK_NUMPAD_RIGHT: case WXK_RIGHT:
            case WXK_NUMPAD_UP:    case WXK_UP:
            case WXK_NUMPAD_DOWN:  case WXK_DOWN:
            {
                m_running = false;
                m_up_action();
                break;
            }
            default: { break; }
            }
        }
        else if (type == wxEVT_KEY_DOWN) {
            bool apply = false;

            switch (keyCode)
            {
            case WXK_SHIFT:
            {
                if (m_running)
                    apply = true;

                break;
            }
            case WXK_NUMPAD_LEFT:
            case WXK_LEFT:
            {
                m_direction = -Vec3d::UnitX();
                apply = true;
                break;
            }
            case WXK_NUMPAD_RIGHT:
            case WXK_RIGHT:
            {
                m_direction = Vec3d::UnitX();
                apply = true;
                break;
            }
            case WXK_NUMPAD_UP:
            case WXK_UP:
            {
                m_direction = Vec3d::UnitY();
                apply = true;
                break;
            }
            case WXK_NUMPAD_DOWN:
            case WXK_DOWN:
            {
                m_direction = -Vec3d::UnitY();
                apply = true;
                break;
            }
            default: { break; }
            }

            if (apply) {
                m_running = true;
                m_down_action(m_direction, evt.ShiftDown(), evt.CmdDown());
            }
        }
    }
};

void GLCanvas3D::on_key(wxKeyEvent& evt)
{
    static GLCanvas3D const * thiz = nullptr;
    static TranslationProcessor translationProcessor(nullptr, nullptr);
    if (thiz != this) {
        thiz = this;
        translationProcessor = TranslationProcessor(
        [this]() {
            do_move(L("Tool Move"));
            m_gizmos.update_data();

            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            // Let the plater know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
            // updates camera target constraints
            refresh_camera_scene_box();
            m_dirty = true;
        },
        [this](const Vec3d& direction, bool slow, bool camera_space) {
            if (m_gizmos.is_ban_move_glvolume()) {
                return;
            }
            m_selection.setup_cache();
            double multiplier = slow ? 1.0 : 10.0;

            Vec3d displacement;
            if (camera_space) {
                Eigen::Matrix<double, 3, 3, Eigen::DontAlign> inv_view_3x3 = get_active_camera().get_view_matrix().inverse().matrix().block(0, 0, 3, 3);
                displacement = multiplier * (inv_view_3x3 * direction);
                displacement.z() = 0.0;
            }
            else
                displacement = multiplier * direction;

            TransformationType trafo_type;
            trafo_type.set_relative();
            m_selection.translate(displacement, trafo_type);
            m_dirty = true;
        }
    );}

    const int keyCode = evt.GetKeyCode();

    auto imgui = wxGetApp().imgui();
    if (imgui->update_key_data(evt)) {
        render();
    }
    else
    {
        if (!m_gizmos.on_key(evt)) {
            if (evt.GetEventType() == wxEVT_KEY_UP) {
                if (evt.ShiftDown() && evt.ControlDown() && keyCode == WXK_SPACE) {
#if !BBL_RELEASE_TO_PUBLIC
                    wxGetApp().plater()->toggle_render_statistic_dialog();
                    m_dirty = true;
#endif
                } else if ((evt.ShiftDown() && evt.ControlDown() && keyCode == WXK_RETURN) ||
                    evt.ShiftDown() && evt.AltDown() && keyCode == WXK_RETURN) {
                    wxGetApp().plater()->toggle_show_wireframe();
                    m_dirty = true;
                } else if ((evt.ShiftDown() && evt.ControlDown() && keyCode == 'T')) {
                    wxGetApp().plater()->toggle_text_cs();
                    m_dirty = true;
                } else if ((evt.ShiftDown() && evt.ControlDown() && keyCode == 'L')) {
                    wxGetApp().plater()->toggle_non_manifold_edges();
                    m_dirty = true;
                }
                else if (m_tab_down && keyCode == WXK_TAB && !evt.HasAnyModifiers()) {
                    // Enable switching between 3D and Preview with Tab
                    // m_canvas->HandleAsNavigationKey(evt);   // XXX: Doesn't work in some cases / on Linux
                    post_event(SimpleEvent(EVT_GLCANVAS_TAB));
                }
                else if (keyCode == WXK_SHIFT) {
                    translationProcessor.process(evt);

                    if (m_picking_enabled && m_rectangle_selection.is_dragging()) {
                        _update_selection_from_hover();
                        m_rectangle_selection.stop_dragging();
                        m_mouse.ignore_left_up = true;
                        m_dirty = true;
                    }
//                    set_cursor(Standard);
                }
                else if (keyCode == WXK_ALT) {
                    if (m_picking_enabled && m_rectangle_selection.is_dragging()) {
                        _update_selection_from_hover();
                        m_rectangle_selection.stop_dragging();
                        m_mouse.ignore_left_up = true;
                        m_dirty = true;
                    }
//                    set_cursor(Standard);
                }
                else if (keyCode == WXK_CONTROL)
                    m_dirty = true;
                else if (m_gizmos.is_enabled() && !m_selection.is_empty() && m_canvas_type != CanvasAssembleView) {
                    translationProcessor.process(evt);

                    switch (keyCode)
                    {
                    case WXK_NUMPAD_PAGEUP:   case WXK_PAGEUP:
                    case WXK_NUMPAD_PAGEDOWN: case WXK_PAGEDOWN:
                    {
                        if (m_gizmos.get_gizmo_active_condition(GLGizmosManager::EType::Rotate)) {
                            do_rotate(L("Tool Rotate"));
                            m_gizmos.update_data();

                            // BBS
                            // wxGetApp().obj_manipul()->set_dirty();
                            // Let the plater know that the dragging finished, so a delayed refresh
                            // of the scene with the background processing data should be performed.
                            post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
                            // updates camera target constraints
                            refresh_camera_scene_box();
                            m_dirty = true;
                        }
                        break;
                    }
                    default: { break; }
                    }
                }

                // BBS: add select view logic
                if (evt.ControlDown()) {
                    switch (keyCode) {
                        case '0':
                        case WXK_NUMPAD0: //0 on numpad
                            { select_view("plate");
                              zoom_to_bed();
                            break;
                        }
                        case '1':
                        case WXK_NUMPAD1: //1 on numpad
                            { select_view("top"); break; }
                        case '2':
                        case WXK_NUMPAD2: //2 on numpad
                            { select_view("bottom"); break; }
                        case '3':
                        case WXK_NUMPAD3: //3 on numpad
                        {
                            select_view("front");
                            m_gizmos.update_paint_base_camera_rotate_rad();
                            break;
                        }
                        case '4':
                        case WXK_NUMPAD4: //4 on numpad
                        {
                            select_view("rear");
                            m_gizmos.update_paint_base_camera_rotate_rad();
                            break;
                        }
                        case '5':
                        case WXK_NUMPAD5: //5 on numpad
                        {
                            select_view("left");
                            m_gizmos.update_paint_base_camera_rotate_rad();
                            break;
                        }
                        case '6':
                        case WXK_NUMPAD6: //6 on numpad
                        {
                            select_view("right");
                            m_gizmos.update_paint_base_camera_rotate_rad();
                            break;
                        }
                        case '7':
                        case WXK_NUMPAD7: //7 on numpad
                        {
                            select_view("iso");
                            m_gizmos.update_paint_base_camera_rotate_rad();
                            break;
                        }
                        default: break;
                    }
                }
            }
            else if (evt.GetEventType() == wxEVT_KEY_DOWN) {
                m_tab_down = keyCode == WXK_TAB && !evt.HasAnyModifiers();
                if (keyCode == WXK_SHIFT) {
                    translationProcessor.process(evt);

                    if (m_picking_enabled && (m_gizmos.get_current_type() != GLGizmosManager::SlaSupports))
                    {
                        m_mouse.ignore_left_up = false;
//                        set_cursor(Cross);
                    }
                }
                else if (keyCode == WXK_ALT) {
                    if (m_picking_enabled && (m_gizmos.get_current_type() != GLGizmosManager::SlaSupports))
                    {
                        m_mouse.ignore_left_up = false;
//                        set_cursor(Cross);
                    }
                }
                else if (keyCode == WXK_CONTROL)
                    m_dirty = true;
                else if (keyCode == WXK_TAB && evt.ShiftDown() && !evt.ControlDown() && !wxGetApp().is_gcode_viewer()) {
                    // Collapse side-panel with Shift+Tab
                    post_event(SimpleEvent(EVT_GLCANVAS_COLLAPSE_SIDEBAR));
                } else if (m_gizmos.is_enabled() && !m_selection.is_empty() && m_canvas_type != CanvasAssembleView) {
                    auto _do_rotate = [this](double angle_z_rad) {
                        if (m_gizmos.is_ban_move_glvolume()) {
                            return;
                        }
                        if (!m_gizmos.get_gizmo_active_condition(GLGizmosManager::EType::Rotate)) {
                            return;
                        }
                        m_selection.setup_cache();
                        m_selection.rotate(Vec3d(0.0, 0.0, angle_z_rad), TransformationType(TransformationType::World_Relative_Joint));
                        m_dirty = true;
//                        wxGetApp().obj_manipul()->set_dirty();
                    };

                    translationProcessor.process(evt);

                    switch (keyCode)
                    {
                    case WXK_NUMPAD_PAGEUP:   case WXK_PAGEUP:   { _do_rotate(0.25 * M_PI); break; }
                    case WXK_NUMPAD_PAGEDOWN: case WXK_PAGEDOWN: { _do_rotate(-0.25 * M_PI); break; }
                    default: { break; }
                    }
                } else if (!m_gizmos.is_enabled()) {
                    // DoubleSlider navigation in Preview
                    if (m_canvas_type == CanvasPreview) {
                        IMSlider *m_layers_slider = get_gcode_viewer().get_layers_slider();
                        IMSlider *m_moves_slider  = get_gcode_viewer().get_moves_slider();
                        if (evt.CmdDown() || evt.ShiftDown()) {
                            if (evt.GetKeyCode() == 'G') {
                                m_layers_slider->show_go_to_layer(true);
                            }
                            IMSlider *m_layers_slider = get_gcode_viewer().get_layers_slider();
                            IMSlider *m_moves_slider  = get_gcode_viewer().get_moves_slider();
                            if (keyCode == WXK_UP || keyCode == WXK_DOWN) {
                                int new_pos;
                                if (m_layers_slider->GetSelection() == ssHigher) {
                                    new_pos = keyCode == WXK_UP ? m_layers_slider->GetHigherValue() + 5 : m_layers_slider->GetHigherValue() - 5;
                                    m_layers_slider->SetHigherValue(new_pos);
                                }
                                else if (m_layers_slider->GetSelection() == ssLower) {
                                    new_pos = keyCode == WXK_UP ? m_layers_slider->GetLowerValue() + 5 : m_layers_slider->GetLowerValue() - 5;
                                    m_layers_slider->SetLowerValue(new_pos);
                                }
                                if (m_layers_slider->is_one_layer()) m_layers_slider->SetLowerValue(m_layers_slider->GetHigherValue());
                                // BBS set as dirty, update in render_gcode()
                                m_layers_slider->set_as_dirty();
                            } else if (keyCode == WXK_LEFT || keyCode == WXK_RIGHT) {
                                const int new_pos = keyCode == WXK_RIGHT ? m_moves_slider->GetHigherValue() + 5 : m_moves_slider->GetHigherValue() - 5;
                                m_moves_slider->SetHigherValue(new_pos);
                                // BBS set as dirty, update in render_gcode()
                                m_moves_slider->set_as_dirty();
                            }
                        }
                        else if (keyCode == WXK_UP || keyCode == WXK_DOWN) {
                            int new_pos;
                            if (m_layers_slider->GetSelection() == ssHigher) {
                                new_pos = keyCode == WXK_UP ? m_layers_slider->GetHigherValue() + 1 : m_layers_slider->GetHigherValue() - 1;
                                m_layers_slider->SetHigherValue(new_pos);
                            }
                            else if (m_layers_slider->GetSelection() == ssLower) {
                                new_pos = keyCode == WXK_UP ? m_layers_slider->GetLowerValue() + 1 : m_layers_slider->GetLowerValue() - 1;
                                m_layers_slider->SetLowerValue(new_pos);
                            }
                            if (m_layers_slider->is_one_layer()) m_layers_slider->SetLowerValue(m_layers_slider->GetHigherValue());
                            // BBS set as dirty, update in render_gcode()
                            m_layers_slider->set_as_dirty();
                        } else if (keyCode == WXK_LEFT || keyCode == WXK_RIGHT) {
                            const int new_pos = keyCode == WXK_RIGHT ? m_moves_slider->GetHigherValue() + 1 : m_moves_slider->GetHigherValue() - 1;
                            m_moves_slider->SetHigherValue(new_pos);
                            // BBS set as dirty, update in render_gcode()
                            m_moves_slider->set_as_dirty();
                        }
                        m_dirty = true;
                    }
                }
            }
        }
        else return;
    }

    if (keyCode != WXK_TAB
        && keyCode != WXK_LEFT
        && keyCode != WXK_UP
        && keyCode != WXK_RIGHT
        && keyCode != WXK_DOWN) {
        evt.Skip();   // Needed to have EVT_CHAR generated as well
    }
}

void GLCanvas3D::on_mouse_wheel(wxMouseEvent& evt)
{
#ifdef WIN32
    // Try to filter out spurious mouse wheel events comming from 3D mouse.
    if (wxGetApp().plater()->get_mouse3d_controller().process_mouse_wheel())
        return;
#endif

    if (!m_initialized)
        return;

    // Ignore the wheel events if the middle button is pressed.
    if (evt.MiddleIsDown())
        return;

#if ENABLE_RETINA_GL
    const float scale = m_retina_helper->get_scale_factor();
    evt.SetX(evt.GetX() * scale);
    evt.SetY(evt.GetY() * scale);
#endif

    if (wxGetApp().imgui()->update_mouse_data(evt)) {
        if (m_canvas_type == CanvasPreview) {
            IMSlider* m_layers_slider = get_gcode_viewer().get_layers_slider();
            IMSlider* m_moves_slider = get_gcode_viewer().get_moves_slider();
            m_layers_slider->on_mouse_wheel(evt);
            m_moves_slider->on_mouse_wheel(evt);
        }
        render();
        m_dirty = true;
        return;
    }

#ifdef __WXMSW__
	// For some reason the Idle event is not being generated after the mouse scroll event in case of scrolling with the two fingers on the touch pad,
	// if the event is not allowed to be passed further.
    // evt.Skip() used to trigger the needed screen refresh, but it does no more. wxWakeUpIdle() seem to work now.
    wxWakeUpIdle();
#endif /* __WXMSW__ */

    // Performs layers editing updates, if enabled
    if (is_layers_editing_enabled()) {
        int object_idx_selected = m_selection.get_object_idx();
        if (object_idx_selected != -1) {
            // A volume is selected. Test, whether hovering over a layer thickness bar.
            if (m_layers_editing.bar_rect_contains(*this, (float)evt.GetX(), (float)evt.GetY())) {
                // Adjust the width of the selection.
                m_layers_editing.band_width = std::max(std::min(m_layers_editing.band_width * (1.0f + 0.1f * (float)evt.GetWheelRotation() / (float)evt.GetWheelDelta()), 10.0f), 1.5f);
                if (m_canvas != nullptr)
                    m_canvas->Refresh();

                return;
            }
        }
    }

    // Inform gizmos about the event so they have the opportunity to react.
    if (m_gizmos.on_mouse_wheel(evt))
        return;

    if (m_canvas_type == CanvasAssembleView && (evt.AltDown() || evt.CmdDown())) {
        float rotation = (float)evt.GetWheelRotation() / (float)evt.GetWheelDelta();
        if (evt.AltDown()) {
            auto clp_dist = m_gizmos.m_assemble_view_data->model_objects_clipper()->get_position();
            clp_dist = rotation < 0.f
                ? std::max(0., clp_dist - 0.01)
                : std::min(1., clp_dist + 0.01);
            m_gizmos.m_assemble_view_data->model_objects_clipper()->set_position(clp_dist, true);
        }
        else if (evt.CmdDown()) {
            m_explosion_ratio = rotation < 0.f
                ? std::max(1., m_explosion_ratio - 0.01)
                : std::min(3., m_explosion_ratio + 0.01);
            if (m_explosion_ratio != GLVolume::explosion_ratio) {
                for (GLVolume* volume : m_volumes.volumes) {
                    volume->set_bounding_boxes_as_dirty();
                }
                GLVolume::explosion_ratio = m_explosion_ratio;
            }
        }
        return;
    }
    // Calculate the zoom delta and apply it to the current zoom factor
#ifdef SUPPORT_REVERSE_MOUSE_ZOOM
    double direction_factor = (wxGetApp().app_config->get("reverse_mouse_wheel_zoom") == "1") ? -1.0 : 1.0;
#else
    double direction_factor = 1.0;
#endif
    auto delta = direction_factor * (double)evt.GetWheelRotation() / (double)evt.GetWheelDelta();
    bool zoom_to_mouse = wxGetApp().app_config->get("zoom_to_mouse") == "true";
    if (!zoom_to_mouse) {// zoom to center
        _update_camera_zoom(delta);
    }
    else {
        auto cnv_size = get_canvas_size();
        Camera& camera = get_active_camera();
        auto screen_center_3d_pos = _mouse_to_3d(camera, { cnv_size.get_width() * 0.5, cnv_size.get_height() * 0.5 });
        auto mouse_3d_pos = _mouse_to_3d(camera, {evt.GetX(), evt.GetY()});
        Vec3d displacement = mouse_3d_pos - screen_center_3d_pos;
        camera.translate(displacement);
        auto origin_zoom = camera.get_zoom();
        _update_camera_zoom(delta);
        auto new_zoom = camera.get_zoom();
        camera.translate((-displacement) / (new_zoom / origin_zoom));
    }
}

void GLCanvas3D::on_timer(wxTimerEvent& evt)
{
    if (m_layers_editing.state == LayersEditing::Editing)
        _perform_layer_editing_action();
}

void GLCanvas3D::on_render_timer(wxTimerEvent& evt)
{
    // no need to wake up idle
    // right after this event, idle event is fired
    // m_dirty = true;
    // wxWakeUpIdle();
}

void GLCanvas3D::on_set_color_timer(wxTimerEvent& evt)
{
    auto obj_list = wxGetApp().obj_list();
    if (m_gizmos.get_current_type() != GLGizmosManager::MmuSegmentation)
        obj_list->set_extruder_for_selected_items(1);
    m_timer_set_color.Stop();
}


void GLCanvas3D::schedule_extra_frame(int miliseconds)
{
    // Schedule idle event right now
    if (miliseconds == 0)
    {
        // We want to wakeup idle evnt but most likely this is call inside render cycle so we need to wait
        if (m_in_render)
            miliseconds = 33;
        else {
            m_dirty = true;
            wxWakeUpIdle();
            return;
        }
    }
    int remaining_time = m_render_timer.GetInterval();
    // Timer is not running
    if (!m_render_timer.IsRunning()) {
        m_render_timer.StartOnce(miliseconds);
    // Timer is running - restart only if new period is shorter than remaning period
    } else {
        if (miliseconds + 20 < remaining_time) {
            m_render_timer.Stop();
            m_render_timer.StartOnce(miliseconds);
        }
    }
}

int GLCanvas3D::get_main_toolbar_item_id(const std::string& name) const
{
    if (!m_main_toolbar) {
        return -1;
    }
    return m_main_toolbar->get_item_id(name);
}

void GLCanvas3D::force_main_toolbar_left_action(int item_id)
{
    if (!m_main_toolbar) {
        return;
    }

    m_dirty |= m_main_toolbar->update_items_state();
    m_main_toolbar->force_left_action(item_id, *this);
}

void GLCanvas3D::force_main_toolbar_right_action(int item_id)
{
    m_main_toolbar->force_right_action(item_id, *this);
}

#ifndef NDEBUG
// #define SLIC3R_DEBUG_MOUSE_EVENTS
#endif

#ifdef SLIC3R_DEBUG_MOUSE_EVENTS
std::string format_mouse_event_debug_message(const wxMouseEvent &evt)
{
	static int idx = 0;
	char buf[2048];
	std::string out;
	sprintf(buf, "Mouse Event %d - ", idx ++);
	out = buf;

	if (evt.Entering())
		out += "Entering ";
	if (evt.Leaving())
		out += "Leaving ";
	if (evt.Dragging())
		out += "Dragging ";
	if (evt.Moving())
		out += "Moving ";
	if (evt.Magnify())
		out += "Magnify ";
	if (evt.LeftDown())
		out += "LeftDown ";
	if (evt.LeftUp())
		out += "LeftUp ";
	if (evt.LeftDClick())
		out += "LeftDClick ";
	if (evt.MiddleDown())
		out += "MiddleDown ";
	if (evt.MiddleUp())
		out += "MiddleUp ";
	if (evt.MiddleDClick())
		out += "MiddleDClick ";
	if (evt.RightDown())
		out += "RightDown ";
	if (evt.RightUp())
		out += "RightUp ";
	if (evt.RightDClick())
		out += "RightDClick ";

	sprintf(buf, "(%d, %d)", evt.GetX(), evt.GetY());
	out += buf;
	return out;
}
#endif /* SLIC3R_DEBUG_MOUSE_EVENTS */

void GLCanvas3D::on_gesture(wxGestureEvent &evt)
{
    if (!m_initialized || !_set_current(true))
        return;

    auto & camera = get_active_camera();
    if (evt.GetEventType() == wxEVT_GESTURE_PAN) {
        auto p = evt.GetPosition();
        auto d = static_cast<wxPanGestureEvent&>(evt).GetDelta();
        float z = 0;
        const Vec3d &p2 = _mouse_to_3d(camera, {p.x, p.y}, &z);
        const Vec3d &p1 = _mouse_to_3d(camera, {p.x - d.x, p.y - d.y}, &z);
        camera.set_target(camera.get_target() + p1 - p2);
    } else if (evt.GetEventType() == wxEVT_GESTURE_ZOOM) {
        static float zoom_start = 1;
        if (evt.IsGestureStart())
            zoom_start = camera.get_zoom();
        camera.set_zoom(zoom_start * static_cast<wxZoomGestureEvent&>(evt).GetZoomFactor());
    } else if (evt.GetEventType() == wxEVT_GESTURE_ROTATE) {
        PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
        bool rotate_limit = current_printer_technology() != ptSLA;
        static double last_rotate = 0;
        if (evt.IsGestureStart())
            last_rotate = 0;
        auto rotate = static_cast<wxRotateGestureEvent&>(evt).GetRotationAngle() - last_rotate;
        last_rotate += rotate;
        if (plate)
            camera.rotate_on_sphere_with_target(-rotate, 0, rotate_limit, plate->get_bounding_box().center());
        else
            camera.rotate_on_sphere(-rotate, 0, rotate_limit);
    }
    m_dirty = true;
}

void GLCanvas3D::on_mouse(wxMouseEvent& evt)
{
    if (!m_initialized || !_set_current(true))
        return;

    // BBS: single snapshot
    Plater::SingleSnapshot single(wxGetApp().plater());

#if ENABLE_RETINA_GL
    const float scale = m_retina_helper->get_scale_factor();
    evt.SetX(evt.GetX() * scale);
    evt.SetY(evt.GetY() * scale);
#endif

    Point pos(evt.GetX(), evt.GetY());

    ImGuiWrapper* imgui = wxGetApp().imgui();
    if (m_tooltip.is_in_imgui() && evt.LeftUp())
        // ignore left up events coming from imgui windows and not processed by them
        m_mouse.ignore_left_up = true;
    m_tooltip.set_in_imgui(false);
    if (imgui->update_mouse_data(evt)) {
        if (evt.LeftDown() && m_canvas != nullptr)
            m_canvas->SetFocus();
        m_mouse.position = evt.Leaving() ? Vec2d(-1.0, -1.0) : pos.cast<double>();
        m_tooltip.set_in_imgui(true);
        render();
#ifdef SLIC3R_DEBUG_MOUSE_EVENTS
        printf((format_mouse_event_debug_message(evt) + " - Consumed by ImGUI\n").c_str());
#endif /* SLIC3R_DEBUG_MOUSE_EVENTS */
        m_dirty = true;
        // do not return if dragging or tooltip not empty to allow for tooltip update
        // also, do not return if the mouse is moving and also is inside MM gizmo to allow update seed fill selection
        if (!m_mouse.dragging && m_tooltip.is_empty() && (m_gizmos.get_current_type() != GLGizmosManager::MmuSegmentation || !evt.Moving()))
            return;
    }

#ifdef __WXMSW__
	bool on_enter_workaround = false;
    if (! evt.Entering() && ! evt.Leaving() && m_mouse.position.x() == -1.0) {
        // Workaround for SPE-832: There seems to be a mouse event sent to the window before evt.Entering()
        m_mouse.position = pos.cast<double>();
        render();
#ifdef SLIC3R_DEBUG_MOUSE_EVENTS
		printf((format_mouse_event_debug_message(evt) + " - OnEnter workaround\n").c_str());
#endif /* SLIC3R_DEBUG_MOUSE_EVENTS */
		on_enter_workaround = true;
    } else
#endif /* __WXMSW__ */
    {
#ifdef SLIC3R_DEBUG_MOUSE_EVENTS
		printf((format_mouse_event_debug_message(evt) + " - other\n").c_str());
#endif /* SLIC3R_DEBUG_MOUSE_EVENTS */
	}

    const auto& p_main_toolbar = get_main_toolbar();
    if (p_main_toolbar) {
        if (p_main_toolbar->on_mouse(evt, *this)) {
            if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
                mouse_up_cleanup();
            m_mouse.set_start_position_3D_as_invalid();
            return;
        }
    }

    if (wxGetApp().plater()->get_collapse_toolbar().on_mouse(evt, *this)) {
        if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
            mouse_up_cleanup();
        m_mouse.set_start_position_3D_as_invalid();
        return;
    }

    // BBS
#if 0
    if (wxGetApp().plater()->get_view_toolbar().on_mouse(evt, *this)) {
        if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
            mouse_up_cleanup();
        m_mouse.set_start_position_3D_as_invalid();
        return;
    }
#endif

    for (GLVolume* volume : m_volumes.volumes) {
        volume->force_sinking_contours = false;
    }

    auto show_sinking_contours = [this]() {
        const Selection::IndicesList& idxs = m_selection.get_volume_idxs();
        for (unsigned int idx : idxs) {
            m_volumes.volumes[idx]->force_sinking_contours = true;
        }
        m_dirty = true;
    };

    if (m_gizmos.on_mouse(evt)) {
        if (m_gizmos.is_running()) {
            _deactivate_arrange_menu();
            _deactivate_orient_menu();
            _deactivate_layersediting_menu();
        }
        if (wxWindow::FindFocus() != m_canvas)
            // Grab keyboard focus for input in gizmo dialogs.
            m_canvas->SetFocus();

        if (evt.LeftDown()) {
            // Clear hover state in main toolbar
            wxMouseEvent evt2 = evt;
            evt2.SetEventType(wxEVT_MOTION);
            evt2.SetLeftDown(false);
            const auto& p_main_toolbar = get_main_toolbar();
            if (p_main_toolbar) {
                p_main_toolbar->on_mouse(evt2, *this);
            }
        }

        if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
            mouse_up_cleanup();

        m_mouse.set_start_position_3D_as_invalid();
        m_mouse.position = pos.cast<double>();

        if (evt.Dragging() && current_printer_technology() == ptFFF && (fff_print()->config().print_sequence == PrintSequence::ByObject)) {
            if (can_sequential_clearance_show_in_gizmo()) {
                update_sequential_clearance();
            }
        }
        else if (evt.Dragging()) {
            switch (m_gizmos.get_current_type())
            {
            case GLGizmosManager::EType::Move:
            case GLGizmosManager::EType::Scale:
            case GLGizmosManager::EType::Rotate:
            {
                show_sinking_contours();
                break;
            }
            default: { break; }
            }
        }
        else if (evt.LeftUp() &&
            m_gizmos.get_current_type() == GLGizmosManager::EType::Scale &&
            m_gizmos.get_current()->get_state() == GLGizmoBase::EState::On) {
            wxGetApp().obj_list()->selection_changed();
        }

        return;
    }

    bool any_gizmo_active = m_gizmos.get_current() != nullptr;

    int selected_object_idx = m_selection.get_object_idx();
    int layer_editing_object_idx = is_layers_editing_enabled() ? selected_object_idx : -1;


    if (m_mouse.drag.move_requires_threshold && m_mouse.is_move_start_threshold_position_2D_defined() && m_mouse.is_move_threshold_met(pos)) {
        m_mouse.drag.move_requires_threshold = false;
        m_mouse.set_move_start_threshold_position_2D_as_invalid();
    }

    if (evt.ButtonDown() && wxWindow::FindFocus() != m_canvas)
        // Grab keyboard focus on any mouse click event.
        m_canvas->SetFocus();

    if (evt.Entering()) {
//#if defined(__WXMSW__) || defined(__linux__)
//        // On Windows and Linux needs focus in order to catch key events
        // Set focus in order to remove it from sidebar fields
        if (m_canvas != nullptr) {
            // Only set focus, if the top level window of this canvas is active.
            auto p = dynamic_cast<wxWindow*>(evt.GetEventObject());
            while (p->GetParent())
                p = p->GetParent();
            auto *top_level_wnd = dynamic_cast<wxTopLevelWindow*>(p);
            if (top_level_wnd && top_level_wnd->IsActive() && !wxGetApp().get_side_menu_popup_status()) {
                // m_canvas->SetFocus();
            }
            m_mouse.position = pos.cast<double>();
            m_tooltip_enabled = false;
            // 1) forces a frame render to ensure that m_hover_volume_idxs is updated even when the user right clicks while
            // the context menu is shown, ensuring it to disappear if the mouse is outside any volume and to
            // change the volume hover state if any is under the mouse
            // 2) when switching between 3d view and preview the size of the canvas changes if the side panels are visible,
            // so forces a resize to avoid multiple renders with different sizes (seen as flickering)
            _refresh_if_shown_on_screen();
            m_tooltip_enabled = true;
        }
        m_mouse.set_start_position_2D_as_invalid();
//#endif
    }
    else if (evt.Leaving()) {
        // to remove hover on objects when the mouse goes out of this canvas
        m_mouse.position = Vec2d(-1.0, -1.0);
        m_dirty = true;
    }
    else if (evt.LeftDClick()) {
        // switch to object panel if double click on object, otherwise switch to global panel if double click on background
        if (selected_object_idx >= 0)
            post_event(SimpleEvent(EVT_GLCANVAS_SWITCH_TO_OBJECT));
        else
            post_event(SimpleEvent(EVT_GLCANVAS_SWITCH_TO_GLOBAL));
    }
    else if (evt.LeftDown() || evt.RightDown() || evt.MiddleDown()) {
        //BBS: add orient deactivate logic
        if (!m_gizmos.on_mouse(evt)) {
            if (_deactivate_arrange_menu() || _deactivate_orient_menu())
                return;
        }

        // If user pressed left or right button we first check whether this happened on a volume or not.
        m_layers_editing.state = LayersEditing::Unknown;
        if (layer_editing_object_idx != -1 && m_layers_editing.bar_rect_contains(*this, pos(0), pos(1))) {
            // A volume is selected and the mouse is inside the layer thickness bar.
            // Start editing the layer height.
            m_layers_editing.state = LayersEditing::Editing;
            _perform_layer_editing_action(&evt);
        }

        else {
            // BBS: define Alt key to enable volume selection mode
            m_selection.set_volume_selection_mode(evt.AltDown() ? Selection::Volume : Selection::Instance);
            if (evt.LeftDown() && evt.ShiftDown() && m_picking_enabled && m_layers_editing.state != LayersEditing::Editing) {
                if (m_gizmos.get_current_type() != GLGizmosManager::SlaSupports
                    && !m_gizmos.is_paint_gizmo()) {
                    m_rectangle_selection.start_dragging(m_mouse.position, evt.ShiftDown() ? GLSelectionRectangle::Select : GLSelectionRectangle::Deselect);
                    m_dirty = true;
                }
            }
            else {
                // Select volume in this 3D canvas.
                // Don't deselect a volume if layer editing is enabled or any gizmo is active. We want the object to stay selected
                // during the scene manipulation.

                if (m_picking_enabled && (!any_gizmo_active || !evt.CmdDown()) && (!m_hover_volume_idxs.empty())) {
                    if (evt.LeftDown() && !m_hover_volume_idxs.empty()) {
                        int volume_idx = get_first_hover_volume_idx();
                        bool already_selected = m_selection.contains_volume(volume_idx);
                        bool ctrl_down = evt.CmdDown();
                        bool alt_down  = evt.AltDown();
                        Selection::IndicesList curr_idxs = m_selection.get_volume_idxs();

                        if (already_selected && ctrl_down)
                            m_selection.remove(volume_idx);
                        else if (alt_down) {
                            Selection::EMode mode = Selection::Volume;
                            if (already_selected) {
                                std::vector<unsigned int> volume_idxs;
                                for (auto idx : curr_idxs) { volume_idxs.emplace_back(idx); }
                                m_selection.remove_volumes(mode, volume_idxs);
                            }
                            std::vector<unsigned int> add_volume_idxs;
                            add_volume_idxs.emplace_back(volume_idx);
                            m_selection.add_volumes(mode, add_volume_idxs, true);
                        }
                        else {
                            m_selection.add(volume_idx, !ctrl_down, true);
                            m_mouse.drag.move_requires_threshold = !already_selected;
                            if (already_selected)
                                m_mouse.set_move_start_threshold_position_2D_as_invalid();
                            else
                                m_mouse.drag.move_start_threshold_position_2D = pos;
                        }

                        // propagate event through callback
                        if (curr_idxs != m_selection.get_volume_idxs()) {
                            if (m_selection.is_empty())
                                m_gizmos.reset_all_states();
                            else
                                m_gizmos.refresh_on_off_state();

                            m_gizmos.update_data();
                            post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
                            m_dirty = true;
                        }
                    }
                }

                if (!m_hover_volume_idxs.empty()) {
                    if (evt.LeftDown() && m_moving_enabled && m_mouse.drag.move_volume_idx == -1) {
                        // Only accept the initial position, if it is inside the volume bounding box.
                        if (!any_gizmo_active || !evt.CmdDown()) {
                            int volume_idx = get_first_hover_volume_idx();
                            BoundingBoxf3 volume_bbox = m_volumes.volumes[volume_idx]->transformed_bounding_box();
                            volume_bbox.offset(1.0);
                            const Camera& camera = wxGetApp().plater()->get_picking_camera();
                            m_mouse.scene_position = _mouse_to_3d(camera, { 0, 1 }, nullptr, OpenGLManager::s_picking_frame);
                            if (volume_bbox.contains(m_mouse.scene_position) && !m_selection.is_any_connector()) {
                                m_volumes.volumes[volume_idx]->hover = GLVolume::HS_None;
                                // The dragging operation is initiated.
                                m_mouse.drag.move_volume_idx = volume_idx;
                                m_selection.start_dragging();
                                m_mouse.drag.start_position_3D = m_mouse.scene_position;
                                m_sequential_print_clearance_first_displacement = true;
                                m_moving = true;
                            }
                        }
                    }
                }
            }
        }
    }
    else if (evt.Dragging() && evt.LeftIsDown() && m_mouse.drag.move_volume_idx != -1 && m_layers_editing.state == LayersEditing::Unknown) {
        if (m_canvas_type != ECanvasType::CanvasAssembleView && m_gizmos.is_allow_drag_volume()) {
            if (!m_mouse.drag.move_requires_threshold) {
                m_mouse.dragging = true;
                Vec3d cur_pos = m_mouse.drag.start_position_3D;
                // we do not want to translate objects if the user just clicked on an object while pressing shift to remove it from the selection and then drag
                if (m_selection.contains_volume(get_first_hover_volume_idx())) {
                    const Camera& camera = get_active_camera();
                    auto          camera_up_down_rad_limit = abs(asin(camera.get_dir_forward()(2) / 1.0f));
                    if (camera_up_down_rad_limit < PI/20.0f) {
                        // side view -> move selected volumes orthogonally to camera view direction
                        Linef3 ray = mouse_ray(pos);
                        Vec3d dir = ray.unit_vector();
                        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
                        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
                        // in our case plane normal and ray direction are the same (orthogonal view)
                        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
                        Vec3d inters = ray.a + (m_mouse.drag.start_position_3D - ray.a).dot(dir) / dir.squaredNorm() * dir;
                        // vector from the starting position to the found intersection
                        Vec3d inters_vec = inters - m_mouse.drag.start_position_3D;

                        Vec3d camera_right = camera.get_dir_right();
                        Vec3d camera_up = camera.get_dir_up();

                        // finds projection of the vector along the camera axes
                        double projection_x = inters_vec.dot(camera_right);
                        double projection_z = inters_vec.dot(camera_up);

                        // apply offset
                        cur_pos = m_mouse.drag.start_position_3D + projection_x * camera_right + projection_z * camera_up;
                        cur_pos[2] = m_mouse.drag.start_position_3D(2);
                    }
                    else {
                        // Generic view
                        // Get new position at the same Z of the initial click point.
                        float z0 = 0.0f;
                        float z1 = 1.0f;
                        cur_pos = Linef3(_mouse_to_3d(camera, pos, &z0), _mouse_to_3d(camera, pos, &z1)).intersect_plane(m_mouse.drag.start_position_3D(2));
                    }
                }
                TransformationType trafo_type;
                trafo_type.set_relative();
                m_selection.translate(cur_pos - m_mouse.drag.start_position_3D, trafo_type);
                if (current_printer_technology() == ptFFF && (fff_print()->config().print_sequence == PrintSequence::ByObject))
                    update_sequential_clearance();
                // BBS
                //wxGetApp().obj_manipul()->set_dirty();
                m_dirty = true;
            }
        }
    }
    else if (evt.Dragging() && evt.LeftIsDown() && m_picking_enabled && m_rectangle_selection.is_dragging()) {
        //BBS not in assemble view
        if (m_canvas_type != ECanvasType::CanvasAssembleView) {
            m_rectangle_selection.dragging(pos.cast<double>());
            m_dirty = true;
        }
    }
    else if (evt.Dragging()) {
        m_mouse.dragging = true;

        if (m_layers_editing.state != LayersEditing::Unknown && layer_editing_object_idx != -1) {
            if (m_layers_editing.state == LayersEditing::Editing) {
                _perform_layer_editing_action(&evt);
                m_mouse.position = pos.cast<double>();
            }
        }
        // do not process the dragging if the left mouse was set down in another canvas
        else if (evt.LeftIsDown()) {
            // if dragging over blank area with left button, rotate
            if ((any_gizmo_active || m_hover_volume_idxs.empty()) && m_mouse.is_start_position_3D_defined()) {
                const Vec3d rot = (Vec3d(pos.x(), pos.y(), 0.) - m_mouse.drag.start_position_3D) * (PI * TRACKBALLSIZE / 180.);
                if (this->m_canvas_type == ECanvasType::CanvasAssembleView || m_gizmos.is_paint_gizmo()) {
                    //BBS rotate around target
                    Camera& camera = get_active_camera();
                    Vec3d rotate_target = Vec3d::Zero();
                    if (!m_selection.is_empty())
                        rotate_target = m_selection.get_bounding_box().center();
                    else
                        rotate_target = volumes_bounding_box(is_volumes_limit_to_expand_plate()).center();
                    //BBS do not limit rotate in assemble view
                    camera.rotate_local_with_target(Vec3d(rot.y(), rot.x(), 0.), rotate_target);
                    //camera.rotate_on_sphere_with_target(rot.x(), rot.y(), false, rotate_target);
                }
                else {
#ifdef SUPPORT_FEEE_CAMERA
                    if (wxGetApp().app_config->get("use_free_camera") == "1")
                        // Virtual track ball (similar to the 3DConnexion mouse).
                        get_active_camera().rotate_local_around_target(Vec3d(rot.y(), rot.x(), 0.));
                    else {
#endif
                        // Forces camera right vector to be parallel to XY plane in case it has been misaligned using the 3D mouse free rotation.
                        // It is cheaper to call this function right away instead of testing wxGetApp().plater()->get_mouse3d_controller().connected(),
                        // which checks an atomics (flushes CPU caches).
                        // See GH issue #3816.
                        Camera& camera = get_active_camera();

                        bool rotate_limit = current_printer_technology() != ptSLA;
                        Vec3d rotate_target = m_selection.get_bounding_box().center();

                        camera.recover_from_free_camera();
                        //BBS modify rotation
                        if (evt.ControlDown() || evt.CmdDown()) {
                            if ((m_rotation_center.x() == 0.f) && (m_rotation_center.y() == 0.f) && (m_rotation_center.z() == 0.f)) {
                                auto canvas_w = float(get_canvas_size().get_width());
                                auto canvas_h = float(get_canvas_size().get_height());
                                Point screen_center(canvas_w/2, canvas_h/2);
                                //camera.rotate_on_sphere_with_target(rot.x(), rot.y(), rotate_limit, wxGetApp().plater()->get_partplate_list().get_bounding_box().center());
                                m_rotation_center = _mouse_to_3d(camera, screen_center);
                                m_rotation_center(2) = 0.f;
                            }
                            camera.rotate_on_sphere_with_target(rot.x(), rot.y(), rotate_limit, m_rotation_center);
                        } else {
                            //BBS rotate with current plate center
                            PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
                            if (plate)
                                camera.rotate_on_sphere_with_target(rot.x(), rot.y(), rotate_limit, plate->get_bounding_box().center());
                            else
                                camera.rotate_on_sphere(rot.x(), rot.y(), rotate_limit);
                        }
#ifdef SUPPORT_FEEE_CAMERA
                    }
#endif
                }

                m_dirty = true;
            }
            if (m_mouse.is_move_threshold_met(pos)) {
                m_mouse.rotating = true;
            }
            m_mouse.drag.start_position_3D = Vec3d((double)pos(0), (double)pos(1), 0.0);
            m_mouse.drag.move_start_threshold_position_2D = pos;
        }
        else if (evt.MiddleIsDown() || evt.RightIsDown()) {
            // If dragging over blank area with right button, pan.
            if (m_mouse.is_start_position_2D_defined()) {
                // get point in model space at Z = 0
                float z = 0.0f;
                Camera& camera = get_active_camera();
                const Vec3d& cur_pos = _mouse_to_3d(camera, pos, &z);
                Vec3d orig = _mouse_to_3d(camera, m_mouse.drag.start_position_2D, &z);
#ifdef SUPPORT_FREE_CAMERA
                if (this->m_canvas_type != ECanvasType::CanvasAssembleView) {
                    if (wxGetApp().app_config->get("use_free_camera") != "1")
                        // Forces camera right vector to be parallel to XY plane in case it has been misaligned using the 3D mouse free rotation.
                        // It is cheaper to call this function right away instead of testing wxGetApp().plater()->get_mouse3d_controller().connected(),
                        // which checks an atomics (flushes CPU caches).
                        // See GH issue #3816.
                        camera.recover_from_free_camera();
                }
#endif

                camera.set_target(camera.get_target() + orig - cur_pos);
                m_dirty = true;
                m_mouse.ignore_right_up = true;
            }
            if (m_mouse.is_move_threshold_met(pos)) {
                m_mouse.panning = true;
            }
            m_mouse.drag.start_position_2D = pos;
            m_mouse.drag.move_start_threshold_position_2D = pos;
        }
    }
    else if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp()) {

        if (evt.LeftUp()) {
            m_selection.stop_dragging();
            m_rotation_center(0) = m_rotation_center(1) = m_rotation_center(2) = 0.f;
        }

        if (m_layers_editing.state != LayersEditing::Unknown) {
            m_layers_editing.state = LayersEditing::Unknown;
            _stop_timer();
            m_layers_editing.accept_changes(*this);
        }
        else if (m_mouse.drag.move_volume_idx != -1 && m_mouse.dragging) {
            do_move(L("Move Object"));
            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            // Let the plater know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
        } else if (evt.LeftUp() && m_hover_volume_idxs.empty() && m_gizmos.is_gizmo_click_empty_not_exit()) {
            // Click on blank and not exit the gizmo tool
        }
        else if (evt.LeftUp() && m_picking_enabled && m_rectangle_selection.is_dragging() && m_layers_editing.state != LayersEditing::Editing) {
            //BBS: don't use alt as de-select
            //if (evt.ShiftDown() || evt.AltDown())
            if (evt.ShiftDown())
                _update_selection_from_hover();

            m_rectangle_selection.stop_dragging();
        }
        else if (evt.LeftUp() && !m_mouse.ignore_left_up && !m_mouse.dragging && m_hover_volume_idxs.empty() && m_hover_plate_idxs.empty() && !is_layers_editing_enabled()) {
            // deselect and propagate event through callback
            if (!evt.ShiftDown() && (!any_gizmo_active || !evt.CmdDown()) && m_picking_enabled)
                deselect_all();
        }
        //BBS Select plate in this 3D canvas.
        else if (evt.LeftUp() && !m_mouse.rotating && !m_mouse.panning && m_picking_enabled && !m_hover_plate_idxs.empty() && (m_canvas_type == CanvasView3D) && !is_layers_editing_enabled())
        {
                int hover_idx = m_hover_plate_idxs.front();
                wxGetApp().plater()->select_plate_by_hover_id(hover_idx);
                //wxGetApp().plater()->get_partplate_list().select_plate_view();
                //deselect all the objects
                if (m_gizmos.get_current_type() != GLGizmosManager::MeshBoolean && m_hover_volume_idxs.empty())
                    deselect_all();
        }
        else if (evt.RightUp() && !is_layers_editing_enabled()) {
            m_mouse.position = pos.cast<double>();
            // forces a frame render to ensure that m_hover_volume_idxs is updated even when the user right clicks while
            // the context menu is already shown
            render();
            if (!m_hover_volume_idxs.empty()) {
                // if right clicking on volume, propagate event through callback (shows context menu)
                int volume_idx = get_first_hover_volume_idx();
                if (!m_volumes.volumes[volume_idx]->is_wipe_tower // no context menu for the wipe tower
                    && m_gizmos.get_current_type() != GLGizmosManager::SlaSupports)  // disable context menu when the gizmo is open
                {
                    // forces the selection of the volume
                    /* m_selection.add(volume_idx); // #et_FIXME_if_needed
                     * To avoid extra "Add-Selection" snapshots,
                     * call add() with check_for_already_contained=true
                     * */
                    m_selection.add(volume_idx, true, true);
                    m_gizmos.refresh_on_off_state();
                    post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
                    m_gizmos.update_data();
                    // BBS
                    //wxGetApp().obj_manipul()->set_dirty();
                    // forces a frame render to update the view before the context menu is shown
                    render();
                }
            }

            //BBS change plate selection
            if (!m_hover_plate_idxs.empty() && (m_canvas_type == CanvasView3D) && !m_mouse.dragging) {
                int hover_idx = m_hover_plate_idxs.front();
                wxGetApp().plater()->select_plate_by_hover_id(hover_idx, true);
                if (m_hover_volume_idxs.empty())
                    deselect_all();
                render();
            }

            Vec2d logical_pos = pos.cast<double>();
#if ENABLE_RETINA_GL
            const float factor = m_retina_helper->get_scale_factor();
            logical_pos = logical_pos.cwiseQuotient(Vec2d(factor, factor));
#endif // ENABLE_RETINA_GL

            if (!m_mouse.ignore_right_up && m_gizmos.get_current_type() == GLGizmosManager::EType::Undefined) {
                //BBS post right click event
                if (!m_hover_plate_idxs.empty()) {
                    post_event(RBtnPlateEvent(EVT_GLCANVAS_PLATE_RIGHT_CLICK, { logical_pos, m_hover_plate_idxs.front() }));
                }
                else {
                    // do not post the event if the user is panning the scene
                    // or if right click was done over the wipe tower
                    bool post_right_click_event = m_hover_volume_idxs.empty() || !m_volumes.volumes[get_first_hover_volume_idx()]->is_wipe_tower;
                    if (post_right_click_event)
                        post_event(RBtnEvent(EVT_GLCANVAS_RIGHT_CLICK, { logical_pos, m_hover_volume_idxs.empty() }));
                }
            }
        }

        mouse_up_cleanup();
    }
    else if (evt.Moving()) {
        m_mouse.position = pos.cast<double>();

        // updates gizmos overlay
        if (m_selection.is_empty())
            m_gizmos.reset_all_states();

        m_dirty = true;
    }
    else
        evt.Skip();

            // Detection of doubleclick on text to open emboss edit window
    auto type = m_gizmos.get_current_type();
    if (evt.LeftDClick() && !m_hover_volume_idxs.empty() &&
        (type == GLGizmosManager::EType::Undefined ||
            type == GLGizmosManager::EType::Text ||
            type == GLGizmosManager::EType::Svg
        )) {
        for (int hover_volume_id : m_hover_volume_idxs) {
            const GLVolume &hover_gl_volume = *m_volumes.volumes[hover_volume_id];
            int             object_idx      = hover_gl_volume.object_idx();
            if (object_idx < 0 || static_cast<size_t>(object_idx) >= m_model->objects.size())
                continue;
            const ModelObject *hover_object     = m_model->objects[object_idx];
            int                hover_volume_idx = hover_gl_volume.volume_idx();
            if (hover_volume_idx < 0 || static_cast<size_t>(hover_volume_idx) >= hover_object->volumes.size())
                continue;
            const ModelVolume *hover_volume = hover_object->volumes[hover_volume_idx];

            if (hover_volume->is_text()) {
                m_selection.add_volumes(Selection::EMode::Volume, {(unsigned) hover_volume_id});
                if (type == GLGizmosManager::EType::Text)
                    m_gizmos.open_gizmo(GLGizmosManager::EType::Text); // close text
                wxGetApp().obj_list()->update_selections();
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Edit existed text by double click");
                m_gizmos.open_gizmo(GLGizmosManager::EType::Text);
                return;
            }
           /* else if (hover_volume->text_configuration.has_value()) {
                m_selection.add_volumes(Selection::EMode::Volume, {(unsigned) hover_volume_id});
                if (type != GLGizmosManager::EType::Emboss) m_gizmos.open_gizmo(GLGizmosManager::EType::Emboss);
                wxGetApp().obj_list()->update_selections();
                return;
            }*/
            else if(hover_volume->emboss_shape.has_value()){
                m_selection.add_volumes(Selection::EMode::Volume, {(unsigned) hover_volume_id});
                wxGetApp().obj_list()->update_selections();
                if (m_main_toolbar) {
                    const auto svg_item_name = GLGizmosManager::convert_gizmo_type_to_string(GLGizmosManager::EType::Svg);
                    if (!m_main_toolbar->is_item_pressed(svg_item_name)) {
                        force_main_toolbar_left_action(get_main_toolbar_item_id(svg_item_name));
                    }
                }
                return;
            }
        }
    }

    if (m_moving)
        show_sinking_contours();

#ifdef __WXMSW__
	if (on_enter_workaround)
		m_mouse.position = Vec2d(-1., -1.);
#endif /* __WXMSW__ */
}

void GLCanvas3D::on_paint(wxPaintEvent& evt)
{
    if (m_initialized)
        m_dirty = true;
    else
        // Call render directly, so it gets initialized immediately, not from On Idle handler.
        this->render();
}

void GLCanvas3D::on_kill_focus(wxFocusEvent &evt)
{
    ImGui::SetWindowFocus(nullptr);
    render();
    evt.Skip();
}

void GLCanvas3D::force_set_focus() {
    m_canvas->SetFocus();
};

void GLCanvas3D::on_set_focus(wxFocusEvent& evt)
{
    m_tooltip_enabled = false;
    _refresh_if_shown_on_screen();
    m_tooltip_enabled = true;
}

Size GLCanvas3D::get_canvas_size() const
{
    int w = 0;
    int h = 0;

    if (m_canvas != nullptr)
        m_canvas->GetSize(&w, &h);

#if ENABLE_RETINA_GL
    const float factor = m_retina_helper->get_scale_factor();
    w *= factor;
    h *= factor;
#else
    const float factor = 1.0f;
#endif

    return Size(w, h, factor);
}

Vec2d GLCanvas3D::get_local_mouse_position() const
{
    if (m_canvas == nullptr)
		return Vec2d::Zero();

    wxPoint mouse_pos = m_canvas->ScreenToClient(wxGetMousePosition());
    const double factor =
#if ENABLE_RETINA_GL
        m_retina_helper->get_scale_factor();
#else
        1.0;
#endif
    return Vec2d(factor * mouse_pos.x, factor * mouse_pos.y);
}

void GLCanvas3D::set_tooltip(const std::string& tooltip)
{
    if (m_canvas != nullptr)
        m_tooltip.set_text(tooltip);
}

void GLCanvas3D::do_move(const std::string &snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(snapshot_type);

    std::set<std::pair<int, int>> done;  // keeps track of modified instances
    bool object_moved = false;

    // BBS: support wipe-tower for multi-plates
    int n_plates = wxGetApp().plater()->get_partplate_list().get_plate_count();
    std::vector<Vec3d> wipe_tower_origins(n_plates, Vec3d::Zero());

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes) {
        int object_idx = v->object_idx();
        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        std::pair<int, int> done_id(object_idx, instance_idx);

        if (0 <= object_idx && object_idx < (int)m_model->objects.size()) {
            done.insert(done_id);

            // Move instances/volumes
            ModelObject* model_object = m_model->objects[object_idx];
            if (model_object != nullptr) {
                if (selection_mode == Selection::Instance) {
                    if (m_canvas_type == GLCanvas3D::ECanvasType::CanvasAssembleView) {
                        if ((model_object->instances[instance_idx]->get_assemble_offset() - v->get_instance_offset()).norm() > 1e-2) {
                            model_object->instances[instance_idx]->set_assemble_offset(v->get_instance_offset());
                        }
                    } else {
                        model_object->instances[instance_idx]->set_offset(v->get_instance_offset());
                    }
                }
                else if (selection_mode == Selection::Volume) {
                    auto cur_mv = model_object->volumes[volume_idx];
                    if (cur_mv->get_offset() != v->get_volume_offset()) {
                        cur_mv->set_transformation(v->get_volume_transformation());
                        // BBS: backup
                        Slic3r::save_object_mesh(*model_object);
                    }
                }

                object_moved = true;
                model_object->invalidate_bounding_box();
            }
        }
        else if (object_idx >= 1000 && object_idx < 1000 + n_plates) {
            // Move a wipe tower proxy.
            wipe_tower_origins[object_idx - 1000] = v->get_volume_offset();
        }
    }

    //BBS: notify instance updates to part plater list
    m_selection.notify_instance_update(-1, 0);

    // Fixes flying instances
    for (const std::pair<int, int>& i : done) {
        ModelObject* m = m_model->objects[i.first];
        const double shift_z = m->get_instance_min_z(i.second);
        //BBS: don't call translate if the z is zero
        if ((current_printer_technology() == ptSLA || shift_z > SINKING_Z_THRESHOLD) && (shift_z != 0.0f)) {
            const Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
            //BBS: notify instance updates to part plater list
            m_selection.notify_instance_update(i.first, i.second);
        }
        wxGetApp().obj_list()->update_info_items(static_cast<size_t>(i.first));
    }
    //BBS: nofity object list to update
    wxGetApp().plater()->sidebar().obj_list()->update_plate_values_for_items();

    if (object_moved)
        post_event(SimpleEvent(EVT_GLCANVAS_INSTANCE_MOVED));

    // BBS: support wipe-tower for multi-plates
    for (int plate_id = 0; plate_id < wipe_tower_origins.size(); plate_id++) {
        Vec3d& wipe_tower_origin = wipe_tower_origins[plate_id];
        if (wipe_tower_origin == Vec3d::Zero())
            continue;

        PartPlateList& ppl = wxGetApp().plater()->get_partplate_list();
        DynamicConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
        Vec3d plate_origin = ppl.get_plate(plate_id)->get_origin();
        ConfigOptionFloat wipe_tower_x(wipe_tower_origin(0) - plate_origin(0));
        ConfigOptionFloat wipe_tower_y(wipe_tower_origin(1) - plate_origin(1));

        ConfigOptionFloats* wipe_tower_x_opt = proj_cfg.option<ConfigOptionFloats>("wipe_tower_x", true);
        ConfigOptionFloats* wipe_tower_y_opt = proj_cfg.option<ConfigOptionFloats>("wipe_tower_y", true);
        wipe_tower_x_opt->set_at(&wipe_tower_x, plate_id, 0);
        wipe_tower_y_opt->set_at(&wipe_tower_y, plate_id, 0);
    }

    reset_sequential_print_clearance();

    m_dirty = true;
}

void GLCanvas3D::do_rotate(const std::string& snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(snapshot_type);

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i) {
        const ModelObject* obj = m_model->objects[i];
        for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j) {
            if (snapshot_type.empty() && m_selection.get_object_idx() == i) {
                // This means we are flattening this object. In that case pretend
                // that it is not sinking (even if it is), so it is placed on bed
                // later on (whatever is sinking will be left sinking).
                min_zs[{ i, j }] = SINKING_Z_THRESHOLD;
            } else
                min_zs[{ i, j }] = obj->instance_bounding_box(j).min.z();

        }
    }

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes) {
        int object_idx = v->object_idx();
        if (object_idx < 0 || (int)m_model->objects.size() <= object_idx)
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Rotate instances/volumes.
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr) {
            if (selection_mode == Selection::Instance) {
                if (m_canvas_type == GLCanvas3D::ECanvasType::CanvasAssembleView) {
                    model_object->instances[instance_idx]->set_assemble_from_transform(v->get_instance_transformation().get_matrix());
                } else {
                    model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
                }
            }
            else if (selection_mode == Selection::Volume) {
                auto cur_mv = model_object->volumes[volume_idx];
                if (cur_mv->get_rotation() != v->get_volume_rotation()) {
                    cur_mv->set_transformation(v->get_volume_transformation());
                    // BBS: backup
                    Slic3r::save_object_mesh(*model_object);
                }
            }
            model_object->invalidate_bounding_box();
        }
    }

    //BBS: notify instance updates to part plater list
    m_selection.notify_instance_update(-1, -1);
    if (m_canvas_type != CanvasAssembleView) {
        // Fixes sinking/flying instances
        for (const std::pair<int, int> &i : done) {
            ModelObject *m = m_model->objects[i.first];

            // BBS: don't call translate if the z is zero
            const double shift_z = m->get_instance_min_z(i.second);
            // leave sinking instances as sinking
            if ((min_zs.find({i.first, i.second})->second >= SINKING_Z_THRESHOLD || shift_z > SINKING_Z_THRESHOLD) && (shift_z != 0.0f)) {
                const Vec3d shift(0.0, 0.0, -shift_z);
                m_selection.translate(i.first, i.second, shift);
                m->translate_instance(i.second, shift);
                // BBS: notify instance updates to part plater list
                m_selection.notify_instance_update(i.first, i.second);
            }

            wxGetApp().obj_list()->update_info_items(static_cast<size_t>(i.first));
        }
    }
    //BBS: nofity object list to update
    wxGetApp().plater()->sidebar().obj_list()->update_plate_values_for_items();

    if (!done.empty())
        post_event(SimpleEvent(EVT_GLCANVAS_INSTANCE_ROTATED));

    m_dirty = true;
}

void GLCanvas3D::do_scale(const std::string& snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(snapshot_type);

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    if (!snapshot_type.empty()) {
        for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i) {
            const ModelObject* obj = m_model->objects[i];
            for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j) {
                min_zs[{ i, j }] = obj->instance_bounding_box(j).min.z();
            }
        }
    }

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes) {
        int object_idx = v->object_idx();
        if (object_idx < 0 || (int)m_model->objects.size() <= object_idx)
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Rotate instances/volumes
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr) {
            if (selection_mode == Selection::Instance) {
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            }
            else if (selection_mode == Selection::Volume) {
                auto cur_mv = model_object->volumes[volume_idx];
                if (cur_mv->get_scaling_factor() != v->get_volume_scaling_factor()) {
                    model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
                    cur_mv->set_transformation(v->get_volume_transformation());
                    // BBS: backup
                    Slic3r::save_object_mesh(*model_object);
                }
            }
            model_object->invalidate_bounding_box();
        }
    }

    //BBS: notify instance updates to part plater list
    m_selection.notify_instance_update(-1, -1);

    // Fixes sinking/flying instances
    for (const std::pair<int, int>& i : done) {
        ModelObject* m = m_model->objects[i.first];

        //BBS: don't call translate if the z is zero
        double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if ((min_zs.empty() || min_zs.find({ i.first, i.second })->second >= SINKING_Z_THRESHOLD || shift_z > SINKING_Z_THRESHOLD) && (shift_z != 0.0f)) {
            Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
            //BBS: notify instance updates to part plater list
            m_selection.notify_instance_update(i.first, i.second);
        }
        wxGetApp().obj_list()->update_info_items(static_cast<size_t>(i.first));
    }
    //BBS: nofity object list to update
    wxGetApp().plater()->sidebar().obj_list()->update_plate_values_for_items();
    //BBS: notify object info update
    wxGetApp().plater()->show_object_info();

    if (!done.empty())
        post_event(SimpleEvent(EVT_GLCANVAS_INSTANCE_SCALED));

    m_dirty = true;
}

void GLCanvas3D::do_flatten(const Vec3d& normal, const std::string& snapshot_type)
{
    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(snapshot_type);

    m_selection.flattening_rotate(normal);
    do_rotate(""); // avoid taking another snapshot
}

void GLCanvas3D::do_center()
{
    if (m_model == nullptr)
        return;

    m_selection.center();
}

void GLCanvas3D::do_center_plate(const int plate_idx) {
    if (m_model == nullptr)
        return;

    m_selection.center_plate(plate_idx);
}

void GLCanvas3D::do_mirror(const std::string& snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(snapshot_type);

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    if (!snapshot_type.empty()) {
        for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i) {
            const ModelObject* obj = m_model->objects[i];
            for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j) {
                min_zs[{ i, j }] = obj->instance_bounding_box(j).min.z();
            }
        }
    }

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes) {
        int object_idx = v->object_idx();
        if (object_idx < 0 || (int)m_model->objects.size() <= object_idx)
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Mirror instances/volumes
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr) {
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            else if (selection_mode == Selection::Volume) {
                if (model_object->volumes[volume_idx]->get_transformation() != v->get_volume_transformation()) {
                    model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());
                    // BBS: backup
                    Slic3r::save_object_mesh(*model_object);
                }
            }

            model_object->invalidate_bounding_box();
        }
    }

    //BBS: notify instance updates to part plater list
    m_selection.notify_instance_update(-1, -1);

    // Fixes sinking/flying instances
    for (const std::pair<int, int>& i : done) {
        ModelObject* m = m_model->objects[i.first];

        //BBS: don't call translate if the z is zero
        double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if ((min_zs.empty() || min_zs.find({ i.first, i.second })->second >= SINKING_Z_THRESHOLD || shift_z > SINKING_Z_THRESHOLD)&&(shift_z != 0.0f)) {
            Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
            //BBS: notify instance updates to part plater list
            m_selection.notify_instance_update(i.first, i.second);
        }
        wxGetApp().obj_list()->update_info_items(static_cast<size_t>(i.first));

        //BBS: notify instance updates to part plater list
        PartPlateList &plate_list = wxGetApp().plater()->get_partplate_list();
        plate_list.notify_instance_update(i.first, i.second);

        //BBS: nofity object list to update
        wxGetApp().plater()->sidebar().obj_list()->update_plate_values_for_items();
    }
    //BBS: nofity object list to update
    wxGetApp().plater()->sidebar().obj_list()->update_plate_values_for_items();

    post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));

    m_dirty = true;
}

void GLCanvas3D::update_gizmos_on_off_state()
{
    set_as_dirty();
    m_gizmos.update_data();
    m_gizmos.refresh_on_off_state();
}

void GLCanvas3D::handle_sidebar_focus_event(const std::string& opt_key, bool focus_on)
{
    m_sidebar_field = focus_on ? opt_key : "";

    //BBS: this event was sent from gizmo now, no need to clear gizmo
    //if (!m_sidebar_field.empty())
    //    m_gizmos.reset_all_states();

    m_dirty = true;
}

void GLCanvas3D::handle_layers_data_focus_event(const t_layer_height_range range, const EditorType type)
{
    std::string field = "layer_" + std::to_string(type) + "_" + std::to_string(range.first) + "_" + std::to_string(range.second);
    m_gizmos.reset_all_states();
    handle_sidebar_focus_event(field, true);
}

void GLCanvas3D::update_ui_from_settings()
{
    m_dirty = true;

#if __APPLE__
    // Update OpenGL scaling on OSX after the user toggled the "use_retina_opengl" settings in Preferences dialog.
    const float orig_scaling = m_retina_helper->get_scale_factor();

    const bool use_retina = true;
    BOOST_LOG_TRIVIAL(debug) << "GLCanvas3D: Use Retina OpenGL: " << use_retina;
    m_retina_helper->set_use_retina(use_retina);
    const float new_scaling = m_retina_helper->get_scale_factor();

    if (new_scaling != orig_scaling) {
        BOOST_LOG_TRIVIAL(debug) << "GLCanvas3D: Scaling factor: " << new_scaling;

        Camera& camera = get_active_camera();
        camera.set_zoom(camera.get_zoom() * new_scaling / orig_scaling);
        _refresh_if_shown_on_screen();
    }
#endif // ENABLE_RETINA_GL
}

// BBS: add partplate logic
GLCanvas3D::WipeTowerInfo GLCanvas3D::get_wipe_tower_info(int plate_idx) const
{
    WipeTowerInfo wti;

    for (const GLVolume* vol : m_volumes.volumes) {
        if (vol->is_wipe_tower && vol->object_idx() - 1000 == plate_idx) {
            DynamicPrintConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
            wti.m_pos = Vec2d(proj_cfg.opt<ConfigOptionFloats>("wipe_tower_x")->get_at(plate_idx),
                              proj_cfg.opt<ConfigOptionFloats>("wipe_tower_y")->get_at(plate_idx));
            // BBS: don't support rotation
            //wti.m_rotation = (M_PI/180.) * proj_cfg->opt_float("wipe_tower_rotation_angle");

            auto& preset = wxGetApp().preset_bundle->prints.get_edited_preset();
            float wt_brim_width = preset.config.opt_float("prime_tower_brim_width");

            const BoundingBoxf3& bb = vol->bounding_box();
            if (wt_brim_width < 0) wt_brim_width = WipeTower::get_auto_brim_by_height((float)bb.max.z());
            wti.m_bb = BoundingBoxf{to_2d(bb.min), to_2d(bb.max)};
            wti.m_bb.offset(wt_brim_width);

            float brim_width = wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_float("prime_tower_brim_width");
            if (brim_width < 0) brim_width = WipeTower::get_auto_brim_by_height((float) bb.max.z());
            wti.m_bb.offset((brim_width));

            // BBS: the wipe tower pos might be outside bed
            PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_plate(plate_idx);
            Vec2d plate_size = plate->get_size();
            wti.m_pos.x() = std::clamp(wti.m_pos.x(), 0.0, plate_size(0) - wti.m_bb.size().x());
            wti.m_pos.y() = std::clamp(wti.m_pos.y(), 0.0, plate_size(1) - wti.m_bb.size().y());

            // BBS: add partplate logic
            wti.m_plate_idx = plate_idx;
            break;
        }
    }

    return wti;
}

Linef3 GLCanvas3D::mouse_ray(const Point& mouse_pos)
{
    float z0 = 0.0f;
    float z1 = 1.0f;
    const Camera& camera = get_active_camera();
    return Linef3(_mouse_to_3d(camera, mouse_pos, &z0), _mouse_to_3d(camera, mouse_pos, &z1));
}

double GLCanvas3D::get_size_proportional_to_max_bed_size(double factor) const
{
    const BoundingBoxf& bbox = m_bed.build_volume().bounding_volume2d();
    return factor * std::max(bbox.size()[0], bbox.size()[1]);
}

//BBS
std::vector<Vec2f> GLCanvas3D::get_empty_cells(const Vec2f start_point, const Vec2f step)
{
    PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
    BoundingBoxf3 build_volume = plate->get_build_volume();
    Vec2d vmin(build_volume.min.x(), build_volume.min.y()), vmax(build_volume.max.x(), build_volume.max.y());
    BoundingBoxf bbox(vmin, vmax);
    std::vector<Vec2f> cells;
    auto min_x = start_point.x() - step(0) * int((start_point.x() - bbox.min.x()) / step(0));
    auto min_y = start_point.y() - step(1) * int((start_point.y() - bbox.min.y()) / step(1));
    for (float x = min_x; x < bbox.max.x() - step(0) / 2; x += step(0))
        for (float y = min_y; y < bbox.max.y() - step(1) / 2; y += step(1))
        {
            cells.emplace_back(x, y);
        }
    for (size_t i = 0; i < m_model->objects.size(); ++i) {
        ModelObject* model_object = m_model->objects[i];
        auto id = model_object->id().id;
        ModelInstance* model_instance0 = model_object->instances.front();
        Geometry::Transformation temp(model_instance0->get_transformation());
        temp.set_offset(Vec3d(0.0, 0.0, model_instance0->get_offset().z()));
        Polygon hull_2d = model_object->convex_hull_2d(temp.get_matrix());
        if (hull_2d.empty())
            continue;

        const auto& instances = model_object->instances;
        double rotation_z0 = instances.front()->get_rotation().z();
        for (const auto& instance : instances) {
            Geometry::Transformation transformation;
            const Vec3d& offset = instance->get_offset();
            transformation.set_offset({ scale_(offset.x()), scale_(offset.y()), 0.0 });
            transformation.set_rotation({0, 0, instance->get_rotation().z() - rotation_z0});
            const Transform3d& trafo = transformation.get_matrix();
            Polygon inst_hull_2d = hull_2d.transform(trafo);

            for (auto it = cells.begin(); it != cells.end(); )
            {
                if (inst_hull_2d.contains(Point(scale_(it->x()), scale_(it->y()))))
                    it = cells.erase(it);
                else
                    it++;
            }
        }
    }

    Vec2f start = start_point;
    if (start_point(0) < 0 && start_point(1) < 0) {
        start(0) = bbox.center()(0);
        start(1) = bbox.center()(1);
    }
    std::sort(cells.begin(), cells.end(), [start](const Vec2f& cell1, const Vec2f& cell2) {return (cell1 - start).norm() < (cell2 - start).norm(); });
    return cells;
}

Vec2f GLCanvas3D::get_nearest_empty_cell(const Vec2f start_point, const Vec2f step)
{
    std::vector<Vec2f> empty_cells = get_empty_cells(start_point, step);
    if (!empty_cells.empty())
        return empty_cells.front();
    else {
        double offset = get_size_proportional_to_max_bed_size(0.05);
        return { start_point(0) + offset, start_point(1) + offset };
    }
}

void GLCanvas3D::set_cursor(ECursorType type)
{
    if ((m_canvas != nullptr) && (m_cursor_type != type))
    {
        switch (type)
        {
        case Standard: { m_canvas->SetCursor(*wxSTANDARD_CURSOR); break; }
        case Cross: { m_canvas->SetCursor(*wxCROSS_CURSOR); break; }
        }

        m_cursor_type = type;
    }
}

void GLCanvas3D::msw_rescale()
{
}

bool GLCanvas3D::has_toolpaths_to_export() const
{
    return m_gcode_viewer.can_export_toolpaths();
}

void GLCanvas3D::export_toolpaths_to_obj(const char* filename) const
{
    m_gcode_viewer.export_toolpaths_to_obj(filename);
}

void GLCanvas3D::mouse_up_cleanup()
{
    m_moving = false;
    m_mouse.drag.move_volume_idx = -1;
    m_mouse.set_start_position_3D_as_invalid();
    m_mouse.set_start_position_2D_as_invalid();
    m_mouse.dragging = false;
    m_mouse.rotating = false;
    m_mouse.panning = false;
    m_mouse.ignore_left_up = false;
    m_mouse.ignore_right_up = false;
    m_dirty = true;

    if (m_canvas->HasCapture())
        m_canvas->ReleaseMouse();
}

bool GLCanvas3D::can_sequential_clearance_show_in_gizmo() {
    switch (m_gizmos.get_current_type()) {
    case GLGizmosManager::EType::Move:
    case GLGizmosManager::EType::Scale:
    case GLGizmosManager::EType::Rotate: {
        return true;
    }
    }
    return false;
}

void GLCanvas3D::update_sequential_clearance()
{
    if (current_printer_technology() != ptFFF || (fff_print()->config().print_sequence == PrintSequence::ByLayer))
        return;

    if (m_gizmos.is_dragging())
        return;

    // collects instance transformations from volumes
    // first define temporary cache
    unsigned int instances_count = 0;
    std::vector<std::vector<std::optional<Geometry::Transformation>>> instance_transforms;
    for (size_t obj = 0; obj < m_model->objects.size(); ++obj) {
        instance_transforms.emplace_back(std::vector<std::optional<Geometry::Transformation>>());
        const ModelObject* model_object = m_model->objects[obj];
        for (size_t i = 0; i < model_object->instances.size(); ++i) {
            instance_transforms[obj].emplace_back(std::optional<Geometry::Transformation>());
            ++instances_count;
        }
    }

    //if (instances_count == 1)
    //    return;

    // second fill temporary cache with data from volumes
    for (const GLVolume* v : m_volumes.volumes) {
        if (v->is_modifier || v->is_wipe_tower)
            continue;

        auto& transform = instance_transforms[v->object_idx()][v->instance_idx()];
        if (!transform.has_value())
            transform = v->get_instance_transformation();
    }

    // calculates objects 2d hulls (see also: Print::sequential_print_horizontal_clearance_valid())
    // this is done only the first time this method is called while moving the mouse,
    // the results are then cached for following displacements
    if (m_sequential_print_clearance_first_displacement) {
        m_sequential_print_clearance.m_hull_2d_cache.clear();
        bool all_objects_are_short = std::all_of(fff_print()->objects().begin(), fff_print()->objects().end(), \
            [&](PrintObject* obj) { return obj->height() < scale_(fff_print()->config().nozzle_height.value - MARGIN_HEIGHT); });
        float shrink_factor;
        if (all_objects_are_short)
            shrink_factor = scale_(0.5 * MAX_OUTER_NOZZLE_RADIUS - 0.1);
        else
            shrink_factor = static_cast<float>(scale_(0.5 * fff_print()->config().extruder_clearance_max_radius.value - EPSILON));

        double mitter_limit = scale_(0.1);
        m_sequential_print_clearance.m_hull_2d_cache.reserve(m_model->objects.size());
        for (size_t i = 0; i < m_model->objects.size(); ++i) {
            ModelObject* model_object = m_model->objects[i];
            ModelInstance* model_instance0 = model_object->instances.front();
            Geometry::Transformation temp(model_instance0->get_transformation());
            temp.set_offset(Vec3d(0.0, 0.0, model_instance0->get_offset().z()));
            Polygon hull_no_offset = model_object->convex_hull_2d(temp.get_matrix());
            auto tmp = offset(hull_no_offset,
                // Shrink the extruder_clearance_max_radius a tiny bit, so that if the object arrangement algorithm placed the objects
                // exactly by satisfying the extruder_clearance_max_radius, this test will not trigger collision.
                shrink_factor,
                jtRound, mitter_limit);
            Polygon hull_2d = !tmp.empty() ? tmp.front() : hull_no_offset;// tmp may be empty due to clipper's bug, see STUDIO-2452

            Pointf3s& cache_hull_2d = m_sequential_print_clearance.m_hull_2d_cache.emplace_back(Pointf3s());
            cache_hull_2d.reserve(hull_2d.points.size());
            for (const Point& p : hull_2d.points) {
                cache_hull_2d.emplace_back(unscale<double>(p.x()), unscale<double>(p.y()), 0.0);
            }
        }
        m_sequential_print_clearance_first_displacement = false;
    }

    // calculates instances 2d hulls (see also: Print::sequential_print_horizontal_clearance_valid())
    //BBS: add the height logic
    PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
    Polygons polygons;
    std::vector<std::pair<Polygon, float>> height_polygons;
    polygons.reserve(instances_count);
    height_polygons.reserve(instances_count);
    std::vector<struct height_info> convex_and_bounding_boxes;
    struct height_info
    {
        double         instance_height;
        BoundingBox    bounding_box;
        Polygon        hull_polygon;
    };
    for (size_t i = 0; i < instance_transforms.size(); ++i) {
        const auto& instances = instance_transforms[i];
        double rotation_z0 = instances.front()->get_rotation().z();
        int index = 0;
        for (const auto& instance : instances) {
            Geometry::Transformation transformation;
            const Vec3d& offset = instance->get_offset();
            transformation.set_offset({ offset.x(), offset.y(), 0.0 });
            transformation.set_rotation({0, 0, instance->get_rotation().z() - rotation_z0});
            const Transform3d& trafo = transformation.get_matrix();
            const Pointf3s& hull_2d = m_sequential_print_clearance.m_hull_2d_cache[i];
            Points inst_pts;
            inst_pts.reserve(hull_2d.size());
            for (size_t j = 0; j < hull_2d.size(); ++j) {
                const Vec3d p = trafo * hull_2d[j];
                inst_pts.emplace_back(scaled<double>(p.x()), scaled<double>(p.y()));
            }
            Polygon convex_hull(std::move(inst_pts));
            BoundingBox bouding_box = convex_hull.bounding_box();
            BoundingBox plate_bb = plate->get_bounding_box_crd();
            double instance_height = m_model->objects[i]->get_instance_max_z(index++);
            //skip the object for not current plate
            if (!plate_bb.overlap(bouding_box))
                continue;
            convex_and_bounding_boxes.push_back({instance_height, bouding_box, convex_hull});
            polygons.emplace_back(std::move(convex_hull));
        }
    }

    //sort the print instance
    std::sort(convex_and_bounding_boxes.begin(), convex_and_bounding_boxes.end(),
        [](auto &l, auto &r) {
            auto ly1 = l.bounding_box.min.y();
            auto ly2 = l.bounding_box.max.y();
            auto ry1 = r.bounding_box.min.y();
            auto ry2 = r.bounding_box.max.y();
            auto inter_min = std::max(ly1, ry1);
            auto inter_max = std::min(ly2, ry2);
            auto lx = l.bounding_box.min.x();
            auto rx = r.bounding_box.min.x();
            if (inter_max - inter_min > 0)
                return (lx < rx) || ((lx == rx)&&(ly1 < ry1));
            else
                return (ly1 < ry1);
        });

    /*bool has_interlaced_objects = false;
    for (int k = 0; k < bounding_box_count; k++)
    {
        Polygon& convex = convex_and_bounding_boxes[k].hull_polygon;
        BoundingBox& bbox = convex_and_bounding_boxes[k].bounding_box;
        auto iy1 = bbox.min.y();
        auto iy2 = bbox.max.y();

        for (int i = k+1; i < bounding_box_count; i++)
        {
            Polygon&     next_convex = convex_and_bounding_boxes[i].hull_polygon;
            BoundingBox& next_bbox   = convex_and_bounding_boxes[i].bounding_box;
            auto py1 = next_bbox.min.y();
            auto py2 = next_bbox.max.y();
            auto inter_min = std::max(iy1, py1); // min y of intersection
            auto inter_max = std::min(iy2, py2); // max y of intersection. length=max_y-min_y>0 means intersection exists
            if (inter_max - inter_min > 0) {
                has_interlaced_objects = true;
                break;
            }
        }
        if (has_interlaced_objects)
            break;
    }*/

    int bounding_box_count = convex_and_bounding_boxes.size();
    double printable_height = fff_print()->config().printable_height;
    double hc1 = fff_print()->config().extruder_clearance_height_to_lid;
    double hc2 = fff_print()->config().extruder_clearance_height_to_rod;
    for (int k = 0; k < bounding_box_count; k++)
    {
        Polygon& convex = convex_and_bounding_boxes[k].hull_polygon;
        BoundingBox& bbox = convex_and_bounding_boxes[k].bounding_box;
        auto iy1 = bbox.min.y();
        auto iy2 = bbox.max.y();
        double height = (k == (bounding_box_count - 1))?printable_height:hc1;

        /*if (has_interlaced_objects) {
            if ((k < (bounding_box_count - 1)) && (convex_and_bounding_boxes[k].instance_height > hc2)) {
                height_polygons.emplace_back(std::make_pair(convex, hc2));
            }
        }
        else {
            if ((k < (bounding_box_count - 1)) && (convex_and_bounding_boxes[k].instance_height > hc1)) {
                height_polygons.emplace_back(std::make_pair(convex, hc1));
            }
        }*/

        for (int i = k+1; i < bounding_box_count; i++)
        {
            Polygon&     next_convex = convex_and_bounding_boxes[i].hull_polygon;
            BoundingBox& next_bbox   = convex_and_bounding_boxes[i].bounding_box;
            auto py1 = next_bbox.min.y();
            auto py2 = next_bbox.max.y();
            auto inter_min = std::max(iy1, py1); // min y of intersection
            auto inter_max = std::min(iy2, py2); // max y of intersection. length=max_y-min_y>0 means intersection exists
            if (inter_max - inter_min > 0) {
                height = hc2;
                break;
            }
        }
        if (height < convex_and_bounding_boxes[k].instance_height)
            height_polygons.emplace_back(std::make_pair(convex, height));
    }

    // sends instances 2d hulls to be rendered
    set_sequential_print_clearance_visible(true);
    set_sequential_print_clearance_render_fill(false);
    set_sequential_print_clearance_polygons(polygons, height_polygons);
}

bool GLCanvas3D::is_object_sinking(int object_idx) const
{
    for (const GLVolume* v : m_volumes.volumes) {
        if (v->object_idx() == object_idx && (v->is_sinking() || (!v->is_modifier && v->is_below_printbed())))
            return true;
    }
    return false;
}

void GLCanvas3D::apply_retina_scale(Vec2d &screen_coordinate) const
{
#if ENABLE_RETINA_GL
    double scale = static_cast<double>(m_retina_helper->get_scale_factor());
    screen_coordinate *= scale;
#endif // ENABLE_RETINA_GL}
}

bool GLCanvas3D::_is_shown_on_screen() const
{
    return (m_canvas != nullptr) ? m_canvas->IsShownOnScreen() : false;
}

// Getter for the const char*[]
static bool string_getter(const bool is_undo, int idx, const char** out_text)
{
    return wxGetApp().plater()->undo_redo_string_getter(is_undo, idx, out_text);
}

// Getter for the const char*[] for the search list
static bool search_string_getter(int idx, const char** label, const char** tooltip)
{
    return wxGetApp().plater()->search_string_getter(idx, label, tooltip);
}

//BBS: GUI refactor: adjust main toolbar position
bool GLCanvas3D::_render_orient_menu(float left, float right, float bottom, float top)
{
    ImGuiWrapper* imgui = wxGetApp().imgui();

    auto canvas_w = float(get_canvas_size().get_width());
    auto canvas_h = float(get_canvas_size().get_height());
    //BBS: GUI refactor: move main toolbar to the right
    //original use center as {0.0}, and top is (canvas_h/2), bottom is (-canvas_h/2), also plus inv_camera
    //now change to left_up as {0,0}, and top is 0, bottom is canvas_h
#if BBS_TOOLBAR_ON_TOP
    const float x = left * float(get_active_camera().get_zoom()) + 0.5f * canvas_w;
    ImGuiWrapper::push_toolbar_style(get_scale());
    float main_toolbar_height = 0.0f;
    const auto& p_main_toolbar = get_main_toolbar();
    if (p_main_toolbar) {
        main_toolbar_height = p_main_toolbar->get_height();
    }
    imgui->set_next_window_pos(x, main_toolbar_height, ImGuiCond_Always, 0.5f, 0.0f);
#else
    float main_toolbar_width = 0.0f;
    const auto& p_main_toolbar = get_main_toolbar();
    if (p_main_toolbar) {
        main_toolbar_width = p_main_toolbar->get_width();
    }
    const float x = canvas_w - main_toolbar_width;
    const float y = 0.5f * canvas_h - top * float(get_active_camera().get_zoom());
    imgui->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    imgui->begin(_L("Auto Orientation options"), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    OrientSettings settings = get_orient_settings();
    OrientSettings& settings_out = get_orient_settings();

    auto& appcfg = wxGetApp().app_config;
    PrinterTechnology ptech = current_printer_technology();

    bool settings_changed = false;
    float angle_min = 45.f;
    std::string angle_key = "overhang_angle", rot_key = "enable_rotation";
    std::string key_min_area = "min_area";
    std::string postfix = "_fff";

    if (ptech == ptSLA) {
        angle_min = 45.f;
        postfix = "_sla";
    }


    angle_key += postfix;
    rot_key += postfix;

    //if (imgui->slider_float(_L("Overhang Angle"), &settings.overhang_angle, angle_min, 90.0f, "%5.2f") || angle_min > settings.overhang_angle) {
    //    settings.overhang_angle = std::max(angle_min, settings.overhang_angle);
    //    settings_out.overhang_angle = settings.overhang_angle;
    //    appcfg->set("orient", angle_key, std::to_string(settings_out.overhang_angle));
    //    settings_changed = true;
    //}

    if (imgui->checkbox(_L("Enable rotation"), settings.enable_rotation)) {
        settings_out.enable_rotation = settings.enable_rotation;
        appcfg->set("orient", rot_key, settings_out.enable_rotation ? "1" : "0");
        settings_changed = true;
    }

    if (imgui->checkbox(_L("Optimize support interface area"), settings.min_area)) {
        settings_out.min_area = settings.min_area;
        appcfg->set("orient", key_min_area, settings_out.min_area ? "1" : "0");
        settings_changed = true;
    }

    ImGui::Separator();

    if (imgui->button(_L("Orient"))) {
        wxGetApp().plater()->set_prepare_state(Job::PREPARE_STATE_DEFAULT);
        wxGetApp().plater()->orient();
    }

    ImGui::SameLine();

    if (imgui->button(_L("Reset"))) {
        settings_out = OrientSettings{};
        settings_out.overhang_angle = 60.f;
        appcfg->set("orient", angle_key, std::to_string(settings_out.overhang_angle));
        appcfg->set("orient", rot_key, settings_out.enable_rotation ? "1" : "0");
        appcfg->set("orient", key_min_area, settings_out.min_area? "1" : "0");
        settings_changed = true;
    }

    imgui->end();
    ImGuiWrapper::pop_toolbar_style();
    return settings_changed;
}

//BBS: GUI refactor: adjust main toolbar position
bool GLCanvas3D::_render_arrange_menu(float left, float toolbar_height)
{
    ImGuiWrapper *imgui = wxGetApp().imgui();

    auto canvas_w = float(get_canvas_size().get_width());
    auto canvas_h = float(get_canvas_size().get_height());
    //BBS: GUI refactor: move main toolbar to the right
    //original use center as {0.0}, and top is (canvas_h/2), bottom is (-canvas_h/2), also plus inv_camera
    //now change to left_up as {0,0}, and top is 0, bottom is canvas_h
#if BBS_TOOLBAR_ON_TOP
    float zoom = (float)get_active_camera().get_zoom();
    const float x = 0.5 * canvas_w + left * zoom;
    imgui->set_next_window_pos(x, toolbar_height, ImGuiCond_Always, 0.0f, 0.0f);

#else
    float main_toolbar_width = 0.0f;
    const auto& p_main_toolbar = get_main_toolbar();
    if (p_main_toolbar) {
        main_toolbar_width = p_main_toolbar->get_width();
    }
    const float x = canvas_w - main_toolbar_width;
    const float y = 0.5f * canvas_h - top * float(get_active_camera().get_zoom());
    imgui->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    //BBS
    ImGuiWrapper::push_toolbar_style(get_scale());

    imgui->begin(_L("Arrange options"), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    ArrangeSettings settings = get_arrange_settings();
    ArrangeSettings &settings_out = get_arrange_settings();
    const float slider_icon_width = imgui->get_slider_icon_size().x;
    const float cursor_slider_left = imgui->calc_text_size(_L("Spacing")).x + imgui->scaled(1.5f);
    const float minimal_slider_width = imgui->scaled(4.f);
    float window_width  = minimal_slider_width + 2 * slider_icon_width;
    auto &appcfg = wxGetApp().app_config;
    PrinterTechnology ptech = current_printer_technology();

    bool settings_changed = false;
    float dist_min = 0.f;  // 0 means auto
    std::string dist_key = "min_object_distance", rot_key = "enable_rotation";
    std::string bed_shrink_x_key = "bed_shrink_x", bed_shrink_y_key = "bed_shrink_y";
    std::string multi_material_key = "allow_multi_materials_on_same_plate";
    std::string avoid_extrusion_key = "avoid_extrusion_cali_region";
    std::string align_to_y_axis_key = "align_to_y_axis";
    std::string save_svg_key        = "save_svg";
    std::string postfix             = settings.postfix;
    //BBS:
    bool seq_print = settings.is_seq_print;

    dist_key += postfix;
    rot_key  += postfix;
    bed_shrink_x_key += postfix;
    bed_shrink_y_key += postfix;

    ImGui::AlignTextToFramePadding();
    imgui->text(_L("Spacing"));
    ImGui::SameLine(1.2 * cursor_slider_left);
    ImGui::PushItemWidth(window_width - slider_icon_width);
    bool b_Spacing = imgui->bbl_slider_float_style("##Spacing", &settings.distance, dist_min, 100.0f, "%5.2f") || dist_min > settings.distance;
    ImGui::SameLine(window_width - slider_icon_width + 1.3 * cursor_slider_left);
    ImGui::PushItemWidth(1.5 * slider_icon_width);
    bool b_spacing_input = ImGui::BBLDragFloat("##spacing_input", &settings.distance, 0.05f, 0.0f, 0.0f, "%.2f");
    if (b_Spacing || b_spacing_input)
    {
        settings.distance = std::max(dist_min, settings.distance);
        settings_out.distance = settings.distance;
        appcfg->set("arrange", dist_key.c_str(), float_to_string_decimal_point(settings_out.distance));
        settings_changed = true;
    }
    imgui->text(_L("0 means auto spacing."));

    ImGui::Separator();
#if !BBL_RELEASE_TO_PUBLIC
    if (imgui->bbl_checkbox(_L("Save SVG"), settings.save_svg)) {
        settings_out.save_svg = settings.save_svg;
        appcfg->set("arrange", save_svg_key.c_str(), settings_out.save_svg ? "1" : "0");
        settings_changed = true;
    }
#endif
    if (imgui->bbl_checkbox(_L("Auto rotate for arrangement"), settings.enable_rotation)) {
        settings_out.enable_rotation = settings.enable_rotation;
        appcfg->set("arrange", rot_key.c_str(), settings_out.enable_rotation? "1" : "0");
        settings_changed = true;
    }

    if (imgui->bbl_checkbox(_L("Allow multiple materials on same plate"), settings.allow_multi_materials_on_same_plate)) {
        settings_out.allow_multi_materials_on_same_plate = settings.allow_multi_materials_on_same_plate;
        appcfg->set("arrange", multi_material_key.c_str(), settings_out.allow_multi_materials_on_same_plate ? "1" : "0");
        settings_changed = true;
    }

    // only show this option if the printer has micro Lidar and can do first layer scan
    DynamicPrintConfig &current_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    auto                op             = current_config.option("scan_first_layer");
    if (op && op->getBool()) {
        if (imgui->bbl_checkbox(_L("Avoid extrusion calibration region"), settings.avoid_extrusion_cali_region)) {
            settings_out.avoid_extrusion_cali_region = settings.avoid_extrusion_cali_region;
            appcfg->set("arrange", avoid_extrusion_key.c_str(), settings_out.avoid_extrusion_cali_region ? "1" : "0");
            settings_changed = true;
        }
    } else {
        settings_out.avoid_extrusion_cali_region = false;
    }

    // Align to Y axis. Only enable this option when auto rotation not enabled
    {
        if (settings_out.enable_rotation) {  // do not allow align to Y axis if rotation is enabled
            imgui->disabled_begin(true);
            settings_out.align_to_y_axis = false;
        }

        if (imgui->bbl_checkbox(_L("Align to Y axis"), settings.align_to_y_axis)) {
            settings_out.align_to_y_axis = settings.align_to_y_axis;
            appcfg->set("arrange", align_to_y_axis_key, settings_out.align_to_y_axis ? "1" : "0");
            settings_changed = true;
        }

        if (settings_out.enable_rotation == true) { imgui->disabled_end(); }
    }

    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(15.0f, 10.0f));
    if (imgui->button(_L("Arrange"))) {
        wxGetApp().plater()->set_prepare_state(Job::PREPARE_STATE_DEFAULT);
        wxGetApp().plater()->arrange();
    }

    ImGui::SameLine();

    if (imgui->button(_L("Reset"))) {
        settings_out.reset();
        //BBS: add specific arrange settings
        if (seq_print) settings_out.is_seq_print = true;

        if (auto printer_structure_opt = wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionEnum<PrinterStructure>>("printer_structure")) {
            settings_out.align_to_y_axis = (printer_structure_opt->value == PrinterStructure::psI3);
        }
        else
            settings_out.align_to_y_axis = false;

        appcfg->set("arrange", dist_key, float_to_string_decimal_point(settings_out.distance));
        appcfg->set("arrange", rot_key, settings_out.enable_rotation ? "1" : "0");
        appcfg->set("arrange", align_to_y_axis_key, settings_out.align_to_y_axis ? "1" : "0");
        settings_changed = true;
    }
    ImGui::PopStyleVar(1);
    imgui->end();

    //BBS
    ImGuiWrapper::pop_toolbar_style();

    return settings_changed;
}

static float       identityMatrix[16]   = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
static const float cameraProjection[16] = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
void GLCanvas3D::_render_3d_navigator()
{
    if (!wxGetApp().show_3d_navigator()) {
        return;
    }
    if (!can_show_3d_navigator()) {
        return;
    }
    ImGuizmo::BeginFrame();
    ImGuizmo::AllowAxisFlip(false);

    auto &style                                = ImGuizmo::GetStyle();
    style.Colors[ImGuizmo::COLOR::DIRECTION_X] = ImGuiWrapper::to_ImVec4(ColorRGBA::Y());
    style.Colors[ImGuizmo::COLOR::DIRECTION_Y] = ImGuiWrapper::to_ImVec4(ColorRGBA::Z());
    style.Colors[ImGuizmo::COLOR::DIRECTION_Z] = ImGuiWrapper::to_ImVec4(ColorRGBA::X());
    style.Colors[ImGuizmo::COLOR::TEXT]        = m_is_dark ? ImVec4(224 / 255.f, 224 / 255.f, 224 / 255.f, 1.f) : ImVec4(.2f, .2f, .2f, 1.0f);
    style.Colors[ImGuizmo::COLOR::FACE]        = m_is_dark ? ImVec4(0.23f, 0.23f, 0.23f, 1.f) : ImVec4(0.77f, 0.77f, 0.77f, 1);
    strcpy(style.AxisLabels[ImGuizmo::Axis::Axis_X], "y");
    strcpy(style.AxisLabels[ImGuizmo::Axis::Axis_Y], "z");
    strcpy(style.AxisLabels[ImGuizmo::Axis::Axis_Z], "x");
    strcpy(style.FaceLabels[ImGuizmo::FACES::FACE_FRONT], _utf8("Front").c_str());
    strcpy(style.FaceLabels[ImGuizmo::FACES::FACE_BACK], _utf8("Back").c_str());
    strcpy(style.FaceLabels[ImGuizmo::FACES::FACE_TOP], _utf8("Top").c_str());
    strcpy(style.FaceLabels[ImGuizmo::FACES::FACE_BOTTOM], _utf8("Bottom").c_str());
    strcpy(style.FaceLabels[ImGuizmo::FACES::FACE_LEFT], _CTX_utf8(L_CONTEXT("Left", "Camera"), "Camera").c_str());
    strcpy(style.FaceLabels[ImGuizmo::FACES::FACE_RIGHT], _CTX_utf8(L_CONTEXT("Right", "Camera"), "Camera").c_str());

    float sc = get_scale();
#ifdef WIN32
    const int dpi = get_dpi_for_window(wxGetApp().GetTopWindow());
    sc *= (float) dpi / (float) DPI_DEFAULT;
#endif // WIN32

    const ImGuiIO &io                 = ImGui::GetIO();
    const float    viewManipulateLeft = 0;
    const float    viewManipulateTop  = io.DisplaySize.y;
    const float    camDistance        = 8.f;
    ImGuizmo::SetID(0);

    Camera &    camera           = get_active_camera();
    Transform3d m                = Transform3d::Identity();
    m.matrix().block(0, 0, 3, 3) = camera.get_view_rotation().toRotationMatrix();
    // Rotate along X and Z axis for 90 degrees to have Y-up
    const auto coord_mapping_transform = Geometry::rotation_transform(Vec3d(0.5 * PI, 0, 0.5 * PI));
    m                                  = m * coord_mapping_transform;
    float cameraView[16];
    for (unsigned int c = 0; c < 4; ++c) {
        for (unsigned int r = 0; r < 4; ++r) { cameraView[c * 4 + r] = m(r, c); }
    }

    const float size  = 128 * sc;
    m_fit_camrea_button_pos[0] = size - 10;
    m_sc                       = sc;
    const bool  dirty = ImGuizmo::ViewManipulate(cameraView, cameraProjection, ImGuizmo::OPERATION::ROTATE, ImGuizmo::MODE::WORLD, identityMatrix, camDistance,
                                                ImVec2(viewManipulateLeft, viewManipulateTop - size), ImVec2(size, size), 0x00101010);

    if (dirty) {
        for (unsigned int c = 0; c < 4; ++c) {
            for (unsigned int r = 0; r < 4; ++r) { m(r, c) = cameraView[c * 4 + r]; }
        }
        // Rotate back
        m = m * (coord_mapping_transform.inverse());
        camera.set_rotation(m);

        request_extra_frame();
    }

    _render_fit_camera_toolbar();
}

bool GLCanvas3D::can_show_3d_navigator() {
    if (m_canvas_type == ECanvasType::CanvasPreview) {
        return m_can_show_navigator;
    }
    return true;
}
#define ENABLE_THUMBNAIL_GENERATOR_DEBUG_OUTPUT 0
#if ENABLE_THUMBNAIL_GENERATOR_DEBUG_OUTPUT
static void debug_output_thumbnail(const ThumbnailData& thumbnail_data)
{
    // debug export of generated image
    wxImage image(thumbnail_data.width, thumbnail_data.height);
    image.InitAlpha();

    for (unsigned int r = 0; r < thumbnail_data.height; ++r)
    {
        unsigned int rr = (thumbnail_data.height - 1 - r) * thumbnail_data.width;
        for (unsigned int c = 0; c < thumbnail_data.width; ++c)
        {
            unsigned char* px = (unsigned char*)thumbnail_data.pixels.data() + 4 * (rr + c);
            image.SetRGB((int)c, (int)r, px[0], px[1], px[2]);
            image.SetAlpha((int)c, (int)r, px[3]);
        }
    }

    image.SaveFile("test.png", wxBITMAP_TYPE_PNG);
}
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG_OUTPUT

void GLCanvas3D::render_thumbnail_framebuffer(const std::shared_ptr<OpenGLManager>& p_ogl_manager, ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params,
    PartPlateList& partplate_list, ModelObjectPtrs& model_objects, const GLVolumeCollection& volumes, std::vector<std::array<float, 4>>& extruder_colors,
                                              const std::shared_ptr<GLShaderProgram>& shader,
                                              Camera::EType                      camera_type,
                                              Camera::ViewAngleType              camera_view_angle_type,
                                              bool                               for_picking,
                                              bool                               ban_light)
{
    thumbnail_data.set(w, h);
    if (!thumbnail_data.is_valid())
        return;
    if (!p_ogl_manager) {
        return;
    }

    std::string thumbnail_fb_name{ "" };
    const auto fb_type = Slic3r::GUI::OpenGLManager::get_framebuffers_type();
    BOOST_LOG_TRIVIAL(info) << boost::format("framebuffer_type: %1%") % Slic3r::GUI::OpenGLManager::framebuffer_type_to_string(fb_type).c_str();
    EMSAAType msaa_type = EMSAAType::Disabled;
    const bool b_need_msaa = !for_picking;
    uint32_t samples = 0;
    if (fb_type != OpenGLManager::EFramebufferType::Unknown) {
        thumbnail_fb_name = "thumbnail_fb";
        if (b_need_msaa) {
            GLint max_samples;
            glsafe(::glGetIntegerv(GL_MAX_SAMPLES, &max_samples));
            if (max_samples >= 4) {
                msaa_type = EMSAAType::X4;
                samples = 4;
                BOOST_LOG_TRIVIAL(info) << boost::format("framebuffer msaa type: %1%") % "X4";
            }
            else if (max_samples == 2) {
                msaa_type = EMSAAType::X2;
                samples = 2;
                BOOST_LOG_TRIVIAL(info) << boost::format("framebuffer msaa type: %1%") % "X2";
            }
            else {
                BOOST_LOG_TRIVIAL(info) << boost::format("framebuffer msaa type: %1%") % "Disabled";
            }
        }
    }
    else if (p_ogl_manager->is_legacy_framebuffer_enabled()) {
        thumbnail_fb_name = OpenGLManager::s_back_frame;
    }

    if (thumbnail_fb_name.empty()) {
        return;
    }

    {
        OpenGLManager::FrameBufferModifier thumbnail_fb(*p_ogl_manager, thumbnail_fb_name, msaa_type);
        thumbnail_fb.set_width(w);
        thumbnail_fb.set_height(h);
    }

    _render_thumbnail_internal(thumbnail_data, thumbnail_params, partplate_list, model_objects, volumes, extruder_colors, shader, camera_type, camera_view_angle_type,
        for_picking, ban_light);

    std::string write_to_framebuffer_name = thumbnail_fb_name;
    if (thumbnail_params.post_processing_enabled) {
         // fxaa pass
        write_to_framebuffer_name = "thumbnail_fb_aa";
        _rebuild_postprocessing_pipeline(p_ogl_manager, thumbnail_fb_name, write_to_framebuffer_name, w, h);
         // end fxaa pass
    }

    p_ogl_manager->read_pixel(write_to_framebuffer_name, 0, 0, w, h, EPixelFormat::RGBA, EPixelDataType::UByte, (void*)thumbnail_data.pixels.data());

    const auto& p_fb = p_ogl_manager->get_frame_buffer(thumbnail_fb_name);
    int fb_id = 0;
    if (p_fb) {
        fb_id = p_fb->get_gl_id();
    }

    BOOST_LOG_TRIVIAL(info) << boost::format("render_thumbnail prepare: w %1%, h %2%, samples  %3%, render_fbo %4%") % w % h % samples % fb_id;

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG_OUTPUT
    debug_output_thumbnail(thumbnail_data);
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG_OUTPUT

    BOOST_LOG_TRIVIAL(info) << boost::format("render_thumbnail prepare: finished");
}

//BBS: GUI refractor

void GLCanvas3D::_update_slice_error_status()
{
    _set_warning_notification_if_needed(EWarning::ToolHeightOutside);
    _set_warning_notification_if_needed(EWarning::ToolpathOutside);
    _set_warning_notification_if_needed(EWarning::GCodeConflict);
    _set_warning_notification_if_needed(EWarning::MultiExtruderPrintableError);
    _set_warning_notification_if_needed(EWarning::MultiExtruderHeightOutside);
    _set_warning_notification_if_needed(EWarning::FilamentUnPrintableOnFirstLayer);
}

void GLCanvas3D::_switch_toolbars_icon_filename()
{
    BackgroundTexture::Metadata background_data;
    background_data.filename = m_is_dark ? "toolbar_background_dark.png" : "toolbar_background.png";
    background_data.left = 16;
    background_data.top = 16;
    background_data.right = 16;
    background_data.bottom = 16;

    const auto& p_main_toolbar = get_main_toolbar();
    if (p_main_toolbar) {
        p_main_toolbar->init(background_data);
        p_main_toolbar->set_dark_mode_enabled(m_is_dark);
    }

    wxGetApp().plater()->get_collapse_toolbar().init(background_data);

}
bool GLCanvas3D::_init_toolbars()
{
    if (!_init_main_toolbar())
        return false;

    if (!_init_return_toolbar())
        return false;

    if (!_init_select_plate_toolbar())
        return false;

#if 0
    if (!_init_view_toolbar())
        return false;
#endif

    if (!_init_collapse_toolbar())
        return false;

    return true;
}

//BBS: GUI refactor: GLToolbar
bool GLCanvas3D::_init_main_toolbar()
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return false;
    }
    if (!p_main_toolbar->is_enabled())
        return true;

    if (m_canvas_type == ECanvasType::CanvasAssembleView) {
        m_main_toolbar->set_position_mode(ToolbarLayout::EPositionMode::Custom);
    }
    else {
        m_main_toolbar->set_position_mode(ToolbarLayout::EPositionMode::TopMiddle);
    }
    m_main_toolbar->set_collapsed_offset(8); // by design from UX

    BackgroundTexture::Metadata background_data;
    background_data.filename = m_is_dark ? "toolbar_background_dark.png" : "toolbar_background.png";
    background_data.left = 16;
    background_data.top = 16;
    background_data.right = 16;
    background_data.bottom = 16;

    if (!p_main_toolbar->init(background_data))
    {
        // unable to init the toolbar texture, disable it
        p_main_toolbar->set_enabled(false);
        return true;
    }

    p_main_toolbar->set_dark_mode_enabled(m_is_dark);

    // init arrow
    BackgroundTexture::Metadata arrow_data;
    arrow_data.filename = "toolbar_arrow.svg";
    arrow_data.left = 0;
    arrow_data.top = 0;
    arrow_data.right = 0;
    arrow_data.bottom = 0;
    if (!p_main_toolbar->init_arrow(arrow_data))
    {
        BOOST_LOG_TRIVIAL(error) << "Main toolbar failed to load arrow texture.";
    }

    p_main_toolbar->set_layout_type(ToolbarLayout::EType::Horizontal);
    //BBS: main toolbar is at the top and left, we don't need the rounded-corner effect at the right side and the top side
    p_main_toolbar->set_horizontal_orientation(ToolbarLayout::HO_Right);
    p_main_toolbar->set_vertical_orientation(ToolbarLayout::VO_Top);
    p_main_toolbar->set_border(5.0f);
    p_main_toolbar->set_separator_size(5);
    p_main_toolbar->set_gap_size(4);

    p_main_toolbar->del_all_item();

    uint8_t sprite_id = 0;
    GLToolbarItem::Data item;

    if (m_canvas_type == ECanvasType::CanvasView3D) {
        item.name = "add";
        item.icon_filename_callback = [](bool is_dark_mode)->std::string {
            return is_dark_mode ? "toolbar_open_dark.svg" : "toolbar_open.svg";
            };
        item.tooltip = _utf8(L("Add")) + " [" + GUI::shortkey_ctrl_prefix() + "I]";
        item.sprite_id = sprite_id++;
        item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_ADD)); };
        item.enabling_callback = []()->bool {return wxGetApp().plater()->can_add_model(); };
        if (!p_main_toolbar->add_item(item))
            return false;

        item.name = "addplate";
        item.icon_filename_callback = [](bool is_dark_mode)->std::string {
            return is_dark_mode ? "toolbar_add_plate_dark.svg" : "toolbar_add_plate.svg";
            };
        item.tooltip = _utf8(L("Add plate"));
        item.sprite_id = sprite_id++;
        item.continuous_click = true;
        item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_ADD_PLATE)); };
        item.enabling_callback = []()->bool {return wxGetApp().plater()->can_add_plate(); };
        if (!p_main_toolbar->add_item(item))
            return false;

        item.name = "orient";
        item.icon_filename_callback = [](bool is_dark_mode)->std::string {
            return is_dark_mode ? "toolbar_orient_dark.svg" : "toolbar_orient.svg";
            };
        item.tooltip = _utf8(L("Auto orient"));
        item.sprite_id = sprite_id++;
        item.left.render_callback = nullptr;
        item.enabling_callback = []()->bool { return wxGetApp().plater()->can_do_ui_job(); };
        item.left.toggable = false;  // allow right mouse click
        //BBS: GUI refactor: adjust the main toolbar position
        item.left.action_callback = [this]() {
            if (m_canvas != nullptr)
            {
                wxGetApp().plater()->set_prepare_state(Job::PREPARE_STATE_DEFAULT);
                wxGetApp().plater()->orient();
                //BBS do not show orient menu
                //_render_orient_menu(left, right, bottom, top);
                NetworkAgent* agent = GUI::wxGetApp().getAgent();
                if (agent) agent->track_update_property("auto_orient", std::to_string(++auto_orient_count));
            }
            };
        if (!p_main_toolbar->add_item(item))
            return false;

        item.name = "arrange";
        item.icon_filename_callback = [](bool is_dark_mode)->std::string {
            return is_dark_mode ? "toolbar_arrange_dark.svg" : "toolbar_arrange.svg";
            };
        item.tooltip = _utf8(L("Arrange all objects")) + " [A]\n" + _utf8(L("Arrange objects on selected plates")) + " [Shift+A]";
        item.sprite_id = sprite_id++;
        item.left.action_callback = [this]() {
            if (m_canvas != nullptr) {
                NetworkAgent* agent = GUI::wxGetApp().getAgent();
                if (agent) agent->track_update_property("auto_arrange", std::to_string(++auto_arrange_count));
            }
            };
        item.enabling_callback = []()->bool { return wxGetApp().plater()->can_do_ui_job(); };
        item.left.toggable = true;
        //BBS: GUI refactor: adjust the main toolbar position
        item.left.render_callback = [this](float left, float right, float bottom, float top, float toolbar_height) {
            if (m_canvas != nullptr)
            {
                _render_arrange_menu(left, toolbar_height);
                //_render_arrange_menu(0.5f * (left + right));
            }
            };
        if (!p_main_toolbar->add_item(item))
            return false;

        GLToolbarItem::Data layers_editing_item;
        layers_editing_item.name = "layersediting";
        layers_editing_item.icon_filename_callback = [](bool is_dark_mode)->std::string {
            return is_dark_mode ? "toolbar_variable_layer_height_dark.svg" : "toolbar_variable_layer_height.svg";
            };
        layers_editing_item.tooltip = _utf8(L("Variable layer height"));
        layers_editing_item.additional_tooltip = _u8L("Please select single object.");
        layers_editing_item.sprite_id = sprite_id++;
        layers_editing_item.left.action_callback = [this]()->void{
            if (m_canvas != nullptr) {
                wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_LAYERSEDITING));
                NetworkAgent* agent = GUI::wxGetApp().getAgent();
                if (agent) agent->track_update_property("custom_height", std::to_string(++custom_height_count));
            }
        };
        layers_editing_item.right.action_callback = [this]()->void {
            enable_layers_editing(false);
        };
        layers_editing_item.visibility_callback = [this, &p_main_toolbar]()->bool {
            bool res = current_printer_technology() == ptFFF;
            // turns off if changing printer technology
            if (!res && p_main_toolbar->is_item_visible("layersediting") && p_main_toolbar->is_item_pressed("layersediting"))
                force_main_toolbar_left_action(get_main_toolbar_item_id("layersediting"));

            return res;
            };
        layers_editing_item.enabling_callback = []()->bool { return wxGetApp().plater()->can_layers_editing(); };
        layers_editing_item.left.toggable = true;
        if (!p_main_toolbar->add_item(layers_editing_item))
            return false;

        {
            GLToolbarItem::Data sperate_item;
            sperate_item.name = "seperator2";
            sperate_item.icon_filename_callback = [](bool is_dark_mode)->std::string {
                return "seperator.svg";
                };
            sperate_item.sprite_id = sprite_id++;
            sperate_item.left.action_callback = [this]() {};
            sperate_item.visibility_callback = []()->bool { return true; };
            sperate_item.enabling_callback = []()->bool { return true; };
            if (!p_main_toolbar->add_item(sperate_item, GLToolbarItem::EType::SeparatorLine))
                return false;
        }
    }

    const auto do_add_other_items = [this](uint8_t& sprite_id)->void {
        if (m_canvas_type == ECanvasType::CanvasView3D) {
            const auto& p_main_toolbar = get_main_toolbar();
            if (!p_main_toolbar) {
                return;
            }
            GLToolbarItem::Data item;
            item.name = "splitobjects";
            item.icon_filename_callback = [](bool is_dark_mode)->std::string {
                return is_dark_mode ? "split_objects_dark.svg" : "split_objects.svg";
                };
            item.tooltip = _utf8(L("Split to objects"));
            item.additional_tooltip = _u8L("Please select single object.") + "\n" +
                _u8L("And it is valid when there are at least two parts in object or stl has at least two meshes.");
            item.sprite_id = sprite_id++;
            item.left.render_callback = nullptr;
            item.left.action_callback = [this]() {
                if (m_canvas != nullptr) {
                    wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_SPLIT_OBJECTS));
                    NetworkAgent* agent = GUI::wxGetApp().getAgent();
                    if (agent) agent->track_update_property("split_to_objects", std::to_string(++split_to_objects_count));
                }
                };
            item.visibility_callback = GLToolbarItem::Default_Visibility_Callback;
            item.left.toggable = false;
            item.enabling_callback = []()->bool { return wxGetApp().plater()->can_split_to_objects(); };
            p_main_toolbar->add_item(item);

            item.name = "splitvolumes";
            item.icon_filename_callback = [](bool is_dark_mode)->std::string {
                return is_dark_mode ? "split_parts_dark.svg" : "split_parts.svg";
                };
            item.tooltip = _utf8(L("Split to parts"));
            item.additional_tooltip = _u8L("Please select single object.") + "\n" +
                _u8L("And it is valid when importing an stl with at least two meshes.");
            item.sprite_id = sprite_id++;
            item.left.action_callback = [this]() {
                if (m_canvas != nullptr) {
                    wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_SPLIT_VOLUMES));
                    NetworkAgent* agent = GUI::wxGetApp().getAgent();
                    if (agent) agent->track_update_property("split_to_part", std::to_string(++split_to_part_count));
                }
                };
            item.visibility_callback = GLToolbarItem::Default_Visibility_Callback;
            item.enabling_callback = []()->bool { return wxGetApp().plater()->can_split_to_volumes(); };
            p_main_toolbar->add_item(item);
        }
    };

    if (m_gizmos.is_enabled()) {
        m_gizmos.add_toolbar_items(p_main_toolbar, sprite_id, do_add_other_items);
    }
    else if (m_canvas_type == ECanvasType::CanvasView3D) {
        do_add_other_items(sprite_id);
    }

    if (m_canvas_type == ECanvasType::CanvasView3D) {

        GLToolbarItem::Data more_item;
        more_item.name = "More";
        more_item.icon_filename_callback = [](bool is_dark_mode)->std::string {
            return is_dark_mode ? "more_dark.svg" : "more.svg";
            };
        more_item.tooltip = _utf8(L("Click to Extend"));
        more_item.on_hover = [this]()->std::string {
            if (m_main_toolbar) {
                if (m_main_toolbar->is_collapsed()) {
                    return _utf8(L("Click to Extend"));
                }
                else {
                    return _utf8(L("Click to Collapse"));
                }
            }
            return "";
        };
        more_item.sprite_id = sprite_id++;
        more_item.visible = false;
        more_item.left.action_callback = [this]()->void{};
        more_item.visibility_callback = [this]()->bool {
            if (m_main_toolbar) {
                return m_main_toolbar->needs_collapsed();
            }
            return false;
        };
        more_item.enabling_callback = []()->bool { return true; };
        more_item.b_collapsible = false;
        more_item.b_collapse_button = true;
        more_item.continuous_click = true;
        if (!p_main_toolbar->add_item(more_item))
            return false;

        {
            GLToolbarItem::Data sperate_item;
            sperate_item.name = "seperator1";
            sperate_item.icon_filename_callback = [](bool is_dark_mode)->std::string {
                return "seperator.svg";
                };
            sperate_item.sprite_id = sprite_id++;
            sperate_item.left.action_callback = [this]() {};
            sperate_item.visibility_callback = []()->bool { return true; };
            sperate_item.enabling_callback = []()->bool { return true; };
            sperate_item.b_collapsible = false;
            if (!p_main_toolbar->add_item(sperate_item, GLToolbarItem::EType::SeparatorLine))
                return false;
        }

        {
            GLToolbarItem::Data item;
            item.name = "assembly_view";
            item.icon_filename_callback = [](bool is_dark_mode)->std::string {
                return is_dark_mode ? "toolbar_assemble_dark.svg" : "toolbar_assemble.svg";
                };
            item.tooltip = _utf8(L("Assembly View"));
            item.sprite_id = sprite_id++;
            item.left.toggable = false;
            item.left.action_callback = [this]() {
                if (m_canvas != nullptr) {
                    wxPostEvent(m_canvas, SimpleEvent(EVT_GLVIEWTOOLBAR_ASSEMBLE)); m_gizmos.reset_all_states(); wxGetApp().plater()->get_assmeble_canvas3D()->get_gizmos_manager().reset_all_states();
                    NetworkAgent* agent = GUI::wxGetApp().getAgent();
                    if (agent) agent->track_update_property("assembly_view", std::to_string(++assembly_view_count));
                }
                };
            item.left.render_callback = GLToolbarItem::Default_Render_Callback;
            item.visible = true;
            item.visibility_callback = [this]()->bool { return true; };
            item.enabling_callback = [this]()->bool {
                return wxGetApp().plater()->has_assmeble_view();
                };
            item.b_collapsible = false;
            if (!p_main_toolbar->add_item(item))
                return false;
        }
    }

    p_main_toolbar->update_items_state();
    return true;
}

//BBS: GUI refactor: GLToolbar
bool GLCanvas3D::_init_select_plate_toolbar()
{
    std::string path = resources_dir() + "/images/";
    IMToolbarItem* item = new IMToolbarItem();
    bool result = item->image_texture.load_from_svg_file(path + "im_all_plates_stats.svg", false, false, false, 128);
    result = result && item->image_texture_transparent.load_from_svg_file(path + "im_all_plates_stats_transparent.svg", false, false, false, 128);
    m_sel_plate_toolbar.m_all_plates_stats_item = item;
    return result;
}

void GLCanvas3D::_update_select_plate_toolbar_stats_item(bool force_selected) {
    PartPlateList& plate_list = wxGetApp().plater()->get_partplate_list();
    if (plate_list.get_nonempty_plate_list().size() > 1)
        m_sel_plate_toolbar.show_stats_item = true;
    else
        m_sel_plate_toolbar.show_stats_item = false;

    if (force_selected && m_sel_plate_toolbar.show_stats_item)
        m_sel_plate_toolbar.m_all_plates_stats_item->selected = true;
}

bool GLCanvas3D::_update_imgui_select_plate_toolbar()
{
    bool result = true;
    if (!m_sel_plate_toolbar.is_enabled()) {
        return false;
    }

    const auto& p_plater = wxGetApp().plater();
    if (!p_plater) {
        return false;
    }

    if (!p_plater->is_plate_toolbar_image_dirty()) {
        return false;
    }

    if (!p_plater->is_gcode_3mf()) { 
        p_plater->update_all_plate_thumbnails(true);
    }

    _update_select_plate_toolbar_stats_item();

    m_sel_plate_toolbar.del_all_item();

    PartPlateList& plate_list = p_plater->get_partplate_list();
    for (int i = 0; i < plate_list.get_plate_count(); i++) {
        IMToolbarItem* item = new IMToolbarItem();
        PartPlate* plate = plate_list.get_plate(i);
        if (plate && plate->thumbnail_data.is_valid()) {
            PartPlate* plate = plate_list.get_plate(i);
            item->image_data = plate->thumbnail_data.pixels;
            item->image_width = plate->thumbnail_data.width;
            item->image_height = plate->thumbnail_data.height;
            result = item->generate_texture();
        }
        m_sel_plate_toolbar.m_items.push_back(item);
    }
    p_plater->clear_plate_toolbar_image_dirty();
    m_sel_plate_toolbar.is_display_scrollbar = false;
    return result;
}

bool GLCanvas3D::_init_return_toolbar()
{
    if (!m_return_toolbar.is_enabled())
        return true;

    return m_return_toolbar.init();
}

// BBS
#if 0
bool GLCanvas3D::_init_view_toolbar()
{
    return wxGetApp().plater()->init_view_toolbar();
}
#endif

bool GLCanvas3D::_init_collapse_toolbar()
{
    return wxGetApp().plater()->init_collapse_toolbar();
}

bool GLCanvas3D::_set_current(bool force_update)
{
    if (!m_context || !m_canvas) {
        return false;
    }
    if (m_dirty_context || force_update) {
        const auto rt = m_canvas->SetCurrent(*m_context);
        if (rt) {
            m_dirty_context = false;
        }
        return rt;
    }
    return true;
}

void GLCanvas3D::_resize(unsigned int w, unsigned int h)
{
    if (m_canvas == nullptr && m_context == nullptr)
        return;

    const std::array<unsigned int, 2> new_size = { w, h };
    if (m_old_size == new_size)
        return;

    m_old_size = new_size;

    auto* imgui = wxGetApp().imgui();
    imgui->set_display_size(static_cast<float>(w), static_cast<float>(h));

    //BBS reduce render
    if (m_last_w == w && m_last_h == h) {
        return;
    }

    m_last_w = w;
    m_last_h = h;

    float font_size = wxGetApp().em_unit();

#ifdef _WIN32
    // On Windows, if manually scaled here, rendering issues can occur when the system's Display
    // scaling is greater than 300% as the font's size gets to be to large. So, use imgui font
    // scaling instead (see: ImGuiWrapper::init_font() and issue #3401)
    font_size *= (font_size > 30.0f) ? 1.0f : 1.5f;
#else
    font_size *= 1.5f;
#endif

#if ENABLE_RETINA_GL
    imgui->set_scaling(font_size, 1.0f, m_retina_helper->get_scale_factor());
#else
    imgui->set_scaling(font_size, m_canvas->GetContentScaleFactor(), 1.0f);
#endif

    this->request_extra_frame();

    // ensures that this canvas is current
    _set_current(true);
}

BoundingBoxf3 GLCanvas3D::_max_bounding_box(bool include_gizmos, bool include_bed_model, bool include_plates, bool volumes_limit_to_expand_plate) const
{
    BoundingBoxf3 bb = volumes_bounding_box(volumes_limit_to_expand_plate);

    // The following is a workaround for gizmos not being taken in account when calculating the tight camera frustrum
    // A better solution would ask the gizmo manager for the bounding box of the current active gizmo, if any
    if (include_gizmos && m_gizmos.is_running())
    {
        const auto& t_aabb = m_gizmos.get_bounding_box();
        bb.merge(t_aabb);
    }

    bb.merge(include_bed_model ? m_bed.extended_bounding_box() : m_bed.build_volume().bounding_volume());
    if (include_plates) {
        bb.merge(wxGetApp().plater()->get_partplate_list().get_bounding_box());
    }

    if (m_canvas_type == CanvasPreview) {
        const BoundingBoxf3& toolpath_bb = m_gcode_viewer.get_max_bounding_box();
        if (toolpath_bb.max_size() > 0.f)
            bb.merge(toolpath_bb);
        else
            bb.merge(m_gcode_viewer.get_shell_bounding_box());
    }

    if ((m_canvas_type == CanvasView3D) && (fff_print()->config().print_sequence == PrintSequence::ByObject)) {
        float height_to_lid, height_to_rod;
        wxGetApp().plater()->get_partplate_list().get_height_limits(height_to_lid, height_to_rod);
        bb.max.z() = std::max(bb.max.z(), (double)height_to_lid);
    }

    return bb;
}

void GLCanvas3D::_zoom_to_box(const BoundingBoxf3& box, double margin_factor)
{
    get_active_camera().zoom_to_box(box, margin_factor);
    m_dirty = true;
}

void GLCanvas3D::_update_camera_zoom(double zoom)
{
    get_active_camera().update_zoom(zoom);
    m_dirty = true;
}

Camera &GLCanvas3D::get_active_camera()
{
    /*if (m_canvas_type == CanvasThumbnailView) {
        return wxGetApp().plater()->get_thumbnail_camera();
    } else {*/
        return wxGetApp().plater()->get_camera(); // global camera
    //}
}

const Camera &GLCanvas3D::get_active_camera() const
{
    /*if (m_canvas_type == CanvasThumbnailView) {
        return wxGetApp().plater()->get_thumbnail_camera();
    } else {*/
    return wxGetApp().plater()->get_camera(); // global camera
    //}
}

std::vector<std::array<float, 4>> GLCanvas3D::get_active_colors() {
    return GUI::wxGetApp().plater()->get_extruders_colors();
}

void GLCanvas3D::_refresh_if_shown_on_screen()
{
    if (_is_shown_on_screen()) {
        const Size& cnv_size = get_canvas_size();
        _resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());

        // Because of performance problems on macOS, where PaintEvents are not delivered
        // frequently enough, we call render() here directly when we can.
        render();
    }
}

void GLCanvas3D::_picking_pass()
{
    if (m_picking_enabled && !m_mouse.dragging && m_mouse.position != Vec2d(DBL_MAX, DBL_MAX)) {

        // Render the object for picking.
        const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
        {
            OpenGLManager::FrameBufferModifier picking_frame(*p_ogl_manager, "pickingframe", EMSAAType::Disabled);
        }

        auto& picking_camera = wxGetApp().plater()->get_picking_camera();
        picking_camera.set_type(Camera::EType::Ortho);
        picking_camera.apply_viewport(0, 0, 1, 1);

        float pick_eye_z = 0.0f;
        const Camera& camera = get_active_camera();
        Vec3d pick_eye = _mouse_to_3d(camera, { m_mouse.position(0), m_mouse.position(1) }, &pick_eye_z);

        float pick_target_z = 1.0f;
        Vec3d pick_target = _mouse_to_3d(camera, { m_mouse.position(0), m_mouse.position(1) }, &pick_target_z);
        picking_camera.look_at(pick_eye, pick_target, Vec3d::UnitY());
        picking_camera.apply_projection(_max_bounding_box(true, true, true,false));//need full volumes boundingbox,not limit_to_expand_plate

        picking_camera.update_frustum();

        glsafe(::glDisable(GL_BLEND));
        glsafe(::glEnable(GL_DEPTH_TEST));

        glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

        //BBS: only render plate in view 3D
        if (m_canvas_type == ECanvasType::CanvasView3D && !m_gizmos.is_paint_gizmo()) {
            _render_plates_for_picking();
        }

        m_camera_clipping_plane = m_gizmos.get_clipping_plane();
        _render_volumes_for_picking();

        //BBS: remove the bed picking logic
        //_render_bed_for_picking(!get_active_camera().is_looking_downward());

        m_gizmos.render_current_gizmo_for_picking_pass();

        const auto gpu_picking_callback = [this]()->void {
            std::vector<int>* hover_volume_idxs = const_cast<std::vector<int>*>(&m_hover_volume_idxs);
            std::vector<int>* hover_plate_idxs = const_cast<std::vector<int>*>(&m_hover_plate_idxs);

            hover_volume_idxs->clear();
            hover_plate_idxs->clear();

            // Render the object for picking.
            const Size& cnv_size = get_canvas_size();

            const auto& p_ogl_manager = wxGetApp().get_opengl_manager();

            int volume_id = -1;
            int gizmo_id = -1;

            GLubyte color[4] = { 0, 0, 0, 0 };
            bool inside = 0 <= m_mouse.position(0) && m_mouse.position(0) < cnv_size.get_width() && 0 <= m_mouse.position(1) && m_mouse.position(1) < cnv_size.get_height();
            if (inside) {
                p_ogl_manager->read_pixel(OpenGLManager::s_picking_frame, 0, 0, 1, 1, EPixelFormat::RGBA, EPixelDataType::UByte, (void*)color);
                if (picking_checksum_alpha_channel(color[0], color[1], color[2]) == color[3]) {
                    // Only non-interpolated colors are valid, those have their lowest three bits zeroed.
                    // we reserve color = (0,0,0) for occluders (as the printbed)
                    // volumes' id are shifted by 1
                    // see: _render_volumes_for_picking()
                    //BBS: remove the bed picking logic
                    //volume_id = color[0] + (color[1] << 8) + (color[2] << 16) - 1;
                    volume_id = color[0] + (color[1] << 8) + (color[2] << 16);
                    // gizmos' id are instead properly encoded by the color
                    gizmo_id = color[0] + (color[1] << 8) + (color[2] << 16);
                }
            }
            else
                m_gizmos.set_hover_id(inside && (unsigned int)gizmo_id <= GLGizmoBase::BASE_ID ? ((int)GLGizmoBase::BASE_ID - gizmo_id) : -1);

            //BBS: add plate picking logic
            int plate_hover_id = PartPlate::PLATE_BASE_ID - volume_id;
            if (plate_hover_id >= 0 && plate_hover_id < PartPlateList::MAX_PLATES_COUNT * PartPlate::GRABBER_COUNT) {
                wxGetApp().plater()->get_partplate_list().set_hover_id(plate_hover_id);
                hover_plate_idxs->emplace_back(plate_hover_id);
                const_cast<GLGizmosManager*>(&m_gizmos)->set_hover_id(-1);
            }
            else {
                wxGetApp().plater()->get_partplate_list().reset_hover_id();
                if (0 <= volume_id && volume_id < (int)m_volumes.volumes.size()) {
                    // do not add the volume id if any gizmo is active and CTRL is pressed
                    if (m_gizmos.get_current_type() == GLGizmosManager::EType::Undefined || !wxGetKeyState(WXK_CONTROL))
                        hover_volume_idxs->emplace_back(volume_id);
                    const_cast<GLGizmosManager*>(&m_gizmos)->set_hover_id(-1);
                }
                else
                    const_cast<GLGizmosManager*>(&m_gizmos)->set_hover_id(inside && (unsigned int)volume_id <= GLGizmoBase::BASE_ID ? ((int)GLGizmoBase::BASE_ID - volume_id) : -1);
            }
            _update_volumes_hover_state();
        };

        _append_to_frame_callback(gpu_picking_callback);
    }
}

void GLCanvas3D::_rectangular_selection_picking_pass()
{
    m_gizmos.set_hover_id(-1);

    int viewport_width = 0;
    int viewport_height = 0;
    if (m_picking_enabled) {

        Vec2f center;
        center.x() = (m_rectangle_selection.get_left() + m_rectangle_selection.get_right()) * 0.5f;
        center.y() = (m_rectangle_selection.get_bottom() + m_rectangle_selection.get_top()) * 0.5f;

        const int viewport_x = static_cast<int>(m_rectangle_selection.get_left());
        const int viewport_y = static_cast<int>(get_canvas_size().get_height() - m_rectangle_selection.get_top());
        viewport_width = static_cast<int>(m_rectangle_selection.get_width());
        viewport_width = viewport_width > 1 ? viewport_width : 1;
        viewport_width = viewport_width;
        viewport_height = static_cast<int>(m_rectangle_selection.get_height());
        viewport_height = viewport_height > 1 ? viewport_height : 1;
        viewport_height = viewport_height;

        const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
        {
            OpenGLManager::FrameBufferModifier picking_frame(*p_ogl_manager, "rectangular_selection_pickingframe", EMSAAType::Disabled);
        }

        const auto& camera = get_active_camera();
        const auto main_camera_type = camera.get_type();

        auto& picking_camera = wxGetApp().plater()->get_picking_camera();
        if (main_camera_type == Camera::EType::Ortho) {

            picking_camera.set_zoom(camera.get_zoom());

            float pick_eye_z = 0.0f;
            Vec3d pick_eye = _mouse_to_3d(camera, { center.x(), center.y() }, &pick_eye_z);
            float pick_target_z = 1.0f;
            Vec3d pick_target = _mouse_to_3d(camera, { center.x(), center.y() }, &pick_target_z);
            picking_camera.look_at(pick_eye, pick_target, camera.get_dir_up());

            picking_camera.apply_viewport(viewport_x, viewport_y, viewport_width, viewport_height);
            picking_camera.set_type(Camera::EType::Ortho);

            picking_camera.apply_projection(_max_bounding_box(true, true, true,false));//need full volumes boundingbox,not limit_to_expand_plate
            picking_camera.update_frustum();
        }
        else {
            picking_camera = camera;
            picking_camera.enable_update_config_on_type_change(false);
        }

        glsafe(::glEnable(GL_SCISSOR_TEST));
        glsafe(::glScissor(viewport_x, viewport_y, viewport_width, viewport_height));

        glsafe(::glDisable(GL_BLEND));
        glsafe(::glEnable(GL_DEPTH_TEST));

        glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

        _render_volumes_for_picking();
        //BBS: remove the bed picking logic
        //_render_bed_for_picking(!get_active_camera().is_looking_downward());

        glsafe(::glDisable(GL_SCISSOR_TEST));
    }

    const auto gpu_picking_callback = [this, viewport_width, viewport_height]()->void {
        std::set<int> idxs;

        if (m_picking_enabled) {
            int px_count = viewport_width * viewport_height;

            int left = (int)m_rectangle_selection.get_left();
            int top = get_canvas_size().get_height() - (int)m_rectangle_selection.get_top();
            if (left >= 0 && top >= 0 && px_count) {
#define USE_PARALLEL 1
#if USE_PARALLEL
                struct Pixel
                {
                    std::array<GLubyte, 4> data;
                    // Only non-interpolated colors are valid, those have their lowest three bits zeroed.
                    bool valid() const { return picking_checksum_alpha_channel(data[0], data[1], data[2]) == data[3]; }
                    // we reserve color = (0,0,0) for occluders (as the printbed)
                    // volumes' id are shifted by 1
                    // see: _render_volumes_for_picking()
                    //BBS: remove the bed picking logic
                    int id() const { return data[0] + (data[1] << 8) + (data[2] << 16); }
                    //int id() const { return data[0] + (data[1] << 8) + (data[2] << 16) - 1; }
                };

                std::vector<Pixel> frame(px_count);
                const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
                p_ogl_manager->read_pixel("rectangular_selection_pickingframe", left, top, viewport_width, viewport_height, EPixelFormat::RGBA, EPixelDataType::UByte, (void*)frame.data());
                tbb::spin_mutex mutex;
                tbb::parallel_for(tbb::blocked_range<size_t>(0, frame.size(), (size_t)viewport_width),
                    [this, &frame, &idxs, &mutex](const tbb::blocked_range<size_t>& range) {
                        for (size_t i = range.begin(); i < range.end(); ++i)
                            if (frame[i].valid()) {
                                int volume_id = frame[i].id();
                                if (0 <= volume_id && volume_id < (int)m_volumes.volumes.size()) {
                                    mutex.lock();
                                    idxs.insert(volume_id);
                                    mutex.unlock();
                                }
                            }
                    });
#else
                std::vector<GLubyte> frame(4 * px_count);
                p_ogl_manager->read_pixel(left, top, viewport_width, viewport_height, EPixelFormat::RGBA, EPixelDataType::UByte, (void*)frame.data());

                for (int i = 0; i < px_count; ++i)
                {
                    int px_id = 4 * i;
                    int volume_id = frame[px_id] + (frame[px_id + 1] << 8) + (frame[px_id + 2] << 16);
                    if (0 <= volume_id && volume_id < (int)m_volumes.volumes.size())
                        idxs.insert(volume_id);
                }
#endif // USE_PARALLEL
            }
        }

        m_hover_volume_idxs.assign(idxs.begin(), idxs.end());
        _update_volumes_hover_state();
    };

    _append_to_frame_callback(gpu_picking_callback);
}

void GLCanvas3D::_render_background() const
{
    bool use_error_color = false;
    if (wxGetApp().is_editor()) {
        use_error_color = m_dynamic_background_enabled &&
        (current_printer_technology() != ptSLA || !m_volumes.empty());

        if (!m_volumes.empty())
            use_error_color &= _is_any_volume_outside();
        else {
            //BBS: use current plater's bounding box
            //BoundingBoxf3 test_volume = (m_config != nullptr) ? print_volume(*m_config) : BoundingBoxf3();
            BoundingBoxf3 test_volume = (const_cast<GLCanvas3D*>(this))->_get_current_partplate_print_volume();
            const BoundingBoxf3& path_bounding_box = m_gcode_viewer.get_paths_bounding_box();
            if (empty(path_bounding_box))
                use_error_color = false;
            else
                //BBS: use previous result
                use_error_color = (test_volume.radius() > 0.0) ? m_toolpath_outside : false;
            //use_error_color &= (test_volume.radius() > 0.0) ? !test_volume.contains(path_bounding_box) : false;
        }
    }

    // Draws a bottom to top gradient over the complete screen.
    glsafe(::glDisable(GL_DEPTH_TEST));

    _init_fullscreen_mesh();

    float* background_color = m_is_dark ? DEFAULT_BG_LIGHT_COLOR_DARK : DEFAULT_BG_LIGHT_COLOR;
    float* error_background_color = m_is_dark ? ERROR_BG_LIGHT_COLOR_DARK : ERROR_BG_LIGHT_COLOR;

    const auto final_bg_color = use_error_color ? error_background_color : background_color;

    const auto& p_background_shader = wxGetApp().get_shader("background");
    if (!p_background_shader)
    {
        BOOST_LOG_TRIVIAL(error) << "Invalid Shader: background";
        return;
    }
    wxGetApp().bind_shader(p_background_shader);

    s_full_screen_mesh.set_color({ final_bg_color[0], final_bg_color[1], final_bg_color[2], 1.0f});

    s_full_screen_mesh.render_geometry();

    wxGetApp().unbind_shader();

    glsafe(::glEnable(GL_DEPTH_TEST));
}

void GLCanvas3D::_render_bed(bool bottom, bool show_axes)
{
    float scale_factor = 1.0;
#if ENABLE_RETINA_GL
    scale_factor = m_retina_helper->get_scale_factor();
#endif // ENABLE_RETINA_GL
    //bool show_texture = true;
    //BBS set axes mode
    const auto& p_main_toolbar = get_main_toolbar();
    bool b_main_toolbar_enabled = p_main_toolbar && p_main_toolbar->is_enabled();
    m_bed.set_axes_mode(b_main_toolbar_enabled && !m_gizmos.is_show_only_active_plate());
    m_bed.render(*this, bottom, scale_factor, show_axes);
}

void GLCanvas3D::_render_bed_for_picking(bool bottom)
{
    float scale_factor = 1.0;
#if ENABLE_RETINA_GL
    scale_factor = m_retina_helper->get_scale_factor();
#endif // ENABLE_RETINA_GL

    //m_bed.render_for_picking(*this, bottom, scale_factor);
}

void GLCanvas3D::_render_platelist(bool bottom, bool only_current, bool only_body, int hover_id, bool render_cali, bool show_grid) const
{
    wxGetApp().plater()->get_partplate_list().render(bottom, only_current, only_body, hover_id, render_cali, show_grid,
                                                     wxGetApp().app_config->get_bool("enable_opengl_multi_instance"));
}

void GLCanvas3D::_render_plates_for_picking() const
{
    wxGetApp().plater()->get_partplate_list().render_for_picking_pass();
}

void GLCanvas3D::_render_plane() const
{
    ;//TODO render assemble plane
}

//BBS: add outline drawing logic
void GLCanvas3D::_render_objects(GLVolumeCollection &cur_volumes, GLVolumeCollection::ERenderType type, bool with_outline, bool in_paint_gizmo)
{
    if (cur_volumes.empty())
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));

    m_camera_clipping_plane = m_gizmos.get_clipping_plane();

    if (m_picking_enabled && !in_paint_gizmo)
        // Update the layer editing selection to the first object selected, update the current object maximum Z.
        m_layers_editing.select_object(*m_model, this->is_layers_editing_enabled() ? m_selection.get_object_idx() : -1);

    if (const BuildVolume &build_volume = m_bed.build_volume(); build_volume.valid()) {
        switch (build_volume.type()) {
        case BuildVolume::Type::Rectangle: {
            const BoundingBox3Base<Vec3d> bed_bb = build_volume.bounding_volume().inflated(BuildVolume::SceneEpsilon);
            cur_volumes.set_print_volume({0, // Rectangle
                { float(bed_bb.min.x()), float(bed_bb.min.y()), float(bed_bb.max.x()), float(bed_bb.max.y()) },
                { 0.0f, float(build_volume.printable_height()) } });
            break;
        }
        case BuildVolume::Type::Circle: {
            cur_volumes.set_print_volume({1, // Circle
                { unscaled<float>(build_volume.circle().center.x()), unscaled<float>(build_volume.circle().center.y()), unscaled<float>(build_volume.circle().radius + BuildVolume::SceneEpsilon), 0.0f },
                { 0.0f, float(build_volume.printable_height() + BuildVolume::SceneEpsilon) } });
            break;
        }
        default:
        case BuildVolume::Type::Convex:
        case BuildVolume::Type::Custom: {
            cur_volumes.set_print_volume({static_cast<int>(type),
                { -FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX },
                { -FLT_MAX, FLT_MAX } }
            );
        }
        }
        if (m_requires_check_outside_state) {
            cur_volumes.check_outside_state(build_volume, nullptr, nullptr, *m_model);
            m_requires_check_outside_state = false;
        }
    }

    if (m_use_clipping_planes)
        cur_volumes.set_z_range(-m_clipping_planes[0].get_data()[3], m_clipping_planes[1].get_data()[3]);
    else
        cur_volumes.set_z_range(-FLT_MAX, FLT_MAX);

    GLGizmosManager& gm = get_gizmos_manager();
    GLGizmoBase* current_gizmo = gm.get_current();
    if (m_canvas_type == CanvasAssembleView) {
        cur_volumes.set_clipping_plane(m_gizmos.get_assemble_view_clipping_plane().get_data());
    }
    else if (current_gizmo && !current_gizmo->apply_clipping_plane()) {
        cur_volumes.set_clipping_plane(ClippingPlane::ClipsNothing().get_data());
    }
    else {
        cur_volumes.set_clipping_plane(m_camera_clipping_plane.get_data());
    }
    if (m_canvas_type == CanvasAssembleView)
        cur_volumes.set_show_sinking_contours(false);
    else
        cur_volumes.set_show_sinking_contours(!m_gizmos.is_hiding_instances());

    const auto& shader = wxGetApp().get_shader("gouraud");
    ECanvasType canvas_type = this->m_canvas_type;
    std::array<float, 4> body_color  = canvas_type == ECanvasType::CanvasAssembleView ? std::array<float, 4>({1.0f, 1.0f, 0.0f, 1.0f}) ://yellow
                                                                                        std::array<float, 4>({1.0f, 1.0f, 1.0f, 1.0f});//white
    bool                 partly_inside_enable = canvas_type == ECanvasType::CanvasAssembleView ? false : true;
    auto printable_height_option = GUI::wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloatsNullable>("extruder_printable_height");
    const GUI::ERenderPipelineStage render_pipeline_stage = _get_current_render_stage();

    const auto& camera = get_active_camera();
    std::vector<std::array<float, 4>> colors = get_active_colors();
    if ((GUI::ERenderPipelineStage::Silhouette == render_pipeline_stage) || shader != nullptr) {
        if (GUI::ERenderPipelineStage::Silhouette != render_pipeline_stage)
        {
            wxGetApp().bind_shader(shader);
        }
        switch (type)
        {
        default:
        case GLVolumeCollection::ERenderType::Opaque:
        {
            const GLGizmosManager& gm = get_gizmos_manager();
            if (in_paint_gizmo) {
                cur_volumes.render(
                    render_pipeline_stage, type, m_picking_enabled, camera, colors, *m_model,
                    [this](const GLVolume &volume) {
                        return true;
                    },
                    with_outline, body_color, partly_inside_enable,  nullptr);
            }
            if (dynamic_cast<GLGizmoPainterBase*>(gm.get_current()) == nullptr){
                if (m_picking_enabled && m_layers_editing.is_enabled() && (m_layers_editing.last_object_id != -1) && (m_layers_editing.object_max_z() > 0.0f) &&
                    GUI::ERenderPipelineStage::Silhouette != render_pipeline_stage) {
                    int object_id = m_layers_editing.last_object_id;
                    cur_volumes.render(
                        render_pipeline_stage, type, false, camera, colors, *m_model,[object_id](const GLVolume &volume) {
                        // Which volume to paint without the layer height profile shader?
                        return volume.is_active && (volume.is_modifier || volume.composite_id.object_id != object_id);
                        });
                    m_layers_editing.render_volumes(*this, cur_volumes);
                }
                else {
                    /*if (wxGetApp().plater()->is_wireframe_enabled()) {
                        if (wxGetApp().plater()->is_show_wireframe())
                            shader->set_uniform("show_wireframe", true);
                        else
                            shader->set_uniform("show_wireframe", false);
                    }*/
                    //BBS:add assemble view related logic
                    // do not cull backfaces to show broken geometry, if any
                    cur_volumes.render(
                        render_pipeline_stage, type, m_picking_enabled, camera, colors,
                        *m_model,[this, canvas_type](const GLVolume &volume) {
                        if (canvas_type == ECanvasType::CanvasAssembleView) {
                            return !volume.is_modifier && !volume.is_wipe_tower;
                        }
                        else {
                            return (m_render_sla_auxiliaries || volume.composite_id.volume_id >= 0);
                        }
                        },
                        with_outline, body_color, partly_inside_enable, printable_height_option ? &printable_height_option->values : nullptr);
                }
            }
            else if (GUI::ERenderPipelineStage::Silhouette != render_pipeline_stage) {
                // In case a painting gizmo is open, it should render the painted triangles
                // before transparent objects are rendered. Otherwise they would not be
                // visible when inside modifier meshes etc.
//                GLGizmosManager::EType type = gm.get_current_type();
                if (dynamic_cast<GLGizmoPainterBase*>(gm.get_current())) {
                    wxGetApp().unbind_shader();
                    gm.render_painter_gizmo();
                    wxGetApp().bind_shader(shader);
                }
            }

            break;
        }
        case GLVolumeCollection::ERenderType::Transparent:
        {
            /*if (wxGetApp().plater()->is_wireframe_enabled()) {
                if (wxGetApp().plater()->is_show_wireframe())
                    shader->set_uniform("show_wireframe", true);
                else
                    shader->set_uniform("show_wireframe", false);
            }*/
            //BBS:add assemble view related logic
            if (in_paint_gizmo) {
                cur_volumes.render(
                    render_pipeline_stage, type, false, camera, colors, *m_model,
                    [this, canvas_type](const GLVolume &volume) {
                        return true;
                    },
                    with_outline, body_color, partly_inside_enable, nullptr);
            } else {
                cur_volumes.render(
                    render_pipeline_stage, type, false, camera, colors, *m_model,
                    [this, canvas_type](const GLVolume &volume) {
                        if (canvas_type == ECanvasType::CanvasAssembleView) {
                            return !volume.is_modifier;
                        } else {
                            return true;
                        }
                    },
                    with_outline, body_color, partly_inside_enable, printable_height_option ? &printable_height_option->values : nullptr);
            }
            if (m_canvas_type == CanvasAssembleView && m_gizmos.m_assemble_view_data->model_objects_clipper()->get_position() > 0 && GUI::ERenderPipelineStage::Silhouette != render_pipeline_stage) {
                const GLGizmosManager& gm = get_gizmos_manager();
                wxGetApp().unbind_shader();
                gm.render_painter_assemble_view();
                wxGetApp().bind_shader(shader);
            }
            if (m_canvas_type == CanvasView3D && m_gizmos.is_paint_gizmo()) {
                cur_volumes.only_render_sinking(
                    render_pipeline_stage, type, false, camera, colors, *m_model,
                    [this, canvas_type](const GLVolume &volume) {
                        if (canvas_type == ECanvasType::CanvasAssembleView) {
                            return !volume.is_modifier;
                        } else {
                            return true;
                        }
                    },
                    with_outline, body_color, partly_inside_enable, printable_height_option ? &printable_height_option->values : nullptr);
            }
            break;
        }
        }

        /*if (wxGetApp().plater()->is_wireframe_enabled()) {
            shader->set_uniform("show_wireframe", false);
        }*/

        if (GUI::ERenderPipelineStage::Silhouette != render_pipeline_stage) {
            wxGetApp().unbind_shader();
        }
    }

    m_camera_clipping_plane = ClippingPlane::ClipsNothing();
}

//BBS: GUI refactor: add canvas size as parameters
void GLCanvas3D::_render_gcode(int canvas_width, int canvas_height)
{
    m_gcode_viewer.render(canvas_width, canvas_height, SLIDER_RIGHT_MARGIN);
    IMSlider *layers_slider = m_gcode_viewer.get_layers_slider();
    IMSlider *moves_slider  = m_gcode_viewer.get_moves_slider();

    if (layers_slider->is_need_post_tick_event()) {
        auto evt = new wxCommandEvent(EVT_CUSTOMEVT_TICKSCHANGED, m_canvas->GetId());
        evt->SetInt((int)layers_slider->get_post_tick_event_type());
        wxPostEvent(m_canvas, *evt);
        layers_slider->reset_post_tick_event();
    }

    if (layers_slider->is_dirty()) {
        set_volumes_z_range({layers_slider->GetLowerValueD(), layers_slider->GetHigherValueD()});
        if (m_gcode_viewer.has_data()) {
            m_gcode_viewer.set_layers_z_range({static_cast<unsigned int>(layers_slider->GetLowerValue()), static_cast<unsigned int>(layers_slider->GetHigherValue())});
        }
        layers_slider->set_as_dirty(false);
        post_event(SimpleEvent(EVT_GLCANVAS_UPDATE));
        m_gcode_viewer.update_marker_curr_move();
    }

    if (moves_slider->is_dirty()) {
        moves_slider->set_as_dirty(false);
        m_gcode_viewer.update_sequential_view_current((moves_slider->GetLowerValueD() - 1.0), static_cast<unsigned int>(moves_slider->GetHigherValueD() - 1.0));
        post_event(SimpleEvent(EVT_GLCANVAS_UPDATE));
        m_gcode_viewer.update_marker_curr_move();
    }
}

void GLCanvas3D::_render_selection() const
{
    float scale_factor = 1.0;
#if ENABLE_RETINA_GL
    scale_factor = m_retina_helper->get_scale_factor();
#endif // ENABLE_RETINA_GL

    if (!m_gizmos.is_running())
        m_selection.render(scale_factor);
}

void GLCanvas3D::_render_sequential_clearance()
{
    if (m_gizmos.is_dragging())
        return;
    auto type = m_gizmos.get_current_type();
    if (type == GLGizmosManager::EType::Undefined
        || can_sequential_clearance_show_in_gizmo()) {
        m_sequential_print_clearance.render();
    }
}

#if ENABLE_RENDER_SELECTION_CENTER
void GLCanvas3D::_render_selection_center() const
{
    m_selection.render_center(m_gizmos.is_dragging());
}
#endif // ENABLE_RENDER_SELECTION_CENTER

void GLCanvas3D::_check_and_update_toolbar_icon_scale()
{
    // Don't update a toolbar scale, when we are on a Preview
    if (wxGetApp().plater()->is_preview_shown())
    {

#if ENABLE_RETINA_GL
        IMSlider* m_layers_slider = get_gcode_viewer().get_layers_slider();
        IMSlider* m_moves_slider = get_gcode_viewer().get_moves_slider();
        const float sc = m_retina_helper->get_scale_factor();
        m_layers_slider->set_scale(sc);
        m_moves_slider->set_scale(sc);
        m_gcode_viewer.set_scale(sc);

        auto* m_notification = wxGetApp().plater()->get_notification_manager();
        m_notification->set_scale(sc);

#endif
        return;
    }

    float scale = 1.0f;

    const auto& p_main_toolbar = get_main_toolbar();
    if (p_main_toolbar) {
        const bool auto_scale = p_main_toolbar->get_rendering_mode() == GLToolbar::EToolbarRenderingMode::Auto;
        scale = wxGetApp().toolbar_icon_scale(auto_scale);
    }
    Size cnv_size = get_canvas_size();

    //BBS: GUI refactor: GLToolbar
    float size = GLToolbar::Default_Icons_Size * scale;
    //float main_size = GLGizmosManager::Default_Icons_Size * scale;

    // Set current size for all top toolbars. It will be used for next calculations
    GLToolbar& collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();
#if ENABLE_RETINA_GL
    const float sc = m_retina_helper->get_scale_factor() * scale;
    //BBS: GUI refactor: GLToolbar
    if (p_main_toolbar) {
        p_main_toolbar->set_scale(sc);
    }

    collapse_toolbar.set_scale(sc);
    size *= m_retina_helper->get_scale_factor();

    auto* m_notification = wxGetApp().plater()->get_notification_manager();
    m_notification->set_scale(sc);
#else
    //BBS: GUI refactor: GLToolbar
    if (p_main_toolbar) {
        p_main_toolbar->set_icons_size(size);
    }
    collapse_toolbar.set_icons_size(wxGetApp().plater()->get_collapse_toolbar_size());
#endif // ENABLE_RETINA_GL
    // Update collapse toolbar
    collapse_toolbar.set_enabled(wxGetApp().plater()->get_sidebar_docking_state() != Sidebar::None);

    //BBS: GUI refactor: GLToolbar
#if BBS_TOOLBAR_ON_TOP
    float collapse_toolbar_width = collapse_toolbar.is_enabled() ? collapse_toolbar.get_width() : GLToolbar::Default_Icons_Size;

    float top_tb_width = collapse_toolbar_width;
    int items_cnt = collapse_toolbar.get_visible_items_cnt();
    if (p_main_toolbar) {
        top_tb_width += p_main_toolbar->get_width();
        items_cnt += p_main_toolbar->get_visible_items_cnt();
    }

    float noitems_width = top_tb_width - size * items_cnt; // width of separators and borders in top toolbars

    // calculate scale needed for items in all top toolbars
#ifdef __WINDOWS__
    cnv_size.set_width(cnv_size.get_width() + collapse_toolbar_width);
#endif
    float new_h_scale = (cnv_size.get_width() - noitems_width) / (items_cnt * GLToolbar::Default_Icons_Size);

    //for protect
    if (new_h_scale <= 0) {
        new_h_scale = 1;
    }

    //use the same value as horizon
    float new_v_scale = new_h_scale;
#else
    float top_tb_width = = collapse_toolbar.get_width();
    int   items_cnt = collapse_toolbar.get_visible_items_cnt();
    float noitems_width = top_tb_width - size * items_cnt; // width of separators and borders in top toolbars

    // calculate scale needed for items in all top toolbars
    float new_h_scale = (cnv_size.get_width() - noitems_width) / (items_cnt * GLToolbar::Default_Icons_Size);

    //items_cnt = m_main_toolbar.get_visible_items_cnt() + m_gizmos.get_selectable_icons_cnt() + 3; // +3 means a place for top and view toolbars and separators in gizmos toolbar

    // calculate scale needed for items in the gizmos toolbar
    if (p_main_toolbar) {
        items_cnt += p_main_toolbar->get_visible_items_cnt();
    }
    float new_v_scale = cnv_size.get_height() / (items_cnt * GLGizmosManager::Default_Icons_Size);
#endif

    // set minimum scale as a auto scale for the toolbars
    float new_scale = std::min(new_h_scale, new_v_scale);
#if ENABLE_RETINA_GL
    new_scale /= m_retina_helper->get_scale_factor();
#endif
    if (fabs(new_scale - scale) > 0.01) // scale is changed by 1% and more
        wxGetApp().set_auto_toolbar_icon_scale(new_scale);
}

void GLCanvas3D::_render_overlays()
{
     glsafe(::glDisable(GL_DEPTH_TEST));

     _check_and_update_toolbar_icon_scale();

     _render_assemble_control();

     _render_toolbar();

    if (m_layers_editing.last_object_id >= 0 && m_layers_editing.object_max_z() > 0.0f)
        m_layers_editing.render_overlay(*this);

	auto curr_plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
    auto curr_print_seq = curr_plate->get_real_print_seq();
    bool sequential_print = (curr_print_seq == PrintSequence::ByObject);
    std::vector<const ModelInstance*> sorted_instances;
    if (sequential_print) {
        const Print* print = fff_print();
        if (print) {
            for (const PrintObject *print_object : print->objects())
            {
                for (const PrintInstance &instance : print_object->instances())
                {
                    sorted_instances.emplace_back(instance.model_instance);
                }
            }
        }
        /*for (ModelObject* model_object : m_model->objects)
            for (ModelInstance* model_instance : model_object->instances) {
                sorted_instances.emplace_back(model_instance);
            }*/
    }
    m_labels.render(sorted_instances);
    _render_3d_navigator();
}

void GLCanvas3D::_render_style_editor()
{
    bool show_style_editor = true;
    ImGui::Begin("ImGui Style Editor", &show_style_editor);
    // You can pass in a reference ImGuiStyle structure to compare to, revert to and save to
    // (without a reference style pointer, we will use one compared locally as a reference)

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.50f);
    ImGui::ShowFontSelector("Fonts##Selector");
    ImGui::Separator();

    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("Colors"))
        {
            static int output_dest = 0;
            static bool output_only_modified = false;
            if (ImGui::Button("Export"))
            {
                if (output_dest == 0)
                    ImGui::LogToClipboard();
                else
                    ImGui::LogToTTY();

                ImGui::LogText("RenderColors:" IM_NEWLINE);
                for (int i = 0; i < RenderCol_Count; i++)
                {
                    const ImVec4& col = RenderColor::colors[i];
                    const char* name = GetRenderColName(i);
                    if (!output_only_modified || memcmp(&col, &RenderColor::colors[i], sizeof(ImVec4)) != 0)
                        ImGui::LogText("RenderColor::colors[%s]%*s= ImVec4(%.2ff, %.2ff, %.2ff, %.2ff);" IM_NEWLINE,
                            name, 23 - (int)strlen(name), "", col.x, col.y, col.z, col.w);
                }
                ImGui::LogFinish();
            }
            ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::Combo("##output_type", &output_dest, "To Clipboard\0To TTY\0");
            ImGui::SameLine(); ImGui::Checkbox("Only Modified Colors", &output_only_modified);

            static ImGuiTextFilter filter;
            filter.Draw("Filter colors", ImGui::GetFontSize() * 16);

            static ImGuiColorEditFlags alpha_flags = 0;
            if (ImGui::RadioButton("Opaque", alpha_flags == ImGuiColorEditFlags_None)) { alpha_flags = ImGuiColorEditFlags_None; } ImGui::SameLine();
            if (ImGui::RadioButton("Alpha", alpha_flags == ImGuiColorEditFlags_AlphaPreview)) { alpha_flags = ImGuiColorEditFlags_AlphaPreview; } ImGui::SameLine();
            if (ImGui::RadioButton("Both", alpha_flags == ImGuiColorEditFlags_AlphaPreviewHalf)) { alpha_flags = ImGuiColorEditFlags_AlphaPreviewHalf; } ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("In the color list:\n"
                "Left-click on color square to open color picker,\n"
                    "Right-click to open edit options menu.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::BeginChild("##colors", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NavFlattened);
            ImGui::PushItemWidth(-160);
            for (int i = 0; i < RenderCol_Count; i++)
            {
                const char* name = GetRenderColName(i);
                if (!filter.PassFilter(name))
                    continue;
                ImGui::PushID(i);
                ImGui::ColorEdit4("##color", (float*)&RenderColor::colors[i], ImGuiColorEditFlags_AlphaBar | alpha_flags);
                // Tips: in a real user application, you may want to merge and use an icon font into the main font,
                // so instead of "Save"/"Revert" you'd use icons!
                // Read the FAQ and docs/FONTS.md about using icon fonts. It's really easy and super convenient!
                ImGui::SameLine(0.0f, 3.0f);
                if (ImGui::Button("Set")) {
                    GLVolume::update_render_colors();
                    PartPlate::update_render_colors();
                    GLGizmoBase::update_render_colors();
                    GLCanvas3D::update_render_colors();
                    Bed3D::update_render_colors();
                }
                ImGui::SameLine(0.0f, 3.0f);
                ImGui::TextUnformatted(name);
                ImGui::PopID();
            }
            ImGui::PopItemWidth();
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void GLCanvas3D::_render_volumes_for_picking() const
{
    const auto& shader = wxGetApp().get_shader("flat");
    if (!shader)
        return;

    wxGetApp().bind_shader(shader);

    static const GLfloat INV_255 = 1.0f / 255.0f;

    // do not cull backfaces to show broken geometry, if any
    glsafe(::glDisable(GL_CULL_FACE));

    Camera& camera = wxGetApp().plater()->get_picking_camera();
    const auto& view_matrix = camera.get_view_matrix();
    const auto& proj_matrix = camera.get_projection_matrix();
    shader->set_uniform("projection_matrix", proj_matrix);
    std::vector<std::array<float, 4>> colors;
    for (size_t type = 0; type < 2; ++ type) {
        GLVolumeWithIdAndZList to_render = volumes_to_render(m_volumes.volumes, (type == 0) ? GLVolumeCollection::ERenderType::Opaque : GLVolumeCollection::ERenderType::Transparent, view_matrix);
        for (const GLVolumeWithIdAndZ& volume : to_render)
	        if (!volume.first->disabled && (volume.first->composite_id.volume_id >= 0 || m_render_sla_auxiliaries)) {
                auto world_box = volume.first->transformed_bounding_box();
                if (!camera.getFrustum().intersects(world_box)) {
                    continue;
                }
		        // Object picking mode. Render the object with a color encoding the object index.
                // we reserve color = (0,0,0) for occluders (as the printbed)
                // so we shift volumes' id by 1 to get the proper color
                //BBS: remove the bed picking logic
                unsigned int id = volume.second.first;
                //unsigned int id = 1 + volume.second.first;
                unsigned int r = (id & (0x000000FF << 0)) << 0;
		        unsigned int g = (id & (0x000000FF << 8)) >> 8;
		        unsigned int b = (id & (0x000000FF << 16)) >> 16;
		        unsigned int a = picking_checksum_alpha_channel(r, g, b);
                shader->set_uniform("view_model_matrix", view_matrix * volume.first->world_matrix());
                std::array<float, 4> t_color{ (GLfloat)r * INV_255, (GLfloat)g * INV_255, (GLfloat)b * INV_255, (GLfloat)a * INV_255 };
                shader->set_uniform("uniform_color", t_color);
                volume.first->picking = true;
                volume.first->render(camera, colors, *m_model); // colors is no use here
                volume.first->picking = false;
            }
    }

    wxGetApp().unbind_shader();

    glsafe(::glEnable(GL_CULL_FACE));
}

void GLCanvas3D::_render_current_gizmo() const
{
    //BBS update inv_zoom
    GLGizmoBase::INV_ZOOM = (float)get_active_camera().get_inv_zoom();
    m_gizmos.render_current_gizmo();
}

//BBS: GUI refactor: GLToolbar adjust
//when rendering, {0, 0} is at the center, left-up is -0.5, 0.5, right-up is 0.5, -0.5
void GLCanvas3D::_render_main_toolbar()
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return;
    }
    if (!p_main_toolbar->is_enabled())
        return;

    const auto& t_camera = get_active_camera();

    if (m_canvas_type == ECanvasType::CanvasAssembleView) {
        float inv_zoom = (float)t_camera.get_inv_zoom();
        const auto& t_viewport = t_camera.get_viewport();
        float top = 0.5f * (float)t_viewport[3] * inv_zoom;
        float left = 0.5f * m_paint_toolbar_width * inv_zoom;
        p_main_toolbar->set_position(top, left);
    }
    else {
        float inv_zoom = (float)t_camera.get_inv_zoom();
        const auto& t_viewport = t_camera.get_viewport();
        float left = get_main_toolbar_left(t_viewport[2], inv_zoom);
        if (is_collapse_toolbar_on_left()) {
            p_main_toolbar->set_offset(get_collapse_toolbar_width() * 2.0f);
        }
        else {
            p_main_toolbar->set_offset(0.0f);
        }
    }
    p_main_toolbar->render(t_camera);
    if (m_toolbar_highlighter.m_render_arrow){
        p_main_toolbar->render_arrow(m_toolbar_highlighter.m_toolbar_item);
    }
}

//BBS: GUI refactor: GLToolbar adjust
//when rendering, {0, 0} is at the center, {-0.5, 0.5} at the left-up
void GLCanvas3D::_render_imgui_select_plate_toolbar()
{
    if (!m_sel_plate_toolbar.is_enabled()) {
        if (!m_render_preview)
            m_render_preview = true;
        return;
    }

    _update_imgui_select_plate_toolbar();

    IMToolbarItem* all_plates_stats_item = m_sel_plate_toolbar.m_all_plates_stats_item;

    PartPlateList& plate_list = wxGetApp().plater()->get_partplate_list();
    for (int i = 0; i < plate_list.get_plate_count(); i++) {
        if (i < m_sel_plate_toolbar.m_items.size()) {
            if (i == plate_list.get_curr_plate_index() && !all_plates_stats_item->selected)
                m_sel_plate_toolbar.m_items[i]->selected = true;
            else
                m_sel_plate_toolbar.m_items[i]->selected = false;

            m_sel_plate_toolbar.m_items[i]->percent = plate_list.get_plate(i)->get_slicing_percent();

            if (plate_list.get_plate(i)->is_slice_result_valid()) {
                if (plate_list.get_plate(i)->is_slice_result_ready_for_print())
                    m_sel_plate_toolbar.m_items[i]->slice_state = IMToolbarItem::SliceState::SLICED;
                else
                    m_sel_plate_toolbar.m_items[i]->slice_state = IMToolbarItem::SliceState::SLICE_FAILED;
            }
            else {
                if (plate_list.get_plate(i)->has_printable_instances() && !plate_list.get_plate(i)->can_slice())
                    m_sel_plate_toolbar.m_items[i]->slice_state = IMToolbarItem::SliceState::SLICE_FAILED;
                else {
                    if (plate_list.get_plate(i)->get_slicing_percent() < 0.0f)
                        m_sel_plate_toolbar.m_items[i]->slice_state = IMToolbarItem::SliceState::UNSLICED;
                    else
                        m_sel_plate_toolbar.m_items[i]->slice_state = IMToolbarItem::SliceState::SLICING;
                }
            }
        }
    }
    if (m_sel_plate_toolbar.show_stats_item) {
        all_plates_stats_item->percent = 0.0f;

        size_t sliced_plates_cnt = 0;
        for (auto plate : plate_list.get_nonempty_plate_list()) {
            if (plate->is_slice_result_valid() && plate->is_slice_result_ready_for_print())
                sliced_plates_cnt++;
        }
        all_plates_stats_item->percent = (float)(sliced_plates_cnt) / (float)(plate_list.get_nonempty_plate_list().size()) * 100.0f;

        if (all_plates_stats_item->percent == 0.0f)
            all_plates_stats_item->slice_state = IMToolbarItem::SliceState::UNSLICED;
        else if (sliced_plates_cnt == plate_list.get_nonempty_plate_list().size())
            all_plates_stats_item->slice_state = IMToolbarItem::SliceState::SLICED;
        else if (all_plates_stats_item->percent < 100.0f)
            all_plates_stats_item->slice_state = IMToolbarItem::SliceState::SLICING;

        for (auto toolbar_item : m_sel_plate_toolbar.m_items) {
            if(toolbar_item->slice_state == IMToolbarItem::SliceState::SLICE_FAILED) {
                all_plates_stats_item->slice_state = IMToolbarItem::SliceState::SLICE_FAILED;
                all_plates_stats_item->selected = false;
                break;
            }
        }

        // Changing parameters does not invalid all plates, need extra logic to validate
        bool gcode_result_valid = true;
        for (auto gcode_result : plate_list.get_nonempty_plates_slice_results()) {
            if (gcode_result->moves.size() == 0) {
                gcode_result_valid = false;
            }
        }
        if (all_plates_stats_item->selected && all_plates_stats_item->slice_state == IMToolbarItem::SliceState::SLICED && gcode_result_valid) {
            m_gcode_viewer.render_all_plates_stats(plate_list.get_nonempty_plates_slice_results());
            m_can_show_navigator = false;
            m_render_preview = false;
        }
        else{
            m_gcode_viewer.render_all_plates_stats(plate_list.get_nonempty_plates_slice_results(), false);
            m_render_preview = true;
        }
    }else
        m_render_preview = true;

    // places the toolbar on the top_left corner of the 3d scene
#if ENABLE_RETINA_GL
    float f_scale  = m_retina_helper->get_scale_factor();
#else
    float f_scale  = 1.0;
#endif
    Size cnv_size = get_canvas_size();
    auto canvas_w = float(cnv_size.get_width());
    auto canvas_h = float(cnv_size.get_height());

    bool is_hovered = false;

    m_sel_plate_toolbar.set_icon_size(100.0f * f_scale, 100.0f * f_scale);

    float button_width = m_sel_plate_toolbar.icon_width;
    float button_height = m_sel_plate_toolbar.icon_height;

    float frame_padding = 1.0f * f_scale;
    float margin_size = 4.0f * f_scale;
    float button_margin = frame_padding;

    const float y_offset = is_collapse_toolbar_on_left() ? (get_collapse_toolbar_height() + 5) : 0;
    // Make sure the window does not overlap the 3d navigator
    auto window_height_max = canvas_h - y_offset;
    if (wxGetApp().show_3d_navigator()) {
        float sc = get_scale();
#ifdef WIN32
        const int dpi = get_dpi_for_window(wxGetApp().GetTopWindow());
        sc *= (float) dpi / (float) DPI_DEFAULT;
#endif // WIN32
        window_height_max -= (128 * sc + 5);
    }

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    int item_count = m_sel_plate_toolbar.m_items.size() + (m_sel_plate_toolbar.show_stats_item ? 1 : 0);
    bool show_scroll = item_count * (button_height + frame_padding * 2.0f + button_margin) - button_margin + 22.0f * f_scale > window_height_max ? true : false;
    show_scroll = m_sel_plate_toolbar.is_display_scrollbar && show_scroll;
    float window_height = std::min(item_count * (button_height + (frame_padding + margin_size) * 2.0f + button_margin) - button_margin + 28.0f * f_scale, window_height_max);
    float window_width = m_sel_plate_toolbar.icon_width + margin_size * 2 + (show_scroll ? 28.0f * f_scale : 20.0f * f_scale);

    ImVec4 window_bg = ImVec4(0.82f, 0.82f, 0.82f, 0.5f);
    ImVec4 button_active = ImVec4(0.12f, 0.56f, 0.92, 1.0f);
    ImVec4 button_hover = ImVec4(0.67f, 0.67f, 0.67, 1.0f);
    ImVec4 scroll_col = ImVec4(0.77f, 0.77f, 0.77f, 1.0f);
    //ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 0.f, 0.f, 1.0f));
    //use white text as the background switch to black
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, window_bg);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, window_bg);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, scroll_col);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, scroll_col);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, scroll_col);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hover);

    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

    imgui.set_next_window_pos(canvas_w * 0, canvas_h * 0 + y_offset, ImGuiCond_Always, 0, 0);
    imgui.set_next_window_size(window_width, window_height, ImGuiCond_Always);

    if (show_scroll)
        imgui.begin(_L("Select Plate"), ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);
    else
        imgui.begin(_L("Select Plate"), ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowFontScale(1.2f);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * f_scale);

    ImVec2 size = ImVec2(button_width, button_height); // Size of the image we want to make visible
    ImVec4 bg_col = ImVec4(128.0f, 128.0f, 128.0f, 0.0f);
    ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);               // No tint
    ImVec2 margin = ImVec2(margin_size, margin_size);

    if(m_sel_plate_toolbar.show_stats_item)
    {
        // draw image
        ImVec2 button_start_pos = ImGui::GetCursorScreenPos();

        if (all_plates_stats_item->selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, button_active);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_active);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active);
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(128.0f, 128.0f, 128.0f, 0.0f));
            if (all_plates_stats_item->slice_state == IMToolbarItem::SliceState::SLICE_FAILED) {
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_Button));
            }
            else {
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_hover);
            }
        }

        ImVec4 text_clr;
        ImTextureID btn_texture_id;
        if (all_plates_stats_item->slice_state == IMToolbarItem::SliceState::UNSLICED || all_plates_stats_item->slice_state == IMToolbarItem::SliceState::SLICING || all_plates_stats_item->slice_state == IMToolbarItem::SliceState::SLICE_FAILED)
        {
            text_clr = ImVec4(0, 174.0f / 255.0f, 66.0f / 255.0f, 0.2f);
            btn_texture_id = (ImTextureID)(intptr_t)(all_plates_stats_item->image_texture_transparent.get_id());
        }
        else
        {
            text_clr = ImVec4(0, 174.0f / 255.0f, 66.0f / 255.0f, 1);
            btn_texture_id = (ImTextureID)(intptr_t)(all_plates_stats_item->image_texture.get_id());
        }

        if (ImGui::ImageButton2(btn_texture_id, size, {0,0}, {1,1}, frame_padding, bg_col, tint_col, margin)) {
            if (all_plates_stats_item->slice_state != IMToolbarItem::SliceState::SLICE_FAILED) {
                if (m_process && !m_process->running()) {
                    for (int i = 0; i < m_sel_plate_toolbar.m_items.size(); i++) {
                        m_sel_plate_toolbar.m_items[i]->selected = false;
                    }
                    all_plates_stats_item->selected = true;
                    wxGetApp().plater()->update(true, true);
                    wxCommandEvent evt = wxCommandEvent(EVT_GLTOOLBAR_SLICE_ALL);
                    wxPostEvent(wxGetApp().plater(), evt);
                }
            }
        }
        if (!all_plates_stats_item->selected) {
            m_can_show_navigator = true;
        }
        ImGui::PopStyleColor(3);

        ImVec2 start_pos = ImVec2(button_start_pos.x + frame_padding + margin.x, button_start_pos.y + frame_padding + margin.y);
        if (all_plates_stats_item->slice_state == IMToolbarItem::SliceState::UNSLICED) {
            ImVec2 size = ImVec2(button_width, button_height);
            ImVec2 end_pos = ImVec2(start_pos.x + size.x, start_pos.y + size.y);
            ImGui::GetWindowDrawList()->AddRectFilled(start_pos, end_pos, IM_COL32(0, 0, 0, 80));
        }
        else if (all_plates_stats_item->slice_state == IMToolbarItem::SliceState::SLICING) {
            ImVec2 size = ImVec2(button_width, button_height * all_plates_stats_item->percent / 100.0f);
            ImVec2 rect_start_pos = ImVec2(start_pos.x, start_pos.y + size.y);
            ImVec2 rect_end_pos = ImVec2(start_pos.x + button_width, start_pos.y + button_height);
            ImGui::GetWindowDrawList()->AddRectFilled(start_pos, rect_end_pos, IM_COL32(0, 0, 0, 10));
            ImGui::GetWindowDrawList()->AddRectFilled(rect_start_pos, rect_end_pos, IM_COL32(0, 0, 0, 80));
        }
        else if (all_plates_stats_item->slice_state == IMToolbarItem::SliceState::SLICE_FAILED) {
            ImVec2 size = ImVec2(button_width, button_height);
            ImVec2 end_pos = ImVec2(start_pos.x + size.x, start_pos.y + size.y);
            ImGui::GetWindowDrawList()->AddRectFilled(start_pos, end_pos, IM_COL32(40, 1, 1, 64));
            ImGui::GetWindowDrawList()->AddRect(start_pos, end_pos, IM_COL32(208, 27, 27, 255), 0.0f, 0, 1.0f);
        }
        else if (all_plates_stats_item->slice_state == IMToolbarItem::SliceState::SLICED) {
            ImVec2 size = ImVec2(button_width, button_height);
            ImVec2 end_pos = ImVec2(start_pos.x + size.x, start_pos.y + size.y);
            ImGui::GetWindowDrawList()->AddRectFilled(start_pos, end_pos, IM_COL32(0, 0, 0, 10));
        }

        // draw text
        GImGui->FontSize = 15.0f;
        ImGui::PushStyleColor(ImGuiCol_Text, text_clr);
        ImVec2 text_size = ImGui::CalcTextSize(("All Plates"));
        ImVec2 text_start_pos = ImVec2(start_pos.x + (button_width - text_size.x) / 2, start_pos.y + 3.0f * button_height / 5.0f);
        ImGui::RenderText(text_start_pos, ("All Plates"));
        text_size = ImGui::CalcTextSize(("Stats"));
        text_start_pos = ImVec2(start_pos.x + (button_width - text_size.x) / 2, text_start_pos.y + ImGui::GetTextLineHeight());
        ImGui::RenderText(text_start_pos, ("Stats"));
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.2f);
    }

    ImVec4 error_text_clr = ImVec4(1, 0, 0, 1);
    for (int i = 0; i < m_sel_plate_toolbar.m_items.size(); i++) {
        IMToolbarItem* item = m_sel_plate_toolbar.m_items[i];

        // draw image
        ImVec2 button_start_pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        ImVec2 uv0 = ImVec2(0.0f, 1.0f);    // UV coordinates for lower-left
        ImVec2 uv1 = ImVec2(1.0f, 0.0f);    // UV coordinates in our texture

        auto button_pos = ImGui::GetCursorPos();
        ImGui::SetCursorPos(button_pos + margin);

        ImGui::Image(item->texture_id, size, uv0, uv1, tint_col);

        ImGui::SetCursorPos(button_pos);

        // invisible button
        auto button_size = size + margin + margin + ImVec2(2 * frame_padding, 2 * frame_padding);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f * f_scale);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));
        if (item->selected) {
            ImGui::PushStyleColor(ImGuiCol_Border, button_active);
        }
        else {
            if (ImGui::IsMouseHoveringRect(button_start_pos, button_start_pos + button_size)) {
                ImGui::PushStyleColor(ImGuiCol_Border, button_hover);
            }
            else {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.0f, .0f, .0f, .0f));
            }
        }
        if(ImGui::Button("##invisible_button", button_size)){
            if (m_process && !m_process->running()) {
                all_plates_stats_item->selected = false;
                item->selected = true;
                // begin to slicing plate
                if (item->slice_state != IMToolbarItem::SliceState::SLICED)
                    wxGetApp().plater()->update(true, true);
                wxCommandEvent* evt = new wxCommandEvent(EVT_GLTOOLBAR_SELECT_SLICED_PLATE);
                evt->SetInt(i);
                wxQueueEvent(wxGetApp().plater(), evt);
            }
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();

        ImVec2 start_pos = ImVec2(button_start_pos.x + frame_padding + margin.x, button_start_pos.y + frame_padding + margin.y);
        if (item->slice_state == IMToolbarItem::SliceState::UNSLICED) {
            ImVec2 size = ImVec2(button_width, button_height);
            ImVec2 end_pos = ImVec2(start_pos.x + size.x, start_pos.y + size.y);
            ImGui::GetWindowDrawList()->AddRectFilled(start_pos, end_pos, IM_COL32(0, 0, 0, 80));
        } else if (item->slice_state == IMToolbarItem::SliceState::SLICING) {
            ImVec2 size = ImVec2(button_width, button_height * item->percent / 100.0f);
            ImVec2 rect_start_pos = ImVec2(start_pos.x, start_pos.y + size.y);
            ImVec2 rect_end_pos = ImVec2(start_pos.x + button_width, start_pos.y + button_height);
            ImGui::GetWindowDrawList()->AddRectFilled(start_pos, rect_end_pos, IM_COL32(0, 0, 0, 10));
            ImGui::GetWindowDrawList()->AddRectFilled(rect_start_pos, rect_end_pos, IM_COL32(0, 0, 0, 80));
        } else if (item->slice_state == IMToolbarItem::SliceState::SLICE_FAILED) {
            ImVec2 size    = ImVec2(button_width, button_height);
            ImVec2 end_pos = ImVec2(start_pos.x + size.x, start_pos.y + size.y);
            ImGui::GetWindowDrawList()->AddRectFilled(start_pos, end_pos, IM_COL32(250, 0, 0, 64));
            ImGui::GetWindowDrawList()->AddRect(start_pos, end_pos, IM_COL32(208, 27, 27, 255), 0.0f, 0, 1.0f);
        } else if (item->slice_state == IMToolbarItem::SliceState::SLICED) {
            ImVec2 size = ImVec2(button_width, button_height);
            ImVec2 end_pos = ImVec2(start_pos.x + size.x, start_pos.y + size.y);
            ImGui::GetWindowDrawList()->AddRectFilled(start_pos, end_pos, IM_COL32(0, 0, 0, 10));
        }
        // draw text
        if (item->slice_state == IMToolbarItem::SliceState::SLICE_FAILED) {
            ImGui::PushStyleColor(ImGuiCol_Text, error_text_clr);
            ImVec2 text_start_pos = ImVec2(start_pos.x + 10.0f, start_pos.y + 8.0f);
            ImGui::RenderText(text_start_pos, std::to_string(i + 1).c_str());
            ImGui::PopStyleColor();

        } else {
            ImVec2 text_start_pos = ImVec2(start_pos.x + 10.0f, start_pos.y + 8.0f);
            ImGui::RenderText(text_start_pos, std::to_string(i + 1).c_str());
        }
        ImGui::PopID();
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(5);

    if (ImGui::IsWindowHovered() || is_hovered) {
        m_sel_plate_toolbar.is_display_scrollbar = true;
    } else {
        m_sel_plate_toolbar.is_display_scrollbar = false;
    }

    imgui.end();
}

void GLCanvas3D::_render_return_toolbar()
{
    if (!m_return_toolbar.is_enabled())
        return;

    float font_size = ImGui::GetFontSize();
    ImVec2 real_size = ImVec2(font_size * 4, font_size * 1.7);
    ImVec2 button_icon_size = ImVec2(font_size * 1.3, font_size * 1.3);

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    Size cnv_size = get_canvas_size();
    auto canvas_w = float(cnv_size.get_width());
    auto canvas_h = float(cnv_size.get_height());
    float window_width = real_size.x + button_icon_size.x + imgui.scaled(2.0f);
    float window_height = button_icon_size.y + imgui.scaled(2.0f);
    float window_pos_x  = 30.0f + (is_collapse_toolbar_on_left() ? (get_collapse_toolbar_width() + 5.f) : 0);
    float window_pos_y = 14.0f;
    {//solve ui overlap issue
        if (m_canvas_type == ECanvasType::CanvasView3D) {
            float       zoom      = (float) get_active_camera().get_zoom();
            float       left_pos = 0.0f;
            const auto& p_main_toolbar = get_main_toolbar();
            if (p_main_toolbar) {
                left_pos = p_main_toolbar->get_item("add")->render_rect[0];
            }
            const float toolbar_x = 0.5 * canvas_w + left_pos * zoom;
            const float margin    = 5;
            if (toolbar_x < window_width + margin * 3) {
                window_pos_x = 5.0f;
                window_pos_y = 2.0f;
                if (p_main_toolbar) {
                    window_pos_y += p_main_toolbar->get_height();
                }
            }
        }
    }
    imgui.set_next_window_pos(window_pos_x, window_pos_y, ImGuiCond_Always, 0, 0);
#ifdef __WINDOWS__
    imgui.set_next_window_size(window_width, window_height, ImGuiCond_Always);
#endif

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 18.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.149f, 0.180f, 0.188f, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.149f, 0.180f, 0.188f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.149f, 0.180f, 0.188f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    imgui.begin(_L("Assembly Return"), ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);

    float button_width = 20;
    float button_height = 20;
    ImVec2 size = ImVec2(button_width, button_height); // Size of the image we want to make visible
    ImVec2 uv0 = ImVec2(0.0f, 0.0f);
    ImVec2 uv1 = ImVec2(1.0f, 1.0f);

    ImVec4 bg_col = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec2 margin = ImVec2(10.0f, 5.0f);

    if (ImGui::ImageTextButton(real_size,_utf8(L("return")).c_str(), m_return_toolbar.get_return_texture_id(), button_icon_size, uv0, uv1, -1, bg_col, tint_col, margin)) {
        if (m_canvas_type == ECanvasType::CanvasView3D) {
            deselect_all();
        } else if (m_canvas_type == ECanvasType::CanvasAssembleView) {
            if (m_canvas != nullptr)
                wxPostEvent(m_canvas, SimpleEvent(EVT_GLVIEWTOOLBAR_3D));
            const_cast<GLGizmosManager *>(&m_gizmos)->reset_all_states();
            wxGetApp().plater()->get_view3D_canvas3D()->get_gizmos_manager().reset_all_states();
            GLVolume::explosion_ratio  = 1.0;//in 3D view  GLVolume::explosion_ratio  = 1.0
            wxGetApp().plater()->get_view3D_canvas3D()->reload_scene(true);
            {
                GLCanvas3D *                          view_3d       = wxGetApp().plater()->get_view3D_canvas3D();
                const auto& p_main_toolbar = view_3d->get_main_toolbar();
                if (!p_main_toolbar) {
                    return;
                }
                std::shared_ptr<GLToolbarItem> assembly_item = p_main_toolbar->get_item("assembly_view");
                std::chrono::system_clock::time_point end           = std::chrono::system_clock::now();
                std::chrono::duration<int>            duration      = std::chrono::duration_cast<std::chrono::duration<int>>(end - assembly_item->get_start_time_point());
                int                                   times         = duration.count();

                NetworkAgent *agent = GUI::wxGetApp().getAgent();
                if (agent) {
                    std::string name          = assembly_item->get_name() + "_duration";
                    std::string value         = "";
                    int         existing_time = 0;

                    agent->track_get_property(name, value);
                    try {
                        if (value != "") { existing_time = std::stoi(value); }
                    } catch (...) {}

                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " tool name:" << name << " duration: " << times + existing_time;
                    agent->track_update_property(name, std::to_string(times + existing_time));
                }
            }
        }
    }
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(1);

    imgui.end();
}

void GLCanvas3D::_render_fit_camera_toolbar()
{
    float  font_size        = ImGui::GetFontSize();
    ImVec2 button_icon_size = ImVec2(font_size * 2.5, font_size * 2.5);

    ImGuiWrapper &imgui         = *wxGetApp().imgui();
    float         window_width  = button_icon_size.x + imgui.scaled(2.0f);
    float         window_height = button_icon_size.y + imgui.scaled(2.0f);

    Size cnv_size              = get_canvas_size();
    m_fit_camrea_button_pos[1] = cnv_size.get_height() - button_icon_size[1] - 20 * m_sc;
    imgui.set_next_window_pos(m_fit_camrea_button_pos[0], m_fit_camrea_button_pos[1], ImGuiCond_Always, 0, 0);
#ifdef __WINDOWS__
    imgui.set_next_window_size(window_width, window_height, ImGuiCond_Always);
#endif

    imgui.begin(_L("Fit camera"), ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);//

    ImTextureID normal_id = m_gizmos.get_icon_texture_id(m_is_dark ? GLGizmosManager::MENU_ICON_NAME::IC_FIT_CAMERA_DARK : GLGizmosManager::MENU_ICON_NAME::IC_FIT_CAMERA);
    ImTextureID hover_id  = m_gizmos.get_icon_texture_id(m_is_dark ? GLGizmosManager::MENU_ICON_NAME::IC_FIT_CAMERA_DARK_HOVER : GLGizmosManager::MENU_ICON_NAME::IC_FIT_CAMERA_HOVER);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});

    if (ImGui::ImageButton3(normal_id, hover_id, button_icon_size, ImVec2(0, 0), ImVec2(1, 1),  -1,
                           ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1), ImVec2(10, 0))) {
        select_view("plate");
        if (m_selection.is_empty()) {
            if (m_canvas_type == ECanvasType::CanvasAssembleView) {
                zoom_to_volumes();
            }
            else {
                zoom_to_bed();
            }
        }
        else {
            zoom_to_selection();
        }
    }
    if (ImGui::IsItemHovered()) {
        auto temp_tooltip = _L("Fit camera to scene or selected object.");
        auto width        = ImGui::CalcTextSize(temp_tooltip.c_str()).x + imgui.scaled(2.0f);
        imgui.tooltip(temp_tooltip, width);
    }
    ImGui::PopStyleVar(2);

    imgui.end();
}

void GLCanvas3D::_render_collapse_toolbar() const
{
    auto &     plater              = *wxGetApp().plater();
    const auto sidebar_docking_dir = plater.get_sidebar_docking_state();
    if (sidebar_docking_dir == Sidebar::None) {
        return;
    }
    GLToolbar& collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();

    const auto& t_camera = get_active_camera();
    collapse_toolbar.render(t_camera);
}

//BBS reander assemble toolbar
void GLCanvas3D::_render_paint_toolbar() const
{
    if (m_canvas_type != ECanvasType::CanvasAssembleView)
        return;
#if ENABLE_RETINA_GL
    float f_scale = m_retina_helper->get_scale_factor();
#else
    float f_scale = 1.0f;
#endif
    int em_unit = wxGetApp().em_unit() / 10;

    std::vector<std::string> colors = wxGetApp().plater()->get_extruder_colors_from_plater_config();
    int extruder_num = colors.size();
    std::vector<std::string> filament_text_first_line;
    std::vector<std::string> filament_text_second_line;
    {
        auto preset_bundle = wxGetApp().preset_bundle;
        for (auto filament_name : preset_bundle->filament_presets) {
            for (auto iter = preset_bundle->filaments.lbegin(); iter != preset_bundle->filaments.end(); iter++) {
                if (filament_name.compare(iter->name) == 0) {
                    std::string display_filament_type;
                    iter->config.get_filament_type(display_filament_type);
                    auto pos = display_filament_type.find(' ');
                    if (pos != std::string::npos) {
                        filament_text_first_line.push_back(display_filament_type.substr(0, pos));
                        filament_text_second_line.push_back(display_filament_type.substr(pos + 1));
                    }
                    else {
                        filament_text_first_line.push_back(display_filament_type);
                        filament_text_second_line.push_back("");
                    }
                }
            }
        }
    }

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    const float canvas_w = float(get_canvas_size().get_width());
    const ImVec2 button_size = ImVec2(64.0f, 48.0f) * f_scale * em_unit;
    const float spacing = 4.0f * em_unit * f_scale;
    const float return_button_margin = 130.0f * em_unit * f_scale;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(spacing, spacing));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, { 0.f, 0.f, 0.f, 0.4f });

    imgui.set_next_window_pos(0.5f * canvas_w, 0, ImGuiCond_Always, 0.5f, 0.0f);
    float constraint_window_width = canvas_w - 2 * return_button_margin;
    ImGui::SetNextWindowSizeConstraints({ 0, 0 }, { constraint_window_width, FLT_MAX });
    imgui.begin(_L("Paint Toolbar"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float cursor_y = ImGui::GetCursorPosY();
    const ImVec2 arrow_button_size = ImVec2(0.375f * button_size.x, ImGui::GetWindowHeight());
    const ImRect left_arrow_button = ImRect(ImGui::GetCurrentWindow()->Pos, ImGui::GetCurrentWindow()->Pos + arrow_button_size);
    const ImRect right_arrow_button = ImRect(ImGui::GetCurrentWindow()->Pos + ImGui::GetWindowSize() - arrow_button_size, ImGui::GetCurrentWindow()->Pos + ImGui::GetWindowSize());
    ImU32 left_arrow_button_color = IM_COL32(0, 0, 0, 0.4f * 255);
    ImU32 right_arrow_button_color = IM_COL32(0, 0, 0, 0.4f * 255);
    ImU32 arrow_color = IM_COL32(255, 255, 255, 255);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiContext& context = *GImGui;
    bool disabled = !wxGetApp().plater()->can_fillcolor();
    unsigned char rgb[3];

    for (int i = 0; i < extruder_num; i++) {
        if (i > 0)
            ImGui::SameLine();
        Slic3r::GUI::BitmapCache::parse_color(colors[i], rgb);
        ImGui::PushStyleColor(ImGuiCol_Button, ImColor(rgb[0], rgb[1], rgb[2]).Value);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(rgb[0], rgb[1], rgb[2]).Value);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImColor(rgb[0], rgb[1], rgb[2]).Value);
        if (disabled)
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        if (ImGui::Button(("##filament_button" + std::to_string(i)).c_str(), button_size)) {
            if (!ImGui::IsMouseHoveringRect(left_arrow_button.Min, left_arrow_button.Max) && !ImGui::IsMouseHoveringRect(right_arrow_button.Min, right_arrow_button.Max))
                wxPostEvent(m_canvas, IntEvent(EVT_GLTOOLBAR_FILLCOLOR, i + 1));
        }
        if (ImGui::IsItemHovered() && i < 9) {
            if (!ImGui::IsMouseHoveringRect(left_arrow_button.Min, left_arrow_button.Max) && !ImGui::IsMouseHoveringRect(right_arrow_button.Min, right_arrow_button.Max)) {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 20.0f * f_scale, 10.0f * f_scale });
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f * f_scale);
                imgui.tooltip(_L("Shortcut Key ") + std::to_string(i + 1), ImGui::GetFontSize() * 20.0f);
                ImGui::PopStyleVar(2);
            }
        }
        ImGui::PopStyleColor(3);
        if (disabled)
            ImGui::PopItemFlag();
    }

    const float text_offset_y = 4.0f * em_unit * f_scale;
    for (int i = 0; i < extruder_num; i++){
        Slic3r::GUI::BitmapCache::parse_color(colors[i], rgb);
        float gray = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2];
        ImVec4 text_color = gray < 80 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0, 0, 0, 1.0f);

        imgui.push_bold_font();
        ImVec2 number_label_size = ImGui::CalcTextSize(std::to_string(i + 1).c_str());
        ImGui::SetCursorPosY(cursor_y + text_offset_y);
        ImGui::SetCursorPosX(spacing + i * (spacing + button_size.x) + (button_size.x - number_label_size.x) / 2);
        ImGui::TextColored(text_color, std::to_string(i + 1).c_str());
        imgui.pop_bold_font();

        ImVec2 filament_first_line_label_size = ImGui::CalcTextSize(filament_text_first_line[i].c_str());
        ImGui::SetCursorPosY(cursor_y + text_offset_y + number_label_size.y);
        ImGui::SetCursorPosX(spacing + i * (spacing + button_size.x) + (button_size.x - filament_first_line_label_size.x) / 2);
        ImGui::TextColored(text_color, filament_text_first_line[i].c_str());

        ImVec2 filament_second_line_label_size = ImGui::CalcTextSize(filament_text_second_line[i].c_str());
        ImGui::SetCursorPosY(cursor_y + text_offset_y + number_label_size.y + filament_first_line_label_size.y);
        ImGui::SetCursorPosX(spacing + i * (spacing + button_size.x) + (button_size.x - filament_second_line_label_size.x) / 2);
        ImGui::TextColored(text_color, filament_text_second_line[i].c_str());
    }

    if (ImGui::GetWindowWidth() == constraint_window_width) {
        if (ImGui::IsMouseHoveringRect(left_arrow_button.Min, left_arrow_button.Max)) {
            left_arrow_button_color = IM_COL32(0, 0, 0, 0.64f * 255);
            if (context.IO.MouseClicked[ImGuiMouseButton_Left]) {
                ImGui::SetScrollX(ImGui::GetScrollX() - button_size.x);
                imgui.set_requires_extra_frame();
            }
        }
        draw_list->AddRectFilled(left_arrow_button.Min, left_arrow_button.Max, left_arrow_button_color);
        ImGui::BBLRenderArrow(draw_list, left_arrow_button.GetCenter() - ImVec2(draw_list->_Data->FontSize, draw_list->_Data->FontSize) * 0.5f, arrow_color, ImGuiDir_Left, 2.0f);

        if (ImGui::IsMouseHoveringRect(right_arrow_button.Min, right_arrow_button.Max)) {
            right_arrow_button_color = IM_COL32(0, 0, 0, 0.64f * 255);
            if (context.IO.MouseClicked[ImGuiMouseButton_Left]) {
                ImGui::SetScrollX(ImGui::GetScrollX() + button_size.x);
                imgui.set_requires_extra_frame();
            }
        }
        draw_list->AddRectFilled(right_arrow_button.Min, right_arrow_button.Max, right_arrow_button_color);
        ImGui::BBLRenderArrow(draw_list, right_arrow_button.GetCenter() - ImVec2(draw_list->_Data->FontSize, draw_list->_Data->FontSize) * 0.5f, arrow_color, ImGuiDir_Right, 2.0f);
    }

    m_paint_toolbar_width = (ImGui::GetWindowWidth() + 50.0f * em_unit * f_scale);
    imgui.end();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

float GLCanvas3D::_show_assembly_tooltip_information(float caption_max, float x, float y) const
{
    ImGuiWrapper *imgui     = wxGetApp().imgui();
    ImTextureID normal_id = m_gizmos.get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id  = m_gizmos.get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    caption_max += imgui->calc_text_size(": ").x + 35.f;

    float  font_size   = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size * 1.8, font_size * 1.3);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, ImGui::GetStyle().FramePadding.y});
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &imgui, & caption_max](const wxString &caption, const wxString &text) {
            imgui->text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };

        for (const auto &t : std::array<std::string, 3>{"object_selection", "part_selection", "number_key"}) {
            draw_text_with_caption(m_assembly_view_desc.at(t + "_caption") + ": ", m_assembly_view_desc.at(t));
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
    auto same_line_size = button_size.x * 1.8;//with an space size
    ImGui::SameLine(same_line_size);
    same_line_size = imgui->calc_text_size("|").x + same_line_size + imgui->calc_text_size("  ").x;
    imgui->text_colored(ImGuiWrapper::COL_ACTIVE, "|");
    ImGui::SameLine(same_line_size);
    return same_line_size;
}

//BBS
void GLCanvas3D::_render_assemble_control()
{
    if (m_canvas_type != ECanvasType::CanvasAssembleView) {
        GLVolume::explosion_ratio = m_explosion_ratio = 1.0;
        return;
    }
    if (m_gizmos.get_current_type() == GLGizmosManager::EType::MmuSegmentation) {
        m_gizmos.m_assemble_view_data->model_objects_clipper()->set_position(0.0, true);
        return;
    }

    ImGuiWrapper* imgui = wxGetApp().imgui();

    ImGuiWrapper::push_toolbar_style(get_scale());

    auto canvas_w = float(get_canvas_size().get_width());
    auto canvas_h = float(get_canvas_size().get_height());

    const float text_padding = 7.0f;
    const float text_size_x = std::max(imgui->calc_text_size(_L("Reset direction")).x + 2 * ImGui::GetStyle().FramePadding.x,
        std::max(imgui->calc_text_size(_L("Explosion Ratio")).x, imgui->calc_text_size(_L("Section View")).x));
    const float slider_width = 60.0f;
    const float value_size = imgui->calc_text_size("3.00").x + text_padding * 2;
    const float item_spacing = imgui->get_item_spacing().x;
    ImVec2 window_padding = ImGui::GetStyle().WindowPadding;

    imgui->set_next_window_pos(canvas_w / 2, canvas_h - 10.0f * get_scale(), ImGuiCond_Always, 0.5f, 1.0f);
    imgui->begin(_L("Assemble Control"), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    ImGui::AlignTextToFramePadding();
    float tip_icon_size;
    {
        float caption_max = 0.f;
        for (const auto &t : std::array<std::string, 3>{"object_selection", "part_selection", "number_key"}) {
            caption_max = std::max(caption_max, imgui->calc_text_size(m_assembly_view_desc.at(t + "_caption")).x);
        }
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float text_y =imgui->calc_text_size(_L("part selection")).y;
        float get_cur_x = pos.x;
        float get_cur_y = pos.y - ImGui::GetFrameHeight() - 4 * text_y;
        tip_icon_size =_show_assembly_tooltip_information(caption_max, get_cur_x, get_cur_y);
    }
    float same_line_width = tip_icon_size;
    {
        float clp_dist = m_gizmos.m_assemble_view_data->model_objects_clipper()->get_position();
        if (clp_dist == 0.f) {
            ImGui::AlignTextToFramePadding();
            imgui->text(_L("Section View"));
        }
        else {
            if (imgui->button(_L("Reset direction"))) {
                wxGetApp().CallAfter([this]() {
                    m_gizmos.m_assemble_view_data->model_objects_clipper()->set_position(-1., false);
                    });
            }
        }
        same_line_width += (text_size_x + item_spacing);
        ImGui::SameLine(same_line_width);
        ImGui::PushItemWidth(slider_width);
        bool view_slider_changed = imgui->bbl_slider_float_style("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true);

        same_line_width += (slider_width + item_spacing);
        ImGui::SameLine(same_line_width);
        ImGui::PushItemWidth(value_size);
        bool view_input_changed = ImGui::BBLDragFloat("##clp_dist_input", &clp_dist, 0.05f, 0.0f, 0.0f, "%.2f");

        if (view_slider_changed || view_input_changed)
            m_gizmos.m_assemble_view_data->model_objects_clipper()->set_position(clp_dist, true);

        same_line_width += (value_size + item_spacing * 2);
    }
    {
        auto temp_x = imgui->calc_text_size(_L("Explosion Ratio")).x;
        ImGui::SameLine(same_line_width);
        ImGui::PushItemWidth(temp_x);
        imgui->text(_L("Explosion Ratio"));

        same_line_width +=   (temp_x + item_spacing);
        ImGui::SameLine(same_line_width);
        ImGui::PushItemWidth(slider_width);
        bool explosion_slider_changed = imgui->bbl_slider_float_style("##ratio_slider", &m_explosion_ratio, 1.0f, 3.0f, "%1.2f");

        same_line_width +=  (slider_width + item_spacing);
        ImGui::SameLine(same_line_width);
        ImGui::PushItemWidth(value_size);
        bool explosion_input_changed = ImGui::BBLDragFloat("##ratio_input", &m_explosion_ratio, 0.1f, 1.0f, 3.0f, "%1.2f");
        same_line_width    +=   (value_size + item_spacing*2);
    }
    {
        ImGui::SameLine(same_line_width);
        // input
        std::vector<std::string> modes = {_u8L("Object"), _u8L("Part")};
        int selection_idx = m_selection.get_volume_selection_mode() == Selection::Instance ? 0 : 1;
        auto label         = _u8L("Selection Mode") + ":" ;
        auto label_width   = imgui->calc_text_size(label).x ;
        auto item_width   = imgui->calc_text_size(_L("Object")).x * 2.5 + imgui->calc_text_size("x").x+imgui->scaled(2);

        //render imgui
        ImGui::AlignTextToFramePadding();
        ImGui::PushItemWidth(label_width);
        imgui->text(label);
        same_line_width += (label_width + item_spacing);
        ImGui::SameLine(same_line_width);
        ImGui::PushItemWidth(item_width);
        size_t selection_out = selection_idx;
        const char *selected_str = (selection_idx >= 0 && selection_idx < int(modes.size())) ? modes[selection_idx].c_str() : "";
        ImGuiWrapper::push_combo_style(get_scale());
        if (ImGui::BBLBeginCombo(("##" + label).c_str(), selected_str, 0)) {
            for (size_t line_idx = 0; line_idx < modes.size(); ++line_idx) {
                ImGui::PushID(int(line_idx));
                if (ImGui::Selectable("", line_idx == selection_idx))
                    selection_out = line_idx;

                ImGui::SameLine();
                ImGui::Text("%s", modes[line_idx].c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGuiWrapper::pop_combo_style();
        if (selection_idx != selection_out) {//do
            if (selection_out == 0) { m_selection.unlock_volume_selection_mode(); }
            m_selection.set_volume_selection_mode(selection_out == 1 ? Selection::Volume : Selection::Instance);
            if (selection_out == 1) { m_selection.lock_volume_selection_mode(); }
        }
        same_line_width += (label_width + item_width);
    }
    imgui->end();

    ImGuiWrapper::pop_toolbar_style();

    //BBS check ratio changed
    if (m_explosion_ratio != GLVolume::explosion_ratio) {
        for (GLVolume* volume : m_volumes.volumes) {
            volume->set_bounding_boxes_as_dirty();
        }
        GLVolume::explosion_ratio = m_explosion_ratio;
    }
}
void GLCanvas3D::_render_assemble_info() const
{
    if (m_canvas_type != ECanvasType::CanvasAssembleView) {
        return;
    }

    if (m_selection.is_empty()) {
        return;
    }

    ImGuiWrapper* imgui = wxGetApp().imgui();
    auto canvas_w = float(get_canvas_size().get_width());
    auto canvas_h = float(get_canvas_size().get_height());
    float space_size = imgui->get_style_scaling() * 8.0f;
    float caption_max = imgui->calc_text_size(_L("Total Volume:")).x + 3 * space_size;

    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->Fonts[0];
    float origScale = font->Scale;
    font->Scale = 1.2;
    ImGui::PushFont(font);
    ImGui::PopFont();
    float margin = 10.0f * get_scale();
    imgui->set_next_window_pos(canvas_w - margin, canvas_h - margin, ImGuiCond_Always, 1.0f, 1.0f);
    ImGuiWrapper::push_toolbar_style(get_scale());
    imgui->begin(_L("Assembly Info"), ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    font->Scale = origScale;
    ImGui::PushFont(font);
    ImGui::PopFont();

    double size0 = m_selection.get_bounding_box().size()(0);
    double size1 = m_selection.get_bounding_box().size()(1);
    double size2 = m_selection.get_bounding_box().size()(2);
    if (!m_selection.is_empty()) {
        ImGui::Text(_L("Volume:").ToUTF8()); ImGui::SameLine(caption_max);
        ImGui::Text("%.2f", size0 * size1 * size2);
        ImGui::Text(_L("Size:").ToUTF8()); ImGui::SameLine(caption_max);
        ImGui::Text("%.2f x %.2f x %.2f", size0, size1, size2);
    }
    imgui->end();
    ImGuiWrapper::pop_toolbar_style();
}

#if ENABLE_SHOW_CAMERA_TARGET
void GLCanvas3D::_render_camera_target() const
{
    const auto& p_flat_shader = wxGetApp().get_shader("flat");
    if (!p_flat_shader)
        return;

    double half_length = 5.0;

    if (!m_camera_target_mark.is_initialized()) {
        GLModel::Geometry geo;
        geo.format.type = GLModel::PrimitiveType::Lines;
        geo.format.vertex_layout = GLModel::Geometry::EVertexLayout::P3;

        // x
        geo.add_vertex(Vec3f{ -0.5f, 0.0f, 0.0f });
        geo.add_vertex(Vec3f{ 0.5f, 0.0f, 0.0f });

        geo.add_line(0, 1);

        m_camera_target_mark.init_from(std::move(geo));
    }

    wxGetApp().bind_shader(p_flat_shader);

    const Camera& camera = get_active_camera();
    const auto& view_matrix = camera.get_view_matrix();
    const auto& proj_matrix = camera.get_projection_matrix();

    glsafe(::glDisable(GL_DEPTH_TEST));

    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    p_ogl_manager.set_line_width(2.0f);

    const Vec3d& target = get_active_camera().get_target();

    const float scale = 2.0f * half_length;
    Transform3d model_matrix{ Transform3d::Identity() };
    model_matrix.data()[3 * 4 + 0] = target.x();
    model_matrix.data()[3 * 4 + 1] = target.y();
    model_matrix.data()[3 * 4 + 2] = target.z();
    model_matrix.data()[0 * 4 + 0] = scale;
    model_matrix.data()[1 * 4 + 1] = 1.0f;
    model_matrix.data()[2 * 4 + 2] = 1.0f;

    p_flat_shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
    p_flat_shader->set_uniform("projection_matrix", proj_matrix);
    m_camera_target_mark.set_color({ 1.0f, 0.0f, 0.0f, 1.0f });
    m_camera_target_mark.render_geometry();

    model_matrix = Geometry::translation_transform(target.cast<double>())
        * Geometry::rotation_transform({ 0.0f, 0.0f, 0.5 * PI })
        * Geometry::scale_transform({ scale, 1.0f, 1.0f });

    p_flat_shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
    m_camera_target_mark.set_color({ 0.0f, 1.0f, 0.0f, 1.0f });
    m_camera_target_mark.render_geometry();

    model_matrix = Geometry::translation_transform(target.cast<double>())
        * Geometry::rotation_transform({ 0.0f, -0.5 * PI, 0.0f })
        * Geometry::scale_transform({ scale, 1.0f, 1.0f });

    p_flat_shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
    m_camera_target_mark.set_color({ 0.0f, 0.0f, 1.0f, 1.0f });
    m_camera_target_mark.render_geometry();

    wxGetApp().unbind_shader();
}
#endif // ENABLE_SHOW_CAMERA_TARGET

void GLCanvas3D::_render_sla_slices()
{
    if (!m_use_clipping_planes || current_printer_technology() != ptSLA)
        return;

    const auto& shader = wxGetApp().get_shader("flat");
    if (!shader) {
        return;
    }

    const SLAPrint* print = this->sla_print();
    const PrintObjects& print_objects = print->objects();
    if (print_objects.empty())
        // nothing to render, return
        return;

    wxGetApp().bind_shader(shader);

    double clip_min_z = -m_clipping_planes[0].get_data()[3];
    double clip_max_z = m_clipping_planes[1].get_data()[3];
    for (unsigned int i = 0; i < (unsigned int)print_objects.size(); ++i) {
        const SLAPrintObject* obj = print_objects[i];

        if (!obj->is_step_done(slaposSliceSupports))
            continue;

        SlaCap::ObjectIdToModelsMap::iterator it_caps_bottom = m_sla_caps[0].triangles.find(i);
        SlaCap::ObjectIdToModelsMap::iterator it_caps_top = m_sla_caps[1].triangles.find(i);
        {
            if (it_caps_bottom == m_sla_caps[0].triangles.end())
                it_caps_bottom = m_sla_caps[0].triangles.emplace(i, SlaCap::Triangles()).first;
            if (!m_sla_caps[0].matches(clip_min_z)) {
                m_sla_caps[0].z = clip_min_z;
                it_caps_bottom->second.object.reset();
                it_caps_bottom->second.supports.reset();
            }
            if (it_caps_top == m_sla_caps[1].triangles.end())
                it_caps_top = m_sla_caps[1].triangles.emplace(i, SlaCap::Triangles()).first;
            if (!m_sla_caps[1].matches(clip_max_z)) {
                m_sla_caps[1].z = clip_max_z;
                it_caps_top->second.object.reset();
                it_caps_top->second.supports.reset();
            }
        }
        GLModel& bottom_obj_triangles = it_caps_bottom->second.object;
        GLModel& bottom_sup_triangles = it_caps_bottom->second.supports;
        GLModel& top_obj_triangles = it_caps_top->second.object;
        GLModel& top_sup_triangles = it_caps_top->second.supports;

        auto init_model = [](GLModel& model, const Pointf3s& triangles, const ColorRGBA& color) {
            GLModel::Geometry init_data;
            init_data.format = { GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
            init_data.reserve_vertices(triangles.size());
            init_data.reserve_indices(triangles.size() / 3);
            init_data.color = color;

            unsigned int vertices_count = 0;
            for (const Vec3d& v : triangles) {
                init_data.add_vertex((Vec3f)v.cast<float>());
                ++vertices_count;
                if (vertices_count % 3 == 0) {
                    init_data.add_triangle(vertices_count - 3, vertices_count - 2, vertices_count - 1);
                }
            }

            if (!init_data.is_empty())
                model.init_from(std::move(init_data));
            };

        if ((!bottom_obj_triangles.is_initialized() || !bottom_sup_triangles.is_initialized() ||
            !top_obj_triangles.is_initialized() || !top_sup_triangles.is_initialized()) && !obj->get_slice_index().empty()) {
            double layer_height = print->default_object_config().layer_height.value;
            double initial_layer_height = print->material_config().initial_layer_height.value;
            bool   left_handed = obj->is_left_handed();

            coord_t key_zero = obj->get_slice_index().front().print_level();
            // Slice at the center of the slab starting at clip_min_z will be rendered for the lower plane.
            coord_t key_low = coord_t((clip_min_z - initial_layer_height + layer_height) / SCALING_FACTOR) + key_zero;
            // Slice at the center of the slab ending at clip_max_z will be rendered for the upper plane.
            coord_t key_high = coord_t((clip_max_z - initial_layer_height) / SCALING_FACTOR) + key_zero;

            const SliceRecord& slice_low = obj->closest_slice_to_print_level(key_low, coord_t(SCALED_EPSILON));
            const SliceRecord& slice_high = obj->closest_slice_to_print_level(key_high, coord_t(SCALED_EPSILON));

            // Offset to avoid OpenGL Z fighting between the object's horizontal surfaces and the triangluated surfaces of the cuts.
            double plane_shift_z = 0.002;

            if (slice_low.is_valid()) {
                const ExPolygons& obj_bottom = slice_low.get_slice(soModel);
                const ExPolygons& sup_bottom = slice_low.get_slice(soSupport);
                // calculate model bottom cap
                // calculate model bottom cap
                if (!bottom_obj_triangles.is_initialized() && !obj_bottom.empty())
                    init_model(bottom_obj_triangles, triangulate_expolygons_3d(obj_bottom, clip_min_z - plane_shift_z, !left_handed), { 1.0f, 0.37f, 0.0f, 1.0f });
                // calculate support bottom cap
                if (!bottom_sup_triangles.is_initialized() && !sup_bottom.empty())
                    init_model(bottom_sup_triangles, triangulate_expolygons_3d(sup_bottom, clip_min_z - plane_shift_z, !left_handed), { 1.0f, 0.0f, 0.37f, 1.0f });
            }

            if (slice_high.is_valid()) {
                const ExPolygons& obj_top = slice_high.get_slice(soModel);
                const ExPolygons& sup_top = slice_high.get_slice(soSupport);
                // calculate model top cap
                // calculate model top cap
                if (!top_obj_triangles.is_initialized() && !obj_top.empty())
                    init_model(top_obj_triangles, triangulate_expolygons_3d(obj_top, clip_max_z + plane_shift_z, left_handed), { 1.0f, 0.37f, 0.0f, 1.0f });
                // calculate support top cap
                if (!top_sup_triangles.is_initialized() && !sup_top.empty())
                    init_model(top_sup_triangles, triangulate_expolygons_3d(sup_top, clip_max_z + plane_shift_z, left_handed), { 1.0f, 0.0f, 0.37f, 1.0f });
            }
        }

        for (const SLAPrintObject::Instance& inst : obj->instances()) {
            const Camera& camera = get_active_camera();
            const Transform3d view_model_matrix = camera.get_view_matrix() *
                Geometry::assemble_transform(Vec3d(unscale<double>(inst.shift.x()), unscale<double>(inst.shift.y()), 0.0),
                    inst.rotation * Vec3d::UnitZ(), Vec3d::Ones(),
                    obj->is_left_handed() ? Vec3d(-1.0f, 1.0f, 1.0f) : Vec3d::Ones());

            shader->set_uniform("view_model_matrix", view_model_matrix);
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());

            bottom_obj_triangles.render_geometry();
            top_obj_triangles.render_geometry();
            bottom_sup_triangles.render_geometry();
            top_sup_triangles.render_geometry();

        }
    }

    wxGetApp().unbind_shader();
}

void GLCanvas3D::_render_selection_sidebar_hints() const
{
    m_selection.render_sidebar_hints(m_sidebar_field, m_gizmos.get_uniform_scaling());
}

void GLCanvas3D::_update_volumes_hover_state()
{
    for (GLVolume* v : m_volumes.volumes) {
        v->hover = GLVolume::HS_None;
    }

    if (m_hover_volume_idxs.empty())
        return;

    bool ctrl_pressed = wxGetKeyState(WXK_CONTROL); // additive select/deselect
    bool shift_pressed = wxGetKeyState(WXK_SHIFT);  // select by rectangle
    bool alt_pressed = wxGetKeyState(WXK_ALT);      // deselect by rectangle

    if (alt_pressed && (shift_pressed || ctrl_pressed)) {
        // illegal combinations of keys
        m_hover_volume_idxs.clear();
        return;
    }

    bool selection_modifiers_only = m_selection.is_empty() || m_selection.is_any_modifier();

    bool hover_modifiers_only = true;
    for (int i : m_hover_volume_idxs) {
        if (!m_volumes.volumes[i]->is_modifier) {
            hover_modifiers_only = false;
            break;
        }
    }

    std::set<std::pair<int, int>> hover_instances;
    for (int i : m_hover_volume_idxs) {
        const GLVolume& v = *m_volumes.volumes[i];
        hover_instances.insert(std::make_pair(v.object_idx(), v.instance_idx()));
    }

    bool hover_from_single_instance = hover_instances.size() == 1;

    if (hover_modifiers_only && !hover_from_single_instance) {
        // do not allow to select volumes from different instances
        m_hover_volume_idxs.clear();
        return;
    }

    for (int i : m_hover_volume_idxs) {
        GLVolume& volume = *m_volumes.volumes[i];
        if (volume.hover != GLVolume::HS_None)
            continue;

        bool deselect = volume.selected && ((ctrl_pressed && !shift_pressed) || alt_pressed);
        // (volume->is_modifier && !selection_modifiers_only && !is_ctrl_pressed) -> allows hovering on selected modifiers belonging to selection of type Instance
        bool select = (!volume.selected || (volume.is_modifier && !selection_modifiers_only && !ctrl_pressed)) && !alt_pressed;

        if (select || deselect) {
            bool as_volume =
                volume.is_modifier && hover_from_single_instance && !ctrl_pressed &&
                (
                (!deselect) ||
                (deselect && !m_selection.is_single_full_instance() && (volume.object_idx() == m_selection.get_object_idx()) && (volume.instance_idx() == m_selection.get_instance_idx()))
                );

            if (as_volume)
                volume.hover = deselect ? GLVolume::HS_Deselect : GLVolume::HS_Select;
            else {
                int object_idx = volume.object_idx();
                int instance_idx = volume.instance_idx();

                for (GLVolume* v : m_volumes.volumes) {
                    if (v->object_idx() == object_idx && v->instance_idx() == instance_idx)
                        v->hover = deselect ? GLVolume::HS_Deselect : GLVolume::HS_Select;
                }
            }
        }
        else if (volume.selected)
            volume.hover = GLVolume::HS_Hover;
    }
}

void GLCanvas3D::_perform_layer_editing_action(wxMouseEvent* evt)
{
    int object_idx_selected = m_layers_editing.last_object_id;
    if (object_idx_selected == -1)
        return;

    // A volume is selected. Test, whether hovering over a layer thickness bar.
    if (evt != nullptr) {
        const Rect& rect = LayersEditing::get_bar_rect_screen(*this);
        float b = rect.get_bottom();
        m_layers_editing.last_z = m_layers_editing.object_max_z() * (b - evt->GetY() - 1.0f) / (b - rect.get_top());
        m_layers_editing.last_action =
            evt->ShiftDown() ? (evt->RightIsDown() ? LAYER_HEIGHT_EDIT_ACTION_SMOOTH : LAYER_HEIGHT_EDIT_ACTION_REDUCE) :
            (evt->RightIsDown() ? LAYER_HEIGHT_EDIT_ACTION_INCREASE : LAYER_HEIGHT_EDIT_ACTION_DECREASE);
    }

    m_layers_editing.adjust_layer_height_profile();
    _refresh_if_shown_on_screen();

    // Automatic action on mouse down with the same coordinate.
    _start_timer();
}

Vec3d GLCanvas3D::_mouse_to_3d(const Camera& camera, const Point& mouse_pos, float* z, const std::string& frame_name)
{
    if (m_canvas == nullptr)
        return Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
    Matrix4d modelview = camera.get_view_matrix().matrix();
    Matrix4d projection = camera.get_projection_matrix().matrix();
    Vec4i viewport(camera.get_viewport().data());
    GLint y = viewport[3] - (GLint)mouse_pos(1);
    GLfloat mouse_z;
    if (z == nullptr) {
        const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
        p_ogl_manager->read_pixel(frame_name, (GLint)mouse_pos(0), y, 1, 1, EPixelFormat::DepthComponent, EPixelDataType::Float, (void*)&mouse_z);
    }
    else
        mouse_z = *z;
    Vec3d out;
    igl::unproject(Vec3d(mouse_pos(0), y, mouse_z), modelview, projection, viewport, out);
    return out;
}

Vec3d GLCanvas3D::_mouse_to_bed_3d(const Point& mouse_pos)
{
    return mouse_ray(mouse_pos).intersect_plane(0.0);
}

void GLCanvas3D::_start_timer()
{
    m_timer.Start(100, wxTIMER_CONTINUOUS);
}

void GLCanvas3D::_stop_timer()
{
    m_timer.Stop();
}

void GLCanvas3D::_load_print_toolpaths(const BuildVolume &build_volume)
{
    const Print *print = this->fff_print();
    if (print == nullptr)
        return;

    if (! print->is_step_done(psSkirtBrim))
        return;

    if (!print->has_skirt() && !print->has_brim())
        return;

    const std::array<float, 4> color = { 0.5f, 1.0f, 0.5f, 1.0f }; // greenish

    // number of skirt layers
    size_t total_layer_count = 0;
    for (const PrintObject* print_object : print->objects()) {
        total_layer_count = std::max(total_layer_count, print_object->total_layer_count());
    }
    size_t skirt_height = print->has_infinite_skirt() ? total_layer_count : std::min<size_t>(print->config().skirt_height.value, total_layer_count);
    if (skirt_height == 0 && print->has_brim())
        skirt_height = 1;

    // Get first skirt_height layers.
    //FIXME This code is fishy. It may not work for multiple objects with different layering due to variable layer height feature.
    // This is not critical as this is just an initial preview.
    const PrintObject* highest_object = *std::max_element(print->objects().begin(), print->objects().end(), [](auto l, auto r){ return l->layers().size() < r->layers().size(); });
    std::vector<float> print_zs;
    print_zs.reserve(skirt_height * 2);
    for (size_t i = 0; i < std::min(skirt_height, highest_object->layers().size()); ++ i)
        print_zs.emplace_back(float(highest_object->layers()[i]->print_z));
    // Only add skirt for the raft layers.
    for (size_t i = 0; i < std::min(skirt_height, std::min(highest_object->slicing_parameters().raft_layers(), highest_object->support_layers().size())); ++ i)
        print_zs.emplace_back(float(highest_object->support_layers()[i]->print_z));
    sort_remove_duplicates(print_zs);
    skirt_height = std::min(skirt_height, print_zs.size());
    print_zs.erase(print_zs.begin() + skirt_height, print_zs.end());

    GLVolume *volume = m_volumes.new_toolpath_volume(color, VERTEX_BUFFER_RESERVE_SIZE);
    for (size_t i = 0; i < skirt_height; ++ i) {
        volume->print_zs.emplace_back(print_zs[i]);
        volume->offsets.emplace_back(volume->indexed_vertex_array->quad_indices.size());
        volume->offsets.emplace_back(volume->indexed_vertex_array->triangle_indices.size());
        //BBS: usage of m_brim are deleted
        _3DScene::extrusionentity_to_verts(print->skirt(), print_zs[i], Point(0, 0), *volume);
        // Ensure that no volume grows over the limits. If the volume is too large, allocate a new one.
        if (volume->indexed_vertex_array->vertices_and_normals_interleaved.size() > MAX_VERTEX_BUFFER_SIZE) {
        	GLVolume &vol = *volume;
            volume = m_volumes.new_toolpath_volume(vol.color);
            reserve_new_volume_finalize_old_volume(*volume, vol, m_initialized);
        }
    }
    volume->is_outside = ! build_volume.all_paths_inside_vertices_and_normals_interleaved(volume->indexed_vertex_array->vertices_and_normals_interleaved, volume->indexed_vertex_array->bounding_box());
    volume->indexed_vertex_array->finalize_geometry(m_initialized);
}

void GLCanvas3D::_load_print_object_toolpaths(const PrintObject& print_object, const BuildVolume& build_volume, const std::vector<std::string>& str_tool_colors, const std::vector<CustomGCode::Item>& color_print_values)
{
    std::vector<std::array<float, 4>> tool_colors = _parse_colors(str_tool_colors);

    struct Ctxt
    {
        const PrintInstances        *shifted_copies;
        std::vector<const Layer*>    layers;
        bool                         has_perimeters;
        bool                         has_infill;
        bool                         has_support;
        const std::vector<std::array<float, 4>>* tool_colors;
        bool                         is_single_material_print;
        int                          filaments_cnt;
        const std::vector<CustomGCode::Item>*   color_print_values;

        static const std::array<float, 4>& color_perimeters() { static std::array<float, 4> color = { 1.0f, 1.0f, 0.0f, 1.f }; return color; } // yellow
        static const std::array<float, 4>& color_infill() { static std::array<float, 4> color = { 1.0f, 0.5f, 0.5f, 1.f }; return color; } // redish
        static const std::array<float, 4>& color_support() { static std::array<float, 4> color = { 0.5f, 1.0f, 0.5f, 1.f }; return color; } // greenish
        static const std::array<float, 4>& color_pause_or_custom_code() { static std::array<float, 4> color = { 0.5f, 0.5f, 0.5f, 1.f }; return color; } // gray

        // For cloring by a tool, return a parsed color.
        bool                         color_by_tool() const { return tool_colors != nullptr; }
        size_t                       number_tools() const { return color_by_tool() ? tool_colors->size() : 0; }
        const std::array<float, 4>&  color_tool(size_t tool) const { return (*tool_colors)[tool]; }

        // For coloring by a color_print(M600), return a parsed color.
        bool                         color_by_color_print() const { return color_print_values!=nullptr; }
        const size_t                 color_print_color_idx_by_layer_idx(const size_t layer_idx) const {
            const CustomGCode::Item value{layers[layer_idx]->print_z + EPSILON, CustomGCode::Custom, 0, ""};
            auto it = std::lower_bound(color_print_values->begin(), color_print_values->end(), value);
            return (it - color_print_values->begin()) % number_tools();
        }

        const size_t                 color_print_color_idx_by_layer_idx_and_extruder(const size_t layer_idx, const int extruder) const
        {
            const coordf_t print_z = layers[layer_idx]->print_z;

            auto it = std::find_if(color_print_values->begin(), color_print_values->end(),
                [print_z](const CustomGCode::Item& code)
                { return fabs(code.print_z - print_z) < EPSILON; });
            if (it != color_print_values->end()) {
                CustomGCode::Type type = it->type;
                // pause print or custom Gcode
                if (type == CustomGCode::PausePrint ||
                    (type != CustomGCode::ColorChange && type != CustomGCode::ToolChange))
                    return number_tools()-1; // last color item is a gray color for pause print or custom G-code

                // change tool (extruder)
                if (type == CustomGCode::ToolChange)
                    return get_color_idx_for_tool_change(it, extruder);
                // change color for current extruder
                if (type == CustomGCode::ColorChange) {
                    int color_idx = get_color_idx_for_color_change(it, extruder);
                    if (color_idx >= 0)
                        return color_idx;
                }
            }

            const CustomGCode::Item value{print_z + EPSILON, CustomGCode::Custom, 0, ""};
            it = std::lower_bound(color_print_values->begin(), color_print_values->end(), value);
            while (it != color_print_values->begin()) {
                --it;
                // change color for current extruder
                if (it->type == CustomGCode::ColorChange) {
                    int color_idx = get_color_idx_for_color_change(it, extruder);
                    if (color_idx >= 0)
                        return color_idx;
                }
                // change tool (extruder)
                if (it->type == CustomGCode::ToolChange)
                    return get_color_idx_for_tool_change(it, extruder);
            }

            return std::min<int>(filaments_cnt - 1, std::max<int>(extruder - 1, 0));;
        }

    private:
        int get_m600_color_idx(std::vector<CustomGCode::Item>::const_iterator it) const
        {
            int shift = 0;
            while (it != color_print_values->begin()) {
                --it;
                if (it->type == CustomGCode::ColorChange)
                    shift++;
            }
            return filaments_cnt + shift;
        }

        int get_color_idx_for_tool_change(std::vector<CustomGCode::Item>::const_iterator it, const int extruder) const
        {
            const int current_extruder = it->extruder == 0 ? extruder : it->extruder;
            if (number_tools() == size_t(filaments_cnt + 1)) // there is no one "M600"
                return std::min<int>(filaments_cnt - 1, std::max<int>(current_extruder - 1, 0));

            auto it_n = it;
            while (it_n != color_print_values->begin()) {
                --it_n;
                if (it_n->type == CustomGCode::ColorChange && it_n->extruder == current_extruder)
                    return get_m600_color_idx(it_n);
            }

            return std::min<int>(filaments_cnt - 1, std::max<int>(current_extruder - 1, 0));
        }

        int get_color_idx_for_color_change(std::vector<CustomGCode::Item>::const_iterator it, const int extruder) const
        {
            if (filaments_cnt == 1)
                return get_m600_color_idx(it);

            auto it_n = it;
            bool is_tool_change = false;
            while (it_n != color_print_values->begin()) {
                --it_n;
                if (it_n->type == CustomGCode::ToolChange) {
                    is_tool_change = true;
                    if (it_n->extruder == it->extruder || (it_n->extruder == 0 && it->extruder == extruder))
                        return get_m600_color_idx(it);
                    break;
                }
            }
            if (!is_tool_change && it->extruder == extruder)
                return get_m600_color_idx(it);

            return -1;
        }

    } ctxt;

    ctxt.has_perimeters = print_object.is_step_done(posPerimeters);
    ctxt.has_infill = print_object.is_step_done(posInfill);
    ctxt.has_support = print_object.is_step_done(posSupportMaterial);
    ctxt.tool_colors = tool_colors.empty() ? nullptr : &tool_colors;
    ctxt.color_print_values = color_print_values.empty() ? nullptr : &color_print_values;
    ctxt.is_single_material_print = this->fff_print()->extruders().size()==1;
    ctxt.filaments_cnt = wxGetApp().filaments_cnt();

    ctxt.shifted_copies = &print_object.instances();

    // order layers by print_z
    {
        size_t nlayers = 0;
        if (ctxt.has_perimeters || ctxt.has_infill)
            nlayers = print_object.layers().size();
        if (ctxt.has_support)
            nlayers += print_object.support_layers().size();
        ctxt.layers.reserve(nlayers);
    }
    if (ctxt.has_perimeters || ctxt.has_infill)
        for (const Layer *layer : print_object.layers())
            ctxt.layers.emplace_back(layer);
    if (ctxt.has_support)
        for (const Layer *layer : print_object.support_layers())
            ctxt.layers.emplace_back(layer);
    std::sort(ctxt.layers.begin(), ctxt.layers.end(), [](const Layer *l1, const Layer *l2) { return l1->print_z < l2->print_z; });

    // Maximum size of an allocation block: 32MB / sizeof(float)
    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - start" << m_volumes.log_memory_info() << log_memory_info();

    const bool is_selected_separate_extruder = m_selected_extruder > 0 && ctxt.color_by_color_print();

    //FIXME Improve the heuristics for a grain size.
    size_t          grain_size = std::max(ctxt.layers.size() / 16, size_t(1));
    tbb::spin_mutex new_volume_mutex;
    auto            new_volume = [this, &new_volume_mutex](const std::array<float, 4>& color) {
        // Allocate the volume before locking.
		GLVolume *volume = new GLVolume(color);
		volume->is_extrusion_path = true;
    	tbb::spin_mutex::scoped_lock lock;
    	// Lock by ROII, so if the emplace_back() fails, the lock will be released.
        lock.acquire(new_volume_mutex);
        m_volumes.volumes.emplace_back(volume);
        lock.release();
        return volume;
    };
    const size_t    volumes_cnt_initial = m_volumes.volumes.size();
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, ctxt.layers.size(), grain_size),
        [&ctxt, &new_volume, is_selected_separate_extruder, this](const tbb::blocked_range<size_t>& range) {
        GLVolumePtrs 		vols;
        auto                volume = [&ctxt, &vols](size_t layer_idx, int extruder, int feature) -> GLVolume& {
            return *vols[ctxt.color_by_color_print()?
                ctxt.color_print_color_idx_by_layer_idx_and_extruder(layer_idx, extruder) :
				ctxt.color_by_tool() ?
					std::min<int>(ctxt.number_tools() - 1, std::max<int>(extruder - 1, 0)) :
					feature
				];
        };
        if (ctxt.color_by_color_print() || ctxt.color_by_tool()) {
            for (size_t i = 0; i < ctxt.number_tools(); ++i)
                vols.emplace_back(new_volume(ctxt.color_tool(i)));
        }
        else
            vols = { new_volume(ctxt.color_perimeters()), new_volume(ctxt.color_infill()), new_volume(ctxt.color_support()) };
        for (GLVolume *vol : vols)
			// Reserving number of vertices (3x position + 3x color)
        	vol->indexed_vertex_array->reserve(VERTEX_BUFFER_RESERVE_SIZE / 6);
        for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
            const Layer *layer = ctxt.layers[idx_layer];

            if (is_selected_separate_extruder)
            {
                bool at_least_one_has_correct_extruder = false;
                for (const LayerRegion* layerm : layer->regions())
                {
                    if (layerm->slices.surfaces.empty())
                        continue;
                    const PrintRegionConfig& cfg = layerm->region().config();
                    if (cfg.wall_filament.value    == m_selected_extruder ||
                        cfg.sparse_infill_filament.value       == m_selected_extruder ||
                        cfg.solid_infill_filament.value == m_selected_extruder ) {
                        at_least_one_has_correct_extruder = true;
                        break;
                    }
                }
                if (!at_least_one_has_correct_extruder)
                    continue;
            }

            for (GLVolume *vol : vols)
                if (vol->print_zs.empty() || vol->print_zs.back() != layer->print_z) {
                    vol->print_zs.emplace_back(layer->print_z);
                    vol->offsets.emplace_back(vol->indexed_vertex_array->quad_indices.size());
                    vol->offsets.emplace_back(vol->indexed_vertex_array->triangle_indices.size());
                }
            for (const PrintInstance &instance : *ctxt.shifted_copies) {
                const Point &copy = instance.shift;
                for (const LayerRegion *layerm : layer->regions()) {
                    if (is_selected_separate_extruder)
                    {
                        const PrintRegionConfig& cfg = layerm->region().config();
                        if (cfg.wall_filament.value    != m_selected_extruder ||
                            cfg.sparse_infill_filament.value       != m_selected_extruder ||
                            cfg.solid_infill_filament.value != m_selected_extruder)
                            continue;
                    }
                    if (ctxt.has_perimeters)
                        _3DScene::extrusionentity_to_verts(layerm->perimeters, float(layer->print_z), copy,
                        	volume(idx_layer, layerm->region().config().wall_filament.value, 0));
                    if (ctxt.has_infill) {
                        for (const ExtrusionEntity *ee : layerm->fills.entities) {
                            // fill represents infill extrusions of a single island.
                            const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                            if (! fill->entities.empty())
                                _3DScene::extrusionentity_to_verts(*fill, float(layer->print_z), copy,
	                                volume(idx_layer,
		                                is_solid_infill(fill->entities.front()->role()) ?
			                                layerm->region().config().solid_infill_filament :
			                                layerm->region().config().sparse_infill_filament,
		                                1));
                        }
                    }
                }
                if (ctxt.has_support) {
                    const SupportLayer *support_layer = dynamic_cast<const SupportLayer*>(layer);
                    if (support_layer) {
                        for (const ExtrusionEntity *extrusion_entity : support_layer->support_fills.entities)
                            _3DScene::extrusionentity_to_verts(extrusion_entity, float(layer->print_z), copy,
	                            volume(idx_layer,
		                            (extrusion_entity->role() == erSupportMaterial ||
                                     extrusion_entity->role() == erSupportTransition) ?
			                            support_layer->object()->config().support_filament :
			                            support_layer->object()->config().support_interface_filament,
		                            2));
                    }
                }
            }
            // Ensure that no volume grows over the limits. If the volume is too large, allocate a new one.
	        for (size_t i = 0; i < vols.size(); ++i) {
	            GLVolume &vol = *vols[i];
	            if (vol.indexed_vertex_array->vertices_and_normals_interleaved.size() > MAX_VERTEX_BUFFER_SIZE) {
	                vols[i] = new_volume(vol.color);
	                reserve_new_volume_finalize_old_volume(*vols[i], vol, false);
	            }
	        }
        }
        for (GLVolume *vol : vols)
        	// Ideally one would call vol->indexed_vertex_array->finalize() here to move the buffers to the OpenGL driver,
        	// but this code runs in parallel and the OpenGL driver is not thread safe.
            vol->indexed_vertex_array->shrink_to_fit();
    });

    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - finalizing results" << m_volumes.log_memory_info() << log_memory_info();
    // Remove empty volumes from the newly added volumes.
    m_volumes.volumes.erase(
        std::remove_if(m_volumes.volumes.begin() + volumes_cnt_initial, m_volumes.volumes.end(),
        [](const GLVolume *volume) { return volume->empty(); }),
        m_volumes.volumes.end());
    for (size_t i = volumes_cnt_initial; i < m_volumes.volumes.size(); ++i) {
        GLVolume* v = m_volumes.volumes[i];
        v->is_outside = ! build_volume.all_paths_inside_vertices_and_normals_interleaved(v->indexed_vertex_array->vertices_and_normals_interleaved, v->indexed_vertex_array->bounding_box());
        v->indexed_vertex_array->finalize_geometry(m_initialized);
    }

    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - end" << m_volumes.log_memory_info() << log_memory_info();
}

void GLCanvas3D::_load_wipe_tower_toolpaths(const BuildVolume& build_volume, const std::vector<std::string>& str_tool_colors)
{
    const Print *print = this->fff_print();
    if (print == nullptr || print->wipe_tower_data().tool_changes.empty())
        return;

    if (!print->is_step_done(psWipeTower))
        return;

    std::vector<std::array<float, 4>> tool_colors = _parse_colors(str_tool_colors);

    struct Ctxt
    {
        const Print                 *print;
        const std::vector<std::array<float, 4>>* tool_colors;
        Vec2f                        wipe_tower_pos;
        float                        wipe_tower_angle;

        static const std::array<float, 4>& color_support() { static std::array<float, 4> color = { 0.5f, 1.0f, 0.5f, 1.f }; return color; } // greenish

        // For cloring by a tool, return a parsed color.
        bool                         color_by_tool() const { return tool_colors != nullptr; }
        size_t                       number_tools() const { return this->color_by_tool() ? tool_colors->size() : 0; }
        const std::array<float, 4>&  color_tool(size_t tool) const { return (*tool_colors)[tool]; }
        int                          volume_idx(int tool, int feature) const {
            return this->color_by_tool() ? std::min<int>(this->number_tools() - 1, std::max<int>(tool, 0)) : feature;
        }

        const std::vector<WipeTower::ToolChangeResult>& tool_change(size_t idx) {
            const auto &tool_changes = print->wipe_tower_data().tool_changes;
            return priming.empty() ?
                ((idx == tool_changes.size()) ? final : tool_changes[idx]) :
                ((idx == 0) ? priming : (idx == tool_changes.size() + 1) ? final : tool_changes[idx - 1]);
        }
        std::vector<WipeTower::ToolChangeResult> priming;
        std::vector<WipeTower::ToolChangeResult> final;
    } ctxt;

    ctxt.print = print;
    ctxt.tool_colors = tool_colors.empty() ? nullptr : &tool_colors;
    //BBS: has no single_extruder_multi_material_priming
    //if (print->wipe_tower_data().priming && print->config().single_extruder_multi_material_priming)
    if (print->wipe_tower_data().priming)
        for (int i=0; i<(int)print->wipe_tower_data().priming.get()->size(); ++i)
            ctxt.priming.emplace_back(print->wipe_tower_data().priming.get()->at(i));
    if (print->wipe_tower_data().final_purge)
        ctxt.final.emplace_back(*print->wipe_tower_data().final_purge.get());

    ctxt.wipe_tower_angle = ctxt.print->config().wipe_tower_rotation_angle.value/180.f * PI;

    // BBS: add partplate logic
    int plate_idx = print->get_plate_index();
    Vec3d plate_origin = print->get_plate_origin();
    double wipe_tower_x = ctxt.print->config().wipe_tower_x.get_at(plate_idx) + plate_origin(0);
    double wipe_tower_y = ctxt.print->config().wipe_tower_y.get_at(plate_idx) + plate_origin(1);
    ctxt.wipe_tower_pos = Vec2f(wipe_tower_x, wipe_tower_y);

    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - start" << m_volumes.log_memory_info() << log_memory_info();

    //FIXME Improve the heuristics for a grain size.
    size_t          n_items = print->wipe_tower_data().tool_changes.size() + (ctxt.priming.empty() ? 0 : 1);
    size_t          grain_size = std::max(n_items / 128, size_t(1));
    tbb::spin_mutex new_volume_mutex;
    auto            new_volume = [this, &new_volume_mutex](const std::array<float, 4>& color) {
        auto *volume = new GLVolume(color);
		volume->is_extrusion_path = true;
        tbb::spin_mutex::scoped_lock lock;
        lock.acquire(new_volume_mutex);
        m_volumes.volumes.emplace_back(volume);
        lock.release();
        return volume;
    };
    const size_t   volumes_cnt_initial = m_volumes.volumes.size();
    std::vector<GLVolumeCollection> volumes_per_thread(n_items);
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, n_items, grain_size),
        [&ctxt, &new_volume](const tbb::blocked_range<size_t>& range) {
        // Bounding box of this slab of a wipe tower.
        GLVolumePtrs vols;
        if (ctxt.color_by_tool()) {
            for (size_t i = 0; i < ctxt.number_tools(); ++i)
                vols.emplace_back(new_volume(ctxt.color_tool(i)));
        }
        else
            vols = { new_volume(ctxt.color_support()) };
        for (GLVolume *volume : vols)
			// Reserving number of vertices (3x position + 3x color)
            volume->indexed_vertex_array->reserve(VERTEX_BUFFER_RESERVE_SIZE / 6);
        for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++idx_layer) {
            const std::vector<WipeTower::ToolChangeResult> &layer = ctxt.tool_change(idx_layer);
            for (size_t i = 0; i < vols.size(); ++i) {
                GLVolume &vol = *vols[i];
                if (vol.print_zs.empty() || vol.print_zs.back() != layer.front().print_z) {
                    vol.print_zs.emplace_back(layer.front().print_z);
                    vol.offsets.emplace_back(vol.indexed_vertex_array->quad_indices.size());
                    vol.offsets.emplace_back(vol.indexed_vertex_array->triangle_indices.size());
                }
            }
            for (const WipeTower::ToolChangeResult &extrusions : layer) {
                for (size_t i = 1; i < extrusions.extrusions.size();) {
                    const WipeTower::Extrusion &e = extrusions.extrusions[i];
                    if (e.width == 0.) {
                        ++i;
                        continue;
                    }
                    size_t j = i + 1;
                    if (ctxt.color_by_tool())
                        for (; j < extrusions.extrusions.size() && extrusions.extrusions[j].tool == e.tool && extrusions.extrusions[j].width > 0.f; ++j);
                    else
                        for (; j < extrusions.extrusions.size() && extrusions.extrusions[j].width > 0.f; ++j);
                    size_t              n_lines = j - i;
                    Lines               lines;
                    std::vector<double> widths;
                    std::vector<double> heights;
                    lines.reserve(n_lines);
                    widths.reserve(n_lines);
                    heights.assign(n_lines, extrusions.layer_height);
                    WipeTower::Extrusion e_prev = extrusions.extrusions[i-1];

                    if (!extrusions.priming) { // wipe tower extrusions describe the wipe tower at the origin with no rotation
                        e_prev.pos = Eigen::Rotation2Df(ctxt.wipe_tower_angle) * e_prev.pos;
                        e_prev.pos += ctxt.wipe_tower_pos;
                    }

                    for (; i < j; ++i) {
                        WipeTower::Extrusion e = extrusions.extrusions[i];
                        assert(e.width > 0.f);
                        if (!extrusions.priming) {
                            e.pos = Eigen::Rotation2Df(ctxt.wipe_tower_angle) * e.pos;
                            e.pos += ctxt.wipe_tower_pos;
                        }

                        lines.emplace_back(Point::new_scale(e_prev.pos.x(), e_prev.pos.y()), Point::new_scale(e.pos.x(), e.pos.y()));
                        widths.emplace_back(e.width);

                        e_prev = e;
                    }
                    _3DScene::thick_lines_to_verts(lines, widths, heights, lines.front().a == lines.back().b, extrusions.print_z,
                        *vols[ctxt.volume_idx(e.tool, 0)]);
                }
            }
        }
        for (size_t i = 0; i < vols.size(); ++i) {
            GLVolume &vol = *vols[i];
            if (vol.indexed_vertex_array->vertices_and_normals_interleaved.size() > MAX_VERTEX_BUFFER_SIZE) {
                vols[i] = new_volume(vol.color);
                reserve_new_volume_finalize_old_volume(*vols[i], vol, false);
            }
        }
        for (GLVolume *vol : vols)
            vol->indexed_vertex_array->shrink_to_fit();
    });

    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - finalizing results" << m_volumes.log_memory_info() << log_memory_info();
    // Remove empty volumes from the newly added volumes.
    m_volumes.volumes.erase(
        std::remove_if(m_volumes.volumes.begin() + volumes_cnt_initial, m_volumes.volumes.end(),
        [](const GLVolume *volume) { return volume->empty(); }),
        m_volumes.volumes.end());
    for (size_t i = volumes_cnt_initial; i < m_volumes.volumes.size(); ++i) {
        GLVolume* v = m_volumes.volumes[i];
        v->is_outside = ! build_volume.all_paths_inside_vertices_and_normals_interleaved(v->indexed_vertex_array->vertices_and_normals_interleaved, v->indexed_vertex_array->bounding_box());
        v->indexed_vertex_array->finalize_geometry(m_initialized);
    }

    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - end" << m_volumes.log_memory_info() << log_memory_info();
}

// While it looks like we can call
// this->reload_scene(true, true)
// the two functions are quite different:
// 1) This function only loads objects, for which the step slaposSliceSupports already finished. Therefore objects outside of the print bed never load.
// 2) This function loads object mesh with the relative scaling correction (the "relative_correction" parameter) was applied,
// 	  therefore the mesh may be slightly larger or smaller than the mesh shown in the 3D scene.
void GLCanvas3D::_load_sla_shells()
{
    const SLAPrint* print = this->sla_print();
    if (print->objects().empty())
        // nothing to render, return
        return;

    auto add_volume = [this](const SLAPrintObject &object, int volume_id, const SLAPrintObject::Instance& instance,
        const TriangleMesh& mesh, const std::array<float, 4>& color, bool outside_printer_detection_enabled) {
        m_volumes.volumes.emplace_back(new GLVolume(color));
        GLVolume& v = *m_volumes.volumes.back();
#if ENABLE_SMOOTH_NORMALS
        v.indexed_vertex_array->load_mesh(mesh, true);
#else
        v.indexed_vertex_array->load_mesh(mesh);
#endif // ENABLE_SMOOTH_NORMALS
        v.indexed_vertex_array->finalize_geometry(m_initialized);
        v.shader_outside_printer_detection_enabled = outside_printer_detection_enabled;
        v.composite_id.volume_id = volume_id;
        v.set_instance_offset(unscale(instance.shift.x(), instance.shift.y(), 0.0));
        v.set_instance_rotation({ 0.0, 0.0, (double)instance.rotation });
        v.set_instance_mirror(X, object.is_left_handed() ? -1. : 1.);
        v.set_convex_hull(mesh.convex_hull_3d());
    };

    // adds objects' volumes
    for (const SLAPrintObject* obj : print->objects())
        if (obj->is_step_done(slaposSliceSupports)) {
            unsigned int initial_volumes_count = (unsigned int)m_volumes.volumes.size();
            for (const SLAPrintObject::Instance& instance : obj->instances()) {
                add_volume(*obj, 0, instance, obj->get_mesh_to_print(), GLVolume::MODEL_COLOR[0], true);
                // Set the extruder_id and volume_id to achieve the same color as in the 3D scene when
                // through the update_volumes_colors_by_extruder() call.
                m_volumes.volumes.back()->extruder_id = obj->model_object()->volumes.front()->extruder_id();
                if (obj->is_step_done(slaposSupportTree) && obj->has_mesh(slaposSupportTree))
                    add_volume(*obj, -int(slaposSupportTree), instance, obj->support_mesh(), GLVolume::SLA_SUPPORT_COLOR, true);
                if (obj->is_step_done(slaposPad) && obj->has_mesh(slaposPad))
                    add_volume(*obj, -int(slaposPad), instance, obj->pad_mesh(), GLVolume::SLA_PAD_COLOR, false);
            }
            double shift_z = obj->get_current_elevation();
            for (unsigned int i = initial_volumes_count; i < m_volumes.volumes.size(); ++ i) {
                // apply shift z
                m_volumes.volumes[i]->set_sla_shift_z(shift_z);
            }
        }

    update_volumes_colors_by_extruder();
}

void GLCanvas3D::_update_sla_shells_outside_state()
{
    check_volumes_outside_state();
}

void GLCanvas3D::_set_warning_notification_if_needed(EWarning warning)
{
    _set_current(true);
    bool show = false;
    if (!m_volumes.empty()) {
        show = _is_any_volume_outside();
        show &= m_gcode_viewer.has_data() && m_gcode_viewer.is_contained_in_bed() && m_gcode_viewer.m_conflict_result.has_value();
    } else {
        if (wxGetApp().is_editor()) {
            if (current_printer_technology() != ptSLA) {
                unsigned int max_z_layer = m_gcode_viewer.get_layers_z_range().back();
                if (warning == EWarning::ToolHeightOutside) // check if max z_layer height exceed max print height
                    show = m_gcode_viewer.has_data() && (m_gcode_viewer.get_layers_zs()[max_z_layer] - m_gcode_viewer.get_max_print_height() >= 1e-6);
                else if (warning == EWarning::ToolpathOutside) { // check if max x,y coords exceed bed area
                    show = m_gcode_viewer.has_data() && !m_gcode_viewer.is_contained_in_bed() &&
                           (m_gcode_viewer.get_max_print_height() - m_gcode_viewer.get_layers_zs()[max_z_layer] >= 1e-6);
                } else if (warning == EWarning::GCodeConflict)
                    show = m_gcode_viewer.has_data() && m_gcode_viewer.is_contained_in_bed() && m_gcode_viewer.m_conflict_result.has_value();
                else if (warning == EWarning::MultiExtruderPrintableError)
                    show = m_gcode_viewer.has_data() && (m_gcode_viewer.m_gcode_check_result.error_code & 1);
                else if (warning == EWarning::MultiExtruderHeightOutside)
                    show = m_gcode_viewer.has_data() && (m_gcode_viewer.m_gcode_check_result.error_code & (1 << 1));
                else if (warning == EWarning::FilamentUnPrintableOnFirstLayer)
                    show = m_gcode_viewer.has_data() && m_gcode_viewer.filament_printable_reuslt.has_value();
            }
        }
    }

    _set_warning_notification(warning, show);
}

std::vector<std::array<float, 4>> GLCanvas3D::_parse_colors(const std::vector<std::string>& colors)
{
    static const float INV_255 = 1.0f / 255.0f;

    std::vector<std::array<float, 4>> output(colors.size(), { 1.0f, 1.0f, 1.0f, 1.0f });
    for (size_t i = 0; i < colors.size(); ++i) {
        const std::string& color = colors[i];
        const char* c = color.data() + 1;
        if (color.size() == 7 && color.front() == '#') {
            for (size_t j = 0; j < 3; ++j) {
                int digit1 = hex_digit_to_int(*c++);
                int digit2 = hex_digit_to_int(*c++);
                if (digit1 == -1 || digit2 == -1)
                    break;

                output[i][j] = float(digit1 * 16 + digit2) * INV_255;
            }
        }
    }
    return output;
}

void GLCanvas3D::_push_render_stage(ERenderPipelineStage stage)
{
    m_render_pipeline_stage_stack.push(stage);
}

void GLCanvas3D::_pop_render_stage()
{
    if (m_render_pipeline_stage_stack.size() > 1)
    {
        m_render_pipeline_stage_stack.pop();
    }
}

ERenderPipelineStage GLCanvas3D::_get_current_render_stage() const
{
    return m_render_pipeline_stage_stack.top();
}

void GLCanvas3D::_render_silhouette_effect()
{
    RenderPipelineStageModifier render_pipeline_stage_modifier(*this, ERenderPipelineStage::Silhouette);

    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    if (!p_ogl_manager) {
        return;
    }

    auto& ogl_manager = *p_ogl_manager;
    float t_viewport_scale = 1.0f;
    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;
    ogl_manager.get_viewport_size(viewport_width, viewport_height);
    viewport_width = viewport_width * t_viewport_scale;
    viewport_height = viewport_height * t_viewport_scale;
    {
        OpenGLManager::FrameBufferModifier frame_buffer_modifier(ogl_manager, "silhouette", EMSAAType::X4);
        frame_buffer_modifier.set_width(viewport_width)
            .set_height(viewport_height);
    }
    // BBS: render silhouette
    glViewport(0, 0, viewport_width, viewport_height);
    glsafe(::glClearColor(0.0f, 0.0f, 0.0f, 0.0f));
    glsafe(::glClearDepth(1.0f));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_BLEND));
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    const auto& p_silhouette_shader = wxGetApp().get_shader("silhouette");
    if (!p_silhouette_shader)
    {
        BOOST_LOG_TRIVIAL(error) << "Invalid shader: silhouette. Failed to render highlight effect.";
        return;
    }

    wxGetApp().bind_shader(p_silhouette_shader);

    const auto& picking_color = wxGetApp().get_picking_color();
    p_silhouette_shader->set_uniform("u_base_color", picking_color);

    const Camera& camera = get_active_camera();
    const Transform3d& view_matrix = camera.get_view_matrix();
    const Transform3d& projection_matrix = camera.get_projection_matrix();
    const Matrix4d view_proj = projection_matrix.matrix() * view_matrix.matrix();
    p_silhouette_shader->set_uniform("u_view_projection_matrix", view_proj);
    _render_objects(m_volumes,GLVolumeCollection::ERenderType::Opaque, false);
    _render_objects(m_volumes, GLVolumeCollection::ERenderType::Transparent, false);
    _render_objects(m_paint_outline_volumes, GLVolumeCollection::ERenderType::Opaque, false,true);
    _render_objects(m_paint_outline_volumes, GLVolumeCollection::ERenderType::Transparent, false, true);
    wxGetApp().unbind_shader();

    // BBS: end render silhouette

    std::string input_fb_name = "silhouette";
    const auto& p_frame_buffer = ogl_manager.get_frame_buffer(input_fb_name);
    if (!p_frame_buffer)
    {
        BOOST_LOG_TRIVIAL(error) << "Invalid framebuffer. Failed to render highlight effect.";
        return;
    }

    int stage = 0;

    const std::array<float, 2> viewport_size{ static_cast<float>(viewport_width), static_cast<float>(viewport_height) };

    {
        glViewport(0, 0, viewport_width, viewport_height);
        glsafe(::glDisable(GL_DEPTH_TEST));
        _init_fullscreen_mesh();
        // BBS: gaussian blur
        {
            OpenGLManager::FrameBufferModifier frame_buffer_gaussian_blur33(ogl_manager, "silhouette_gaussian_blur33", EMSAAType::Disabled);
            frame_buffer_gaussian_blur33.set_width(viewport_width)
                .set_height(viewport_height);
        }
        const auto& p_gaussian_blur33_shader = wxGetApp().get_shader("gaussian_blur33");
        if (!p_gaussian_blur33_shader)
        {
            BOOST_LOG_TRIVIAL(error) << "Invalid gaussian_blur33 shader. Failed to render highlight effect.";
            return;
        }
        wxGetApp().bind_shader(p_gaussian_blur33_shader);

        p_gaussian_blur33_shader->set_uniform("u_viewport_size", viewport_size);

        Matrix3f gaussian_blur33_convolution_matrix;
        // https://lodev.org/cgtutor/filtering.html#Gaussian_Blur_
        gaussian_blur33_convolution_matrix.data()[3 * 0 + 0] = 0.077847;
        gaussian_blur33_convolution_matrix.data()[3 * 0 + 1] = 0.123317;
        gaussian_blur33_convolution_matrix.data()[3 * 0 + 2] = 0.077847;
        gaussian_blur33_convolution_matrix.data()[3 * 1 + 0] = 0.123317;
        gaussian_blur33_convolution_matrix.data()[3 * 1 + 1] = 0.195346;
        gaussian_blur33_convolution_matrix.data()[3 * 1 + 2] = 0.123317;
        gaussian_blur33_convolution_matrix.data()[3 * 2 + 0] = 0.077847;
        gaussian_blur33_convolution_matrix.data()[3 * 2 + 1] = 0.123317;
        gaussian_blur33_convolution_matrix.data()[3 * 2 + 2] = 0.077847;
        p_gaussian_blur33_shader->set_uniform("u_convolution_matrix", gaussian_blur33_convolution_matrix);

        const auto color_texture_id = p_frame_buffer->get_color_texture();
        if (!p_frame_buffer->is_texture_valid(color_texture_id))
        {
            BOOST_LOG_TRIVIAL(error) << "Invalid silhouette texture. Failed to render highlight effect.";
            return;
        }
        int stage = 0;
        p_gaussian_blur33_shader->set_uniform("u_sampler", stage);
        glsafe(::glActiveTexture(GL_TEXTURE0 + stage));
        glsafe(::glBindTexture(GL_TEXTURE_2D, color_texture_id));

        s_full_screen_mesh.render_geometry();

        wxGetApp().unbind_shader();
        glsafe(::glEnable(GL_DEPTH_TEST));
        // BBS: end gaussian blur
    }
    ogl_manager.get_viewport_size(viewport_width, viewport_height);
    glViewport(0, 0, viewport_width, viewport_height);
}

void GLCanvas3D::_composite_silhouette_effect()
{
    // BBS: composite silhouette

    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    if (!p_ogl_manager) {
        return;
    }
    const auto& p_frame_buffer = p_ogl_manager->get_frame_buffer("silhouette_gaussian_blur33");
    if (!p_frame_buffer)
    {
        BOOST_LOG_TRIVIAL(error) << "Invalid framebuffer. Failed to render highlight effect.";
        return;
    }

    const auto color_texture_id = p_frame_buffer->get_color_texture();
    if (!p_frame_buffer->is_texture_valid(color_texture_id))
    {
        BOOST_LOG_TRIVIAL(error) << "Invalid silhouette texture. Failed to render highlight effect.";
        return;
    }

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    const auto& p_silhouette_composite_shader = wxGetApp().get_shader("silhouette_composite");
    if (!p_silhouette_composite_shader)
    {
        BOOST_LOG_TRIVIAL(error) << "Invalid silhouette shader. Failed to render highlight effect.";
        return;
    }
    wxGetApp().bind_shader(p_silhouette_composite_shader);

    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;
    p_ogl_manager->get_viewport_size(viewport_width, viewport_height);

    const float alpha = 0.4f;
    Matrix3f convolution_matrix;
    convolution_matrix.data()[3 * 0 + 0] = -alpha;
    convolution_matrix.data()[3 * 0 + 1] = -alpha;
    convolution_matrix.data()[3 * 0 + 2] = -alpha;

    convolution_matrix.data()[3 * 1 + 0] = -alpha;
    convolution_matrix.data()[3 * 1 + 1] = alpha * 9.0f;
    convolution_matrix.data()[3 * 1 + 2] = -alpha;

    convolution_matrix.data()[3 * 2 + 0] = -alpha;
    convolution_matrix.data()[3 * 2 + 1] = -alpha;
    convolution_matrix.data()[3 * 2 + 2] = -alpha;
    p_silhouette_composite_shader->set_uniform("u_convolution_matrix", convolution_matrix);

    const std::array<float, 3> viewport_size_alpha{ static_cast<float>(viewport_width), static_cast<float>(viewport_height), alpha };
    p_silhouette_composite_shader->set_uniform("u_viewport_size_alpha", viewport_size_alpha);

    const auto& picking_color = wxGetApp().get_picking_color();
    p_silhouette_composite_shader->set_uniform("u_picking_color", picking_color);

    const int stage = 0;
    p_silhouette_composite_shader->set_uniform("u_sampler", stage);
    glsafe(::glActiveTexture(GL_TEXTURE0 + stage));
    glsafe(::glBindTexture(GL_TEXTURE_2D, color_texture_id));

    _init_fullscreen_mesh();

    s_full_screen_mesh.render_geometry();

    wxGetApp().unbind_shader();

    glsafe(::glDisable(GL_BLEND));
    // BBS: end composite silhouette
}

bool GLCanvas3D::is_volume_in_plate_boundingbox(const GLVolume &v, int plate_idx, const BoundingBoxf3 &plate_build_volume)
{
    bool ret = v.printable;
    if (plate_idx >= 0) {
        bool          contained          = false;
        BoundingBoxf3 plate_bbox         = plate_build_volume;
        plate_bbox.min(2)                = -1e10;
        const BoundingBoxf3 &volume_bbox = v.transformed_convex_hull_bounding_box();
        if (plate_bbox.contains(volume_bbox) && (volume_bbox.max(2) > 0)) { contained = true; }
        ret &= contained;
    } else {
        ret &= (!v.shader_outside_printer_detection_enabled || !v.is_outside);
    }
    return ret;
}

void GLCanvas3D::_init_fullscreen_mesh()
{
    if (s_full_screen_mesh.is_initialized()) {
        return;
    }
    GLModel::Geometry geo;
    geo.format.type = GLModel::PrimitiveType::Triangles;
    geo.format.vertex_layout = GLModel::Geometry::EVertexLayout::P3T2;

    geo.add_vertex(Vec3f{ -1.0f, -1.0f, 0.0f }, Vec2f{ 0.0f, 0.0f });
    geo.add_vertex(Vec3f{ 3.0f, -1.0f, 0.0f }, Vec2f{ 2.0f, 0.0f });
    geo.add_vertex(Vec3f{ -1.0f, 3.0f, 0.0f }, Vec2f{ 0.0f, 2.0f });

    geo.add_triangle(0, 1, 2);

    s_full_screen_mesh.init_from(std::move(geo));
}

void GLCanvas3D::_rebuild_postprocessing_pipeline(const std::shared_ptr<OpenGLManager>& p_ogl_manager, const std::string& input_framebuffer_name, std::string& output_framebuffer_name, uint32_t width, uint32_t height)
{
    if (!p_ogl_manager) {
        return;
    }

    auto &ogl_manager = *p_ogl_manager;
    const bool offscreen_rendering = input_framebuffer_name != OpenGLManager::s_back_frame;
    if (!offscreen_rendering && !ogl_manager.is_fxaa_enabled()) {
        return;
    }

    _init_fullscreen_mesh();

    uint32_t output_texture_id = UINT32_MAX;
    if (ogl_manager.is_fxaa_enabled()) {
        if (!offscreen_rendering) {
            {
                OpenGLManager::FrameBufferModifier fxaa_frame(ogl_manager, "fxaaframe_temp", EMSAAType::Disabled);
                fxaa_frame.set_width(width)
                    .set_height(height);
            }

            p_ogl_manager->blit_framebuffer(OpenGLManager::s_back_frame, "fxaaframe_temp");
        }

        {
            OpenGLManager::FrameBufferModifier fxaa_frame(ogl_manager, "fxaaframe", EMSAAType::Disabled);
            fxaa_frame.set_width(width)
                .set_height(height);
        }

        // BBS: composite main frame
        glsafe(::glDisable(GL_DEPTH_TEST));
        glsafe(::glDisable(GL_BLEND));

        const auto& p_main_frame_buffer = ogl_manager.get_frame_buffer(offscreen_rendering ? input_framebuffer_name : "fxaaframe_temp");
        if (p_main_frame_buffer) {
            output_texture_id = p_main_frame_buffer->get_color_texture();
            if (p_main_frame_buffer->is_texture_valid(output_texture_id)) {
                const auto& p_fxaa_shader = p_ogl_manager->get_shader("fxaa");
                if (p_fxaa_shader) {
                    p_ogl_manager->bind_shader(p_fxaa_shader);

                    const int stage = 0;
                    p_fxaa_shader->set_uniform("u_sampler", stage);
                    glsafe(::glActiveTexture(GL_TEXTURE0 + stage));
                    glsafe(::glBindTexture(GL_TEXTURE_2D, output_texture_id));

                    const std::array<float, 4> viewport_size{ static_cast<float>(width), static_cast<float>(height), 1.0f / width, 1.0f / height };
                    p_fxaa_shader->set_uniform("u_viewport_size", viewport_size);

                    s_full_screen_mesh.render_geometry();

                    p_ogl_manager->unbind_shader();

                    const auto& p_fxaa_frame_buffer = p_ogl_manager->get_frame_buffer("fxaaframe");
                    if (p_fxaa_frame_buffer) {
                        output_texture_id = p_fxaa_frame_buffer->get_color_texture();
                        if (!p_fxaa_frame_buffer->is_texture_valid(output_texture_id)) {
                            BOOST_LOG_TRIVIAL(error) << "Invalid fxaa texture.";
                        }
                    }
                    else {
                        BOOST_LOG_TRIVIAL(error) << "Invalid fxaa framebuffer.";
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(error) << "Invalid fxaa shader.";
                }
            }
            else {
                BOOST_LOG_TRIVIAL(error) << "Invalid main frame texture. Failed to composite main frame.";
            }
        }
    }
    else if (offscreen_rendering) {
        const auto& p_main_frame_buffer = ogl_manager.get_frame_buffer(input_framebuffer_name);
        if (p_main_frame_buffer) {
            output_texture_id = p_main_frame_buffer->get_color_texture();
        }
    }

    {
        OpenGLManager::FrameBufferModifier output_frame(ogl_manager, output_framebuffer_name);
        output_frame.set_width(width)
            .set_height(height);
    }

    const auto& p_mainframe_composite_shader = p_ogl_manager->get_shader("mainframe_composite");
    if (!p_mainframe_composite_shader)
    {
        BOOST_LOG_TRIVIAL(error) << "Invalid mainframe composite shader. Failed to composite main frame.";
        return;
    }
    p_ogl_manager->bind_shader(p_mainframe_composite_shader);

    const int stage = 0;
    p_mainframe_composite_shader->set_uniform("u_sampler", stage);
    glsafe(::glActiveTexture(GL_TEXTURE0 + stage));
    glsafe(::glBindTexture(GL_TEXTURE_2D, output_texture_id));

    s_full_screen_mesh.render_geometry();

    p_ogl_manager->unbind_shader();

    // BBS: end composite mainframe
}

void GLCanvas3D::_debug_draw_camera(const Camera& t_camera)
{
    if (!m_unit_cube.is_initialized()) {
        _init_unit_cube();
    }

    const auto& p_flat_shader = wxGetApp().get_shader("flat");
    if (p_flat_shader) {
        glsafe(::glDisable(GL_DEPTH_TEST));
        glsafe(::glLineWidth(2.0f));

        Transform3d final_model_matrix{ Transform3d::Identity() };
        final_model_matrix.data()[0 * 4 + 0] = 2.0f;
        final_model_matrix.data()[1 * 4 + 1] = 2.0f;
        final_model_matrix.data()[2 * 4 + 2] = 2.0f;

        auto vp_matrix = t_camera.get_projection_matrix().matrix() * t_camera.get_view_matrix().matrix();
        auto inv_vp_matrix = vp_matrix.inverse();
        auto m_inv_vp = inv_vp_matrix * final_model_matrix.matrix();
        final_model_matrix = m_inv_vp.eval();

        wxGetApp().bind_shader(p_flat_shader);

        const Camera& camera = get_active_camera();
        const auto& view_matrix = camera.get_view_matrix();
        const auto& proj_matrix = camera.get_projection_matrix();
        auto mv = view_matrix.matrix() * final_model_matrix.matrix();
        Matrix4d final_mv = mv.eval();
        p_flat_shader->set_uniform("view_model_matrix", final_mv);
        p_flat_shader->set_uniform("projection_matrix", proj_matrix);

        m_unit_cube.set_color({ 1.0f, 0.0f, 0.0f, 1.0f });
        m_unit_cube.render_geometry();

        wxGetApp().unbind_shader();
    }
}

void GLCanvas3D::_debug_draw_aabb()
{
    if (!m_unit_cube.is_initialized()) {
        _init_unit_cube();
    }

    std::vector<BoundingBoxf3> bbox_list;

    const auto& partplate_list = wxGetApp().plater()->get_partplate_list().get_plate_list();
    for (int i = 0; i < partplate_list.size(); ++i) {
        const auto& bbox = partplate_list[i]->get_bounding_box(true);
        bbox_list.emplace_back(bbox);
    }

    if (m_gizmos.is_running()) {
        const auto t_aabb = m_gizmos.get_bounding_box();
        if (t_aabb.defined) {
            bbox_list.emplace_back(t_aabb);
        }
    }

    const auto& p_flat_shader = wxGetApp().get_shader("flat");
    if (p_flat_shader) {
        glsafe(::glDisable(GL_DEPTH_TEST));
        glsafe(::glLineWidth(2.0f));
        wxGetApp().bind_shader(p_flat_shader);

        const Camera& camera = get_active_camera();
        const auto& view_matrix = camera.get_view_matrix();
        const auto& proj_matrix = camera.get_projection_matrix();
        p_flat_shader->set_uniform("projection_matrix", proj_matrix);
        m_unit_cube.set_color({ 1.0f, 0.0f, 0.0f, 1.0f });

        for (int i = 0; i < bbox_list.size(); ++i) {
            const auto& bbox = bbox_list[i];
            Vec3f b_min = bbox.min.cast<float>();
            Vec3f b_max = bbox.max.cast<float>();
            Vec3f size = bbox.size().cast<float>();
            const auto& center = bbox.center();

            Transform3d final_model_matrix{ Transform3d::Identity() };
            final_model_matrix.data()[3 * 4 + 0] = center(0);
            final_model_matrix.data()[3 * 4 + 1] = center(1);
            final_model_matrix.data()[3 * 4 + 2] = center(2);
            final_model_matrix.data()[0 * 4 + 0] = size(0);
            final_model_matrix.data()[1 * 4 + 1] = size(1);
            final_model_matrix.data()[2 * 4 + 2] = size(2);

            p_flat_shader->set_uniform("view_model_matrix", view_matrix * final_model_matrix);

            m_unit_cube.render_geometry();
        }

        wxGetApp().unbind_shader();
    }
}

void GLCanvas3D::_init_unit_cube()
{
    GLModel::Geometry geo;
    geo.format.type = GLModel::PrimitiveType::Lines;
    geo.format.vertex_layout = GLModel::Geometry::EVertexLayout::P3;

    const float size = 1.0f;
    Vec3f b_min{ -0.5f, -0.5f, -0.5f };
    Vec3f b_max{ 0.5f, 0.5f, 0.5f };
    geo.add_vertex(Vec3f{ b_min(0), b_min(1), b_min(2) });
    geo.add_vertex(Vec3f{ b_max(0), b_min(1), b_min(2) });
    geo.add_vertex(Vec3f{ b_max(0), b_max(1), b_min(2) });
    geo.add_vertex(Vec3f{ b_min(0), b_max(1), b_min(2) });
    geo.add_vertex(Vec3f{ b_min(0), b_min(1), b_max(2) });
    geo.add_vertex(Vec3f{ b_max(0), b_min(1), b_max(2) });
    geo.add_vertex(Vec3f{ b_max(0), b_max(1), b_max(2) });
    geo.add_vertex(Vec3f{ b_min(0), b_max(1), b_max(2) });

    geo.add_line(0, 1);
    geo.add_line(0, 3);
    geo.add_line(0, 4);

    geo.add_line(1, 2);
    geo.add_line(1, 5);

    geo.add_line(2, 3);
    geo.add_line(2, 6);

    geo.add_line(3, 7);

    geo.add_line(4, 5);
    geo.add_line(4, 7);

    geo.add_line(5, 6);

    geo.add_line(6, 7);

    m_unit_cube.init_from(std::move(geo));
}

void GLCanvas3D::_append_to_frame_callback(const FrameCallback& cb)
{
    m_frame_callback_list.emplace_back(cb);
}

void GLCanvas3D::_render_toolbar()
{
    _render_main_toolbar();
    _render_collapse_toolbar();

    //BBS: GUI refactor: GLToolbar
    _render_imgui_select_plate_toolbar();
    _render_return_toolbar();
    // BBS
    //_render_view_toolbar();
    _render_paint_toolbar();
}

const std::shared_ptr<GLToolbar>& GLCanvas3D::get_main_toolbar() const
{
    if (!m_main_toolbar) {
        m_main_toolbar = std::make_shared<GLToolbar>(GLToolbar::EType::Normal, "Main");
    }
    return m_main_toolbar;
}

void GLCanvas3D::_render_thumbnail_internal(ThumbnailData& thumbnail_data, const ThumbnailsParams& thumbnail_params,
    PartPlateList& partplate_list, ModelObjectPtrs& model_objects, const GLVolumeCollection& volumes, std::vector<std::array<float, 4>>& extruder_colors,
    const std::shared_ptr<GLShaderProgram>& shader,
    Camera::EType                      camera_type,
    Camera::ViewAngleType              camera_view_angle_type,
    bool                               for_picking,
    bool                               ban_light)
{
    //BBS modify visible calc function
    static std::array<float, 4>       curr_color;
    static const std::array<float, 4> orange = { 0.923f, 0.504f, 0.264f, 1.0f };
    static const std::array<float, 4> gray = { 0.64f, 0.64f, 0.64f, 1.0f };
    GLVolumePtrs                      visible_volumes;
    BoundingBoxf3                     plate_build_volume;
    if (thumbnail_params.use_plate_box) {
        int           plate_idx = thumbnail_params.plate_id;
        PartPlate* plate = partplate_list.get_plate(plate_idx);
        plate_build_volume      = plate->get_build_volume();

        for (GLVolume* vol : volumes.volumes) {
            if (!vol->is_modifier && !vol->is_wipe_tower && (!thumbnail_params.parts_only || vol->composite_id.volume_id >= 0)) {
                if (is_volume_in_plate_boundingbox(*vol, plate_idx, plate_build_volume)) {
                    visible_volumes.emplace_back(vol);
                }
            }
        }
        BOOST_LOG_TRIVIAL(info) << boost::format("render_thumbnail: plate_idx %1% volumes size %2%, shader %3%, use_top_view=%4%, for_picking=%5%") % plate_idx %
            visible_volumes.size() % shader.get() % (int)camera_view_angle_type % for_picking;
    }
    else {
        visible_volumes = volumes.volumes;
    }
    //BoundingBoxf3 volumes_box = plate_build_volume;
    BoundingBoxf3 volumes_box;
    volumes_box.min.z() = 0;
    volumes_box.max.z() = 0;
    if (!visible_volumes.empty()) {
        for (const GLVolume* vol : visible_volumes) {
            volumes_box.merge(vol->transformed_bounding_box());
        }
    }
    volumes_box.min.z() = -Slic3r::BuildVolume::SceneEpsilon;
    double width = volumes_box.max.x() - volumes_box.min.x();
    double depth = volumes_box.max.y() - volumes_box.min.y();
    double height = volumes_box.max.z() - volumes_box.min.z();
    volumes_box.max.x() = volumes_box.max.x() + width * 0.1f;
    volumes_box.min.x() = volumes_box.min.x() - width * 0.1f;
    volumes_box.max.y() = volumes_box.max.y() + depth * 0.1f;
    volumes_box.min.y() = volumes_box.min.y() - depth * 0.1f;
    volumes_box.max.z() = volumes_box.max.z() + height * 0.2f;
    volumes_box.min.z() = volumes_box.min.z() - height * 0.2f;

    Camera camera;
    camera.set_type(camera_type);
    //BBS modify scene box to plate scene bounding box
    //plate_build_volume.min(2) = - plate_build_volume.max(2);
    if (thumbnail_params.use_plate_box) {
        camera.set_scene_box(plate_build_volume);
    }

    camera.apply_viewport(0, 0, thumbnail_data.width, thumbnail_data.height);

    //BoundingBoxf3 plate_box = plate->get_bounding_box(false);
    //plate_box.min.z() = 0.0;
    //plate_box.max.z() = 0.0;

    if (camera_view_angle_type == Camera::ViewAngleType::Top_Plate) {
        float center_x = (plate_build_volume.max(0) + plate_build_volume.min(0)) / 2;
        float center_y = (plate_build_volume.max(1) + plate_build_volume.min(1)) / 2;
        float distance_z = plate_build_volume.max(2) - plate_build_volume.min(2);
        Vec3d center(center_x, center_y, 0.f);
        double zoom_ratio, scale_x, scale_y;

        scale_x = ((double)thumbnail_data.width) / (plate_build_volume.max(0) - plate_build_volume.min(0));
        scale_y = ((double)thumbnail_data.height) / (plate_build_volume.max(1) - plate_build_volume.min(1));
        zoom_ratio = (scale_x <= scale_y) ? scale_x : scale_y;
        camera.look_at(center + distance_z * Vec3d::UnitZ(), center, Vec3d::UnitY());
        camera.set_zoom(zoom_ratio);
        //camera.select_view("top");
    }
    else {
        //camera.zoom_to_box(volumes_box);

        //const Vec3d& target = camera.get_target();
        //double distance = camera.get_distance();
        //camera.look_at(target - 0.707 * distance * Vec3d::UnitY() + 0.3 * distance * Vec3d::UnitZ(), target, Vec3d::UnitY() + Vec3d::UnitZ());

        //BBS: use original iso view for thumbnail
        camera.select_view(camera_view_angle_type);
        camera.zoom_to_box(volumes_box);
    }

    if (thumbnail_params.use_plate_box) {
        camera.apply_projection(plate_build_volume);
    }
    else {
        camera.apply_projection(volumes_box);
    }
    //double near_z = -1.0;
    //double far_z = -1.0;
    //camera.apply_projection(volumes_box, near_z, far_z);

    //GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (!shader) {
        BOOST_LOG_TRIVIAL(info) << boost::format("render_thumbnail with invalid shader");
        return;
    }

    //if (thumbnail_params.transparent_background)
    if (for_picking)
        glsafe(::glClearColor(0.f, 0.f, 0.f, 0.f));
    else {
        const auto& bg_color = thumbnail_params.background_color;
        glsafe(::glClearColor(bg_color.x(), bg_color.y(), bg_color.z(), bg_color.w()));
        //glsafe(::glClearColor(0.50f, 0.5f, 0.5f, 1.0f));
        //glsafe(::glClearColor(0.121568f, 0.121568f, 0.121568f, 1.0f));
        //glsafe(::glClearColor(0.17647f, 0.17647f, 0.17647f, 1.0f));
        //glsafe(::glClearColor(0.906f, 0.906f, 0.906f, 1.0f));
        //glsafe(::glClearColor(0.37647f, 0.37647f, 0.37647f, 0.5f)); too lite
        //glsafe(::glClearColor(0.0f, 0.0f, 0.0f, 0.0f));
    }

    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));
    if (ban_light) {
        glsafe(::glDisable(GL_BLEND));
    }

    const auto& view_matrix = camera.get_view_matrix();
    const auto& projection_matrix = camera.get_projection_matrix();
    shader->set_uniform("ban_light", ban_light);
    if (for_picking) {
        //if (OpenGLManager::can_multisample())
              // This flag is often ignored by NVIDIA drivers if rendering into a screen buffer.
        //    glsafe(::glDisable(GL_MULTISAMPLE));

        glsafe(::glDisable(GL_BLEND));

        static const GLfloat INV_255 = 1.0f / 255.0f;

        // do not cull backfaces to show broken geometry, if any
        glsafe(::glDisable(GL_CULL_FACE));

        for (GLVolume* vol : visible_volumes) {
            // Object picking mode. Render the object with a color encoding the object index.
            // we reserve color = (0,0,0) for occluders (as the printbed)
            // so we shift volumes' id by 1 to get the proper color
            //BBS: remove the bed picking logic
            unsigned int id = vol->model_object_ID;
            //unsigned int id = 1 + volume.second.first;
            unsigned int r = (id & (0x000000FF << 0)) >> 0;
            unsigned int g = (id & (0x000000FF << 8)) >> 8;
            unsigned int b = (id & (0x000000FF << 16)) >> 16;
            unsigned int a = 0xFF;

            const std::array<float, 4> color{ (GLfloat)r * INV_255, (GLfloat)g * INV_255, (GLfloat)b * INV_255, (GLfloat)a * INV_255 };
            shader->set_uniform("uniform_color", color);

            bool is_active = vol->is_active;
            vol->is_active = true;
            vol->picking = true;

            const Transform3d matrix = view_matrix * vol->world_matrix();
            shader->set_uniform("view_model_matrix", matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
            shader->set_uniform("normal_matrix", (Matrix3d)matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
            shader->set_uniform("volume_world_matrix", vol->world_matrix());
            vol->simple_render(shader, model_objects, extruder_colors);
            vol->is_active = is_active;
            vol->picking = false;
        }
        glsafe(::glEnable(GL_CULL_FACE));

        //if (OpenGLManager::can_multisample())
        //    glsafe(::glEnable(GL_MULTISAMPLE));
    }
    else {
        shader->set_uniform("emission_factor", 0.1f);
        for (GLVolume* vol : visible_volumes) {
            //BBS set render color for thumbnails
            curr_color[0] = vol->color[0];
            curr_color[1] = vol->color[1];
            curr_color[2] = vol->color[2];
            curr_color[3] = vol->color[3];

            std::array<float, 4> new_color = adjust_color_for_rendering(curr_color);
            if (ban_light) {
                new_color[3] = (255 - (vol->extruder_id - 1)) / 255.0f;
            }
            shader->set_uniform("uniform_color", new_color);
            shader->set_uniform("volume_world_matrix", vol->world_matrix());
            //BBS set all volume to orange
            //shader->set_uniform("uniform_color", orange);
            /*if (plate_idx > 0) {
                shader->set_uniform("uniform_color", orange);
            }
            else {
                shader->set_uniform("uniform_color", (vol->printable && !vol->is_outside) ? orange : gray);
            }*/
            // the volume may have been deactivated by an active gizmo
            bool is_active = vol->is_active;
            vol->is_active = true;
            const Transform3d matrix = view_matrix * vol->world_matrix();
            shader->set_uniform("view_model_matrix", matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
            shader->set_uniform("normal_matrix", (Matrix3d)matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
            vol->simple_render(shader, model_objects, extruder_colors, ban_light);
            vol->is_active = is_active;
        }
    }

    glsafe(::glDisable(GL_DEPTH_TEST));

    //don't render plate in thumbnail
    //plate->render( false, true, true);

    // restore background color
    //if (thumbnail_params.transparent_background)
    //    glsafe(::glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
    BOOST_LOG_TRIVIAL(info) << boost::format("render_thumbnail: finished");
}

void GLCanvas3D::_set_warning_notification(EWarning warning, bool state)
{
    using NotificationLevel = NotificationManager::NotificationLevel;
    enum ErrorType{
        PLATER_WARNING,
        PLATER_ERROR,
        SLICING_SERIOUS_WARNING,
        SLICING_ERROR,
        SLICING_LIMIT_ERROR,
        SLICING_HEIGHT_OUTSIDE,
        ASSEMBLY_WARNNING
    };
    const std::vector<std::string> extruder_name_list= {_u8L("left nozzle"), _u8L("right nozzle")};  // in ui, we treat extruder as nozzle
    std::string text;
    ErrorType error = ErrorType::PLATER_WARNING;
    const ModelObject* conflictObj=nullptr;
    switch (warning) {
    case EWarning::GCodeConflict: {
        static std::string prevConflictText;
        text  = prevConflictText;
        error = ErrorType::SLICING_SERIOUS_WARNING;
        if (!m_gcode_viewer.m_conflict_result) { break; }
        std::string objName1 = m_gcode_viewer.m_conflict_result.value()._objName1;
        std::string objName2 = m_gcode_viewer.m_conflict_result.value()._objName2;
        double      height   = m_gcode_viewer.m_conflict_result.value()._height;
        int         layer    = m_gcode_viewer.m_conflict_result.value().layer;
        text = (boost::format(_u8L("Conflicts of gcode paths have been found at layer %d. Please separate the conflicted objects farther (%s <-> %s).")) % (layer + 1) %
                objName1 % objName2)
                   .str();
        prevConflictText        = text;
        const PrintObject *obj2 = reinterpret_cast<const PrintObject *>(m_gcode_viewer.m_conflict_result.value()._obj2);
        conflictObj             = obj2->model_object();
        break;
    }
    case EWarning::ObjectOutside:      text = _u8L("An object is layed over the boundary of plate."); break;
    case EWarning::ToolHeightOutside:  text = _u8L("A G-code path goes beyond the max print height."); error = ErrorType::SLICING_ERROR; break;
    case EWarning::ToolpathOutside:    text = _u8L("A G-code path goes beyond the boundary of plate."); error = ErrorType::SLICING_ERROR; break;
    case EWarning::TPUPrintableError: {
        text = _u8L("Not support printing 2 or more TPU filaments.");
        error = ErrorType::SLICING_ERROR;
        break;
    }
    case EWarning::FilamentPrintableError: {
        text  = filament_printable_error_msg.ToUTF8();
        error = ErrorType::SLICING_ERROR;
        break;
    }
    case EWarning::LeftExtruderPrintableError:
    case EWarning::RightExtruderPrintableError: {
        error = ErrorType::PLATER_ERROR;
        break;
    }
    case EWarning::MultiExtruderPrintableError: {
        for (auto error_iter = m_gcode_viewer.m_gcode_check_result.print_area_error_infos.begin(); error_iter != m_gcode_viewer.m_gcode_check_result.print_area_error_infos.end(); ++error_iter) {
            if (error_iter != m_gcode_viewer.m_gcode_check_result.print_area_error_infos.begin()) {
                text += "\n";
            }
            int extruder_id = error_iter->first + 1; // change extruder id to 1 based
            std::string filaments;
            std::vector<int> slice_error_object_idxs;
            for (size_t i = 0; i < error_iter->second.size(); ++i) {
                if (i > 0) {
                    filaments += ", ";
                }
                int filament_id = error_iter->second[i].first + 1; // change filament id to 1 based
                int object_label_id = error_iter->second[i].second;

                filaments += std::to_string(filament_id);
                for (int object_idx = 0; object_idx < (int) m_model->objects.size(); ++object_idx) {
                    const ModelObject *model_object = m_model->objects[object_idx];
                    for (int instance_idx = 0; instance_idx < (int) model_object->instances.size(); ++instance_idx) {
                        const ModelInstance *model_instance = model_object->instances[instance_idx];
                        auto                 expect_id      = model_instance->get_labeled_id();
                        if (object_label_id == expect_id) {
                            slice_error_object_idxs.emplace_back(object_idx);
                        }
                    }
                }
            }
            for (GLVolume *volume : m_gcode_viewer.m_shells.volumes.volumes) {
                for (auto obj_idx : slice_error_object_idxs) {
                    if (volume->object_idx() == obj_idx) {
                        volume->slice_error = true;
                        volume->selected    = true;
                    }
                }
            }
            std::string extruder_name = extruder_name_list[extruder_id-1];
            if (error_iter->second.size() == 1) {
                text += (boost::format(_u8L("Filament %s is placed in the %s, but the generated G-code path exceeds the printable range of the %s.")) %filaments %extruder_name %extruder_name).str();
            }
            else {
                text += (boost::format(_u8L("Filaments %s is placed in the %s, but the generated G-code path exceeds the printable range of the %s.")) %filaments %extruder_name %extruder_name).str();
            }
        }
        error = ErrorType::SLICING_LIMIT_ERROR;
        break;
    }
    case EWarning::MultiExtruderHeightOutside: {
        for (auto error_iter = m_gcode_viewer.m_gcode_check_result.print_height_error_infos.begin(); error_iter != m_gcode_viewer.m_gcode_check_result.print_height_error_infos.end(); ++error_iter) {
            if (error_iter != m_gcode_viewer.m_gcode_check_result.print_height_error_infos.begin()) {
                text += "\n";
            }
            int              extruder_id = error_iter->first + 1; // change extruder id to 1 based
            std::set<int>    filament_ids;
            std::vector<int> slice_error_object_idxs;
            for (size_t i = 0; i < error_iter->second.size(); ++i) {
                int filament_id     = error_iter->second[i].first + 1; // change filament id to 1 based
                int object_label_id = error_iter->second[i].second;
                filament_ids.insert(filament_id);
                for (int object_idx = 0; object_idx < (int) m_model->objects.size(); ++object_idx) {
                    const ModelObject *model_object = m_model->objects[object_idx];
                    for (int instance_idx = 0; instance_idx < (int) model_object->instances.size(); ++instance_idx) {
                        const ModelInstance *model_instance = model_object->instances[instance_idx];
                        auto                 expect_id      = model_instance->get_labeled_id();
                        if (object_label_id == expect_id) {
                            slice_error_object_idxs.emplace_back(object_idx);
                        }
                    }
                }
            }

            std::string filaments;
            int         index = 0;
            for (auto filament_id : filament_ids) {
                if (index > 0) {
                    filaments += ", ";
                }
                filaments += std::to_string(filament_id);
                ++index;
            }

            for (GLVolume *volume : m_gcode_viewer.m_shells.volumes.volumes) {
                for (auto obj_idx : slice_error_object_idxs) {
                    if (volume->object_idx() == obj_idx) {
                        volume->slice_error = true;
                        volume->selected    = true;
                    }
                }
            }
            std::string extruder_name = extruder_name_list[extruder_id-1];
            if (error_iter->second.size() == 1) {
                text += (boost::format(_u8L("Filament %s is placed in the %s, but the generated G-code path exceeds the printable height of the %s.")) % filaments % extruder_name % extruder_name).str();
            } else {
                text += (boost::format(_u8L("Filaments %s is placed in the %s, but the generated G-code path exceeds the printable height of the %s.")) % filaments % extruder_name % extruder_name).str();
            }
        }
        if (!text.empty()) {
            text += "\n";
            text += _u8L("Open wiki for more information.");
        }
        error = ErrorType::SLICING_HEIGHT_OUTSIDE;
        break;
    }

    // BBS: remove _u8L() for SLA
    case EWarning::SlaSupportsOutside: text = ("SLA supports outside the print area were detected."); error = ErrorType::PLATER_ERROR; break;
    case EWarning::SomethingNotShown:  text = _u8L("Only the object being edited is visible."); break;
    case EWarning::ObjectClashed:
        error = ErrorType::PLATER_ERROR;
        break;
    case EWarning::ObjectLimited:
        text = get_object_limited_text();
        break;
    case EWarning::FilamentUnPrintableOnFirstLayer: {
        std::string             warning;
        const std::vector<int> &conflict_filament = m_gcode_viewer.filament_printable_reuslt.conflict_filament;
        auto                    iter              = conflict_filament.begin();
        for (int filament : conflict_filament) {
            warning += std::to_string(filament + 1);
            warning += " ";
        }
        text  = (boost::format(_u8L("filaments %s cannot be printed directly on the surface of this plate.")) % warning).str();
        error = ErrorType::SLICING_ERROR;
        break;
    }
    case EWarning::MixUsePLAAndPETG:
        text = _u8L("PLA and PETG filaments detected in the mixture. Adjust parameters according to the Wiki to ensure print quality.");
        break;
    case EWarning::PrimeTowerOutside:
        text  = _u8L("The prime tower extends beyond the plate boundary.");
        break;
    case EWarning::AsemblyInvalid:
    {
        error = ErrorType::ASSEMBLY_WARNNING;
        break;
    }
    case EWarning::NozzleFilamentIncompatible: {
        text = _u8L(get_nozzle_filament_incompatible_text());
        break;
    }
    }
    //BBS: this may happened when exit the app, plater is null
    if (!wxGetApp().plater())
        return;
    auto& notification_manager = *wxGetApp().plater()->get_notification_manager();

    switch (error)
    {
    case PLATER_WARNING:
        if (warning == EWarning::MixUsePLAAndPETG) {
            if (state) {
                notification_manager.push_slicing_customize_error_notification(NotificationType::BBLMixUsePLAAndPETG, NotificationLevel::WarningNotificationLevel, text, _u8L("Click Wiki for help."),
                    [](wxEvtHandler*) {
                        std::string language = wxGetApp().app_config->get("language");
                        wxString    region = L"en";
                        if (language.find("zh") == 0)
                        	region = L"zh";
                        wxGetApp().open_browser_with_warning_dialog(wxString::Format(L"https://wiki.bambulab.com/%s/filament-acc/filament/h2d-pla-and-petg-mutual-support", region));
                        return false;
                    });
            }
            else
                notification_manager.close_slicing_customize_error_notification(NotificationType::BBLMixUsePLAAndPETG, NotificationLevel::WarningNotificationLevel);
        }
        else if (warning == EWarning::NozzleFilamentIncompatible){
            if(state){
                notification_manager.push_slicing_customize_error_notification(NotificationType::BBLNozzleFilamentIncompatible, NotificationLevel::WarningNotificationLevel, text);
            }
            else{
                notification_manager.close_slicing_customize_error_notification(NotificationType::BBLNozzleFilamentIncompatible, NotificationLevel::WarningNotificationLevel);
            }
        }
        else {
            if (state)
                notification_manager.push_plater_warning_notification(text);
            else
                notification_manager.close_plater_warning_notification(text);
        }
        break;
    case PLATER_ERROR:
        if (warning == EWarning::LeftExtruderPrintableError) {
            if (state) {
                if (auto left_str = get_left_extruder_unprintable_text(); !left_str.empty())
                    notification_manager.bbl_show_filament_map_invalid_notification_before_slice(NotificationType::LeftExtruderUnprintableError, left_str);
            }
            else {
                notification_manager.bbl_close_filament_map_invalid_notification_before_slice(NotificationType::LeftExtruderUnprintableError);
            }
        }
        else if (warning == EWarning::RightExtruderPrintableError) {
            if (state) {
                if (auto right_str = get_right_extruder_unprintable_text(); !right_str.empty())
                    notification_manager.bbl_show_filament_map_invalid_notification_before_slice(NotificationType::RightExtruderUnprintableError, right_str);
            }
            else {
                notification_manager.bbl_close_filament_map_invalid_notification_before_slice(NotificationType::RightExtruderUnprintableError);
            }
        }
        else if (warning == EWarning::ObjectClashed) {
            auto str = get_object_clashed_text();
            if(state){
                if (!str.empty())
                    notification_manager.push_plater_error_notification(str);
            }
            else{
                notification_manager.close_plater_error_notification(str);
            }
        }
        else {
            if (state)
                notification_manager.push_plater_error_notification(text);
            else
                notification_manager.close_plater_error_notification(text);
        }
        break;
    case SLICING_SERIOUS_WARNING:
        if (state)
            notification_manager.push_slicing_serious_warning_notification(text, conflictObj ? std::vector<ModelObject const*>{conflictObj} : std::vector<ModelObject const*>{});
        else
            notification_manager.close_slicing_serious_warning_notification(text);
        break;
    case SLICING_ERROR:
        if (warning == EWarning::FilamentUnPrintableOnFirstLayer) {
            if (state) {
                notification_manager.bbl_show_bed_filament_incompatible_notification(text);
            }
            else {
                notification_manager.bbl_close_bed_filament_incompatible_notification();
            }
        }
        if (warning == EWarning::FilamentPrintableError) {
            if (state){
                auto callback = [](wxEvtHandler*) {
                    auto plater = wxGetApp().plater();
                    auto partplate = plater->get_partplate_list().get_curr_plate();
                    try_pop_up_before_slice(false, plater, partplate, true); // ignore the return value
                    return false;
                };
                notification_manager.push_slicing_customize_error_notification(NotificationType::BBLFilamentPrintableError, NotificationLevel::ErrorNotificationLevel, text,  _u8L("Click here to regroup"), callback);
            }
            else
                notification_manager.close_slicing_customize_error_notification(NotificationType::BBLFilamentPrintableError, NotificationLevel::ErrorNotificationLevel);
        }
        else {
            if (state)
                notification_manager.push_slicing_error_notification(text, conflictObj ? std::vector<ModelObject const*>{conflictObj} : std::vector<ModelObject const*>{});
            else
                notification_manager.close_slicing_error_notification(text);
        }
        break;
    case SLICING_LIMIT_ERROR:
        if (state)
            notification_manager.bbl_show_filament_map_invalid_notification_after_slice(NotificationType::BBLSliceLimitError, text);
        else
            notification_manager.bbl_close_filament_map_invalid_notification_after_slice(NotificationType::BBLSliceLimitError);
        break;
    case SLICING_HEIGHT_OUTSIDE:
        if (state)
            notification_manager.push_slicing_customize_error_notification(NotificationType::BBLSliceMultiExtruderHeightOutside, NotificationLevel::ErrorNotificationLevel, text);
        else
            notification_manager.close_slicing_customize_error_notification(NotificationType::BBLSliceMultiExtruderHeightOutside, NotificationLevel::ErrorNotificationLevel);
        break;
    case ASSEMBLY_WARNNING:
    {
        text = get_assembly_too_far_text();
        if (state)
            notification_manager.push_assembly_warning_notification(text);
        else
            notification_manager.close_assembly_warning_notification(text);
        break;
    }
    default:
        break;
    }
}

bool GLCanvas3D::_is_any_volume_outside() const
{
    for (const GLVolume* volume : m_volumes.volumes) {
        if (volume != nullptr && volume->is_outside)
            return true;
    }

    return false;
}

void GLCanvas3D::_update_selection_from_hover()
{
    bool ctrl_pressed = wxGetKeyState(WXK_CONTROL);

    if (m_hover_volume_idxs.empty()) {
        if (!ctrl_pressed && (m_rectangle_selection.get_state() == GLSelectionRectangle::Select))
            m_selection.remove_all();

        return;
    }

    GLSelectionRectangle::EState state = m_rectangle_selection.get_state();

    bool hover_modifiers_only = true;
    for (int i : m_hover_volume_idxs) {
        if (!m_volumes.volumes[i]->is_modifier) {
            hover_modifiers_only = false;
            break;
        }
    }

    bool selection_changed = false;
    if (state == GLSelectionRectangle::Select) {
        bool contains_all = true;
        for (int i : m_hover_volume_idxs) {
            if (!m_selection.contains_volume((unsigned int)i)) {
                contains_all = false;
                break;
            }
        }

        // the selection is going to be modified (Add)
        if (!contains_all) {
            wxGetApp().plater()->take_snapshot(std::string("Select by rectangle"), UndoRedo::SnapshotType::Selection);
            selection_changed = true;
        }
    }
    else {
        bool contains_any = false;
        for (int i : m_hover_volume_idxs) {
            if (m_selection.contains_volume((unsigned int)i)) {
                contains_any = true;
                break;
            }
        }

        // the selection is going to be modified (Remove)
        if (contains_any) {
            wxGetApp().plater()->take_snapshot(std::string("Unselect by rectangle"), UndoRedo::SnapshotType::Selection);
            selection_changed = true;
        }
    }

    if (!selection_changed)
        return;

    Plater::SuppressSnapshots suppress(wxGetApp().plater());

    if ((state == GLSelectionRectangle::Select) && !ctrl_pressed)
        m_selection.clear();

    for (int i : m_hover_volume_idxs) {
        if (state == GLSelectionRectangle::Select) {
            if (hover_modifiers_only) {
                const GLVolume& v = *m_volumes.volumes[i];
                m_selection.add_volume(v.object_idx(), v.volume_idx(), v.instance_idx(), false);
            }
            else
                m_selection.add(i, false);
        }
        else
            m_selection.remove(i);
    }

    if (m_selection.is_empty())
        m_gizmos.reset_all_states();
    else
        m_gizmos.refresh_on_off_state();

    m_gizmos.update_data();
    post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
    m_dirty = true;
}

bool GLCanvas3D::_deactivate_arrange_menu()
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return false;
    }
    if (p_main_toolbar->is_item_pressed("arrange")) {
        p_main_toolbar->force_right_action(p_main_toolbar->get_item_id("arrange"), *this);
        return true;
    }

    return false;
}

//BBS: add deactivate orient menu
bool GLCanvas3D::_deactivate_orient_menu()
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return false;
    }
    if (p_main_toolbar->is_item_pressed("orient")) {
        p_main_toolbar->force_right_action(p_main_toolbar->get_item_id("orient"), *this);
        return true;
    }

    return false;
}

//BBS: add deactivate layersediting menu
bool GLCanvas3D::_deactivate_layersediting_menu()
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return false;
    }
    if (p_main_toolbar->is_item_pressed("layersediting")) {
        p_main_toolbar->force_right_action(p_main_toolbar->get_item_id("layersediting"), *this);
        return true;
    }

    return false;
}

bool GLCanvas3D::_deactivate_collapse_toolbar_items()
{
    GLToolbar& collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();
    if (collapse_toolbar.is_item_pressed("print")) {
        collapse_toolbar.force_left_action(collapse_toolbar.get_item_id("print"), *this);
        return true;
    }

    return false;
}

void GLCanvas3D::highlight_toolbar_item(const std::string& item_name)
{
    const auto& p_main_toolbar = get_main_toolbar();
    if (!p_main_toolbar) {
        return;
    }
    std::shared_ptr<GLToolbarItem> item = p_main_toolbar->get_item(item_name);
    if (!item || !item->is_visible())
        return;
    m_toolbar_highlighter.init(item, this);
}

void GLCanvas3D::highlight_gizmo(const std::string& gizmo_name)
{
    GLGizmosManager::EType gizmo = m_gizmos.get_gizmo_from_name(gizmo_name);
    if(gizmo == GLGizmosManager::EType::Undefined)
        return;
    m_gizmo_highlighter.init(&m_gizmos, gizmo, this);
}

const Print* GLCanvas3D::fff_print() const
{
    return (m_process == nullptr) ? nullptr : m_process->fff_print();
}

const SLAPrint* GLCanvas3D::sla_print() const
{
    return (m_process == nullptr) ? nullptr : m_process->sla_print();
}

void GLCanvas3D::WipeTowerInfo::apply_wipe_tower() const
{
    if (m_plate_idx >= wxGetApp().plater()->get_partplate_list().get_plate_count()) {
        BOOST_LOG_TRIVIAL(error) << "Invalid plate index: " << m_plate_idx << ">=" << wxGetApp().plater()->get_partplate_list().get_plate_count();
        return;
    }
    // BBS: add partplate logic
    DynamicConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
    Vec3d plate_origin = wxGetApp().plater()->get_partplate_list().get_plate(m_plate_idx)->get_origin();
    ConfigOptionFloat wipe_tower_x(m_pos(X) - plate_origin(0));
    ConfigOptionFloat wipe_tower_y(m_pos(Y) - plate_origin(1));

    ConfigOptionFloats* wipe_tower_x_opt = proj_cfg.option<ConfigOptionFloats>("wipe_tower_x", true);
    ConfigOptionFloats* wipe_tower_y_opt = proj_cfg.option<ConfigOptionFloats>("wipe_tower_y", true);
    wipe_tower_x_opt->set_at(&wipe_tower_x, m_plate_idx, 0);
    wipe_tower_y_opt->set_at(&wipe_tower_y, m_plate_idx, 0);

    //q->update();
}

void GLCanvas3D::RenderTimer::Notify()
{
    wxPostEvent((wxEvtHandler*)GetOwner(), RenderTimerEvent( EVT_GLCANVAS_RENDER_TIMER, *this));
}

void GLCanvas3D::ToolbarHighlighterTimer::Notify()
{
    wxPostEvent((wxEvtHandler*)GetOwner(), ToolbarHighlighterTimerEvent(EVT_GLCANVAS_TOOLBAR_HIGHLIGHTER_TIMER, *this));
}

void GLCanvas3D::GizmoHighlighterTimer::Notify()
{
    wxPostEvent((wxEvtHandler*)GetOwner(), GizmoHighlighterTimerEvent(EVT_GLCANVAS_GIZMO_HIGHLIGHTER_TIMER, *this));
}

GLCanvas3D::RenderPipelineStageModifier::RenderPipelineStageModifier(GLCanvas3D& canvas, ERenderPipelineStage stage)
    : m_canvas(canvas)
{
    m_canvas._push_render_stage(stage);
}

GLCanvas3D::RenderPipelineStageModifier::~RenderPipelineStageModifier()
{
    m_canvas._pop_render_stage();
}

void GLCanvas3D::ToolbarHighlighter::set_timer_owner(wxEvtHandler* owner, int timerid/* = wxID_ANY*/)
{
    m_timer.SetOwner(owner, timerid);
}

void GLCanvas3D::ToolbarHighlighter::init(const std::shared_ptr<GLToolbarItem>& toolbar_item, GLCanvas3D* canvas)
{
    if (m_timer.IsRunning())
        invalidate();
    if (!toolbar_item || !canvas)
        return;

    m_timer.Start(300, false);

    m_toolbar_item = toolbar_item;
    m_canvas       = canvas;
}

void GLCanvas3D::ToolbarHighlighter::invalidate()
{
    m_timer.Stop();

    if (const auto& p_toolbar_item = m_toolbar_item.lock()) {
        p_toolbar_item->set_highlight(GLToolbarItem::EHighlightState::NotHighlighted);
    }
    m_toolbar_item.reset();
    m_blink_counter = 0;
    m_render_arrow = false;
}

void GLCanvas3D::ToolbarHighlighter::blink()
{
    const auto& p_toolbar_item = m_toolbar_item.lock();
    if (p_toolbar_item) {
        char state = p_toolbar_item->get_highlight();
        if (state != (char)GLToolbarItem::EHighlightState::HighlightedShown)
            p_toolbar_item->set_highlight(GLToolbarItem::EHighlightState::HighlightedShown);
        else
            p_toolbar_item->set_highlight(GLToolbarItem::EHighlightState::HighlightedHidden);

        m_render_arrow = !m_render_arrow;
        m_canvas->set_as_dirty();
    }
    else
        invalidate();

    if ((++m_blink_counter) >= 11)
        invalidate();
}

void GLCanvas3D::GizmoHighlighter::set_timer_owner(wxEvtHandler* owner, int timerid/* = wxID_ANY*/)
{
    m_timer.SetOwner(owner, timerid);
}

void GLCanvas3D::GizmoHighlighter::init(GLGizmosManager* manager, GLGizmosManager::EType gizmo, GLCanvas3D* canvas)
{
    if (m_timer.IsRunning())
        invalidate();
    if (!gizmo || !canvas)
        return;

    m_timer.Start(300, false);

    m_gizmo_manager = manager;
    m_gizmo_type    = gizmo;
    m_canvas        = canvas;
}

void GLCanvas3D::GizmoHighlighter::invalidate()
{
    m_timer.Stop();

    if (m_gizmo_manager) {
        m_gizmo_manager->set_highlight(GLGizmosManager::EType::Undefined, false);
    }
    m_gizmo_manager = nullptr;
    m_gizmo_type = GLGizmosManager::EType::Undefined;
    m_blink_counter = 0;
    m_render_arrow = false;
}

void GLCanvas3D::GizmoHighlighter::blink()
{
    if (m_gizmo_manager) {
        if (m_blink_counter % 2 == 0)
            m_gizmo_manager->set_highlight(m_gizmo_type, true);
        else
            m_gizmo_manager->set_highlight(m_gizmo_type, false);

        m_render_arrow = !m_render_arrow;
        m_canvas->set_as_dirty();
    }
    else
        invalidate();

    if ((++m_blink_counter) >= 11)
        invalidate();
}

const ModelVolume *get_model_volume(const GLVolume &v, const Model &model)
{
    const ModelVolume * ret = nullptr;

    if (v.object_idx() < (int)model.objects.size()) {
        const ModelObject *obj = model.objects[v.object_idx()];
        if (v.volume_idx() < (int)obj->volumes.size())
            ret = obj->volumes[v.volume_idx()];
    }

    return ret;
}

ModelVolume *get_model_volume(const ObjectID &volume_id, const ModelObjectPtrs &objects)
{
    for (const ModelObject *obj : objects)
        for (ModelVolume *vol : obj->volumes)
            if (vol->id() == volume_id)
                return vol;
    return nullptr;
}

ModelVolume *get_model_volume(const GLVolume &v, const ModelObject& object) {
    if (v.volume_idx() < 0)
        return nullptr;

    size_t volume_idx = static_cast<size_t>(v.volume_idx());
    if (volume_idx >= object.volumes.size())
        return nullptr;

    return object.volumes[volume_idx];
}

ModelVolume *get_selected_model_volume(const GLCanvas3D &canvas)
{
    auto gl_volume = get_selected_gl_volume(canvas);
    if (gl_volume) {
        return get_model_volume(*gl_volume, canvas.get_model()->objects);
    }
    return nullptr;
}

ModelVolume *get_model_volume(const GLVolume &v, const ModelObjectPtrs &objects)
{
    if (v.object_idx() < 0)
        return nullptr;
    size_t objext_idx = static_cast<size_t>(v.object_idx());
    if (objext_idx >= objects.size())
        return nullptr;
    if (objects[objext_idx] == nullptr)
        return nullptr;
    return get_model_volume(v, *objects[objext_idx]);
}

GLVolume *get_first_hovered_gl_volume(const GLCanvas3D &canvas)
{
    int hovered_id_signed =  canvas.get_first_hover_volume_idx();
    if (hovered_id_signed < 0)
        return nullptr;

    size_t              hovered_id = static_cast<size_t>(hovered_id_signed);
    const GLVolumePtrs &volumes    = canvas.get_volumes().volumes;
    if (hovered_id >= volumes.size())
        return nullptr;

    return volumes[hovered_id];
}

GLVolume *get_selected_gl_volume(const GLCanvas3D &canvas)
{
    const GLVolume *gl_volume = get_selected_gl_volume(canvas.get_selection());
    if (gl_volume == nullptr) return nullptr;

    const GLVolumePtrs &gl_volumes = canvas.get_volumes().volumes;
    for (GLVolume *v : gl_volumes)
        if (v->composite_id == gl_volume->composite_id) return v;
    return nullptr;
}

ModelObject *get_selected_model_object(GLCanvas3D &canvas) {
    auto v = get_selected_gl_volume(canvas);
    if (v) {
        auto mo = get_model_object(*v, canvas.get_model()->objects);
        return mo;
    }
    else {
        int out_object_idx;
        return canvas.get_selection().get_selected_single_object(out_object_idx);
    }
    return nullptr;
}

ModelObject *get_model_object(const GLVolume &gl_volume, const Model &model) { return get_model_object(gl_volume, model.objects); }

ModelObject *get_model_object(const GLVolume &gl_volume, const ModelObjectPtrs &objects)
{
    if (gl_volume.object_idx() < 0) return nullptr;
    size_t objext_idx = static_cast<size_t>(gl_volume.object_idx());
    if (objext_idx >= objects.size()) return nullptr;
    return objects[objext_idx];
}

ModelInstance *get_model_instance(const GLVolume &gl_volume, const Model &model) { return get_model_instance(gl_volume, model.objects); }

ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObjectPtrs &objects)
{
    if (gl_volume.instance_idx() < 0) return nullptr;
    ModelObject *object = get_model_object(gl_volume, objects);
    return get_model_instance(gl_volume, *object);
}

ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObject &object)
{
    if (gl_volume.instance_idx() < 0) return nullptr;
    size_t instance_idx = static_cast<size_t>(gl_volume.instance_idx());
    if (instance_idx >= object.instances.size()) return nullptr;
    return object.instances[instance_idx];
}

} // namespace GUI
} // namespace Slic3r
