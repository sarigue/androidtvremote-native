#pragma once

#include "androidtvremote/export.hpp"
#include "androidtvremote/types.hpp"

#include <optional>
#include <string_view>

namespace androidtvremote {

// Resolve the names accepted by the Python androidtvremote2 API and by
// Freebox Pop Remote. Names are case-insensitive and KEYCODE_ is optional.
ANDROIDTVREMOTE_API std::optional<std::int32_t> keyCodeFromName(std::string_view name);

}  // namespace androidtvremote
