#include "viewer/OcctViewerWidget.h"

#include <QApplication>
#include <QKeyEvent>
#include <QDebug>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include <AIS_AnimationCamera.hxx>
#include <AIS_ViewCube.hxx>
#include <V3d.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_Grid.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_LineAspect.hxx>

namespace {

// Wraps the FBO that QOpenGLWidget renders into, so OCCT draws straight into it.
class QtFrameBuffer : public OpenGl_FrameBuffer
{
    DEFINE_STANDARD_RTTI_INLINE(QtFrameBuffer, OpenGl_FrameBuffer)

public:
    void BindBuffer(const Handle(OpenGl_Context)& ctx) override
    {
        OpenGl_FrameBuffer::BindBuffer(ctx);
        ctx->SetFrameBufferSRGB(true, false);
    }

    void BindDrawBuffer(const Handle(OpenGl_Context)& ctx) override
    {
        OpenGl_FrameBuffer::BindDrawBuffer(ctx);
        ctx->SetFrameBufferSRGB(true, false);
    }
};

// Transparent widget stacked over the GL surface. Drawing text and dimensions
// with QPainter here keeps them crisp and leaves OCCT's GL state alone.
class ViewerOverlay : public QWidget
{
public:
    explicit ViewerOverlay(OcctViewerWidget* viewer)
        : QWidget(viewer)
        , m_viewer(viewer)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        m_viewer->paintOverlay(painter);
    }

private:
    OcctViewerWidget* m_viewer;
};

// Touchpad scroll units -> screen pixels, and pinch scale delta -> AIS zoom units
// (one wheel notch is 15 units). Tuned by feel; adjust once tested on hardware.
constexpr double kScrollToPixels = 0.25;
constexpr double kPinchToZoom = 100.0;

Aspect_VKeyMouse toVKeyMouse(Qt::MouseButtons buttons)
{
    Aspect_VKeyMouse keys = Aspect_VKeyMouse_NONE;
    if (buttons & Qt::LeftButton)
        keys |= Aspect_VKeyMouse_LeftButton;
    if (buttons & Qt::MiddleButton)
        keys |= Aspect_VKeyMouse_MiddleButton;
    if (buttons & Qt::RightButton)
        keys |= Aspect_VKeyMouse_RightButton;
    return keys;
}

Aspect_VKeyFlags toVKeyFlags(Qt::KeyboardModifiers modifiers)
{
    Aspect_VKeyFlags flags = Aspect_VKeyFlags_NONE;
    if (modifiers & Qt::ShiftModifier)
        flags |= Aspect_VKeyFlags_SHIFT;
    if (modifiers & Qt::ControlModifier)
        flags |= Aspect_VKeyFlags_CTRL;
    if (modifiers & Qt::AltModifier)
        flags |= Aspect_VKeyFlags_ALT;
    return flags;
}

} // namespace

