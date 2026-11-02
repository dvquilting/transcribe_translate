// pathutil.h - UTF-8 <-> std::filesystem::path, shared with the other programs
// (../common/src/utf8_path.h); brought into namespace tl for the code here.
#pragma once

#include "utf8_path.h"

namespace tl {

using pk::path_from_utf8;
using pk::path_to_utf8;

}  // namespace tl
