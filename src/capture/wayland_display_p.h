#pragma once

#include <functional>

class QObject;

namespace screenshot {

// The monitor belongs to context and keeps watching after discovery.
void watchLayerShell(QObject* context, std::function<void(bool)> discovered, std::function<void()> removed);

}  // namespace screenshot
