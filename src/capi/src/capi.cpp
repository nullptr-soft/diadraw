#include "diadraw/diadraw.h"

#include <cstdlib>
#include <utility>

#include "diadraw/model/json.hpp"
#include "internal.hpp"

#ifndef DIADRAW_VERSION
#define DIADRAW_VERSION "0.0.0"
#endif

using dd::capi::copy_bytes;
using dd::capi::guarded;
using dd::capi::set_error;

extern "C" {

const char* dd_version(void) { return DIADRAW_VERSION; }
int dd_abi_version(void) { return DD_ABI_VERSION; }

void dd_string_free(char* s) { std::free(s); }

dd_document* dd_document_new(void) {
  return guarded<dd_document*>(nullptr, nullptr, [] { return new dd_document{}; });
}

dd_document* dd_document_from_json(const char* model_json, char** error) {
  if (!model_json) {
    set_error(error, "model_json is NULL");
    return nullptr;
  }
  return guarded<dd_document*>(error, nullptr, [&]() -> dd_document* {
    auto json = dd::Json::parse(model_json);
    if (!json) {
      set_error(error, json.error().message);
      return nullptr;
    }
    auto model = dd::model_from_json(*json);
    if (!model) {
      set_error(error, model.error().message);
      return nullptr;
    }
    return new dd_document{dd::History(std::move(*model))};
  });
}

void dd_document_free(dd_document* doc) { delete doc; }

dd_status dd_document_apply(dd_document* doc, const char* operation_json, char** error) {
  if (!doc || !operation_json) {
    set_error(error, "NULL argument");
    return DD_INVALID_ARGUMENT;
  }
  return guarded<dd_status>(error, DD_FAILED, [&]() -> dd_status {
    auto json = dd::Json::parse(operation_json);
    if (!json) {
      set_error(error, json.error().message);
      return DD_INVALID_ARGUMENT;
    }
    auto op = dd::operation_from_json(*json);
    if (!op) {
      set_error(error, op.error().message);
      return DD_INVALID_ARGUMENT;
    }
    auto applied = doc->history.apply(*op);
    if (!applied) {
      set_error(error, applied.error().message);
      return DD_REJECTED;
    }
    return DD_OK;
  });
}

int dd_document_undo(dd_document* doc) {
  if (!doc || !doc->history.can_undo()) return 0;
  return guarded<int>(nullptr, 0, [&] {
    doc->history.undo();
    return 1;
  });
}

int dd_document_redo(dd_document* doc) {
  if (!doc || !doc->history.can_redo()) return 0;
  return guarded<int>(nullptr, 0, [&] {
    doc->history.redo();
    return 1;
  });
}

int dd_document_can_undo(const dd_document* doc) { return doc && doc->history.can_undo() ? 1 : 0; }
int dd_document_can_redo(const dd_document* doc) { return doc && doc->history.can_redo() ? 1 : 0; }

char* dd_document_model_json(const dd_document* doc) {
  if (!doc) return nullptr;
  return guarded<char*>(nullptr, nullptr, [&] { return copy_bytes(dd::to_json(doc->history.model()).dump()); });
}

char* dd_document_resolve_view_json(const dd_document* doc, const char* view_id, char** error) {
  if (!doc || !view_id) {
    set_error(error, "NULL argument");
    return nullptr;
  }
  return guarded<char*>(error, nullptr, [&]() -> char* {
    auto resolved = dd::resolve_view(doc->history.model(), view_id);
    if (!resolved) {
      set_error(error, resolved.error().message);
      return nullptr;
    }
    return copy_bytes(dd::to_json(*resolved).dump());
  });
}

}  // extern "C"
