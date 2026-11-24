#include "region_overlay.h"

#include "grim_acquisition.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickPaintedItem>
#include <QQuickView>
#include <QScreen>

#include <algorithm>

static void initializePreviewResource() { Q_INIT_RESOURCE(region_preview); }

namespace screenshot {
namespace {

using namespace Holonight::Wayland;

class FrozenOutput : public QQuickPaintedItem {
  Q_OBJECT
 public:
  void setFrame(const CaptureFrame& frame, const QRectF& bounds) {
    origin_ = bounds.topLeft();
    const auto& mapping = *frame.desktopMapping();
    const auto start = mapping.rasterEdge(bounds.topLeft());
    rasterStart_ = start;
    scale_ = mapping.captureScale;
    const auto end = mapping.rasterEdge(bounds.bottomRight());
    // Use the continuous raster edges to align fractional-scale outputs.
    // Limit the source to the actual canvas without stretching its last pixel.
    const auto clipped = QRectF(start, end).intersected(QRectF(frame.image().rect()));
    target_ = QRectF((clipped.topLeft() - start) / mapping.captureScale, clipped.size() / mapping.captureScale);
    source_ = clipped;
    frame_ = frame;
    update();
  }

  void clearFrame() {
    frame_.reset();
    update();
  }

  void setSelection(const RegionSelection& selection) {
    selection_ = selection;
    update();
  }

  Q_SIGNAL void desktopRequested(bool desktop);
  Q_SIGNAL void copyRequested();
  Q_SIGNAL void saveRequested();
  Q_SIGNAL void cancelRequested();
  Q_SIGNAL void toolRequested(int tool);
  Q_SIGNAL void colorRequested(const QColor& color);
  Q_SIGNAL void undoRequested();

  void paint(QPainter* painter) override {
    const QRectF bounds = boundingRect();
    painter->fillRect(bounds, Qt::black);
    if (frame_) {
      painter->drawImage(target_, frame_->image(), source_);
      painter->save();
      painter->setClipRect(target_);
      painter->scale(1 / scale_, 1 / scale_);
      painter->translate(-rasterStart_);
      selection_.paintAnnotations(*painter, *frame_, true);
      painter->restore();
    }

    if (property("pointPicker").toBool() || property("desktop").toBool()) {
      return;
    }
    const auto selection = selection_.rectangle().translated(-origin_);

    QPainterPath shade;
    shade.addRect(bounds);
    const auto visibleSelection = selection.intersected(bounds);
    if (!visibleSelection.isEmpty()) {
      shade.addRect(visibleSelection);
    }
    painter->fillPath(shade, QColor(0, 0, 0, 145));
    if (!selection.isEmpty()) {
      painter->setPen(QPen(Qt::black, 3));
      painter->drawRect(selection);
      painter->setPen(QPen(Qt::white, 1));
      painter->drawRect(selection);
    }
  }

