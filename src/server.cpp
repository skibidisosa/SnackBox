#include "server.hpp"
#include "http.hpp"
#include "utils.hpp"

#include <cstdio>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

namespace fs = std::filesystem;

namespace sb {

static std::string guess_type(const std::string& p){
    if (ends_with(p, ".html")) return "text/html; charset=utf-8";
    if (ends_with(p, ".css"))  return "text/css; charset=utf-8";
    if (ends_with(p, ".js"))   return "application/javascript";
    if (ends_with(p, ".png"))  return "image/png";
    if (ends_with(p, ".jpg") || ends_with(p, ".jpeg")) return "image/jpeg";
    if (ends_with(p, ".svg"))  return "image/svg+xml";
    return "application/octet-stream";
}

Server::Server(int port): port_(port) {}

int Server::create_listen_socket(int port){
#if defined(_WIN32)
    WSADATA wsaData; WSAStartup(MAKEWORD(2,2), &wsaData);
#endif
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
#if defined(_WIN32)
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons(port);
    if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); std::exit(1);
    }
    if (::listen(sock, 64) < 0) {
        perror("listen"); std::exit(1);
    }
    return sock;
}

void Server::set_nonblock(int fd, bool nb){
#if !defined(_WIN32)
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, nb ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#else
    u_long mode = nb ? 1 : 0;
    ioctlsocket(fd, FIONBIO, &mode);
#endif
}

std::string Server::read_all(int fd) {
    std::string buf; buf.reserve(4096);
    char tmp[4096];
    for (;;) {
#if defined(_WIN32)
        int n = ::recv(fd, tmp, sizeof(tmp), 0);
#else
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
#endif
        if (n <= 0) break;
        buf.append(tmp, tmp + n);
        if (buf.find("\r\n\r\n") != std::string::npos) {
            // rudimentary: stop reading; small requests fit in first packets
            break;
        }
    }
    return buf;
}

void Server::write_all(int fd, const std::string& data){
    size_t sent = 0;
    while (sent < data.size()){
#if defined(_WIN32)
        int n = ::send(fd, data.data()+sent, (int)(data.size()-sent), 0);
#else
        ssize_t n = ::send(fd, data.data()+sent, data.size()-sent, 0);
#endif
        if (n <= 0) break;
        sent += n;
    }
}

Response Server::serve_static(const std::string& req_path) {
    // prevent path traversal
    fs::path p = fs::path(public_dir_) / fs::path(req_path.substr(1));
    p = fs::weakly_canonical(p);
    if (!starts_with(p.string(), fs::weakly_canonical(public_dir_).string())) {
        return Response::NotFound();
    }
    if (fs::is_directory(p)) {
        p /= "index.html";
    }
    if (!fs::exists(p)) return Response::NotFound();
    std::ifstream ifs(p, std::ios::binary);
    std::ostringstream oss; oss << ifs.rdbuf();
    Response r = Response::Text(200, oss.str(), guess_type(p.string()));
    // Note: Content-Length set by Response::Text
    return r;
}

void Server::run() {
    int lsock = create_listen_socket(port_);
    std::printf("[%s] SnackBox listening on http://localhost:%d\n", now_rfc3339().c_str(), port_);

    while (running_) {
#if defined(_WIN32)
        SOCKET csock = ::accept(lsock, nullptr, nullptr);
        if (csock == INVALID_SOCKET) continue;
#else
        int csock = ::accept(lsock, nullptr, nullptr);
        if (csock < 0) continue;
#endif
        std::thread([this, csock](){
            std::string raw = read_all(csock);
            Request req;
            if (!HttpCodec::parse_request(raw, req)) {
                Response bad = Response::Text(400, "Bad Request");
                write_all(csock, HttpCodec::serialize_response(bad));
#if defined(_WIN32)
                closesocket(csock);
#else
                close(csock);
#endif
                return;
            }
            // remote ip not filled in naive accept() branch; skip for now
            Response res;

            // Try router first
            if (router_) {
                auto routed = router_->dispatch(req);
                if (routed) {
                    res = *routed;
                } else {
                    // if no route matched, attempt static
                    res = serve_static(req.path);
                    if (res.status == 404) {
                        // maybe method not allowed?
                        auto allowed = router_->allowed_methods_for(req.path);
                        if (!allowed.empty()) res = Response::MethodNotAllowed();
                    }
                }
            } else {
                res = serve_static(req.path);
            }

            if (!res.headers.count("Date")) res.headers["Date"] = now_rfc3339();
            if (!res.headers.count("Content-Length")) res.headers["Content-Length"] = std::to_string(res.body.size());
            write_all(csock, HttpCodec::serialize_response(res));
#if defined(_WIN32)
            closesocket(csock);
#else
            close(csock);
#endif
        }).detach();
    }
}

void Server::stop(){ running_ = false; }

} // namespace sb
