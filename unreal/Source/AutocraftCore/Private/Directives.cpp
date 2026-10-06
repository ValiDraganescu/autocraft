// Port of the logic of Sources/GameCore/Directives.swift that `Types.cpp`
// leaves out: `Request.title`, which reads `Simulation.title`. (The data
// types are in `Types.h`; `ore`, `hydrogen`, `hydrogenWanted`, `Stance.title`
// and `Player.orders` are in `Types.cpp` and `Types.h`.)
#include "Simulation.h"
#include "Types.h"

namespace ac {

std::string Request::title() const {
    if (auto c = what.as<What::Unit>()) return Simulation::title(c->kind);
    if (auto c = what.as<What::Building>()) return Simulation::title(c->kind);
    if (auto c = what.as<What::Upgrade>()) return name(c->upgrade);
    return "";
}

} // namespace ac
