// English number preparation follows this project's Outspoken-derived parser.
#include "settings.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <vector>

namespace messenger_sapi {
static const char* ones[] = {"zero",    "one",     "two",       "three",    "four",
                             "five",    "six",     "seven",     "eight",    "nine",
                             "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
                             "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
static const char* tens[] = {"",      "",      "twenty",  "thirty", "forty",
                             "fifty", "sixty", "seventy", "eighty", "ninety"};
static std::string cardinal(unsigned long long n) {
    if (n < 20)
        return ones[n];
    if (n < 100)
        return std::string(tens[n / 10]) + (n % 10 ? " " + cardinal(n % 10) : "");
    if (n < 1000)
        return cardinal(n / 100) + " hundred" + (n % 100 ? " " + cardinal(n % 100) : "");
    const unsigned long long scales[] = {1000000000000000ULL, 1000000000000ULL, 1000000000ULL,
                                         1000000ULL, 1000ULL};
    const char* names[] = {"quadrillion", "trillion", "billion", "million", "thousand"};
    for (int i = 0; i < 5; i++)
        if (n >= scales[i])
            return cardinal(n / scales[i]) + " " + names[i] +
                   (n % scales[i] ? " " + cardinal(n % scales[i]) : "");
    return "large number";
}
static std::string whole(std::string s, bool ordinal) {
    auto first = s.find_first_not_of('0');
    s = first == std::string::npos ? "0" : s.substr(first);
    if (s.size() > 18)
        return "large number";
    std::string result = cardinal(std::stoull(s));
    if (!ordinal)
        return result;
    size_t pos = result.find_last_of(' ');
    pos = pos == std::string::npos ? 0 : pos + 1;
    std::string last = result.substr(pos), changed;
    const char* irregular[][2] = {{"one", "first"},     {"two", "second"},   {"three", "third"},
                                  {"five", "fifth"},    {"eight", "eighth"}, {"nine", "ninth"},
                                  {"twelve", "twelfth"}};
    for (auto& entry : irregular)
        if (last == entry[0])
            changed = entry[1];
    if (changed.empty())
        changed = last.back() == 'y' ? last.substr(0, last.size() - 1) + "ieth" : last + "th";
    return result.substr(0, pos) + changed;
}
static std::string digits(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (!r.empty())
            r += ' ';
        r += ones[c - '0'];
    }
    return r;
}
std::string normalise(const std::wstring& input, bool spellDigits, bool spell) {
    // Decompose accented Latin letters, retaining ASCII and replacing unsupported
    // characters. Control bytes never reach the original driver's command parser.
    int count = NormalizeString(NormalizationKD, input.data(), int(input.size()), nullptr, 0);
    std::wstring decomposed(count > 0 ? count : input.size(), L' ');
    if (count > 0) {
        count = NormalizeString(NormalizationKD, input.data(), int(input.size()), &decomposed[0],
                                count);
        decomposed.resize(count > 0 ? count : 0);
    } else
        decomposed = input;
    std::string text;
    for (wchar_t c : decomposed) {
        if (c >= 0x300 && c <= 0x36f)
            continue;
        if (c == 0x2018 || c == 0x2019)
            c = '\'';
        if (c == 0x201c || c == 0x201d)
            c = '"';
        if (c == 0x2013 || c == 0x2014)
            c = '-';
        if (c == 0x2026) {
            text += "...";
            continue;
        }
        if (c == '~') {
            text += " tilde ";
            continue;
        }
        char a = c < 32 || c == 127 ? ' ' : c < 128 ? char(c) : '?';
        if (spell && !text.empty())
            text += ' ';
        text += a;
    }
    // C++ ECMAScript has no lookbehind; inspect the preceding character in the loop.
    static const std::regex number(
        R"((\d+(?:st|nd|rd|th)\b)|(-?\d{1,3}(?:,\d{3})+(?:\.\d+)?)(?![\w.])|(-?\d+\.\d+)(?![\w.])|(-?\d+)(?![\w])(?!\.\d))",
        std::regex::icase);
    std::string out;
    for (size_t i = 0; i < text.size();) {
        char before = i ? text[i - 1] : ' ';
        std::match_results<std::string::const_iterator> m;
        bool eligible = !(std::isalnum((unsigned char)before) || before == '_' || before == '.');
        if (eligible && std::regex_search(text.cbegin() + i, text.cend(), m, number,
                                          std::regex_constants::match_continuous)) {
            std::string raw = m.str();
            bool ord = m[1].matched;
            if (ord)
                out += whole(raw.substr(0, raw.size() - 2), true);
            else {
                bool negative = raw[0] == '-';
                if (negative)
                    raw.erase(0, 1);
                raw.erase(std::remove(raw.begin(), raw.end(), ','), raw.end());
                auto dot = raw.find('.');
                std::string head = raw.substr(0, dot);
                if (negative)
                    out += "minus ";
                out += (spellDigits || spell) ? digits(head) : whole(head, false);
                if (dot != std::string::npos)
                    out += " point " + digits(raw.substr(dot + 1));
            }
            i += m.length();
        } else
            out += text[i++];
    }
    return out;
}
} // namespace messenger_sapi
