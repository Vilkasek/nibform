#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Assemblies (M17): parts placed as components, joined so they move together. They live
// beside the history, not in it: a component shows the shape of its source where the
// joints put it, without rebuilding anything.

// What a component is a copy of: a body of this project (found again after every rebuild
// by a point inside it, so it follows its history) or another project file.
struct ComponentSource
{
    enum class Kind { Body, File };
    Kind kind = Kind::Body;
    int id = 0;
    gp_Pnt inside;     // Body
    std::string path;  // File: the project, absolute
    // File: its shape when last read (BREP text), used while the file can't be read.
    std::shared_ptr<const std::string> cache;
    std::string name;  // shown in the parts list; empty: the body's or the file's name
};

struct Component
{
    int id = 0;
    int source = 0;             // its source's id
    std::vector<double> matrix; // where it sits (a rigid motion, see matrixOf()); empty: as the source
    bool grounded = false;      // stays put; joints move the others
    bool hidden = false;
};

enum class JointKind { Rigid, Revolute, Slider, Cylindrical, Ball, Planar };
constexpr int kJointKinds = 6;

// How many values a joint has, and whether value `i` is an angle (degrees) or a length (mm):
// Revolute: the turn about z; Slider: the slide along z; Cylindrical: both; Ball: turns
// about x, y and z; Planar: slides along x and y and the turn about z.
int degreesOfFreedom(JointKind kind);
bool isAngle(JointKind kind, int value);

// A joint's frame on one component, in that component's own coordinates (as its source
// lies): where the parts meet and the axis they move about or along.
struct JointEnd
{
    int component = 0; // id
    gp_Ax3 frame;
};

// Joins `b` to `a`: their frames meet, z axes facing each other (faces flush; `flip`: the
// same way), and `b` moves relative to `a` as the kind and values say.
struct Joint
{
    int id = 0;
    JointKind kind = JointKind::Rigid;
    JointEnd a, b;
    bool flip = false;
    std::array<double, 3> values{};
    std::array<double, 3> min{}, max{};
    std::array<bool, 3> limited{};
};

struct Assembly
{
    std::vector<ComponentSource> sources;
    std::vector<Component> components;
    std::vector<Joint> joints;
    // The exploded view: how far each component (by id) is moved apart, and whether it shows.
    std::map<int, gp_Vec> explode;
    bool exploded = false;

    bool empty() const { return sources.empty() && components.empty(); }
    const ComponentSource* source(int id) const;
    const Component* component(int id) const;
    int componentIndex(int id) const; // -1 if there is none
};

// The motion a joint's values make, in its frame.
gp_Trsf jointMotion(JointKind kind, const std::array<double, 3>& values);
// A joint's values within its limits.
std::array<double, 3> clampedValues(const Joint& joint);
// The transformation that takes the standard axes to `frame` (local to world).
gp_Trsf frameTrsf(const gp_Ax3& frame);

// Where each component sits (in the order of `components`), as its grounded parts and
// joints say: grounded components (or the first, if none is) stay where their matrix puts
// them, and the joints place the rest from them outward; a component no joint reaches
// stays where its matrix puts it. A joint that would close a loop, or join two grounded
// parts, is skipped and gets an error (one per joint, empty when it applied).
struct AssemblySolution
{
    std::vector<gp_Trsf> placements;
    std::vector<std::string> jointErrors;
    // For each component, the joint (index) that placed it from its parent, and whether
    // it is that joint's `b` (its values move it forward) or `a` (backward); -1: none.
    std::vector<int> placedBy;
    std::vector<bool> placedAsB;
};
AssemblySolution solveAssembly(const Assembly& assembly);

// The joint frame of a picked face, edge or corner of a placed component (world
// coordinates): a flat face's middle and outward normal; a round face's or edge's axis
// (at its middle / center); a straight edge's middle and direction; a corner, z up; a
// sphere's center. Empty for anything else.
std::optional<gp_Ax3> jointFrame(const TopoDS_Shape& picked);
// A world frame in the coordinates of a component placed at `placement`.
gp_Ax3 localFrame(const gp_Ax3& world, const gp_Trsf& placement);

// Sources that are bodies of this project (by source id): the body (index into `bodies`)
// that has the source's point inside it. A source whose body is gone is left out.
std::map<int, int> bodySources(const Assembly& assembly, const std::vector<TopoDS_Shape>& bodies);

// The components' shapes where they sit (shared geometry, only located): `shapes` by
// source id; a hidden component, or one whose source has no shape, gets a null shape.
// With the exploded view on, its offsets are added.
std::vector<TopoDS_Shape> placedShapes(const Assembly& assembly, const std::vector<gp_Trsf>& placements,
                                       const std::map<int, TopoDS_Shape>& shapes);

// The exploded view's offsets (by component id): each part moved away from the one its
// joint holds it to, along the joint's axis, about as far as it is big; parts further
// along a chain add up. Parts no joint places stay.
std::map<int, gp_Vec> explodedOffsets(const Assembly& assembly, const std::map<int, TopoDS_Shape>& shapes);

// Components (indices) whose solids overlap, by how much, and the shared solid.
struct Interference
{
    int a = -1, b = -1;
    double volume = 0.0;
    TopoDS_Shape common;
};
std::vector<Interference> interferences(const std::vector<TopoDS_Shape>& placed, double minVolume = 1e-3);
// The nearest distance between two placed components' shapes (0 when they touch or overlap).
double clearance(const TopoDS_Shape& a, const TopoDS_Shape& b);

// Moves joint `joint` (index) through its range of value `value` in `steps` steps (from
// min to max; without limits, a full turn or ±50 mm) and returns where parts first collide.
struct SweepHit
{
    double at = 0.0;
    Interference what;
};
std::optional<SweepHit> sweepJoint(const Assembly& assembly, const std::map<int, TopoDS_Shape>& shapes, int joint,
                                   int value, int steps = 36);
