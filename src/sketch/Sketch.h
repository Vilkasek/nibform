#pragma once

#include "sketch/Constraint.h"

#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <optional>
#include <set>
#include <vector>

struct SketchPoint
{
    PointId id = kNoId;
    gp_Pnt2d pos;
};

enum class EntityKind { Line, Circle, Arc };

// Geometry references points by id, so connected curves share their endpoints.
//   Line:   a = start, b = end
//   Circle: c = center, radius
//   Arc:    c = center, a = start, b = end, counter-clockwise from a to b;
//           `radius` mirrors |c - a| (the solver keeps |c - b| equal to it)
struct SketchEntity
{
    EntityId id = kNoId;
    EntityKind kind = EntityKind::Line;
    PointId a = kNoId;
    PointId b = kNoId;
    PointId c = kNoId;
    double radius = 0.0;
    // Construction (reference) geometry: drawn dashed, snapped to and constrained like
    // any curve, but it never bounds a region.
    bool construction = false;
};

// A 2D sketch living on a plane in 3D. Plain value type: copying it is how undo works.
class Sketch
{
public:
    // Everything but the plane, as stored in project files.
    struct Contents
    {
        std::vector<SketchPoint> points;
        std::vector<SketchEntity> entities;
        std::vector<SketchConstraint> constraints;
        int nextId = 1;
    };

    explicit Sketch(const gp_Ax3& plane = gp_Ax3());
    Sketch(const gp_Ax3& plane, Contents contents);
    Contents contents() const { return {m_points, m_entities, m_constraints, m_nextId}; }

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

    // Returns the id of an existing identical relation instead of adding a duplicate.
    ConstraintId addConstraint(SketchConstraint c);
    void removeConstraints(const std::set<ConstraintId>& ids);
    SketchConstraint* constraint(ConstraintId id);
    const SketchConstraint* constraint(ConstraintId id) const;

    // Removes the entities, constraints that refer to them and any points nothing references anymore.
    void removeEntities(const std::set<EntityId>& ids);
    // Replaces `drop` with `keep` everywhere; false (and no change) if that would collapse an entity.
    bool mergePoints(PointId keep, PointId drop);
    void clear();

    void setPos(PointId id, const gp_Pnt2d& pos);
    void setRadius(EntityId id, double radius);
    void setConstruction(EntityId id, bool construction);

    const std::vector<SketchPoint>& points() const { return m_points; }
    const std::vector<SketchEntity>& entities() const { return m_entities; }
    const std::vector<SketchConstraint>& constraints() const { return m_constraints; }
    const SketchPoint* point(PointId id) const;
    const SketchEntity* entity(EntityId id) const;
    SketchEntity* entity(EntityId id);
    gp_Pnt2d pos(PointId id) const;
    bool isEmpty() const { return m_entities.empty() && m_points.empty(); }
    // Entities that use `point` as an endpoint or center.
    std::vector<EntityId> entitiesAt(PointId point) const;

    double distanceTo(const SketchEntity& e, const gp_Pnt2d& p) const;
    // Nearest entity within `tolerance` of `p`.
    std::optional<EntityId> hitTest(const gp_Pnt2d& p, double tolerance) const;

private:
    const SketchConstraint* findDuplicate(const SketchConstraint& c) const;

    gp_Ax3 m_plane;
    std::vector<SketchPoint> m_points;
    std::vector<SketchEntity> m_entities;
    std::vector<SketchConstraint> m_constraints;
    int m_nextId = 1;
};

// Circle through three points; empty when they are (nearly) collinear.
std::optional<gp_Pnt2d> circumcenter(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& c);

// True when `p` lies on the counter-clockwise arc from `start` to `end` around `center`.
bool isOnCcwArc(const gp_Pnt2d& center, const gp_Pnt2d& start, const gp_Pnt2d& end, const gp_Pnt2d& p);
