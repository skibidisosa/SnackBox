// Snack Box — minimal raw TCP HTTP server (C++17, no third-party libs)
// Strict routing with static files from /public (works from CLion build dir)

#include <iostream>
#include <string>
#include <csignal>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  using socklen_t = int;
  static void closesock(SOCKET s){ closesocket(s); }
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <errno.h>
  using SOCKET = int;
  static void closesock(int s){ close(s); }
#endif

static void ignore_sigpipe() {
#if !defined(_WIN32)
  std::signal(SIGPIPE, SIG_IGN);
#endif
}

static std::string guess_type(const std::string& p){
  auto ends_with = [&](const char* ext){
    size_t n = std::strlen(ext);
    return p.size() >= n && p.compare(p.size()-n, n, ext) == 0;
  };
  if (ends_with(".html")) return "text/html; charset=utf-8";
  if (ends_with(".css"))  return "text/css; charset=utf-8";
  if (ends_with(".js"))   return "application/javascript";
  if (ends_with(".png"))  return "image/png";
  if (ends_with(".jpg") || ends_with(".jpeg")) return "image/jpeg";
  if (ends_with(".svg"))  return "image/svg+xml";
  return "application/octet-stream";
}

static void send_response(SOCKET fd, int code, const std::string& status,
                          const std::string& ctype, const std::string& body) {
  std::string resp;
  resp += "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
  if (!ctype.empty()) resp += "Content-Type: " + ctype + "\r\n";
  resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  resp += "Connection: close\r\n\r\n";
  resp += body;
  size_t sent = 0;
  while (sent < resp.size()) {
#if defined(_WIN32)
    int n = send(fd, resp.data()+sent, static_cast<int>(resp.size()-sent), 0);
#else
    ssize_t n = send(fd, resp.data()+sent, resp.size()-sent, 0);
#endif
    if (n <= 0) break;
    sent += static_cast<size_t>(n);
  }
}

static void send_404(SOCKET fd, const std::string& target) {
  const std::string html =
    "<!doctype html><meta charset=utf-8>"
    "<title>404 Not Found</title>"
    "<style>body{font-family:system-ui;margin:2rem;color:#222}code{background:#f6f6f6;padding:2px 4px;border-radius:4px}</style>"
    "<h1>404 — Not Found</h1>"
    "<p>No page for <code>" + target + "</code>.</p>"
    "<p>Try <a href=\"/\">home</a> or a valid file under <code>/public/</code>.</p>";
  send_response(fd, 404, "Not Found", "text/html; charset=utf-8", html);
}

// --- NEW: resolve files from public/ regardless of CLion working dir ---
static bool load_from_public(const std::string& rel, std::string& out, std::string& fullPathOut) {
  const char* prefixes[] = {
    "public/",        // running from project root
    "../public/",     // running from build dir: project/cmake-build-debug
    "../../public/"   // running from a deeper build dir layout
  };
  for (const char* pref : prefixes) {
    std::string full = std::string(pref) + rel;
    std::ifstream ifs(full, std::ios::binary);
    if (ifs) {
      std::ostringstream oss; oss << ifs.rdbuf();
      out = oss.str();
      fullPathOut = full;
      return true;
    }
  }
  return false;
}

int main() {
  ignore_sigpipe();

#if defined(_WIN32)
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }
#endif

  SOCKET server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
#if defined(_WIN32)
  if (server_fd == INVALID_SOCKET) {
    std::cerr << "socket() failed: " << WSAGetLastError() << "\n";
    return 1;
  }
#else
  if (server_fd < 0) {
    std::perror("socket");
    return 1;
  }
#endif

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
#if defined(_WIN32)
    std::cerr << "setsockopt() failed: " << WSAGetLastError() << "\n";
#else
    std::perror("setsockopt");
#endif
    closesock(server_fd);
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
  addr.sin_port = htons(8080);

  if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
#if defined(_WIN32)
    std::cerr << "bind() failed: " << WSAGetLastError()
              << " (is something already on :8080?)\n";
#else
    std::perror("bind (is something already on :8080?)");
#endif
    closesock(server_fd);
    return 1;
  }

  if (listen(server_fd, SOMAXCONN) < 0) {
#if defined(_WIN32)
    std::cerr << "listen() failed: " << WSAGetLastError() << "\n";
#else
    std::perror("listen");
#endif
    closesock(server_fd);
    return 1;
  }

  std::cout << "[SnackBox strict] http://localhost:8080\n";

  for (;;) {
    sockaddr_in client{};
    socklen_t clen = sizeof(client);
#if defined(_WIN32)
    SOCKET client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client), &clen);
    if (client_fd == INVALID_SOCKET) {
      std::cerr << "accept() failed: " << WSAGetLastError() << "\n";
      continue;
    }
#else
    SOCKET client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client), &clen);
    if (client_fd < 0) {
      std::perror("accept");
      continue;
    }
#endif

    // Read request until end of headers (\r\n\r\n).
    std::string request;
    char buf[4096];
    for (;;) {
#if defined(_WIN32)
      int n = recv(client_fd, buf, static_cast<int>(sizeof(buf)), 0);
#else
      ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
#endif
      if (n <= 0) break;
      request.append(buf, buf + n);
      if (request.find("\r\n\r\n") != std::string::npos) break;
      if (request.size() > (1<<20)) break;
    }

    // Parse first line: METHOD SP TARGET SP HTTP/1.1
    std::string method = "GET", target = "/";
    if (!request.empty()) {
      auto end = request.find("\r\n");
      std::string first = request.substr(0, end);
      std::istringstream iss(first);
      std::string httpver;
      iss >> method >> target >> httpver;
    }

    // ---- Strict routing ----
    if (target == "/" || target.empty()) {
      const std::string body =
        "<!doctype html><meta charset=utf-8>"
        "<h1>Hello Snack Box!</h1>"
        "<ul>"
        "<li>Static: <a href=\"/public/index.html\">/public/index.html</a></li>"
        "<li>Anything else returns 404</li>"
        "</ul>";
      send_response(client_fd, 200, "OK", "text/html; charset=utf-8", body);

    } else if (target == "/public" || target == "/public/") {
      std::string content, full;
      if (load_from_public("index.html", content, full)) {
        send_response(client_fd, 200, "OK", guess_type(full), content);
      } else {
        send_404(client_fd, target);
      }

    } else if (target.rfind("/public/", 0) == 0) {
      std::string rel = target.substr(8); // strip '/public/'
      if (rel.find("..") != std::string::npos) {
        send_response(client_fd, 403, "Forbidden", "text/plain; charset=utf-8", "Forbidden");
      } else {
        std::string content, full;
        if (load_from_public(rel, content, full)) {
          send_response(client_fd, 200, "OK", guess_type(full), content);
        } else {
          send_404(client_fd, target);
        }
      }

    } else {
      send_404(client_fd, target);
    }

    closesock(client_fd);
  }

  closesock(server_fd);
#if defined(_WIN32)
  WSACleanup();
#endif
  return 0;
}
