#include <cassert>
#include <iostream>
#include <string>
#include "http.hpp"
#include "router.hpp"

using namespace sb;

static void test_parse_request() {
    std::string raw =
        "GET /hello/world?x=1&y=2 HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "User-Agent: curl/8\r\n"
        "\r\n";
    Request req;
    bool ok = HttpCodec::parse_request(raw, req);
    assert(ok);
    assert(req.method == Method::GET);
    assert(req.path == "/hello/world");
    assert(req.query.at("x") == "1");
    assert(req.query.at("y") == "2");
}

static void test_router_path_params() {
    Router r;
    r.get("/hello/:name", [](Request& req){
        assert(req.path_params.at("name") == "alice");
        return Response::Text(200, "ok");
    });
    Request req; req.method=Method::GET; req.path="/hello/alice"; req.raw_target=req.path;
    auto res = r.dispatch(req);
    assert(res.has_value());
    assert(res->status == 200);
}

static void test_405_detection() {
    Router r;
    r.get("/users/:id", [](Request& req){ (void)req; return Response::Text(200,"ok"); });
    auto allowed = r.allowed_methods_for("/users/123");
    bool hasGET = false;
    for (auto m : allowed) if (m == Method::GET) hasGET=true;
    assert(hasGET);
}

int main() {
    test_parse_request();
    test_router_path_params();
    test_405_detection();
    std::cout << "[OK] All tests passed.\n";
    return 0;
}
