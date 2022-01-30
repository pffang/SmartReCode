#include "recoder.h"

#include <iostream>
#include <stdexcept>
#include <string>

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        const std::string text = "\xe4\xbd\xa0\xe5\xa5\xbd";
        std::string utf8_sample;
        for (int i = 0; i < 32; ++i) utf8_sample += text;
        check(recoder::detect(utf8_sample) == "UTF-8", "UTF-8 without BOM detection");
        const auto gb = recoder::encode(text, "GB18030", false);
        check(recoder::decode(gb, "GB18030") == text, "GB18030 round trip");
        const auto utf16 = recoder::encode(text, "UTF-16LE", true);
        check(recoder::detect(utf16) == "UTF-16LE", "UTF-16 BOM detection");
        check(recoder::decode(utf16, "UTF-16LE") == text, "UTF-16 BOM removal");
        check(recoder::detect(std::string("\xff\xfe\0\0", 4)) == "UTF-32LE", "UTF-32 BOM precedence");
        const auto utf32 = recoder::encode(text, "UTF-32BE", true);
        check(recoder::detect(utf32) == "UTF-32BE" && recoder::decode(utf32, "UTF-32BE") == text, "UTF-32 round trip");
        check(recoder::detect("") == "UTF-8", "empty file");
        check(recoder::decode(std::string("\xef\xbb\xbf", 3) + text, "UTF-8") == text, "UTF-8 BOM removal");
        check(recoder::convert(std::string(12000, 'a'), "UTF-8", "UTF-16LE").size() == 24000, "buffer growth");
        const std::string japanese = "\xe3\x81\x82";
        const auto iso2022 = recoder::encode(japanese, "ISO-2022-JP", false);
        check(iso2022.ends_with("\x1b(B") && recoder::decode(iso2022, "ISO-2022-JP") == japanese,
              "stateful encoding flush");
        bool rejected = false;
        try { recoder::decode(std::string("\xff", 1), "UTF-8"); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "invalid input must fail");
        rejected = false;
        try { recoder::encode(text, "ASCII", false); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "unrepresentable character must fail");
        rejected = false;
        try { recoder::encode(text, "GB18030", true); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "non-Unicode BOM must fail");
        std::cout << "All recoder tests passed\n";
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
