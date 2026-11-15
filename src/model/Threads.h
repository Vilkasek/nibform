#pragma once

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>
#include <vector>

// Screw threads: ISO metric (coarse) and Unified (UNC, UNF), on cylinders and in holes.
// A thread is cosmetic (the cylinder stays smooth; it is shown, and called out in
// drawings) or modelled (a helical groove is cut, for 3D printing).

enum class ThreadStandard { Metric, UNC, UNF };

struct ThreadSpec
{
    ThreadStandard standard = ThreadStandard::Metric;
    double diameter = 8.0; // nominal (major), mm
    double pitch = 1.25;   // mm
    double length = 0.0;   // along the cylinder from where it starts; 0: all of it
    bool modelled = false;
};

// A standard size: its name ("M8", "1/4-20"), nominal diameter and pitch (mm), and the
// drill for a tapped hole.
struct ThreadSize
{
    const char* name;
    double diameter;
    double pitch;
    double tapDrill;
};
const std::vector<ThreadSize>& threadSizes(ThreadStandard standard);

// The standard size nearest to `diameter` (length and modelled taken from `like`).
ThreadSpec nearestThread(ThreadStandard standard, double diameter, const ThreadSpec& like = {});
// The thread for a cylinder of `diameter`: a bolt's nominal size (`internal` false), or
// the size whose tap drill a hole of that diameter is.
ThreadSpec threadForCylinder(ThreadStandard standard, double diameter, bool internal, const ThreadSpec& like = {});
// The drill for a tapped hole (from the table for a standard size).
double tapDrill(const ThreadSpec& spec);
// Where a bolt's thread bottoms out (the minor diameter, ISO 68-1).
double minorDiameter(const ThreadSpec& spec);
// "M8×1.25", "1/4-20 UNC" (a size not in the table: "M7.5×1").
std::string threadCallout(const ThreadSpec& spec);

// A thread placed on a cylinder of a body: it starts at `axis.Location()` (on the axis,
// at one end of the cylinder) and runs `length` along `axis.Direction()`.
struct ThreadMark
{
    gp_Ax1 axis;
    double length = 0.0;
    double radius = 0.0; // the cylinder's
    bool internal = false;
    bool openStart = false; // the cylinder ends in air there: the groove runs out past it
    bool openEnd = false;
    ThreadSpec spec;
};

// Whether a round face of `body` is a hole's wall (material around it), not a bolt's.
bool isHoleWall(const TopoDS_Shape& body, const TopoDS_Face& face);

// Places `spec` on a cylindrical face of `body`: it starts at the end of the cylinder
// that is open (a bolt's free end, a hole's mouth; of two, the one nearer `near`).
// Empty with `error` set when the face isn't a cylinder or the thread doesn't fit it.
std::optional<ThreadMark> placeThread(const TopoDS_Shape& body, const TopoDS_Face& face, const ThreadSpec& spec,
                                      const gp_Pnt& near, std::string& error);

// What cutting a modelled thread takes away: the helical groove, one solid per turn
// (a single sweep of many turns trips the booleans up).
TopoDS_Shape threadGroove(const ThreadMark& mark);
// How a cosmetic thread is shown: a surface at its other diameter (a bolt's minor, a
// hole's major one) over its length.
TopoDS_Shape threadSurface(const ThreadMark& mark);

// The thread of `marks` whose cylinder is the circle of `radius` around `center` (a hole
// or bolt seen end-on in a drawing), if any.
const ThreadMark* threadAt(const std::vector<ThreadMark>& marks, const gp_Pnt& center, double radius);
