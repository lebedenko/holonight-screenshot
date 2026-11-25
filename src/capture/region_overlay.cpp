#include "region_overlay.h"

#include "grim_acquisition.h"
#include "wayland_display_p.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickPaintedItem>
#include <QQuickView>
#include <QScreen>

#include <LayerShellQt/Window>
#include <algorithm>

static void initializePreviewResource() { Q_INIT_RESOURCE(region_preview); }

namespace screenshot {
namespace {

void configureOverlayView(QQuickView& view, QScreen* screen, bool primary) {
  using Window = LayerShellQt::Window;
  view.setScreen(screen);
  view.setResizeMode(QQuickView::SizeRootObjectToView);
  view.setColor(Qt::black);
  auto* role = Window::get(&view);
  role->setScreen(screen);
  role->setScope(QStringLiteral("holonight-screenshot"));
  role->setLayer(Window::LayerOverlay);
  role->setAnchors(Window::Anchors(Window::AnchorTop) | Window::AnchorBottom | Window::AnchorLeft |
                   Window::AnchorRight);
  role->setExclusiveZone(-1);
  role->setCloseOnDismissed(true);
  role->setKeyboardInteractivity(primary ? Window::KeyboardInteractivityExclusive : Window::KeyboardInteractivityNone);
}

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
      const bool editable = property("selectionOnly").toBool();
      const QColor lavender(168, 154, 255);
      painter->setPen(QPen(editable ? lavender : QColor(Qt::white), 1));
      painter->drawRect(selection);
      if (editable) {
        painter->setPen(QPen(QColor(24, 20, 40), 1));
        painter->setBrush(lavender);
        for (const auto& handle : RegionSelection::handles(selection)) {
          painter->drawRect(QRectF(handle.center - QPointF(4, 4), QSizeF(8, 8)));
        }
      }
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
    if (finished_ || closing_) {
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
  connect(&timeout_, &QTimer::timeout, this, [this] {
    finish({}, groupShown_ ? QStringLiteral("Screenshot overlays did not configure within two seconds.")
                           : QStringLiteral("Screenshot previews did not load within two seconds."));
  });
}

RegionOverlay::~RegionOverlay() { retireViews(); }

void RegionOverlay::open() {
  if (finished_ || opening_) {
    return;
  }
  opening_ = true;
  watchLayerShell(
      this,
      [this](bool available) {
        if (available) {
          openViews();
        } else {
          finish({}, QStringLiteral("Could not discover Wayland layer-shell support on the Qt display."));
        }
      },
      [this] { finish({}, QStringLiteral("Wayland layer-shell support disappeared during region selection.")); });
}

void RegionOverlay::openViews() {
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
    surfaces_.push_back({*match, {}, output, std::make_unique<QQuickView>()});
  }

  static const auto typeId = qmlRegisterType<FrozenOutput>("Holonight.Screenshot", 1, 0, "FrozenOutput");
  Q_UNUSED(typeId);
  connect(qGuiApp, &QGuiApplication::screenAdded, this, layoutChanged);
  connect(qGuiApp, &QGuiApplication::screenRemoved, this, layoutChanged);
  connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, layoutChanged);
  for (auto& surface : surfaces_) {
    auto* screen = surface.screen;
    auto* view = surface.view.get();
    connect(screen, &QScreen::geometryChanged, this, layoutChanged);
    connect(screen, &QScreen::orientationChanged, this, layoutChanged);
    configureOverlayView(*view, screen, screen == QGuiApplication::primaryScreen());
    view->installEventFilter(this);
    connect(
        view, &QQuickView::statusChanged, this,
        [this](QQuickView::Status status) {
          if (status == QQuickView::Error) {
            finish({}, QStringLiteral("Could not load the frozen screenshot preview."));
          } else if (status == QQuickView::Ready) {
            showLoadedViews();
          }
        },
        Qt::QueuedConnection);
    connect(
        view, &QQuickWindow::sceneGraphError, this,
        [this](auto, const QString& reason) {
          finish({}, QStringLiteral("Could not render screenshot overlay. %1").arg(reason));
        },
        Qt::QueuedConnection);
    view->setSource(QUrl(QStringLiteral("qrc:/screenshot/RegionPreview.qml")));
  }
  timeout_.start(2000);
  showLoadedViews();
}

void RegionOverlay::showLoadedViews() {
  if (finished_ || closing_ || groupShown_) {
    return;
  }
  if (!std::ranges::all_of(surfaces_,
                           [](const auto& surface) { return surface.view->status() == QQuickView::Ready; })) {
    return;
  }
  for (auto& surface : surfaces_) {
    auto* view = surface.view.get();
    auto* screen = surface.screen;
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
  }
  timeout_.start(2000);
  for (const auto& surface : surfaces_) {
    surface.view->show();
    if (finished_ || closing_) {
      return;
    }
  }
  groupShown_ = true;
  checkReadiness();
}

