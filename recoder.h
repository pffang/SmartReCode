#pragma once

#include <string>
#include <string_view>

namespace recoder {
// An empty result means that detection was inconclusive.
std::string detect(std::string_view bytes);
std::string convert(std::string_view bytes, std::string_view from, std::string_view to);
std::string decode(std::string_view bytes, std::string_view encoding);
std::string encode(std::string_view utf8, std::string_view encoding, bool bom);
}
