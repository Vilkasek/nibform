#pragma once

#include "input/PalmRejection.h"
#include "viewer/PenSample.h"
#include "viewer/ViewerInteraction.h"

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QTimer>

#include <set>

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <Bnd_Box.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

class AIS_ViewCube;
class QNativeGestureEvent;
class QTouchEvent;

// 3D viewport: renders OCCT into a QOpenGLWidget and maps input the way a
// pen-first CAD expects it:
//   pen            -> active interaction (sketch tools), else AIS selection
//   pen barrel btn -> orbit
//   1 finger       -> orbit
//   2 fingers      -> pan + pinch zoom
//   mouse          -> LMB select, RMB orbit, MMB / Shift+RMB pan, wheel zoom
//   touchpad       -> 2-finger scroll orbit, Shift+scroll pan, pinch zoom,
//                     3-finger swipe pan (TrackPoint scroll behaves the same)
class OcctViewerWidget : public QOpenGLWidget, public AIS_ViewController
{
    Q_OBJECT

public:
    explicit OcctViewerWidget(QWidget* parent = nullptr);
    ~OcctViewerWidget() override;

    const Handle(V3d_Viewer)& viewer() const { return m_viewer; }
    const Handle(V3d_View)& view() const { return m_view; }
    const Handle(AIS_InteractiveContext)& aisContext() const { return m_context; }

    void fitAll();
    // Background gradient and grid colors for a dark or light UI.
    void setDark(bool dark);
    // A standard view, framing the whole scene; glides there unless `animate` is false.
    void setProjection(V3d_TypeOfOrientation orientation, bool animate = true);
    // Looks straight at a plane: from the side `normal` points to, `up` pointing up.
    void lookAt(const gp_Dir& normal, const gp_Dir& up);
    // Glides the camera (in a fraction of a second) to look from the `eyeSide` direction,
    // `up` pointing up, framing `box` (the whole scene when the box is void).
    void animateCamera(const gp_Dir& eyeSide, const gp_Dir& up, const Bnd_Box& box);
    // Frames the whole scene again from the same direction (after the window changed shape).
    void refit();
    // Where the camera looks from (unit vector from the scene toward the eye).
    gp_Dir eyeSide() const;
    void releaseGraphics();

    // Routes the primary pointer (pen tip, left mouse button) to `interaction`
    // before AIS selection; navigation input is unaffected. Not owned.
    void setInteraction(ViewerInteraction* interaction) { m_interaction = interaction; }
    // Ignore touches while the pen is in use (on by default).
    void setPalmRejection(bool on) { m_palm.setEnabled(on); }

    // Where a model point appears on screen, in logical widget pixels.
    QPointF project(const gp_Pnt& p) const;
    // Called by the overlay that sits on top of the GL surface.
    void paintOverlay(QPainter& painter);

    QSize minimumSizeHint() const override { return {200, 200}; }
    QSize sizeHint() const override { return {1280, 800}; }

signals:
    void glReady(const QString& info);
    void glFailed(const QString& reason);
    void penInput(const PenSample& sample);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override; // pen proximity
    void initializeGL() override;
    void paintGL() override;

    bool event(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

    void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                          const Handle(V3d_View)& view) override;

private:
    Aspect_Drawable nativeDrawable() const;
    Graphic3d_Vec2i toPixels(const QPointF& p) const;
    PointerEvent makePointer(PointerEvent::Source source, const QPointF& pos, double pressure,
                             Qt::KeyboardModifiers modifiers) const;
    Aspect_VKeyMouse navigationButtons(Qt::MouseButtons buttons) const;
    // True when the view cube is under `pixel`; its clicks go to AIS, not the interaction.
    bool isViewCubeAt(const Graphic3d_Vec2i& pixel);
    bool handleTouch(QTouchEvent* e);
    void penActivity(); // the pen is around: palms are ignored, touches in progress dropped
    bool handleNativeGesture(QNativeGestureEvent* e);

    enum class DragGesture { None, Orbit, Pan };
    // Feeds a relative, button-less gesture (touchpad scroll, swipe) into AIS
    // as if it were a mouse drag that started at `cursor`.
    void continuousDrag(DragGesture mode, const Graphic3d_Vec2i& cursor, const Graphic3d_Vec2d& delta);
    void updateView();

    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(AIS_InteractiveContext) m_context;
    Handle(AIS_ViewCube) m_viewCube;
    bool m_coreProfile = true;
    bool m_penDown = false;
    ViewerInteraction* m_interaction = nullptr;
    QWidget* m_overlay = nullptr;
    bool m_primaryGrab = false; // interaction owns the current primary press
    bool m_debugInput = false;
    bool m_invertScroll = false;

    PalmRejection m_palm;
    QElapsedTimer m_clock;
    std::set<int> m_touches;         // touch points driving the view
    std::set<int> m_rejectedTouches; // touch points ignored until they lift

    DragGesture m_dragGesture = DragGesture::None;
    Graphic3d_Vec2d m_dragPoint; // virtual cursor for orbit gestures
    QTimer m_dragEndTimer;       // touchpad scroll has no reliable end phase under XWayland
};
