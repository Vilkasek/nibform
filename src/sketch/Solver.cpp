#include "sketch/Solver.h"

#include <Eigen/Dense>
#include <Eigen/SVD>

#include <cmath>
#include <functional>

namespace {

using Eigen::MatrixXd;
using Eigen::Vector2d;
using Eigen::VectorXd;

constexpr double kTolerance = 1e-9;     // max residual, mm or radians
constexpr double kRankTolerance = 1e-8; // relative singular value cut-off
constexpr double kNullTolerance = 1e-6; // a variable is defined when the null space ignores it
constexpr int kMaxIterations = 60;

double wrapAngle(double a)
{
    while (a > M_PI)
        a -= 2.0 * M_PI;
    while (a <= -M_PI)
        a += 2.0 * M_PI;
    return a;
}

double cross(const Vector2d& a, const Vector2d& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

// A residual f(x) = 0 that depends only on `vars`.
struct Equation
{
    std::vector<int> vars;
    std::function<double(const VectorXd&)> f;
};

// The sketch flattened into a vector of unknowns: x, y per point, radius per circle.
class System
{
public:
    explicit System(const Sketch& sketch)
        : m_sketch(sketch)
    {
        std::vector<double> values;
        for (const SketchPoint& p : sketch.points()) {
            m_pointVar[p.id] = static_cast<int>(values.size());
            values.push_back(p.pos.X());
            values.push_back(p.pos.Y());
        }
        for (const SketchEntity& e : sketch.entities()) {
            if (e.kind == EntityKind::Circle) {
                m_radiusVar[e.id] = static_cast<int>(values.size());
                values.push_back(e.radius);
            }
        }
        x = Eigen::Map<VectorXd>(values.data(), static_cast<Eigen::Index>(values.size()));

        for (const SketchEntity& e : sketch.entities()) {
            if (e.kind == EntityKind::Arc) {
                // Both ends stay on the arc's circle.
                const P c = pt(e.c), a = pt(e.a), b = pt(e.b);
                add({c.i, c.i + 1, a.i, a.i + 1, b.i, b.i + 1},
                    [=](const VectorXd& v) { return (a(v) - c(v)).norm() - (b(v) - c(v)).norm(); });
            }
        }
        for (const SketchConstraint& c : sketch.constraints())
            addConstraint(c);
    }

    VectorXd x;
    std::vector<Equation> equations;

    int pointVar(PointId id) const
    {
        auto it = m_pointVar.find(id);
        return it == m_pointVar.end() ? -1 : it->second;
    }
    int radiusVar(EntityId id) const
    {
        auto it = m_radiusVar.find(id);
        return it == m_radiusVar.end() ? -1 : it->second;
    }

    VectorXd residuals(const VectorXd& v) const
    {
        VectorXd r(static_cast<Eigen::Index>(equations.size()));
        for (std::size_t i = 0; i < equations.size(); ++i)
            r[static_cast<Eigen::Index>(i)] = equations[i].f(v);
        return r;
    }

    // Central differences, only over the few variables each equation touches.
    // `column` maps a variable to its Jacobian column, -1 when it is held fixed.
    MatrixXd jacobian(const VectorXd& v, const std::vector<int>& column, int columns) const
    {
        MatrixXd j = MatrixXd::Zero(static_cast<Eigen::Index>(equations.size()), columns);
        VectorXd probe = v;
        for (std::size_t row = 0; row < equations.size(); ++row) {
            const Equation& eq = equations[row];
            for (int var : eq.vars) {
                if (column[var] < 0)
                    continue;
                const double h = 1e-7 * std::max(1.0, std::abs(v[var]));
                probe[var] = v[var] + h;
                const double fp = eq.f(probe);
                probe[var] = v[var] - h;
                const double fm = eq.f(probe);
                probe[var] = v[var];
                j(static_cast<Eigen::Index>(row), column[var]) += (fp - fm) / (2.0 * h);
            }
        }
        return j;
    }

    void writeBack(Sketch& sketch, const VectorXd& v) const
    {
        for (const auto& [id, i] : m_pointVar)
            sketch.setPos(id, gp_Pnt2d(v[i], v[i + 1]));
        for (const auto& [id, i] : m_radiusVar)
            sketch.setRadius(id, std::abs(v[i]));
        for (const SketchEntity& e : sketch.entities()) {
            if (e.kind == EntityKind::Arc)
                sketch.setRadius(e.id, sketch.pos(e.c).Distance(sketch.pos(e.a)));
        }
    }

private:
    struct P
    {
        int i;
        Vector2d operator()(const VectorXd& v) const { return {v[i], v[i + 1]}; }
    };
    // Radius of a circle (its own variable) or an arc (|start - center|).
    struct R
    {
        int r = -1;
        int c = -1;
        int a = -1;
        double operator()(const VectorXd& v) const
        {
            if (r >= 0)
                return v[r];
            return Vector2d(v[a] - v[c], v[a + 1] - v[c + 1]).norm();
        }
        std::vector<int> vars() const
        {
            if (r >= 0)
                return {r};
            return {c, c + 1, a, a + 1};
        }
    };
    struct L
    {
        P a, b;
        Vector2d dir(const VectorXd& v) const { return b(v) - a(v); }
        std::vector<int> vars() const { return {a.i, a.i + 1, b.i, b.i + 1}; }
    };

    P pt(PointId id) const { return {pointVar(id)}; }
    L line(EntityId id) const
    {
        const SketchEntity* e = m_sketch.entity(id);
        return {pt(e->a), pt(e->b)};
    }
    R radius(EntityId id) const
    {
        const SketchEntity* e = m_sketch.entity(id);
        if (e->kind == EntityKind::Circle)
            return {radiusVar(id)};
        return {-1, pointVar(e->c), pointVar(e->a)};
    }
    P center(EntityId id) const { return pt(m_sketch.entity(id)->c); }
    bool isLine(EntityId id) const
    {
        const SketchEntity* e = m_sketch.entity(id);
        return e && e->kind == EntityKind::Line;
    }

    void add(std::vector<int> vars, std::function<double(const VectorXd&)> f)
    {
        equations.push_back({std::move(vars), std::move(f)});
    }

    static std::vector<int> join(std::initializer_list<std::vector<int>> parts)
    {
        std::vector<int> out;
        for (const auto& p : parts)
            out.insert(out.end(), p.begin(), p.end());
        return out;
    }

    void addConstraint(const SketchConstraint& c)
    {
        // Skip anything that refers to geometry which no longer exists.
        for (PointId p : {c.p1, c.p2}) {
            if (p != kNoId && pointVar(p) < 0)
                return;
        }
        for (EntityId e : {c.e1, c.e2}) {
            if (e != kNoId && !m_sketch.entity(e))
                return;
        }

        switch (c.kind) {
        case ConstraintKind::Horizontal: {
            const P a = pt(c.p1), b = pt(c.p2);
            add({a.i + 1, b.i + 1}, [=](const VectorXd& v) { return v[b.i + 1] - v[a.i + 1]; });
            break;
        }
        case ConstraintKind::Vertical: {
            const P a = pt(c.p1), b = pt(c.p2);
            add({a.i, b.i}, [=](const VectorXd& v) { return v[b.i] - v[a.i]; });
            break;
        }
        case ConstraintKind::Parallel:
        case ConstraintKind::Perpendicular: {
            const L l1 = line(c.e1), l2 = line(c.e2);
            const bool parallel = c.kind == ConstraintKind::Parallel;
            add(join({l1.vars(), l2.vars()}), [=](const VectorXd& v) {
                const Vector2d d1 = l1.dir(v), d2 = l2.dir(v);
                const double n = d1.norm() * d2.norm() + 1e-300;
                return (parallel ? cross(d1, d2) : d1.dot(d2)) / n;
            });
            break;
        }
        case ConstraintKind::Equal:
            if (isLine(c.e1)) {
                const L l1 = line(c.e1), l2 = line(c.e2);
                add(join({l1.vars(), l2.vars()}),
                    [=](const VectorXd& v) { return l1.dir(v).norm() - l2.dir(v).norm(); });
            } else {
                const R r1 = radius(c.e1), r2 = radius(c.e2);
                add(join({r1.vars(), r2.vars()}), [=](const VectorXd& v) { return r1(v) - r2(v); });
            }
            break;
        case ConstraintKind::Tangent: {
            const P c2 = center(c.e2);
            const R r2 = radius(c.e2);
            const double side = c.value < 0.0 ? -1.0 : 1.0;
            if (isLine(c.e1)) {
                // Signed distance from the center to the line equals the radius.
                const L l = line(c.e1);
                add(join({l.vars(), {c2.i, c2.i + 1}, r2.vars()}), [=](const VectorXd& v) {
                    const Vector2d d = l.dir(v);
                    return cross(d, c2(v) - l.a(v)) / (d.norm() + 1e-300) - side * r2(v);
                });
            } else {
                // Externally (side > 0) or internally touching circles.
                const P c1 = center(c.e1);
                const R r1 = radius(c.e1);
                add(join({{c1.i, c1.i + 1, c2.i, c2.i + 1}, r1.vars(), r2.vars()}), [=](const VectorXd& v) {
                    const double d = (c2(v) - c1(v)).norm();
                    return side > 0.0 ? d - (r1(v) + r2(v)) : d - std::abs(r1(v) - r2(v));
                });
            }
            break;
        }
        case ConstraintKind::Concentric: {
            const P a = center(c.e1), b = center(c.e2);
            add({a.i, b.i}, [=](const VectorXd& v) { return v[b.i] - v[a.i]; });
            add({a.i + 1, b.i + 1}, [=](const VectorXd& v) { return v[b.i + 1] - v[a.i + 1]; });
            break;
        }
        case ConstraintKind::PointOnEntity: {
            const P p = pt(c.p1);
            if (isLine(c.e1)) {
                const L l = line(c.e1);
                add(join({{p.i, p.i + 1}, l.vars()}), [=](const VectorXd& v) {
                    const Vector2d d = l.dir(v);
                    return cross(d, p(v) - l.a(v)) / (d.norm() + 1e-300);
                });
            } else {
                const P o = center(c.e1);
                const R r = radius(c.e1);
                add(join({{p.i, p.i + 1, o.i, o.i + 1}, r.vars()}),
                    [=](const VectorXd& v) { return (p(v) - o(v)).norm() - r(v); });
            }
            break;
        }
        case ConstraintKind::Midpoint: {
            const P p = pt(c.p1);
            const L l = line(c.e1);
            for (int axis : {0, 1}) {
                add({p.i + axis, l.a.i + axis, l.b.i + axis}, [=](const VectorXd& v) {
                    return v[p.i + axis] - 0.5 * (v[l.a.i + axis] + v[l.b.i + axis]);
                });
            }
            break;
        }
        case ConstraintKind::Fixed: {
            const P p = pt(c.p1);
            const double tx = c.target.X(), ty = c.target.Y();
            add({p.i}, [=](const VectorXd& v) { return v[p.i] - tx; });
            add({p.i + 1}, [=](const VectorXd& v) { return v[p.i + 1] - ty; });
            if (c.e1 != kNoId && m_sketch.entity(c.e1)->kind == EntityKind::Circle) {
                const R r = radius(c.e1);
                const double size = c.value;
                add(r.vars(), [=](const VectorXd& v) { return r(v) - size; });
            }
            break;
        }
        case ConstraintKind::Distance: {
            const P a = pt(c.p1), b = pt(c.p2);
            const double target = c.value;
            add({a.i, a.i + 1, b.i, b.i + 1}, [=](const VectorXd& v) { return (b(v) - a(v)).norm() - target; });
            break;
        }
        case ConstraintKind::Radius:
        case ConstraintKind::Diameter: {
            const R r = radius(c.e1);
            const double target = c.kind == ConstraintKind::Diameter ? c.value / 2.0 : c.value;
            add(r.vars(), [=](const VectorXd& v) { return r(v) - target; });
            break;
        }
        case ConstraintKind::PointLineDistance:
        case ConstraintKind::LineDistance: {
            const L l = line(c.e1);
            const double target = c.value;
            if (c.kind == ConstraintKind::PointLineDistance) {
                const P p = pt(c.p1);
                add(join({{p.i, p.i + 1}, l.vars()}), [=](const VectorXd& v) {
                    const Vector2d d = l.dir(v);
                    return cross(d, p(v) - l.a(v)) / (d.norm() + 1e-300) - target;
                });
            } else {
                const L other = line(c.e2);
                add(join({other.vars(), l.vars()}), [=](const VectorXd& v) {
                    const Vector2d d = l.dir(v);
                    const Vector2d mid = 0.5 * (other.a(v) + other.b(v));
                    return cross(d, mid - l.a(v)) / (d.norm() + 1e-300) - target;
                });
            }
            break;
        }
        case ConstraintKind::Angle: {
            const SketchEntity* e1 = m_sketch.entity(c.e1);
            const SketchEntity* e2 = m_sketch.entity(c.e2);
            // Rays leave the shared point, so the angle reads like the corner it describes.
            auto ray = [&](const SketchEntity* e) {
                const bool flip = e->b == c.p1;
                return flip ? L{pt(e->b), pt(e->a)} : L{pt(e->a), pt(e->b)};
            };
            const L l1 = ray(e1), l2 = ray(e2);
            const double target = c.value;
            add(join({l1.vars(), l2.vars()}), [=](const VectorXd& v) {
                const Vector2d d1 = l1.dir(v), d2 = l2.dir(v);
                return wrapAngle(std::atan2(cross(d1, d2), d1.dot(d2)) - target);
            });
            break;
        }
        }
    }

    const Sketch& m_sketch;
    std::map<PointId, int> m_pointVar;
    std::map<EntityId, int> m_radiusVar;
};

// Gauss-Newton with minimum-norm steps: among all corrections that fix the
// residuals, take the smallest, so unconstrained geometry stays where it is.
bool converge(const System& sys, VectorXd& x, const std::vector<bool>& held)
{
    std::vector<int> column(static_cast<std::size_t>(x.size()), -1);
    int columns = 0;
    for (Eigen::Index i = 0; i < x.size(); ++i) {
        if (!held[static_cast<std::size_t>(i)])
            column[static_cast<std::size_t>(i)] = columns++;
    }

    VectorXd f = sys.residuals(x);
    for (int iter = 0; iter < kMaxIterations; ++iter) {
        if (f.size() == 0 || f.lpNorm<Eigen::Infinity>() < kTolerance)
            return true;

        const MatrixXd j = sys.jacobian(x, column, columns);
        const VectorXd step = j.completeOrthogonalDecomposition().solve(-f);

        // Backtrack until the residual actually shrinks.
        const double f0 = f.squaredNorm();
        bool improved = false;
        for (double alpha = 1.0; alpha > 1e-4; alpha *= 0.5) {
            VectorXd trial = x;
            for (Eigen::Index i = 0; i < x.size(); ++i) {
                const int col = column[static_cast<std::size_t>(i)];
                if (col >= 0)
                    trial[i] += alpha * step[col];
            }
            VectorXd ft = sys.residuals(trial);
            if (ft.squaredNorm() < f0) {
                x = std::move(trial);
                f = std::move(ft);
                improved = true;
                break;
            }
        }
        if (!improved)
            break;
    }
    return f.size() == 0 || f.lpNorm<Eigen::Infinity>() < kTolerance;
}

void analyzeAt(const Sketch& sketch, const System& sys, const VectorXd& x, SolveResult& out)
{
    const int n = static_cast<int>(x.size());
    out.variables = n;
    out.equations = static_cast<int>(sys.equations.size());
    if (n == 0)
        return;

    std::vector<int> column(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        column[static_cast<std::size_t>(i)] = i;
    const MatrixXd j = sys.jacobian(x, column, n);

    int rank = 0;
    MatrixXd nullSpace;
    if (j.rows() == 0) {
        nullSpace = MatrixXd::Identity(n, n);
    } else {
        Eigen::BDCSVD<MatrixXd, Eigen::ComputeFullV> svd(j);
        const VectorXd& s = svd.singularValues();
        const double cutoff = kRankTolerance * std::max(1.0, s.size() ? s[0] : 0.0);
        for (Eigen::Index i = 0; i < s.size(); ++i) {
            if (s[i] > cutoff)
                ++rank;
        }
        nullSpace = svd.matrixV().rightCols(n - rank);
    }
    out.dof = n - rank;

    // A variable is pinned down when no remaining freedom can change it.
    auto defined = [&](int var) { return var >= 0 && (nullSpace.cols() == 0 || nullSpace.row(var).norm() < kNullTolerance); };
    for (const SketchPoint& p : sketch.points()) {
        const int i = sys.pointVar(p.id);
        if (defined(i) && defined(i + 1))
            out.definedPoints.insert(p.id);
    }
    for (const SketchEntity& e : sketch.entities()) {
        bool all = true;
        for (PointId p : {e.a, e.b, e.c}) {
            if (p != kNoId && !out.definedPoints.count(p))
                all = false;
        }
        if (e.kind == EntityKind::Circle && !defined(sys.radiusVar(e.id)))
            all = false;
        if (all)
            out.definedEntities.insert(e.id);
    }
}

} // namespace

SolveResult solve(Sketch& sketch, const std::map<PointId, gp_Pnt2d>& drag, bool analyze)
{
    const System sys(sketch);
    const std::size_t n = static_cast<std::size_t>(sys.x.size());

    VectorXd dragged = sys.x;
    std::vector<bool> held(n, false);
    for (const auto& [id, target] : drag) {
        const int i = sys.pointVar(id);
        if (i < 0)
            continue;
        dragged[i] = target.X();
        dragged[i + 1] = target.Y();
        held[static_cast<std::size_t>(i)] = held[static_cast<std::size_t>(i) + 1] = true;
    }

    // First try to put dragged points exactly under the pointer; if the constraints
    // forbid that, start from there anyway and let everything settle nearby.
    VectorXd x = dragged;
    bool ok = !drag.empty() && converge(sys, x, held);
    if (!ok) {
        x = dragged;
        ok = converge(sys, x, std::vector<bool>(n, false));
    }

    SolveResult result;
    result.ok = ok;
    if (ok)
        sys.writeBack(sketch, x);
    if (analyze)
        analyzeAt(sketch, sys, ok ? x : sys.x, result);
    return result;
}

SolveResult analyze(const Sketch& sketch)
{
    const System sys(sketch);
    SolveResult result;
    result.ok = sys.residuals(sys.x).lpNorm<Eigen::Infinity>() < kTolerance * 10.0 || sys.equations.empty();
    analyzeAt(sketch, sys, sys.x, result);
    return result;
}

gp_Vec2d rayDirection(const Sketch& sketch, const SketchEntity& line, PointId pivot)
{
    const gp_Pnt2d a = sketch.pos(line.a);
    const gp_Pnt2d b = sketch.pos(line.b);
    return line.b == pivot ? gp_Vec2d(b, a) : gp_Vec2d(a, b);
}

double signedDistanceToLine(const Sketch& sketch, const SketchEntity& line, const gp_Pnt2d& p)
{
    const gp_Pnt2d a = sketch.pos(line.a);
    const gp_Vec2d d(a, sketch.pos(line.b));
    const double len = d.Magnitude();
    return len > 0.0 ? d.Crossed(gp_Vec2d(a, p)) / len : a.Distance(p);
}

double measure(const Sketch& sketch, const SketchConstraint& c)
{
    switch (c.kind) {
    case ConstraintKind::PointLineDistance:
    case ConstraintKind::LineDistance: {
        const SketchEntity* line = sketch.entity(c.e1);
        if (!line)
            return 0.0;
        gp_Pnt2d p = sketch.pos(c.p1);
        if (c.kind == ConstraintKind::LineDistance) {
            const SketchEntity* other = sketch.entity(c.e2);
            if (!other)
                return 0.0;
            p = gp_Pnt2d((sketch.pos(other->a).XY() + sketch.pos(other->b).XY()) / 2.0);
        }
        return signedDistanceToLine(sketch, *line, p);
    }
    case ConstraintKind::Distance:
        return sketch.pos(c.p1).Distance(sketch.pos(c.p2));
    case ConstraintKind::Radius:
    case ConstraintKind::Diameter: {
        const SketchEntity* e = sketch.entity(c.e1);
        if (!e)
            return 0.0;
        const double r = e->kind == EntityKind::Arc ? sketch.pos(e->c).Distance(sketch.pos(e->a)) : e->radius;
        return c.kind == ConstraintKind::Diameter ? 2.0 * r : r;
    }
    case ConstraintKind::Angle: {
        const SketchEntity* e1 = sketch.entity(c.e1);
        const SketchEntity* e2 = sketch.entity(c.e2);
        if (!e1 || !e2)
            return 0.0;
        const gp_Vec2d d1 = rayDirection(sketch, *e1, c.p1);
        const gp_Vec2d d2 = rayDirection(sketch, *e2, c.p1);
        return std::atan2(d1.Crossed(d2), d1.Dot(d2));
    }
    default:
        return c.value;
    }
}