 private:
  std::optional<CaptureFrame> frame_;
  QPointF rasterStart_;
  qreal scale_ = 1;
  QPointF origin_;
  QRectF source_;
  QRectF target_;
  RegionSelection selection_;
};

}  // namespace

RegionOverlay::RegionOverlay(CaptureFrame frame, GrimAcquisition& acquisition, Mode mode)
    : RegionOverlay(std::move(frame), acquisition, mode, {}) {}

RegionOverlay::RegionOverlay(CaptureFrame frame, GrimAcquisition& acquisition, Mode mode, QString appName)
    : mode_(mode),
      appName_(std::move(appName)),
      desktop_(mode == Mode::Desktop),
      frame_(std::move(frame)),
      acquisition_(acquisition) {
  layoutTimer_.setInterval(500);
  connect(&layoutTimer_, &QTimer::timeout, this, [this] {
    if (layoutCheck_ == LayoutCheck::Idle) {
      layoutCheck_ = LayoutCheck::Polling;
      acquisition_.verifyLayout();
    }
  });
  connect(&acquisition_, &GrimAcquisition::layoutVerified, this, [this] {
    if (finished_) {
      return;
    }
    if (layoutCheck_ == LayoutCheck::Confirming) {
      if (pendingAction_ == Action::PickColor) {
        const auto color = frame_->sampleSrgb(pickedPoint_);
        finish({}, color.isValid() ? QString() : QStringLiteral("Could not sample the selected pixel."), color);
        return;
      }
      auto crop = desktop_ ? frame_->image() : selection_.confirm(*frame_);
      if (crop.isNull()) {
        finish({}, QStringLiteral("Could not render the selected screenshot."));
      } else {
        finish(std::move(crop));
      }
    } else if (!pendingAction_) {
      layoutCheck_ = LayoutCheck::Idle;
    } else {
      // A poll started before confirmation. Check again against the confirmed selection.
      layoutCheck_ = LayoutCheck::Confirming;
      acquisition_.verifyLayout();
    }
  });
  connect(&acquisition_, &GrimAcquisition::layoutVerificationFailed, this,
          [this](const QString& reason) { finish({}, reason); });
  timeout_.setSingleShot(true);
  timeout_.setTimerType(Qt::PreciseTimer);
  connect(&timeout_, &QTimer::timeout, this,
          [this] { finish({}, QStringLiteral("Screenshot overlays did not configure within two seconds.")); });
}

void RegionOverlay::open() {
  if (finished_) {
    return;
  }
  static const bool resourceReady = [] {
    initializePreviewResource();
    return true;
  }();
  Q_UNUSED(resourceReady);
  const auto screens = QGuiApplication::screens();
  const auto& mapping = frame_->desktopMapping();
  const auto layoutChanged = [this] { finish({}, QStringLiteral("Output layout changed during region selection.")); };
  if (!mapping || mapping->outputs.isEmpty() || screens.size() != mapping->outputs.size()) {
    layoutChanged();
    return;
  }
  for (const auto& output : mapping->outputs) {
    const auto match = std::ranges::find(screens, output.name, &QScreen::name);
    if (match == screens.end() || QRectF((*match)->geometry()) != output.logicalBounds) {
      layoutChanged();
      return;
    }
    surfaces_.push_back({*match, {}, output, std::make_unique<LayerSurfaceHost>()});
  }

  static const auto typeId = qmlRegisterType<FrozenOutput>("Holonight.Screenshot", 1, 0, "FrozenOutput");
  Q_UNUSED(typeId);
  connect(qGuiApp, &QGuiApplication::screenAdded, this, layoutChanged);
  connect(qGuiApp, &QGuiApplication::screenRemoved, this, layoutChanged);
  connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, layoutChanged);
  timeout_.start(2000);
  for (auto& surface : surfaces_) {
    auto* screen = surface.screen;
    auto* host = surface.host.get();
    connect(screen, &QScreen::geometryChanged, this, layoutChanged);
    connect(screen, &QScreen::orientationChanged, this, layoutChanged);
    connect(host, &LayerSurfaceHost::configured, this, &RegionOverlay::configured);
    connect(host, &LayerSurfaceHost::closed, this,
            [this] { finish({}, QStringLiteral("A screenshot overlay closed unexpectedly.")); });
    connect(host, &LayerSurfaceHost::failed, this, [this](const QString& reason) {
      finish({}, QStringLiteral("Could not open screenshot overlay. %1").arg(reason));
    });

    LayerSurfaceSpec spec;
    spec.output = screen;
    spec.name_space = QStringLiteral("holonight-screenshot");
    spec.layer = Layer::Overlay;
    spec.anchors = Anchor::Top | Anchor::Bottom | Anchor::Left | Anchor::Right;
    spec.exclusive_zone = -1;
    spec.keyboard_interactivity =
        screen == QGuiApplication::primaryScreen() ? KeyboardInteractivity::Exclusive : KeyboardInteractivity::None;
    spec.qml_url = QUrl(QStringLiteral("qrc:/screenshot/RegionPreview.qml"));
    spec.color = Qt::black;
    if (!host->open(spec)) {
      return;
    }
    auto* view = host->view();
    auto* item = qobject_cast<FrozenOutput*>(view->rootObject());
    if (!item) {
      finish({}, QStringLiteral("Could not load the frozen screenshot preview."));
      return;
    }
    surface.preview = item;
    item->setFrame(*frame_, surface.output.logicalBounds);
    item->setProperty("primaryOutput", screen == QGuiApplication::primaryScreen());
    item->setProperty("portalMode", mode_ != Mode::Cli && mode_ != Mode::SelectionOnly);
    item->setProperty("selectionOnly", mode_ == Mode::SelectionOnly);
    item->setProperty("pointPicker", mode_ == Mode::PickColor);
    item->setProperty("desktop", desktop_);
    item->setProperty("targetChoice", mode_ == Mode::Choice);
    item->setProperty("appName", appName_.isEmpty() ? tr("An application") : appName_);
    connect(item, &FrozenOutput::desktopRequested, this, &RegionOverlay::setDesktop);
    connect(item, &FrozenOutput::cancelRequested, this, &RegionOverlay::cancel);
    connect(item, &FrozenOutput::copyRequested, this, [this] { confirm(Action::Copy); });
    connect(item, &FrozenOutput::saveRequested, this, [this] {
      const auto action = mode_ == Mode::SelectionOnly ? Action::Select
                          : mode_ == Mode::Cli         ? Action::Save
                                                       : Action::Share;
      confirm(action);
    });
    connect(item, &FrozenOutput::toolRequested, this, [this](int tool) {
      selection_.setTool(static_cast<RegionSelection::Tool>(tool));
      redraw();
    });
    connect(item, &FrozenOutput::colorRequested, this, [this](const QColor& color) {
      selection_.setColor(color);
      redraw();
    });
    connect(item, &FrozenOutput::undoRequested, this, &RegionOverlay::undo);
    view->installEventFilter(this);
    connect(
        view, &QQuickWindow::sceneGraphError, this,
        [this](auto, const QString& reason) {
          finish({}, QStringLiteral("Could not render screenshot overlay. %1").arg(reason));
        },
        Qt::QueuedConnection);
  }
}

