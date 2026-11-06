#pragma once

#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <optional>
#include <set>
#include <vector>

using PointId = int;
using EntityId = int;
constexpr int kNoId = -1;

struct SketchPoint
{
    PointId id = kNoId;
    gp_Pnt2d pos;
};

enum class EntityKind { Line, Circle, Arc };

// Geometry references points by id, so connected curves share their endpoints.
//   Line:   a = start, b = end
//   Circle: c = center, radius
//   Arc:    c = center, a = start, b = end, counter-clockwise from a to b
struct SketchEntity
{
    EntityId id = kNoId;
    EntityKind kind = EntityKind::Line;
    PointId a = kNoId;
    PointId b = kNoId;
    PointId c = kNoId;
    double radius = 0.0;
};

// A 2D sketch living on a plane in 3D. Plain value type: copying it is how undo works.
class Sketch
{
public:
    explicit Sketch(const gp_Ax3& plane = gp_Ax3());

    const gp_Ax3& plane() const { return m_plane; }
    gp_Pnt to3d(const gp_Pnt2d& p) const;
    gp_Pnt2d to2d(const gp_Pnt& p) const;

    PointId addPoint(const gp_Pnt2d& pos);
    // Returns an existing point at `pos` (within `eps`) or creates one.
    PointId pointAt(const gp_Pnt2d& pos, double eps = 1e-7);
    std::optional<PointId> findPoint(const gp_Pnt2d& pos, double eps) const;

    EntityId addLine(PointId start, PointId end);
    EntityId addCircle(PointId center, double radius);
    EntityId addArc(PointId center, PointId start, PointId end);

    // Removes the entities and any points nothing references anymore.
    void removeEntities(const std::set<EntityId>& ids);
    void clear();

    const std::vector<SketchPoint>& points() const { return m_points; }
    const std::vector<SketchEntity>& entities() const { return m_entities; }
    const SketchPoint* point(PointId id) const;
    const SketchEntity* entity(EntityId id) const;
    gp_Pnt2d pos(PointId id) const;
    bool isEmpty() const { return m_entities.empty() && m_points.empty(); }

    double distanceTo(const SketchEntity& e, const gp_Pnt2d& p) const;
    // Nearest entity within `tolerance` of `p`.
    std::optional<EntityId> hitTest(const gp_Pnt2d& p, double tolerance) const;

private:
    gp_Ax3 m_plane;
    std::vector<SketchPoint> m_points;
    std::vector<SketchEntity> m_entities;
    int m_nextId = 1;
};

// Circle through three points; empty when they are (nearly) collinear.
std::optional<gp_Pnt2d> circumcenter(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& c);

// True when `p` lies on the counter-clockwise arc from `start` to `end` around `center`.
bool isOnCcwArc(const gp_Pnt2d& center, const gp_Pnt2d& start, const gp_Pnt2d& end, const gp_Pnt2d& p);
