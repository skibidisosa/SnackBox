#pragma once
#include <string>
#include <string_view>
#include <chrono>
#include <vector>
#include <unordered_map>

namespace sb {

    using HeaderMap = std::unordered_map<std::string, std::string>;

    std::string now_rfc3339();
    std::string url_decode(std::string_view in);
    std::unordered_map<std::string, std::string> parse_query(std::string_view query);
    std::vector<std::string> split(std::string_view s, char delim);
    bool starts_with(std::string_view s, std::string_view p);
    bool ends_with(std::string_view s, std::string_view p);

} // namespace sb