void RegionOverlay::configured() {
  if (finished_) {
    return;
  }
  for (const auto& surface : surfaces_) {
    if (surface.host->state() != LayerSurfaceHost::State::Configured) {
      return;
    }
    if (surface.host->view()->size() != surface.output.logicalBounds.size().toSize()) {
      finish({}, QStringLiteral("A screenshot overlay does not cover its captured output."));
      return;
    }
  }
  timeout_.stop();
  if (!ready_) {
    ready_ = true;
    layoutTimer_.start();
    redraw();
  }
}

bool RegionOverlay::eventFilter(QObject* watched, QEvent* event) {
  if (finished_) {
    return false;
  }
  const auto surface =
      std::ranges::find_if(surfaces_, [watched](const auto& candidate) { return candidate.host->view() == watched; });
  if (surface == surfaces_.end()) {
    return false;
  }
  auto* view = surface->host->view();
  switch (event->type()) {
    case QEvent::Close:
    case QEvent::Hide:
      finish({}, QStringLiteral("A screenshot overlay closed or its output changed."));
      return true;
    case QEvent::KeyPress: {
      const auto* key = static_cast<QKeyEvent*>(event);
      if (key->key() == Qt::Key_Escape) {
        cancel();
      } else if (key->keyCombination() == QKeyCombination(Qt::ControlModifier, Qt::Key_Z)) {
        undo();
      } else if (mode_ == Mode::Cli && key->keyCombination() == QKeyCombination(Qt::ControlModifier, Qt::Key_C)) {
        confirm(Action::Copy);
      } else if (mode_ == Mode::SelectionOnly && (key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return)) {
        confirm(Action::Select);
      } else if (mode_ == Mode::Cli && (key->keyCombination() == QKeyCombination(Qt::ControlModifier, Qt::Key_S) ||
                                        key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return)) {
        confirm(Action::Save);
      } else {
        return mode_ == Mode::Cli;
      }
      return true;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
      if (!ready_) {
        return true;
      }
      const auto* mouse = static_cast<QMouseEvent*>(event);
      const auto point = surface->output.logicalBounds.topLeft() + mouse->position();
      if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) &&
          mouse->button() == Qt::LeftButton && !dragView_) {
        const auto* actions = view->rootObject()->property("actionBar").value<QQuickItem*>();
        if (actions && actions->isVisible()) {
          const auto local = actions->mapFromScene(mouse->position());
          if (actions->contains(local)) {
            return false;
          }
        }
        if (pendingAction_) {
          return true;
        }
        if (mode_ == Mode::PickColor) {
          pickedPoint_ = point;
          confirm(Action::PickColor);
          return true;
        }
        if (desktop_) {
          return true;
        }
        // Wayland keeps delivering the drag to the surface of the button press.
        if (selection_.begin(point, *frame_)) {
          dragView_ = view;
          redraw();
        }
      } else if (view == dragView_ && event->type() == QEvent::MouseMove) {
        selection_.move(point);
        redraw();
      } else if (view == dragView_ && event->type() == QEvent::MouseButtonRelease &&
                 mouse->button() == Qt::LeftButton) {
        selection_.release(point);
        dragView_.clear();
        redraw();
      } else if (!dragView_) {
        return false;
      }
      return true;
    }
    default:
      return false;
  }
}

