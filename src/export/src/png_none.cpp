// No PNG backend (DIADRAW_PNG_BACKEND=none).

#include "diadraw/export/export.hpp"

namespace dd::exporting {

std::unique_ptr<Rasterizer> make_rasterizer() { return nullptr; }

}  // namespace dd::exporting
