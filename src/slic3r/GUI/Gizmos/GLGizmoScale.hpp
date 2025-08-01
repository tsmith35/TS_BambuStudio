#ifndef slic3r_GLGizmoScale_hpp_
#define slic3r_GLGizmoScale_hpp_

#include "GLGizmoBase.hpp"
//BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"

#include "libslic3r/BoundingBox.hpp"


namespace Slic3r {
namespace GUI {

class GLGizmoScale3D : public GLGizmoBase
{
    static const float Offset;

    struct StartingData
    {
        Vec3d scale;
        Vec3d drag_position;
        Vec3d constraint_position;
        Vec3d center{Vec3d::Zero()};//sphere bounding box center
        Vec3d instance_center{Vec3d::Zero()};
        Vec3d plane_center;  // keep the relative center position for scale in the bottom plane
        Vec3d plane_nromal;  // keep the bottom plane
        BoundingBoxf3 box;
        Vec3d pivots[6];// Vec3d constraint_position{Vec3d::Zero()};
        Vec3d local_pivots[6];

        StartingData() : scale(Vec3d::Ones()), drag_position(Vec3d::Zero()) { for (int i = 0; i < 5; ++i) { pivots[i] = Vec3d::Zero(); } }
    };

    mutable BoundingBoxf3 m_bounding_box;
    Geometry::Transformation m_grabbers_tran;//m_grabbers_transform
    Vec3d                 m_center{Vec3d::Zero()};
    Vec3d                 m_instance_center{Vec3d::Zero()};
    Vec3d m_scale;
    Vec3d m_offset;
    double m_snap_step;
    StartingData m_starting;

    //BBS: add size adjust related
    GizmoObjectManipulation* m_object_manipulation;

    struct GrabberConnection
    {
        GLModel model;
        std::pair<unsigned int, unsigned int> grabber_indices;
        Vec3d old_v1{ Vec3d::Zero() };
        Vec3d old_v2{ Vec3d::Zero() };
    };
    mutable std::array<GrabberConnection, 7> m_grabber_connections;

public:
    //BBS: add obj manipulation logic
    //GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    GLGizmoScale3D(GLCanvas3D& parent, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d &get_scale();
    void set_scale(const Vec3d& scale) { m_starting.scale = scale; m_scale = scale; }

    const Vec3d& get_offset() const { return m_offset; }

    std::string get_tooltip() const override;
    void        data_changed(bool is_serializing) override;
    void enable_ununiversal_scale(bool enable);
    BoundingBoxf3 get_bounding_box() const override;

    bool on_key(const wxKeyEvent& key_event) override;

    std::string get_icon_filename(bool b_dark_mode) const override;
protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual std::string on_get_name_str() override { return "Scale"; }
    virtual bool on_is_activable() const override;
    virtual void on_set_state() override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void on_update(const UpdateData& data) override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    //BBS: GUI refactor: add object manipulation
    virtual void on_render_input_window(float x, float y, float bottom_limit);
private:
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color) const;

    void do_scale_along_axis(Axis axis, const UpdateData& data);
    void do_scale_uniform(const UpdateData& data);

    double calc_ratio(const UpdateData& data) const;
    void   update_grabbers_data();
    void   change_cs_by_selection(); // cs mean Coordinate System
    void set_asymmetric_scalling_enable(bool is_enabled);
    bool is_asymmetric_scalling_enabled() const;
    void lock_scalling_mode(bool is_locked);
    bool is_scalling_mode_locked() const;
private:
    int m_last_selected_obejct_idx, m_last_selected_volume_idx;
    bool m_b_asymmetric_scalling{ false };
    bool m_b_scalling_mode_locked{ false };
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoScale_hpp_