void RegionOverlay::checkReadiness() {
  if (finished_ || closing_ || !groupShown_) {
    return;
  }
  for (const auto& surface : surfaces_) {
    if (!surface.view->isExposed() || surface.view->size() != surface.output.logicalBounds.size().toSize()) {
      if (ready_) {
        finish({}, QStringLiteral("A screenshot overlay no longer covers its captured output."));
      }
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
      std::ranges::find_if(surfaces_, [watched](const auto& candidate) { return candidate.view.get() == watched; });
  if (surface == surfaces_.end()) {
    return false;
  }
  auto* view = surface->view.get();
  switch (event->type()) {
    case QEvent::Close:
    case QEvent::Hide:
      if (closing_) {
        return false;
      }
      closing_ = true;
      QMetaObject::invokeMethod(
          this, [this] { finish({}, QStringLiteral("A screenshot overlay closed or its output changed.")); },
          Qt::QueuedConnection);
      return false;
    case QEvent::Expose:
    case QEvent::Resize:
      QMetaObject::invokeMethod(this, &RegionOverlay::checkReadiness, Qt::QueuedConnection);
      return false;
    case QEvent::KeyPress: {
      const auto* key = static_cast<QKeyEvent*>(event);
      if (closing_) {
        return true;
      }
      if (key->key() == Qt::Key_Escape) {
        cancel();
      } else if (!ready_) {
        return true;
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
    case QEvent::KeyRelease:
    case QEvent::ShortcutOverride:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
      return !ready_ || closing_;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
      if (!ready_ || closing_) {
        return true;
      }
      const auto* mouse = static_cast<QMouseEvent*>(event);
      const auto point = surface->output.logicalBounds.topLeft() + mouse->position();
      const bool editable = mode_ == Mode::SelectionOnly;
      const auto* actions = view->rootObject()->property("actionBar").value<QQuickItem*>();
      const bool overActions =
          actions && actions->isVisible() && actions->contains(actions->mapFromScene(mouse->position()));
      const auto updateCursor = [&] {
        if (editable) {
          view->setCursor(
              QCursor(overActions || pendingAction_ ? Qt::ArrowCursor : selection_.cursorAt(point, *frame_)));
        }
      };
      if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) &&
          mouse->button() == Qt::LeftButton && !dragView_) {
        updateCursor();
        if (overActions) {
          return false;
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
        if (selection_.begin(point, *frame_, editable)) {
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
        updateCursor();
        redraw();
      } else if (!dragView_) {
        updateCursor();
        return false;
      }
      return true;
    }
    default:
      return false;
  }
}

void RegionOverlay::confirm(Action action) {
  if (finished_ || closing_ || !ready_ || pendingAction_ ||
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
  const auto liveCrop = mode_ == Mode::SelectionOnly ? selection_.liveCropRect(*frame_) : QRect{};
  const Surface* badgeSurface = nullptr;
  QRectF badgeOverlap;
  qreal largestArea = 0;
  if (!liveCrop.isEmpty()) {
    for (const auto& surface : surfaces_) {
      const auto overlap = selection_.rectangle().intersected(surface.output.logicalBounds);
      const auto area = overlap.width() * overlap.height();
      if (area > largestArea ||
          (area > 0 && area == largestArea && surface.screen == QGuiApplication::primaryScreen())) {
        largestArea = area;
        badgeSurface = &surface;
        badgeOverlap = overlap.translated(-surface.output.logicalBounds.topLeft());
      }
    }
  }
  for (const auto& surface : surfaces_) {
    auto* item = static_cast<FrozenOutput*>(surface.preview.get());
    item->setSelection(desktop_ ? RegionSelection{} : selection_);
    item->setProperty("desktop", desktop_);
    item->setProperty("hasCrop", hasCrop);
    item->setProperty("actionsAvailable", actionsAvailable);
    item->setProperty("undoAvailable", actionsAvailable && selection_.canUndo());
    item->setProperty("activeTool", static_cast<int>(selection_.tool()));
    item->setProperty("activeColor", selection_.color());
    const bool showBadge = &surface == badgeSurface;
    item->setProperty("badgePixels", showBadge ? liveCrop.size() : QSize{});
    item->setProperty("visibleCrop", showBadge ? badgeOverlap : QRectF{});
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
  retireViews();
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

void RegionOverlay::retireViews() {
  for (auto& surface : surfaces_) {
    auto* view = surface.view.release();
    if (!view) {
      continue;
    }
    view->removeEventFilter(this);
    if (surface.preview) {
      static_cast<FrozenOutput*>(surface.preview.get())->clearFrame();
    }
    view->setPersistentSceneGraph(false);
    view->setPersistentGraphics(false);
    view->hide();
    view->releaseResources();
    view->deleteLater();
  }
  surfaces_.clear();
}

}  // namespace screenshot

#include "region_overlay.moc"
