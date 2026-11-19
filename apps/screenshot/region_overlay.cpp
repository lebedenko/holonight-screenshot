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
    const auto end = mapping.rasterEdge(bounds.bottomRight());
    // Use the continuous raster edges to align fractional-scale outputs.
    // Limit the source to the actual canvas without stretching its last pixel.
    const auto clipped = QRectF(start, end).intersected(QRectF(frame.image().rect()));
    target_ = QRectF((clipped.topLeft() - start) / mapping.captureScale, clipped.size() / mapping.captureScale);
    source_ = clipped;
    image_ = frame.image();
    update();
  }

  void setSelection(const QRectF& desktopSelection) {
    selection_ = desktopSelection.translated(-origin_);
    update();
  }

  void paint(QPainter* painter) override {
    const QRectF bounds = boundingRect();
    painter->fillRect(bounds, Qt::black);
    painter->drawImage(target_, image_, source_);

    QPainterPath shade;
    shade.addRect(bounds);
    const auto visibleSelection = selection_.intersected(bounds);
    if (!visibleSelection.isEmpty()) {
      shade.addRect(visibleSelection);
    }
    painter->fillPath(shade, QColor(0, 0, 0, 145));
    if (!selection_.isEmpty()) {
      painter->setPen(QPen(Qt::black, 3));
      painter->drawRect(selection_);
      painter->setPen(QPen(Qt::white, 1));
      painter->drawRect(selection_);
    }
  }

 private:
  QImage image_;
  QPointF origin_;
  QRectF source_;
  QRectF target_;
  QRectF selection_;
};

}  // namespace

RegionOverlay::RegionOverlay(CaptureFrame frame, GrimAcquisition& acquisition)
    : frame_(std::move(frame)), acquisition_(acquisition) {
  layoutTimer_.setInterval(500);
  connect(&layoutTimer_, &QTimer::timeout, this, [this] {
    if (layoutCheck_ == LayoutCheck::Idle) {
      layoutCheck_ = LayoutCheck::Polling;
      acquisition_.verifyLayout();
    }
  });
  connect(&acquisition_, &GrimAcquisition::layoutVerified, this, [this] {
    if (layoutCheck_ == LayoutCheck::Confirming) {
      finish(std::move(pendingCrop_));
    } else if (pendingCrop_.isNull()) {
      layoutCheck_ = LayoutCheck::Idle;
    } else {
      // A poll started before Enter. Check again against the confirmed selection.
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
  const auto screens = QGuiApplication::screens();
  const auto& mapping = frame_.desktopMapping();
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
    surfaces_.push_back({*match, output, std::make_unique<LayerSurfaceHost>()});
  }

  qmlRegisterType<FrozenOutput>("Holonight.Screenshot", 1, 0, "FrozenOutput");
  connect(qGuiApp, &QGuiApplication::screenAdded, this, layoutChanged);
  connect(qGuiApp, &QGuiApplication::screenRemoved, this, layoutChanged);
  connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, layoutChanged);
  timeout_.start(2000);
  for (const auto& surface : surfaces_) {
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
    item->setFrame(frame_, surface.output.logicalBounds);
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
      const auto key = static_cast<QKeyEvent*>(event)->key();
      if (key == Qt::Key_Escape) {
        selection_.cancel();
        finish();
      } else if (ready_ && pendingCrop_.isNull() && (key == Qt::Key_Enter || key == Qt::Key_Return)) {
        auto crop = selection_.confirm(frame_);
        if (!crop.isNull()) {
          pendingCrop_ = std::move(crop);
          layoutTimer_.stop();
          if (layoutCheck_ == LayoutCheck::Idle) {
            layoutCheck_ = LayoutCheck::Confirming;
            acquisition_.verifyLayout();
          }
        }
      }
      return true;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
      if (!ready_ || !pendingCrop_.isNull()) {
        return true;
      }
      const auto* mouse = static_cast<QMouseEvent*>(event);
      const auto point = surface->output.logicalBounds.topLeft() + mouse->position();
      if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) &&
          mouse->button() == Qt::LeftButton && !dragView_) {
        // Wayland keeps delivering the drag to the surface of the button press.
        dragView_ = view;
        selection_.begin(point);
        redraw();
      } else if (view == dragView_ && event->type() == QEvent::MouseMove) {
        selection_.move(point);
        redraw();
      } else if (view == dragView_ && event->type() == QEvent::MouseButtonRelease &&
                 mouse->button() == Qt::LeftButton) {
        selection_.release(point);
        dragView_.clear();
        redraw();
      }
      return true;
    }
    default:
      return false;
  }
}

void RegionOverlay::redraw() {
  for (const auto& surface : surfaces_) {
    static_cast<FrozenOutput*>(surface.host->rootObject())->setSelection(selection_.rectangle());
  }
}

void RegionOverlay::finish(QImage crop, QString error) {
  if (finished_) {
    return;
  }
  finished_ = true;
  timeout_.stop();
  layoutTimer_.stop();
  acquisition_.cancel();
  for (const auto& surface : surfaces_) {
    if (auto* view = surface.host->view()) {
      view->removeEventFilter(this);
    }
    surface.host->close();
  }
  if (!error.isEmpty()) {
    emit failed(error);
  } else if (!crop.isNull()) {
    emit selected(crop);
  } else {
    emit canceled();
  }
}

}  // namespace screenshot

#include "region_overlay.moc"