void RegionOverlay::confirm(Action action) {
  if (finished_ || !ready_ || pendingAction_ ||
      (action != Action::PickColor && !desktop_ && !selection_.canConfirm(*frame_))) {
    return;
  }
  pendingAction_ = action;
  redraw();
  layoutTimer_.stop();
  if (layoutCheck_ == LayoutCheck::Idle) {
    layoutCheck_ = LayoutCheck::Confirming;
    acquisition_.verifyLayout();
  }
}

void RegionOverlay::undo() {
  if (ready_ && !finished_ && !desktop_ && !pendingAction_) {
    selection_.undo();
    redraw();
  }
}

void RegionOverlay::setDesktop(bool desktop) {
  if (ready_ && !finished_ && !pendingAction_ && !dragView_ && mode_ == Mode::Choice) {
    desktop_ = desktop;
    redraw();
  }
}

void RegionOverlay::redraw() {
  const bool hasCrop = !desktop_ && !selection_.cropRect(*frame_).isEmpty();
  const bool actionsAvailable = !pendingAction_ && (desktop_ || selection_.canConfirm(*frame_));
  for (const auto& surface : surfaces_) {
    auto* item = static_cast<FrozenOutput*>(surface.host->rootObject());
    item->setSelection(desktop_ ? RegionSelection{} : selection_);
    item->setProperty("desktop", desktop_);
    item->setProperty("hasCrop", hasCrop);
    item->setProperty("actionsAvailable", actionsAvailable);
    item->setProperty("undoAvailable", actionsAvailable && selection_.canUndo());
    item->setProperty("activeTool", static_cast<int>(selection_.tool()));
    item->setProperty("activeColor", selection_.color());
  }
}

void RegionOverlay::cancel() {
  selection_.cancel();
  finish();
}

void RegionOverlay::finish(QImage crop, QString error, QColor color) {
  if (finished_) {
    return;
  }
  finished_ = true;
  timeout_.stop();
  layoutTimer_.stop();
  acquisition_.cancel();
  frame_.reset();
  for (const auto& surface : surfaces_) {
    QPointer<QQuickWindow> window;
    if (surface.preview) {
      window = surface.preview->window();
      static_cast<FrozenOutput*>(surface.preview.get())->clearFrame();
    }
    if (window) {
      window->removeEventFilter(this);
    }
    surface.host->close();
    if (window) {
      window->setPersistentSceneGraph(false);
      window->setPersistentGraphics(false);
      window->hide();
      window->releaseResources();
    }
  }
  if (!error.isEmpty()) {
    emit failed(error);
  } else if (color.isValid()) {
    emit colorPicked(color);
  } else if (!crop.isNull() && pendingAction_) {
    emit selected(*pendingAction_, crop);
  } else {
    emit canceled();
  }
}

}  // namespace screenshot

#include "region_overlay.moc"