OcctViewerWidget::OcctViewerWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    // Compatibility profile by default: core profiles cap glLineWidth at 1 px on Mesa,
    // which makes sketch lines too thin.
    m_coreProfile = qEnvironmentVariableIntValue("SHAPR_GL_CORE") != 0;
    m_debugInput = qEnvironmentVariableIsSet("SHAPR_DEBUG_INPUT");
    m_invertScroll = qEnvironmentVariableIntValue("SHAPR_INVERT_SCROLL") != 0;

    m_dragEndTimer.setSingleShot(true);
    m_dragEndTimer.setInterval(200);
    connect(&m_dragEndTimer, &QTimer::timeout, this, [this] { m_dragGesture = DragGesture::None; });

    Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display, false);
    // QOpenGLWidget owns the buffers and swaps them itself.
    driver->ChangeOptions().buffersNoSwap = true;
    driver->ChangeOptions().buffersOpaqueAlpha = true;
    driver->ChangeOptions().useSystemBuffer = false;

    m_viewer = new V3d_Viewer(driver);
    m_viewer->SetDefaultLights();
    m_viewer->SetLightOn();
    m_viewer->SetRectangularGridValues(0.0, 0.0, 10.0, 10.0, 0.0);
    m_viewer->SetRectangularGridGraphicValues(200.0, 200.0, 0.0);
    m_viewer->ActivateGrid(Aspect_GT_Rectangular, Aspect_GDM_Lines);

    m_context = new AIS_InteractiveContext(m_viewer);
    // Dark edges on shaded bodies, like Shapr3D.
    const Handle(Prs3d_Drawer)& drawer = m_context->DefaultDrawer();
    drawer->SetFaceBoundaryDraw(true);
    drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(Quantity_Color(0.15, 0.17, 0.20, Quantity_TOC_sRGB),
                                                       Aspect_TOL_SOLID, 1.5));

    m_viewCube = new AIS_ViewCube();
    m_viewCube->SetViewAnimation(myViewAnimation);
    m_viewCube->SetFixedAnimationLoop(false);
    m_viewCube->SetAutoStartAnimation(true);
    m_viewCube->SetDrawAxes(false);
    m_viewCube->SetSize(48.0);
    m_viewCube->SetTransformPersistence(new Graphic3d_TransformPers(
        Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Graphic3d_Vec2i(110, 110)));

    m_view = m_viewer->CreateView();
    m_view->SetImmediateUpdate(false);
    setDark(false);
    m_view->ChangeRenderingParams().NbMsaaSamples = 4;
    m_view->ChangeRenderingParams().IsAntialiasingEnabled = true;

    SetShowRotateCenter(false);

    // Mouse: LMB stays free for selection (and later sketching).
    myMouseGestureMap.Clear();
    myMouseGestureMap.Bind(Aspect_VKeyMouse_RightButton, AIS_MouseGesture_RotateOrbit);
    myMouseGestureMap.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
    myMouseGestureMap.Bind(unsigned(Aspect_VKeyMouse_RightButton) | Aspect_VKeyFlags_SHIFT, AIS_MouseGesture_Pan);
    myMouseGestureMap.Bind(unsigned(Aspect_VKeyMouse_RightButton) | Aspect_VKeyFlags_CTRL, AIS_MouseGesture_Zoom);

    setMouseTracking(true);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_TabletTracking);
    setBackgroundRole(QPalette::NoRole);
    setFocusPolicy(Qt::StrongFocus);
    setUpdatesEnabled(true);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    m_overlay = new ViewerOverlay(this);

    // Pen proximity is reported to the application, not to the widget under it.
    m_clock.start();
    qApp->installEventFilter(this);

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    if (m_coreProfile) {
        format.setVersion(4, 5);
        format.setProfile(QSurfaceFormat::CoreProfile);
    }
    setFormat(format);

    // Tear OCCT down while the native window and GL surface still exist;
    // doing it from the destructor makes RemoveView() bind a dead drawable.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &OcctViewerWidget::releaseGraphics);
}

OcctViewerWidget::~OcctViewerWidget()
{
    releaseGraphics();
}

void OcctViewerWidget::releaseGraphics()
{
    if (m_viewer.IsNull())
        return;
    m_interaction = nullptr;

    // Keep the X display alive until the GL context is released.
    Handle(Aspect_DisplayConnection) display = m_viewer->Driver()->GetDisplayConnection();
    makeCurrent(); // OCCT releases its GL resources in the current context
    m_context->RemoveAll(false);
    m_context.Nullify();
    m_view->Remove();
    m_view.Nullify();
    m_viewer.Nullify();
    doneCurrent();
    display.Nullify();
}

void OcctViewerWidget::fitAll()
{
    myViewAnimation->Stop(); // an instant change wins over a camera still gliding
    if (m_view.IsNull())
        return;
    const Bnd_Box box = m_view->View()->MinMaxValues();
    if (box.IsVoid() || box.SquareExtent() < 1e-6) {
        // Nothing to frame yet: look at the origin with a ~200 mm wide view.
        const Handle(Graphic3d_Camera)& camera = m_view->Camera();
        const gp_Vec back = -gp_Vec(camera->Direction()) * 1000.0;
        camera->SetEyeAndCenter(gp::Origin().Translated(back), gp::Origin());
        m_view->SetSize(200.0);
    } else {
        m_view->FitAll(0.05, false);
    }
    m_view->Invalidate();
    updateView();
}

void OcctViewerWidget::lookAt(const gp_Dir& normal, const gp_Dir& up)
{
    myViewAnimation->Stop(); // an instant change wins over a camera still gliding
    if (m_view.IsNull())
        return;
    m_view->SetProj(normal.X(), normal.Y(), normal.Z());
    m_view->SetUp(up.X(), up.Y(), up.Z());
    fitAll();
}

