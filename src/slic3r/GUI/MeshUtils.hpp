#ifndef slic3r_MeshUtils_hpp_
#define slic3r_MeshUtils_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "admesh/stl.h"
#include "libslic3r/AnyPtr.hpp"
#include "slic3r/GUI/GLModel.hpp"

#include <cfloat>

namespace Slic3r {

namespace GUI {

struct Camera;


// lm_FIXME: Following class might possibly be replaced by Eigen::Hyperplane
class ClippingPlane
{
    double m_data[4];

public:
    ClippingPlane()
    {
        *this = ClipsNothing();
    }

    ClippingPlane(const Vec3d& direction, double offset)
    {
        set_normal(direction);
        set_offset(offset);
    }

    bool operator==(const ClippingPlane& cp) const {
        return m_data[0]==cp.m_data[0] && m_data[1]==cp.m_data[1] && m_data[2]==cp.m_data[2] && m_data[3]==cp.m_data[3];
    }
    bool operator!=(const ClippingPlane& cp) const { return ! (*this==cp); }

    double distance(const Vec3d& pt) const {
        // FIXME: this fails: assert(is_approx(get_normal().norm(), 1.));
        return (-get_normal().dot(pt) + m_data[3]);
    }

    bool is_point_clipped(const Vec3d& point) const { return distance(point) < 0.; }
    void set_normal(const Vec3d& normal)
    {
        const Vec3d norm_dir = normal.normalized();
        m_data[0] = norm_dir.x();
        m_data[1] = norm_dir.y();
        m_data[2] = norm_dir.z();
    }
    void set_offset(double offset) { m_data[3] = offset; }
    double get_offset() const { return m_data[3]; }
    Vec3d  get_normal() const { return Vec3d(m_data[0], m_data[1], m_data[2]); }
    void   invert_normal(){
        m_data[0] *= -1.0;
        m_data[1] *= -1.0;
        m_data[2] *= -1.0;
    }
    ClippingPlane    inverted_normal() const { return ClippingPlane(-get_normal(), get_offset()); }
    bool is_active() const { return m_data[3] != DBL_MAX; }
    static ClippingPlane ClipsNothing() { return ClippingPlane(Vec3d(0., 0., 1.), DBL_MAX); }
    const double* get_data() const { return m_data; }

    // Serialization through cereal library
    template <class Archive>
    void serialize( Archive & ar )
    {
        ar( m_data[0], m_data[1], m_data[2], m_data[3] );
    }
};


// MeshClipper class cuts a mesh and is able to return a triangulated cut.
class MeshClipper {
public:
    ~MeshClipper();
    // Set whether the cut should be triangulated and whether a cut
    // contour should be calculated and shown.
    void set_behaviour(bool fill_cut, double contour_width);
    // Inform MeshClipper about which plane we want to use to cut the mesh
    // This is supposed to be in world coordinates.
    void set_plane(const ClippingPlane& plane);

    // In case the object is clipped by two planes (e.g. in case of sinking
    // objects), this will be used to clip the triagnulated cut.
    // Pass ClippingPlane::ClipsNothing to turn this off.
    void set_limiting_plane(const ClippingPlane& plane);

    // Which mesh to cut. MeshClipper remembers const * to it, caller
    // must make sure that it stays valid.
    void set_mesh(const indexed_triangle_set &mesh);
    void set_mesh(AnyPtr<const indexed_triangle_set> &&ptr);

    void set_negative_mesh(const indexed_triangle_set &mesh);
    void set_negative_mesh(AnyPtr<const indexed_triangle_set> &&ptr);

    // Inform the MeshClipper about the transformation that transforms the mesh
    // into world coordinates.
    void set_transformation(const Geometry::Transformation& trafo);

    // Render the triangulated cut. Transformation matrices should
    // be set in world coords.
    void render_cut(const ColorRGBA &color, const std::vector<size_t> *ignore_idxs = nullptr);
    void render_contour(const ColorRGBA &color, const std::vector<size_t> *ignore_idxs = nullptr);

    int                is_projection_inside_cut(const Vec3d &point) const;
    bool               has_valid_contour() const;
    int                get_number_of_contours() const { return m_result ? m_result->cut_islands.size() : 0; }
    std::vector<Vec3d> point_per_contour() const;

private:
    void recalculate_triangles();
    void reset();
    Geometry::Transformation  m_trafo;
    AnyPtr<const indexed_triangle_set> m_mesh;
    AnyPtr<const indexed_triangle_set> m_negative_mesh;

    ClippingPlane m_plane;
    ClippingPlane m_limiting_plane = ClippingPlane::ClipsNothing();
    /*std::vector<Vec2f> m_triangles2d;
    GLIndexedVertexArray m_vertex_array;
    bool m_triangles_valid = false;*/

