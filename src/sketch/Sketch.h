#pragma once

#include "sketch/Constraint.h"

#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct SketchPoint
{
    PointId id = kNoId;
    gp_Pnt2d pos;
};

enum class EntityKind { Line, Circle, Arc, Ellipse, Spline };

// Geometry references points by id, so connected curves share their endpoints.
//   Line:    a = start, b = end
//   Circle:  c = center, radius
//   Arc:     c = center, a = start, b = end, counter-clockwise from a to b;
//            `radius` mirrors |c - a| (the solver keeps |c - b| equal to it)
//   Ellipse: c = center, a and b = the ends of its two half-axes (the solver keeps
//            them perpendicular); whichever is longer is the major one
//   Spline:  `through` = the points it passes through, in order. Open: a and b are the
//            first and last of them; closed (periodic): a = b = kNoId
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
    std::vector<PointId> through; // Spline only
    bool closed = false;          // Spline only

    // Every point the curve is defined by: its ends, center and through points.
    std::vector<PointId> pointIds() const;
    bool usesPoint(PointId p) const;
    // Whether constraints and trim / offset know its shape (lines, circles and arcs).
    bool isClassic() const { return kind == EntityKind::Line || kind == EntityKind::Circle || kind == EntityKind::Arc; }
};

// A picture on the sketch plane to trace over. The file's bytes are kept as they were
// read (PNG, JPEG, ...) and shared between copies, so undo steps cost nothing.
struct ReferenceImage
{
    std::shared_ptr<const std::string> data;
    int pixelWidth = 0;
    int pixelHeight = 0;
    gp_Pnt2d corner;      // lower left, in sketch coordinates
    double width = 100.0; // in sketch units; the height follows the picture's proportions
    double opacity = 0.5;

    double height() const { return pixelWidth > 0 ? width * pixelHeight / pixelWidth : width; }
    bool operator==(const ReferenceImage& o) const
    {
        return data == o.data && pixelWidth == o.pixelWidth && pixelHeight == o.pixelHeight
            && corner.IsEqual(o.corner, 0.0) && width == o.width && opacity == o.opacity;
    }
};

// Text on the sketch plane, in a system font: its letters' outlines bound regions like
// curves do (to extrude, emboss or engrave). It starts at point `at` (the left end of
// its baseline), which moves, snaps and takes dimensions like any point. `height` is the
// font's size; `font` a family name (empty: the default sans-serif).
struct SketchText
{
    EntityId id = kNoId;
    PointId at = kNoId;
    std::string text;
    std::string font;
    double height = 10.0;
    bool bold = false;
    bool italic = false;
    bool operator==(const SketchText&) const = default;
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
        std::optional<ReferenceImage> image;
        std::vector<SketchText> texts;
    };

    explicit Sketch(const gp_Ax3& plane = gp_Ax3());
    Sketch(const gp_Ax3& plane, Contents contents);
    Contents contents() const { return {m_points, m_entities, m_constraints, m_nextId, m_image, m_texts}; }

    const gp_Ax3& plane() const { return m_plane; }
    void setPlane(const gp_Ax3& plane) { m_plane = plane; }
    gp_Pnt to3d(const gp_Pnt2d& p) const;
    gp_Pnt2d to2d(const gp_Pnt& p) const;

    PointId addPoint(const gp_Pnt2d& pos);
    // Returns an existing point at `pos` (within `eps`) or creates one.
    PointId pointAt(const gp_Pnt2d& pos, double eps = 1e-7);
    std::optional<PointId> findPoint(const gp_Pnt2d& pos, double eps) const;

    EntityId addLine(PointId start, PointId end);
    EntityId addCircle(PointId center, double radius);
    EntityId addArc(PointId center, PointId start, PointId end);
    // `axisA` and `axisB` end the two half-axes (perpendicular around `center`).
    EntityId addEllipse(PointId center, PointId axisA, PointId axisB);
    // Through `points` in order (2+ open, 3+ closed).
    EntityId addSpline(std::vector<PointId> points, bool closed);
    // Text starting at `at` (an existing point).
    EntityId addText(PointId at, std::string text, double height, std::string font = {}, bool bold = false,
                     bool italic = false);
    // Removes texts (and their points, unless something else uses them).
    void removeTexts(const std::set<EntityId>& ids);
    const std::vector<SketchText>& texts() const { return m_texts; }
    const SketchText* text(EntityId id) const;
    SketchText* text(EntityId id);
    // Takes a through point out of a spline that keeps enough of them; false otherwise.
    bool removeThroughPoint(EntityId spline, PointId point);

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
    bool isEmpty() const { return m_entities.empty() && m_points.empty() && !m_image && m_texts.empty(); }

    const std::optional<ReferenceImage>& image() const { return m_image; }
    void setImage(std::optional<ReferenceImage> image) { m_image = std::move(image); }
    // Entities that use `point` as an endpoint or center.
    std::vector<EntityId> entitiesAt(PointId point) const;

    double distanceTo(const SketchEntity& e, const gp_Pnt2d& p) const;
    // The curve as a polyline, fine enough to draw and pick (start to end, counter-
    // clockwise for circles and arcs; closed curves repeat their first point).
    std::vector<gp_Pnt2d> polyline(const SketchEntity& e) const;
    // Nearest entity within `tolerance` of `p`.
    std::optional<EntityId> hitTest(const gp_Pnt2d& p, double tolerance) const;

private:
    const SketchConstraint* findDuplicate(const SketchConstraint& c) const;

    gp_Ax3 m_plane;
    std::vector<SketchPoint> m_points;
    std::vector<SketchEntity> m_entities;
    std::vector<SketchConstraint> m_constraints;
    int m_nextId = 1;
    std::optional<ReferenceImage> m_image;
    std::vector<SketchText> m_texts;
};

// Circle through three points; empty when they are (nearly) collinear.
std::optional<gp_Pnt2d> circumcenter(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& c);

// Half-axes of an ellipse entity: center, and the vectors to the ends of its two axes.
struct EllipseAxes
{
    gp_Pnt2d center;
    gp_Vec2d u, v;
    gp_Pnt2d at(double t) const;
};
EllipseAxes ellipseAxes(const Sketch& sketch, const SketchEntity& e);

// Points along the spline through `points` (chord-length parametrized interpolation;
// periodic when `closed`). Falls back to the straight polyline when interpolation fails.
std::vector<gp_Pnt2d> sampleSpline(const std::vector<gp_Pnt2d>& points, bool closed, int perSpan = 24);

// True when `p` lies on the counter-clockwise arc from `start` to `end` around `center`.
bool isOnCcwArc(const gp_Pnt2d& center, const gp_Pnt2d& start, const gp_Pnt2d& end, const gp_Pnt2d& p);