void OcctViewerWidget::animateCamera(const gp_Dir& eyeSide, const gp_Dir& up, const Bnd_Box& box)
{
    if (m_view.IsNull())
        return;
    Handle(Graphic3d_Camera) start = new Graphic3d_Camera();
    start->Copy(m_view->Camera());
    Handle(Graphic3d_Camera) end = new Graphic3d_Camera();
    end->Copy(m_view->Camera());
    end->SetDirection(eyeSide.Reversed());
    end->SetUp(up);
    const Bnd_Box frame = box.IsVoid() ? m_view->View()->MinMaxValues() : box;
    if (!frame.IsVoid())
        m_view->FitMinMax(end, frame, 0.15);
    // The same animation the view cube uses; the controller advances it frame by frame.
    myViewAnimation->Stop();
    myViewAnimation->SetView(m_view);
    myViewAnimation->SetCameraStart(start);
    myViewAnimation->SetCameraEnd(end);
    myViewAnimation->SetOwnDuration(0.45);
    myViewAnimation->StartTimer(0.0, 1.0, true, false);
    updateView();
}

void OcctViewerWidget::refit()
{
    if (!m_view.IsNull())
        animateCamera(eyeSide(), m_view->Camera()->Up(), Bnd_Box());
}

gp_Dir OcctViewerWidget::eyeSide() const
{
    return m_view.IsNull() ? gp::DZ() : m_view->Camera()->Direction().Reversed();
}

void OcctViewerWidget::setDark(bool dark)
{
    auto srgb = [](double r, double g, double b) { return Quantity_Color(r, g, b, Quantity_TOC_sRGB); };
    if (dark) {
        m_view->SetBgGradientColors(srgb(0.19, 0.20, 0.22), srgb(0.11, 0.12, 0.13), Aspect_GradientFillMethod_Vertical);
        m_viewer->Grid()->SetColors(srgb(0.28, 0.30, 0.33), srgb(0.38, 0.41, 0.45));
    } else {
        m_view->SetBgGradientColors(srgb(0.95, 0.96, 0.97), srgb(0.80, 0.83, 0.87), Aspect_GradientFillMethod_Vertical);
        m_viewer->Grid()->SetColors(srgb(0.70, 0.72, 0.75), srgb(0.52, 0.55, 0.59));
    }
    m_view->Invalidate();
    update();
}

void OcctViewerWidget::setProjection(V3d_TypeOfOrientation orientation, bool animate)
{
    if (m_view.IsNull())
        return;
    if (!animate) {
        myViewAnimation->Stop();
        m_view->SetProj(orientation, false);
        fitAll();
        return;
    }
    // Same up direction as V3d picks: Z, except looking straight down / up.
    const gp_Dir eye = V3d::GetProjAxis(orientation);
    const gp_Dir up = std::abs(eye.Z()) > 0.99 ? gp::DY() : gp::DZ();
    animateCamera(eye, up, Bnd_Box());
}

QPointF OcctViewerWidget::project(const gp_Pnt& p) const
{
    if (m_view.IsNull())
        return {};
    const gp_Pnt ndc = m_view->Camera()->Project(p);
    return {(ndc.X() + 1.0) * 0.5 * width(), (1.0 - ndc.Y()) * 0.5 * height()};
}

void OcctViewerWidget::paintOverlay(QPainter& painter)
{
    if (m_interaction && !m_view.IsNull())
        m_interaction->paintOverlay(painter);
}

void OcctViewerWidget::resizeEvent(QResizeEvent* e)
{
    QOpenGLWidget::resizeEvent(e);
    if (m_overlay)
        m_overlay->setGeometry(rect());
}

