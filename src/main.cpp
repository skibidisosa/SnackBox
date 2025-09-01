// Snack Box — minimal raw TCP HTTP server (C++17, no third-party libs)
// - Listens on :8080
// - Prints incoming HTTP request to console
// - Responds: "Hello Snack Box!"

#include <iostream>
#include <string>
#include <csignal>
#include <cstring>

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

  // Reuse address so restarts don't block bind()
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

  std::cout << "Snack Box server listening on http://localhost:8080\n";

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

    // Read request until end of headers (\r\n\r\n) or socket would block/close.
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
      if (request.size() > 1 << 20) break; // safety: cap at ~1MB
    }

    // Log request to console
    std::cout << "----- Incoming Request -----\n"
              << request << "\n----------------------------\n";

    // Minimal HTTP/1.1 response
    const std::string body = "Hello Snack Box!";
    std::string resp;
    resp += "HTTP/1.1 200 OK\r\n";
    resp += "Content-Type: text/plain; charset=utf-8\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "Connection: close\r\n";
    resp += "\r\n";
    resp += body;

    // Send all bytes
    size_t sent = 0;
    while (sent < resp.size()) {
#if defined(_WIN32)
      int s = send(client_fd, resp.data() + sent, static_cast<int>(resp.size() - sent), 0);
#else
      ssize_t s = send(client_fd, resp.data() + sent, resp.size() - sent, 0);
#endif
      if (s <= 0) break;
      sent += static_cast<size_t>(s);
    }

    closesock(client_fd);
  }

  closesock(server_fd);
#if defined(_WIN32)
  WSACleanup();
#endif
  return 0;
}
