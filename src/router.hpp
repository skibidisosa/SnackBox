#pragma once
#include "http.hpp"
#include <functional>
#include <vector>
#include <regex>

namespace sb {

    using Handler = std::function<Response(Request&)>;

    struct Route {
        Method method;
        std::regex pattern;                 // compiled from path template
        std::vector<std::string> paramNames;
        Handler handler;
    };

    class Router {
    public:
        Router& use(const Handler& middleware); // middleware runs before route
        Router& get (std::string path, Handler h){ return add(Method::GET,  std::move(path), std::move(h)); }
        Router& post(std::string path, Handler h){ return add(Method::POST, std::move(path), std::move(h)); }
        Router& put (std::string path, Handler h){ return add(Method::PUT,  std::move(path), std::move(h)); }
        Router& del (std::string path, Handler h){ return add(Method::DELETE_,std::move(path), std::move(h)); }

        std::optional<Response> dispatch(Request& req) const;
        std::vector<Method> allowed_methods_for(std::string_view path) const;

    private:
        Router& add(Method m, std::string path, Handler h);
        static std::pair<std::regex, std::vector<std::string>> compile_path(const std::string& path);
        std::vector<Handler> middlewares_;
        std::vector<Route> routes_;
    };

} // namespace sb