void OcctViewerWidget::initializeGL()
{
    const qreal dpr = devicePixelRatioF();
    const Graphic3d_Vec2i size(qRound(width() * dpr), qRound(height() * dpr));
    const Aspect_Drawable nativeWindow = nativeDrawable();

    Handle(OpenGl_Context) glCtx = new OpenGl_Context();
    if (!glCtx->Init(m_coreProfile)) {
        emit glFailed(tr("Could not attach OpenCASCADE to the OpenGL context (try SHAPR_GL_CORE=1)."));
        return;
    }

    m_view->ChangeRenderingParams().Resolution = static_cast<unsigned int>(96.0 * dpr + 0.5);

    Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(m_view->Window());
    const bool firstInit = window.IsNull();
    if (firstInit) {
        window = new Aspect_NeutralWindow();
        window->SetVirtual(true);
    }
    window->SetNativeHandle(nativeWindow);
    window->SetSize(size.x(), size.y());
    m_view->SetWindow(window, glCtx->RenderingContext());

    if (firstInit) {
        m_context->Display(m_viewCube, 0, 0, false);

        QString info;
        if (auto* fn = QOpenGLContext::currentContext()->functions()) {
            info = QStringLiteral("%1 | OpenGL %2")
                       .arg(reinterpret_cast<const char*>(fn->glGetString(GL_RENDERER)),
                            reinterpret_cast<const char*>(fn->glGetString(GL_VERSION)));
        }
        emit glReady(info);
    }
}

void OcctViewerWidget::paintGL()
{
    if (m_view.IsNull() || m_view->Window().IsNull())
        return;

    // The native handle changes when the widget gets reparented (e.g. fullscreen toggle).
    if (m_view->Window()->NativeHandle() != nativeDrawable())
        initializeGL();

    Handle(OpenGl_GraphicDriver) driver = Handle(OpenGl_GraphicDriver)::DownCast(m_viewer->Driver());
    const Handle(OpenGl_Context)& glCtx = driver->GetSharedContext();

    Handle(OpenGl_FrameBuffer) fbo = glCtx->DefaultFrameBuffer();
    if (fbo.IsNull()) {
        fbo = new QtFrameBuffer();
        glCtx->SetDefaultFrameBuffer(fbo);
    }
    if (!fbo->InitWrapper(glCtx)) {
        fbo.Nullify();
        emit glFailed(tr("Could not wrap the Qt framebuffer."));
        return;
    }

    Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(m_view->Window());
    Graphic3d_Vec2i oldSize;
    window->Size(oldSize.x(), oldSize.y());
    const Graphic3d_Vec2i newSize = fbo->GetVPSize();
    if (newSize != oldSize) {
        window->SetSize(newSize.x(), newSize.y());
        m_view->MustBeResized();
        m_view->Invalidate();
    }

    // Redraw the whole scene, not just OCCT's immediate layer: presentations changed in
    // place (a preview growing, bodies recolored) don't mark the view invalid by
    // themselves, and would otherwise show up only after some later event.
    m_view->Invalidate();
    FlushViewEvents(m_context, m_view, true);

    // The overlay projects model points, so it has to follow every camera change.
    if (m_overlay)
        m_overlay->update();
}

void OcctViewerWidget::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                                        const Handle(V3d_View)& view)
{
    AIS_ViewController::handleViewRedraw(ctx, view);
    // Animations (view cube, inertia) ask for more frames.
    if (myToAskNextFrame)
        updateView();
}

bool OcctViewerWidget::isViewCubeAt(const Graphic3d_Vec2i& pixel)
{
    m_context->MoveTo(pixel.x(), pixel.y(), m_view, false);
    return m_context->HasDetected() && m_context->DetectedInteractive() == m_viewCube;
}

Aspect_Drawable OcctViewerWidget::nativeDrawable() const
{
    // Use the top-level window: Qt creates it with a GLX-compatible visual.
    // Calling winId() on this widget would turn it into a native child window
    // without depth/stencil, which glXMakeCurrent() later rejects.
    return static_cast<Aspect_Drawable>(window()->winId());
}

Graphic3d_Vec2i OcctViewerWidget::toPixels(const QPointF& p) const
{
    const qreal dpr = devicePixelRatioF();
    return Graphic3d_Vec2i(qRound(p.x() * dpr), qRound(p.y() * dpr));
}

void OcctViewerWidget::updateView()
{
    update();
}

bool OcctViewerWidget::event(QEvent* e)
{
    switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        if (handleTouch(static_cast<QTouchEvent*>(e)))
            return true;
        break;
    case QEvent::NativeGesture:
        if (handleNativeGesture(static_cast<QNativeGestureEvent*>(e)))
            return true;
        break;
    case QEvent::Leave:
        if (m_interaction && !m_primaryGrab && m_interaction->leave())
            updateView();
        break;
    default:
        break;
    }
    return QOpenGLWidget::event(e);
}

