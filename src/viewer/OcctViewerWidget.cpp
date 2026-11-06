#include "viewer/OcctViewerWidget.h"

#include <QApplication>
#include <QKeyEvent>
#include <QDebug>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
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
    m_coreProfile = qEnvironmentVariableIntValue("SHAPR_GL_COMPAT") == 0;
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
    m_view->SetBgGradientColors(Quantity_Color(0.95, 0.96, 0.97, Quantity_TOC_sRGB),
                                Quantity_Color(0.80, 0.83, 0.87, Quantity_TOC_sRGB),
                                Aspect_GradientFillMethod_Vertical);
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
    if (m_view.IsNull())
        return;
    m_view->FitAll(0.05, false);
    m_view->Invalidate();
    updateView();
}

void OcctViewerWidget::setProjection(V3d_TypeOfOrientation orientation)
{
    if (m_view.IsNull())
        return;
    m_view->SetProj(orientation, false);
    fitAll();
}

void OcctViewerWidget::initializeGL()
{
    const qreal dpr = devicePixelRatioF();
    const Graphic3d_Vec2i size(qRound(width() * dpr), qRound(height() * dpr));
    const Aspect_Drawable nativeWindow = nativeDrawable();

    Handle(OpenGl_Context) glCtx = new OpenGl_Context();
    if (!glCtx->Init(m_coreProfile)) {
        emit glFailed(QStringLiteral("Nie udało się podpiąć OpenCASCADE do kontekstu OpenGL "
                                     "(spróbuj SHAPR_GL_COMPAT=1)."));
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
        emit glFailed(QStringLiteral("Nie udało się opakować framebuffera Qt."));
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

    m_view->InvalidateImmediate();
    FlushViewEvents(m_context, m_view, true);
}

void OcctViewerWidget::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                                        const Handle(V3d_View)& view)
{
    AIS_ViewController::handleViewRedraw(ctx, view);
    // Animations (view cube, inertia) ask for more frames.
    if (myToAskNextFrame)
        updateView();
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
    default:
        break;
    }
    return QOpenGLWidget::event(e);
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

    for (const QEventPoint& point : e->points()) {
        const Standard_Size id = static_cast<Standard_Size>(point.id());
        const Graphic3d_Vec2d pos(point.position().x() * dpr, point.position().y() * dpr);

        if (cancel) {
            RemoveTouchPoint(id);
            continue;
        }
        switch (point.state()) {
        case QEventPoint::Pressed:
            AddTouchPoint(id, pos);
            break;
        case QEventPoint::Updated:
            UpdateTouchPoint(id, pos);
            break;
        case QEventPoint::Released:
            RemoveTouchPoint(id);
            break;
        default:
            break;
        }
    }

    e->accept();
    updateView();
    return true;
}

void OcctViewerWidget::mousePressEvent(QMouseEvent* e)
{
    QOpenGLWidget::mousePressEvent(e);
    if (m_view.IsNull())
        return;
    if (UpdateMouseButtons(toPixels(e->position()), toVKeyMouse(e->buttons()),
                           toVKeyFlags(e->modifiers()), false))
        updateView();
}

void OcctViewerWidget::mouseReleaseEvent(QMouseEvent* e)
{
    QOpenGLWidget::mouseReleaseEvent(e);
    if (m_view.IsNull())
        return;
    if (UpdateMouseButtons(toPixels(e->position()), toVKeyMouse(e->buttons()),
                           toVKeyFlags(e->modifiers()), false))
        updateView();
}

void OcctViewerWidget::mouseMoveEvent(QMouseEvent* e)
{
    QOpenGLWidget::mouseMoveEvent(e);
    if (m_view.IsNull())
        return;
    if (UpdateMousePosition(toPixels(e->position()), toVKeyMouse(e->buttons()),
                            toVKeyFlags(e->modifiers()), false))
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

    // Until sketch tools exist the pen drives the viewer like a mouse:
    // tip = select, barrel button = orbit.
    Aspect_VKeyMouse buttons = Aspect_VKeyMouse_NONE;
    if (sample.barrelButton)
        buttons = Aspect_VKeyMouse_RightButton;
    else if (e->buttons() & Qt::LeftButton)
        buttons = Aspect_VKeyMouse_LeftButton;

    const Graphic3d_Vec2i pt = toPixels(e->position());
    const Aspect_VKeyFlags flags = toVKeyFlags(e->modifiers());
    bool redraw = false;
    if (e->type() == QEvent::TabletMove)
        redraw = UpdateMousePosition(pt, buttons, flags, false);
    else
        redraw = UpdateMouseButtons(pt, buttons, flags, false);
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
    switch (e->key()) {
    case Qt::Key_F:
        fitAll();
        return;
    case Qt::Key_Escape:
        m_context->ClearSelected(true);
        updateView();
        return;
    default:
        QOpenGLWidget::keyPressEvent(e);
    }
}
