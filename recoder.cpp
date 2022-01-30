#include "recoder.h"

#include <uchardet.h>
#include <iconv.h>

#include <array>
#include <cerrno>
#include <memory>
#include <stdexcept>

namespace recoder {
namespace {
bool starts_with(std::string_view bytes, std::string_view prefix) {
    return bytes.size() >= prefix.size() && bytes.substr(0, prefix.size()) == prefix;
}

std::string_view bom_for(std::string_view encoding) {
    if (encoding == "UTF-8") return { "\xef\xbb\xbf", 3 };
    if (encoding == "UTF-16LE") return { "\xff\xfe", 2 };
    if (encoding == "UTF-16BE") return { "\xfe\xff", 2 };
    if (encoding == "UTF-32LE") return { "\xff\xfe\x00\x00", 4 };
    if (encoding == "UTF-32BE") return { "\x00\x00\xfe\xff", 4 };
    return {};
}
}

std::string detect(std::string_view bytes) {
    for (auto encoding : { "UTF-32LE", "UTF-32BE", "UTF-8", "UTF-16LE", "UTF-16BE" }) {
        if (starts_with(bytes, bom_for(encoding))) return encoding;
    }
    if (bytes.empty()) return "UTF-8";
    std::unique_ptr<std::remove_pointer_t<uchardet_t>, decltype(&uchardet_delete)>
        detector(uchardet_new(), &uchardet_delete);
    if (!detector) throw std::runtime_error("无法创建编码检测器");
    if (uchardet_handle_data(detector.get(), bytes.data(), bytes.size()) != 0)
        throw std::runtime_error("编码检测失败");
    uchardet_data_end(detector.get());
    return uchardet_get_charset(detector.get());
}

std::string convert(std::string_view bytes, std::string_view from, std::string_view to) {
    std::string source(from), target(to);
    iconv_t raw = iconv_open(target.c_str(), source.c_str());
    if (raw == reinterpret_cast<iconv_t>(-1))
        throw std::runtime_error("不支持所选编码：" + source + " -> " + target);
    struct closer { void operator()(void* value) const { iconv_close(value); } };
    std::unique_ptr<void, closer> converter(raw);
    std::string result;
    std::array<char, 4096> buffer{};
    char* input = const_cast<char*>(bytes.data());
    size_t remaining = bytes.size();
    bool flushing = false;
    for (;;) {
        char* output = buffer.data();
        size_t available = buffer.size();
        size_t status = flushing
            ? iconv(raw, nullptr, nullptr, &output, &available)
            : iconv(raw, &input, &remaining, &output, &available);
        result.append(buffer.data(), buffer.size() - available);
        if (status == static_cast<size_t>(-1)) {
            if (errno == E2BIG) continue;
            if (errno == EILSEQ || errno == EINVAL)
                throw std::runtime_error("文件包含无效字符，或目标编码无法表示这些字符");
            throw std::runtime_error("编码转换失败");
        }
        if (status != 0) throw std::runtime_error("编码转换会丢失字符，已取消");
        if (flushing) break;
        flushing = true;
    }
    return result;
}

std::string decode(std::string_view bytes, std::string_view encoding) {
    auto bom = bom_for(encoding);
    if (!bom.empty() && starts_with(bytes, bom)) bytes.remove_prefix(bom.size());
    return convert(bytes, encoding, "UTF-8");
}

std::string encode(std::string_view utf8, std::string_view encoding, bool bom) {
    auto prefix = bom_for(encoding);
    if (bom && prefix.empty()) throw std::runtime_error("所选编码不支持 BOM");
    auto result = convert(utf8, "UTF-8", encoding);
    if (bom) result.insert(0, prefix);
    return result;
}
}