bool OcctViewerWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::TabletEnterProximity || event->type() == QEvent::TabletLeaveProximity) {
        m_palm.penNear(event->type() == QEvent::TabletEnterProximity, m_clock.elapsed());
        if (event->type() == QEvent::TabletEnterProximity)
            penActivity();
    }
    return QOpenGLWidget::eventFilter(watched, event);
}

void OcctViewerWidget::penActivity()
{
    m_palm.penSeen(m_clock.elapsed());
    if (!m_palm.enabled() || m_touches.empty())
        return;
    // A hand already on the screen when the pen shows up was resting, not navigating.
    for (int id : m_touches) {
        RemoveTouchPoint(static_cast<Standard_Size>(id));
        m_rejectedTouches.insert(id);
    }
    m_touches.clear();
    updateView();
}

bool OcctViewerWidget::handleTouch(QTouchEvent* e)
{
    // Touchpads deliver gestures/scroll instead; only handle real touch screens here.
    if (e->device() && e->device()->type() != QInputDevice::DeviceType::TouchScreen)
        return false;
    if (m_view.IsNull())
        return false;

    const qreal dpr = devicePixelRatioF();
    const bool cancel = e->type() == QEvent::TouchCancel;
    // Contact size in millimetres, when the screen reports it.
    const double dpi = screen() ? screen()->physicalDotsPerInch() : 0.0;
    auto contactMm = [&](const QEventPoint& point) {
        const QSizeF d = point.ellipseDiameters();
        return dpi > 0.0 ? std::max(d.width(), d.height()) * dpr * 25.4 / dpi : 0.0;
    };

    for (const QEventPoint& point : e->points()) {
        const int id = point.id();
        const Graphic3d_Vec2d pos(point.position().x() * dpr, point.position().y() * dpr);

        if (cancel) {
            if (m_touches.erase(id))
                RemoveTouchPoint(static_cast<Standard_Size>(id));
            m_rejectedTouches.erase(id);
            continue;
        }
        // A rejected contact stays ignored for its whole life, even after the pen leaves.
        if (m_rejectedTouches.count(id)) {
            if (point.state() == QEventPoint::Released)
                m_rejectedTouches.erase(id);
            continue;
        }
        switch (point.state()) {
        case QEventPoint::Pressed:
            if (m_palm.rejectTouch(m_clock.elapsed(), contactMm(point))) {
                m_rejectedTouches.insert(id);
                break;
            }
            setFocus(Qt::MouseFocusReason);
            m_touches.insert(id);
            AddTouchPoint(static_cast<Standard_Size>(id), pos);
            break;
        case QEventPoint::Updated:
            if (m_touches.count(id))
                UpdateTouchPoint(static_cast<Standard_Size>(id), pos);
            break;
        case QEventPoint::Released:
            if (m_touches.erase(id))
                RemoveTouchPoint(static_cast<Standard_Size>(id));
            break;
        default:
            break;
        }
    }

    e->accept();
    updateView();
    return true;
}

PointerEvent OcctViewerWidget::makePointer(PointerEvent::Source source, const QPointF& pos,
                                           double pressure, Qt::KeyboardModifiers modifiers) const
{
    PointerEvent pe;
    pe.source = source;
    pe.pos = pos;
    pe.pixel = toPixels(pos);
    pe.pressure = pressure;
    pe.modifiers = modifiers;
    return pe;
}

Aspect_VKeyMouse OcctViewerWidget::navigationButtons(Qt::MouseButtons buttons) const
{
    // While an interaction owns the primary button, AIS must not see it.
    if (m_primaryGrab)
        buttons &= ~Qt::LeftButton;
    return toVKeyMouse(buttons);
}

void OcctViewerWidget::mousePressEvent(QMouseEvent* e)
{
    QOpenGLWidget::mousePressEvent(e);
    if (m_view.IsNull())
        return;

    if (e->button() == Qt::LeftButton && m_interaction && !isViewCubeAt(toPixels(e->position()))) {
        const PointerEvent pe = makePointer(PointerEvent::Source::Mouse, e->position(), 1.0, e->modifiers());
        if (m_interaction->wantsPress(pe)) {
            m_primaryGrab = true;
            if (m_interaction->press(pe))
                updateView();
            return;
        }
    }

    if (UpdateMouseButtons(toPixels(e->position()), navigationButtons(e->buttons()),
                           toVKeyFlags(e->modifiers()), false))
        updateView();
}

