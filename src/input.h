#pragma once
#include <charconv>
#include <string_view>
#include <system_error>

// Consume the complete token, with no exceptions and no partial publication.
template<class T>
bool parseInteger(std::string_view text, T minimum, T maximum, T& value) {
    if (text.empty()) return false;
    T parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || parsed < minimum || parsed > maximum) return false;
    value = parsed;
    return true;
}
