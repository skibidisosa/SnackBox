#include "router.hpp"
#include <sstream>
#include <algorithm>
namespace sb {

Router& Router::use(const Handler& m) { middlewares_.push_back(m); return *this; }

Router& Router::add(Method m, std::string path, Handler h) {
    auto [rgx, names] = compile_path(path);
    routes_.push_back(Route{m, std::move(rgx), std::move(names), std::move(h)});
    return *this;
}

std::pair<std::regex, std::vector<std::string>> Router::compile_path(const std::string& path) {
    // Template like: /users/:id/books/:bookId -> ^/users/([^/]+)/books/([^/]+)$
    std::ostringstream pat;
    std::vector<std::string> names;
    pat << '^';
    for (size_t i=0;i<path.size();) {
        if (path[i]==':') {
            size_t j=i+1;
            while (j<path.size() && path[j] != '/' ) j++;
            names.emplace_back(path.substr(i+1, j-(i+1)));
            pat << "([^/]+)";
            i=j;
        } else {
            if (std::isalnum(static_cast<unsigned char>(path[i]))) pat << path[i];
            else {
                // escape regex special
                static const std::string special = R"(\.^$|()[]{}*+?!)";
                if (special.find(path[i]) != std::string::npos) pat << '\\';
                pat << path[i];
            }
            ++i;
        }
    }
    pat << '$';
    return {std::regex(pat.str()), names};
}

std::optional<Response> Router::dispatch(Request& req) const {
    for (auto& m : middlewares_) {
        Response midRes = m(req);
        // Convention: middleware returns 0 status to continue
        if (midRes.status != 0) {
            return midRes;
        }
    }

    for (auto& r : routes_) {
        if (r.method != req.method) continue;
        std::smatch m;
        if (std::regex_match(req.path, m, r.pattern)) {
            req.path_params.clear();
            for (size_t i=0;i<r.paramNames.size();++i) {
                req.path_params[r.paramNames[i]] = m[i+1].str();
            }
            return r.handler(req);
        }
    }
    return std::nullopt;
}

    std::vector<Method> Router::allowed_methods_for(std::string_view path) const {
    std::vector<Method> out;
    std::string path_s(path);               // <-- make it an lvalue
    for (const auto& r : routes_) {
        std::smatch m;
        if (std::regex_match(path_s, m, r.pattern)) {
            if (std::find(out.begin(), out.end(), r.method) == out.end()) {
                out.push_back(r.method);
            }
        }
    }
    return out;
}

} // namespace sb