void OcctViewerWidget::mouseReleaseEvent(QMouseEvent* e)
{
    QOpenGLWidget::mouseReleaseEvent(e);
    if (m_view.IsNull())
        return;

    if (e->button() == Qt::LeftButton && m_primaryGrab) {
        m_primaryGrab = false;
        if (m_interaction
            && m_interaction->release(makePointer(PointerEvent::Source::Mouse, e->position(), 1.0, e->modifiers())))
            updateView();
        return;
    }

    if (UpdateMouseButtons(toPixels(e->position()), navigationButtons(e->buttons()),
                           toVKeyFlags(e->modifiers()), false))
        updateView();
}

void OcctViewerWidget::mouseMoveEvent(QMouseEvent* e)
{
    QOpenGLWidget::mouseMoveEvent(e);
    if (m_view.IsNull())
        return;

    bool redraw = false;
    if (m_interaction) {
        const PointerEvent pe = makePointer(PointerEvent::Source::Mouse, e->position(), 1.0, e->modifiers());
        redraw = m_interaction->move(pe, m_primaryGrab);
    }
    redraw |= UpdateMousePosition(toPixels(e->position()), navigationButtons(e->buttons()),
                                  toVKeyFlags(e->modifiers()), false);
    if (redraw)
        updateView();
}

bool OcctViewerWidget::handleNativeGesture(QNativeGestureEvent* e)
{
    if (m_view.IsNull())
        return false;
    if (m_debugInput)
        qInfo() << "gesture" << e->gestureType() << e->value() << e->delta() << e->fingerCount();

    const Graphic3d_Vec2i cursor = toPixels(e->position());
    switch (e->gestureType()) {
    case Qt::ZoomNativeGesture:
        UpdateZoom(Aspect_ScrollDelta(cursor, e->value() * kPinchToZoom));
        break;
    case Qt::PanNativeGesture: {
        const QPointF d = e->delta() * devicePixelRatioF();
        continuousDrag(DragGesture::Pan, cursor, Graphic3d_Vec2d(d.x(), d.y()));
        break;
    }
    case Qt::BeginNativeGesture:
    case Qt::EndNativeGesture:
        m_dragGesture = DragGesture::None;
        break;
    default:
        return false;
    }

    e->accept();
    updateView();
    return true;
}

void OcctViewerWidget::continuousDrag(DragGesture mode, const Graphic3d_Vec2i& cursor,
                                      const Graphic3d_Vec2d& delta)
{
    if (mode != m_dragGesture) {
        m_dragGesture = mode;
        m_dragPoint = Graphic3d_Vec2d(cursor);
        if (mode == DragGesture::Orbit) {
            myUI.OrbitRotation.ToStart = true;
            myUI.OrbitRotation.PointStart = m_dragPoint;
        } else {
            myUI.Panning.ToStart = true;
            myUI.Panning.PointStart = cursor;
        }
    }

    m_dragPoint += delta;
    if (mode == DragGesture::Orbit) {
        myUI.OrbitRotation.ToRotate = true;
        myUI.OrbitRotation.PointTo = m_dragPoint;
    } else {
        // AIS pans in GL coordinates (Y up) and only resets Delta when ToPan is off.
        const Graphic3d_Vec2i step(qRound(delta.x()), qRound(-delta.y()));
        myUI.Panning.Delta = myUI.Panning.ToPan ? myUI.Panning.Delta + step : step;
        myUI.Panning.ToPan = true;
    }
    m_dragEndTimer.start();
}

