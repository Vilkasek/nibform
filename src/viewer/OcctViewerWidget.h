#pragma once

#include "input/PalmRejection.h"
#include "input/TapGesture.h"
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
//   pen            -> active interaction (sketch tools), else AIS selection;
//                     a long press opens the radial menu
//   pen barrel btn -> orbit; a click opens the radial menu
//   1 finger       -> orbit
//   2 fingers      -> pan + pinch zoom; a quick tap undoes (3 fingers redo)
//   mouse          -> LMB select, RMB orbit (a click: radial menu), MMB / Shift+RMB pan,
//                     wheel zoom
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
    // A long press of the pen tip, or a click of its barrel button / the right mouse
    // button, at `pos` (widget coordinates): the radial menu goes there.
    void radialMenuRequested(const QPointF& pos);
    // The pen that opened the menu with a long press is still down: where it is now,
    // and whether it just lifted (releasing over an item picks it).
    void radialMenuTrack(const QPointF& pos, bool lifted);
    // A quick tap of `fingers` (2 or 3) fingers on the touch screen.
    void fingerTap(int fingers);

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

    // A press that stays put this long opens the radial menu instead.
    static constexpr int kLongPressMs = 500;
    // How far the pointer may drift and still be holding / clicking (the editor's drag threshold).
    static constexpr double kHoldSlopPx = 6.0;
    static constexpr qint64 kClickMs = 600;
    void longPress();
    // Right button / barrel button: a click (no drag, quick) opens the radial menu.
    void secondaryDown(const QPointF& pos);
    void secondaryMove(const QPointF& pos);
    void secondaryUp(const QPointF& pos);

    QTimer m_longPress;
    QPointF m_longPressAt;
    bool m_longPressed = false; // the menu is open; the rest of this press goes to it
    bool m_secondaryDown = false;
    bool m_secondaryMoved = false;
    QPointF m_secondaryAt;
    qint64 m_secondaryTime = 0;
    TapGesture m_tap;

    PalmRejection m_palm;
    QElapsedTimer m_clock;
    std::set<int> m_touches;         // touch points driving the view
    std::set<int> m_rejectedTouches; // touch points ignored until they lift

    DragGesture m_dragGesture = DragGesture::None;
    Graphic3d_Vec2d m_dragPoint; // virtual cursor for orbit gestures
    QTimer m_dragEndTimer;       // touchpad scroll has no reliable end phase under XWayland
};
