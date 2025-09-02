#include "utils.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace sb {

std::string now_rfc3339() {
    using namespace std::chrono;
    auto tp = system_clock::now();
    std::time_t t = system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

static int from_hex(char c) {
    if ('0'<=c && c<='9') return c-'0';
    if ('a'<=c && c<='f') return c-'a'+10;
    if ('A'<=c && c<='F') return c-'A'+10;
    return 0;
}

std::string url_decode(std::string_view in) {
    std::string out;
    for (size_t i=0;i<in.size();++i) {
        if (in[i] == '%' && i+2 < in.size()) {
            out.push_back(static_cast<char>(from_hex(in[i+1]) * 16 + from_hex(in[i+2])));
            i+=2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> parse_query(std::string_view query) {
    std::unordered_map<std::string, std::string> out;
    size_t start = 0;
    while (start < query.size()) {
        size_t amp = query.find('&', start);
        if (amp == std::string_view::npos) amp = query.size();
        auto pair = query.substr(start, amp - start);
        size_t eq = pair.find('=');
        if (eq == std::string_view::npos) {
            out[std::string(url_decode(pair))] = "";
        } else {
            out[std::string(url_decode(pair.substr(0,eq)))] = url_decode(pair.substr(eq+1));
        }
        start = amp + 1;
    }
    return out;
}

std::vector<std::string> split(std::string_view s, char d) {
    std::vector<std::string> v;
    size_t start=0;
    while (start < s.size()) {
        size_t pos = s.find(d, start);
        if (pos == std::string_view::npos) pos = s.size();
        v.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return v;
}

bool starts_with(std::string_view s, std::string_view p){ return s.rfind(p,0)==0; }
bool ends_with(std::string_view s, std::string_view p){
    return p.size()<=s.size() && std::equal(p.rbegin(), p.rend(), s.rbegin());
}

} // namespace sb