void OcctViewerWidget::wheelEvent(QWheelEvent* e)
{
    QOpenGLWidget::wheelEvent(e);
    if (m_view.IsNull())
        return;
    if (m_debugInput)
        qInfo() << "wheel" << e->angleDelta() << e->pixelDelta() << e->phase() << e->modifiers();

    // XWayland merges every pointer into one device, so tell them apart by the data:
    // wheel mice scroll in whole 120-unit notches, touchpads and the TrackPoint smoothly.
    const QPoint angle = e->angleDelta();
    const bool smooth = m_dragGesture != DragGesture::None || !e->pixelDelta().isNull()
                        || angle.x() % 120 != 0 || angle.y() % 120 != 0;

    if (smooth && !(e->modifiers() & Qt::ControlModifier)) {
        QPointF d = e->pixelDelta().isNull() ? QPointF(angle) * kScrollToPixels : QPointF(e->pixelDelta());
        d *= devicePixelRatioF();
        if (m_invertScroll)
            d = -d;
        // Scrolling down means the fingers moved up; drag the model along with the fingers.
        const DragGesture mode = (e->modifiers() & Qt::ShiftModifier) ? DragGesture::Pan : DragGesture::Orbit;
        continuousDrag(mode, toPixels(e->position()), Graphic3d_Vec2d(-d.x(), -d.y()));
        updateView();
        return;
    }

    if (UpdateZoom(Aspect_ScrollDelta(toPixels(e->position()), angle.y() / 8.0)))
        updateView();
}

void OcctViewerWidget::tabletEvent(QTabletEvent* e)
{
    if (m_view.IsNull()) {
        e->ignore();
        return;
    }

    penActivity();

    PenSample sample;
    sample.pos = e->position();
    sample.pixel = e->position() * devicePixelRatioF();
    sample.pressure = e->pressure();
    sample.eraser = e->pointerType() == QPointingDevice::PointerType::Eraser;
    sample.barrelButton = e->buttons() & (Qt::MiddleButton | Qt::RightButton);

    switch (e->type()) {
    case QEvent::TabletPress:
        sample.phase = PenSample::Phase::Down;
        m_penDown = true;
        break;
    case QEvent::TabletRelease:
        sample.phase = PenSample::Phase::Up;
        m_penDown = false;
        break;
    default:
        sample.phase = m_penDown ? PenSample::Phase::Move : PenSample::Phase::Hover;
        break;
    }

    const Graphic3d_Vec2i pt = toPixels(e->position());
    const Aspect_VKeyFlags flags = toVKeyFlags(e->modifiers());
    PointerEvent pe = makePointer(PointerEvent::Source::Pen, e->position(), e->pressure(), e->modifiers());
    pe.eraser = sample.eraser;
    bool redraw = false;
    bool handled = false;

    // Pen tip goes to the active interaction (sketch tools); the barrel button orbits.
    if (m_interaction && !sample.barrelButton) {
        switch (e->type()) {
        case QEvent::TabletPress:
            // Pen taps don't move keyboard focus by themselves; shortcuts like Esc need it here.
            setFocus(Qt::MouseFocusReason);
            if (!isViewCubeAt(pt) && m_interaction->wantsPress(pe)) {
                m_primaryGrab = true;
                redraw = m_interaction->press(pe);
                handled = true;
            }
            break;
        case QEvent::TabletRelease:
            if (m_primaryGrab) {
                m_primaryGrab = false;
                redraw = m_interaction->release(pe);
                handled = true;
            }
            break;
        default:
            redraw = m_interaction->move(pe, m_primaryGrab);
            handled = m_primaryGrab;
            break;
        }
    }

    if (!handled) {
        Aspect_VKeyMouse buttons = Aspect_VKeyMouse_NONE;
        if (sample.barrelButton)
            buttons = Aspect_VKeyMouse_RightButton;
        else if (e->buttons() & Qt::LeftButton)
            buttons = Aspect_VKeyMouse_LeftButton;
        if (e->type() == QEvent::TabletMove)
            redraw |= UpdateMousePosition(pt, buttons, flags, false);
        else
            redraw |= UpdateMouseButtons(pt, buttons, flags, false);
    }
    if (redraw)
        updateView();

    emit penInput(sample);
    e->accept(); // no synthesized mouse events
}

void OcctViewerWidget::keyPressEvent(QKeyEvent* e)
{
    if (m_view.IsNull()) {
        QOpenGLWidget::keyPressEvent(e);
        return;
    }
    // Window-wide keys (Esc, Enter, F, tools) are QAction shortcuts in MainWindow,
    // so they work whichever widget has focus.
    if (m_interaction && m_interaction->key(e)) {
        updateView();
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}
