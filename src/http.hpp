#pragma once
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>
#include <optional>
#include "utils.hpp"

namespace sb {

    enum class Method { GET, POST, PUT, PATCH, DELETE_, HEAD, OPTIONS, UNKNOWN };

    struct Request {
        Method method{Method::UNKNOWN};
        std::string raw_target;        // e.g. /hello/world?x=1
        std::string path;              // e.g. /hello/world
        std::unordered_map<std::string, std::string> query;
        HeaderMap headers;
        std::string body;
        std::unordered_map<std::string, std::string> path_params;
        std::string remote_ip;
    };

    struct Response {
        int status{200};
        HeaderMap headers{{"Server","SnackBox/0.1"},{"Connection","close"}};
        std::string body;

        static Response Text(int code, std::string text, std::string_view contentType="text/plain; charset=utf-8");
        static Response Html(int code, std::string html);
        static Response NotFound(std::string msg="Not Found");
        static Response MethodNotAllowed();
    };

    Method method_from_string(std::string_view s);
    std::string status_message(int code);

    // Minimal HTTP parsing/serialization
    class HttpCodec {
    public:
        static bool parse_request(const std::string& data, Request& out);
        static std::string serialize_response(const Response& res);
    };

} // namespace sb