    struct CutIsland
    {
        ExPolygon   expoly;
        BoundingBox expoly_bb;
        GLModel     model;
        GLModel     model_expanded;
        bool        disabled = false;
        size_t      hash;
    };
    struct ClipResult
    {
        std::vector<CutIsland*> cut_islands;
        Transform3d            trafo; // this rotates the cut into world coords
    };
    std::optional<ClipResult> m_result; // the cut plane
    bool                      m_fill_cut      = true;
    double                    m_contour_width = 0.;
};



// MeshRaycaster class answers queries such as where on the mesh someone clicked,
// whether certain points are visible or obscured by the mesh etc.
class MeshRaycaster {
public:
    // The class references extern TriangleMesh, which must stay alive
    // during MeshRaycaster existence.
    MeshRaycaster(const TriangleMesh &mesh);

    static void line_from_mouse_pos_static(const Vec2d &mouse_pos, const Transform3d &trafo,
        const Camera &camera, Vec3d &point, Vec3d &direction);

    void line_from_mouse_pos(const Vec2d& mouse_pos, const Transform3d& trafo, const Camera& camera,
                             Vec3d& point, Vec3d& direction) const;

    // Given a mouse position, this returns true in case it is on the mesh.
    bool unproject_on_mesh(
        const Vec2d& mouse_pos,
        const Transform3d& trafo, // how to get the mesh into world coords
        const Camera& camera, // current camera position
        Vec3f& position, // where to save the positibon of the hit (mesh coords)
        Vec3f& normal, // normal of the triangle that was hit
        const ClippingPlane* clipping_plane = nullptr, // clipping plane (if active)
        size_t* facet_idx = nullptr, // index of the facet hit
        bool sinking_limit = true
    ) const;

    const sla::IndexedMesh &get_aabb_mesh() const { return m_emesh; }
    // Given a point and direction in world coords, returns whether the respective line
    // intersects the mesh if it is transformed into world by trafo.
    bool intersects_line(Vec3d point, Vec3d direction, const Transform3d &trafo) const;
    // Given a vector of points in woorld coordinates, this returns vector
    // of indices of points that are visible (i.e. not cut by clipping plane
    // or obscured by part of the mesh.
    std::vector<unsigned> get_unobscured_idxs(
        const Geometry::Transformation& trafo,  // how to get the mesh into world coords
        const Camera& camera,                   // current camera position
        const std::vector<Vec3f>& points,       // points in world coords
        const ClippingPlane* clipping_plane = nullptr // clipping plane (if active)
    ) const;

    // Returns true if the ray, built from mouse position and camera direction, intersects the mesh.
    // In this case, position and normal contain the position and normal, in model coordinates, of the intersection closest to the camera,
    // depending on the position/orientation of the clipping_plane, if specified
    bool closest_hit(const Vec2d &        mouse_pos,
                     const Transform3d &  trafo,                    // how to get the mesh into world coords
                     const Camera &       camera,                   // current camera position
                     Vec3f &              position,                 // where to save the positibon of the hit (mesh coords)
                     Vec3f &              normal,                   // normal of the triangle that was hit
                     const ClippingPlane *clipping_plane = nullptr, // clipping plane (if active)
                     size_t *             facet_idx      = nullptr  // index of the facet hit
    ) const;
    // Given a point in world coords, the method returns closest point on the mesh.
    // The output is in mesh coords.
    // normal* can be used to also get normal of the respective triangle.

    Vec3f get_closest_point(const Vec3f& point, Vec3f* normal = nullptr) const;
    bool  get_closest_point_and_normal(const Vec3f &point, const Vec3f &direction, Vec3f *closest_point, Vec3f *normal, int *face_id = nullptr) const;
    // Given a point in mesh coords, the method returns the closest facet from mesh.
    int get_closest_facet(const Vec3f &point) const;

    Vec3f get_triangle_normal(size_t facet_idx) const;

private:
    sla::IndexedMesh m_emesh;
    std::vector<stl_normal> m_normals;
};

class PickRaycaster
{
public:
    //PickRaycaster(TriangleMesh *mesh) {
    //    mesh_raycaster = std::make_shared<MeshRaycaster>(*mesh);
    //}
    /*PickRaycaster(TriangleMesh *mesh, const Transform3d &tran) : PickRaycaster(mesh) {
        set_transform(tran);
    }*/
    PickRaycaster(TriangleMesh *mesh,  int _id)
    {
        mesh_raycaster = std::make_shared<MeshRaycaster>(*mesh);
        m_id = _id;
    }
    PickRaycaster(TriangleMesh *mesh, int _id, const Transform3d &tran)
    {
        mesh_raycaster = std::make_shared<MeshRaycaster>(*mesh);
        set_transform(tran);
        m_id = _id;
    }
    void set_transform(const Transform3d &tran) {
        world_tran.set_from_transform(tran);
    }

    std::shared_ptr<MeshRaycaster> mesh_raycaster{nullptr};
    Geometry::Transformation       world_tran;

    bool is_active() const { return m_active; }
    void set_active(bool active) { m_active = active; }
    int  get_id() { return m_id; }
private:
    bool  m_active{true};
    int   m_id{-1};
};

} // namespace GUI
} // namespace Slic3r


#endif // slic3r_MeshUtils_hpp_
