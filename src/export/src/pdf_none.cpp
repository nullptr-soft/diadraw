// No PDF backend (DIADRAW_PDF_BACKEND=none).

#include "diadraw/export/export.hpp"

namespace dd::exporting {

std::unique_ptr<PdfWriter> make_pdf_writer() { return nullptr; }

}  // namespace dd::exporting
