#include "http.hpp"
#include <sstream>
#include <algorithm>

namespace sb {

Method method_from_string(std::string_view s) {
    if (s=="GET") return Method::GET;
    if (s=="POST") return Method::POST;
    if (s=="PUT") return Method::PUT;
    if (s=="PATCH") return Method::PATCH;
    if (s=="DELETE") return Method::DELETE_;
    if (s=="HEAD") return Method::HEAD;
    if (s=="OPTIONS") return Method::OPTIONS;
    return Method::UNKNOWN;
}

std::string status_message(int code) {
    switch(code){
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

Response Response::Text(int code, std::string text, std::string_view contentType) {
    Response r;
    r.status = code;
    r.body = std::move(text);
    r.headers["Content-Type"] = std::string(contentType);
    r.headers["Content-Length"] = std::to_string(r.body.size());
    return r;
}

Response Response::Html(int code, std::string html) {
    return Text(code, std::move(html), "text/html; charset=utf-8");
}

Response Response::NotFound(std::string msg){ return Text(404, std::move(msg)); }
Response Response::MethodNotAllowed(){ return Text(405, "Method Not Allowed"); }

static std::string trim(std::string s){
    auto issp = [](unsigned char c){return std::isspace(c);};
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){return !issp(c);} ));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){return !issp(c);}).base(), s.end());
    return s;
}

bool HttpCodec::parse_request(const std::string& data, Request& out) {
    // Expect full request in data; simple parser sufficient for tests/dev
    auto pos = data.find("\r\n\r\n");
    if (pos == std::string::npos) return false; // incomplete
    std::string head = data.substr(0, pos);
    out.body = data.substr(pos + 4);

    std::istringstream iss(head);
    std::string line;
    if (!std::getline(iss, line)) return false;
    if (!line.empty() && line.back()=='\r') line.pop_back();

    auto parts = split(line, ' ');
    if (parts.size() < 3) return false;
    out.method = method_from_string(parts[0]);
    out.raw_target = parts[1];

    // Path & query
    auto qpos = out.raw_target.find('?');
    if (qpos == std::string::npos) {
        out.path = out.raw_target;
    } else {
        out.path = out.raw_target.substr(0, qpos);
        out.query = parse_query(out.raw_target.substr(qpos+1));
    }

    // Headers
    out.headers.clear();
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(colon+1));
        out.headers[key] = val;
    }
    return true;
}

std::string HttpCodec::serialize_response(const Response& res) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << res.status << ' ' << status_message(res.status) << "\r\n";
    for (auto& [k,v] : res.headers) {
        oss << k << ": " << v << "\r\n";
    }
    oss << "\r\n";
    oss << res.body;
    return oss.str();
}

} // namespace sb
