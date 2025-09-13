// Snack Box — minimal raw TCP HTTP server (C++17, no third-party libs)
// Strict routing with static files from /public
// + Local search over /data/index.tsv at /search?q=...&type=...&limit=...
// + Docs viewer: /docs (index from data/docs/index.tsv) and /docs/:slug (html from data/docs/:slug.html)

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>
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
  signal(SIGPIPE, SIG_IGN);
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
  if (ends_with(".json")) return "application/json";
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
    "<p>Try <a href=\"/\">home</a>, <a href=\"/public/index.html\">UI</a>, or <a href=\"/docs\">docs</a>.</p>";
  send_response(fd, 404, "Not Found", "text/html; charset=utf-8", html);
}

// Resolve files from public/ regardless of CLion working dir
static bool load_from_public(const std::string& rel, std::string& out, std::string& fullPathOut) {
  const char* prefixes[] = { "public/", "../public/", "../../public/" };
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

// Resolve files from data/ regardless of CLion working dir
static bool load_from_data(const std::string& rel, std::string& out, std::string& fullPathOut) {
  const char* prefixes[] = { "data/", "../data/", "../../data/" };
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

static std::string to_lower(std::string s){
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
static std::string url_decode(const std::string& in){
  std::string out;
  for (size_t i=0;i<in.size();++i){
    if (in[i] == '%' && i+2 < in.size()){
      auto hex = [](char c)->int{
        if ('0'<=c && c<='9') return c-'0';
        if ('a'<=c && c<='f') return c-'a'+10;
        if ('A'<=c && c<='F') return c-'A'+10;
        return 0;
      };
      out.push_back((char)(hex(in[i+1])*16 + hex(in[i+2])));
      i+=2;
    } else if (in[i] == '+') out.push_back(' ');
    else out.push_back(in[i]);
  }
  return out;
}

static std::vector<std::pair<std::string,std::string>> parse_query_kv(const std::string& q){
  std::vector<std::pair<std::string,std::string>> kv;
  size_t start=0;
  while (start < q.size()){
    size_t amp = q.find('&', start); if (amp==std::string::npos) amp=q.size();
    std::string pair = q.substr(start, amp-start);
    size_t eq = pair.find('=');
    std::string k = url_decode(eq==std::string::npos ? pair : pair.substr(0,eq));
    std::string v = url_decode(eq==std::string::npos ? ""   : pair.substr(eq+1));
    kv.emplace_back(k,v);
    start = amp+1;
  }
  return kv;
}

struct Item {
  std::string type, name, desc, tags_str, url;
  std::vector<std::string> tags() const {
    std::vector<std::string> t;
    std::string cur;
    for (char c: tags_str) {
      if (c==',' || c==';' || std::isspace((unsigned char)c)) {
        if (!cur.empty()) { t.push_back(cur); cur.clear(); }
      } else cur.push_back(c);
    }
    if (!cur.empty()) t.push_back(cur);
    return t;
  }
};

static std::vector<Item> load_index_tsv(){
  std::vector<Item> items;
  std::string data, full;
  if (!load_from_data("index.tsv", data, full)) return items;
  std::istringstream iss(data);
  std::string line; bool header=true;
  while (std::getline(iss, line)){
    if (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
    if (line.empty()) continue;
    if (header){ header=false; continue; }
    // Split by TAB: type name description tags url
    std::vector<std::string> cols;
    size_t start=0;
    while (start < line.size()){
      size_t tab = line.find('\t', start);
      if (tab==std::string::npos) tab=line.size();
      cols.emplace_back(line.substr(start, tab-start));
      start = tab + 1;
    }
    while (cols.size() < 5) cols.emplace_back("");
    Item it{cols[0], cols[1], cols[2], cols[3], cols[4]};
    items.push_back(std::move(it));
  }
  return items;
}

// ---- Search ----
static std::string json_escape(const std::string& s){
  std::string out; out.reserve(s.size()+8);
  for (char c : s){
    switch (c){
      case '\"': out+="\\\""; break;
      case '\\': out+="\\\\"; break;
      case '\b': out+="\\b";  break;
      case '\f': out+="\\f";  break;
      case '\n': out+="\\n";  break;
      case '\r': out+="\\r";  break;
      case '\t': out+="\\t";  break;
      default:
        if ((unsigned char)c < 0x20){
          char buf[7]; std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
          out += buf;
        } else out.push_back(c);
    }
  }
  return out;
}

static std::string json_for_items(const std::string& q_show,
                                  const std::string& type_show,
                                  const std::vector<Item>& results){
  std::ostringstream oss;
  oss << "{";
  oss << "\"query\":\"" << json_escape(q_show) << "\",";
  if (!type_show.empty()) oss << "\"type\":\"" << json_escape(type_show) << "\",";
  oss << "\"count\":" << results.size() << ",";
  oss << "\"results\":[";
  for (size_t i=0;i<results.size();++i){
    const auto& it = results[i];
    oss << "{";
    oss << "\"type\":\"" << json_escape(it.type) << "\",";
    oss << "\"name\":\"" << json_escape(it.name) << "\",";
    oss << "\"description\":\"" << json_escape(it.desc) << "\",";
    oss << "\"url\":\"" << json_escape(it.url) << "\",";
    oss << "\"tags\":[";
    auto t = it.tags();
    for (size_t j=0;j<t.size();++j){
      oss << "\"" << json_escape(t[j]) << "\"";
      if (j+1<t.size()) oss << ",";
    }
    oss << "]";
    oss << "}";
    if (i+1<results.size()) oss << ",";
  }
  oss << "]}";
  return oss.str();
}

static void handle_search(SOCKET client_fd, const std::string& target) {
  std::string path = target, query;
  size_t qpos = target.find('?');
  if (qpos != std::string::npos) { path = target.substr(0,qpos); query = target.substr(qpos+1); }

  std::string q, type; int limit = 50;
  for (auto& [k,v] : parse_query_kv(query)) {
    if (k=="q") q = v;
    else if (k=="type") type = to_lower(v);
    else if (k=="limit") { try { limit = std::max(1, std::min(1000, std::stoi(v))); } catch(...){} }
  }

  auto items = load_index_tsv();
  std::vector<Item> out;
  std::string ql = to_lower(q);
  for (const auto& it : items){
    if (!type.empty() && to_lower(it.type) != type) continue;
    std::string hay = to_lower(it.name + " " + it.desc + " " + it.tags_str);
    if (ql.empty() || hay.find(ql) != std::string::npos){
      out.push_back(it);
      if ((int)out.size() >= limit) break;
    }
  }

  const std::string body = json_for_items(q, type, out);
  send_response(client_fd, 200, "OK", "application/json; charset=utf-8", body);
}

// ---- Docs ----
struct DocRow { std::string slug, title, summary; };

static std::vector<DocRow> load_docs_index(){
  std::vector<DocRow> rows;
  std::string data, full;
  if (!load_from_data("docs/index.tsv", data, full)) return rows;
  std::istringstream iss(data);
  std::string line; bool header=true;
  while (std::getline(iss, line)){
    if (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
    if (line.empty()) continue;
    if (header){ header=false; continue; }
    std::vector<std::string> cols;
    size_t start=0;
    while (start < line.size()){
      size_t tab = line.find('\t', start);
      if (tab==std::string::npos) tab=line.size();
      cols.emplace_back(line.substr(start, tab-start));
      start = tab + 1;
    }
    while (cols.size() < 3) cols.emplace_back("");
    rows.push_back(DocRow{cols[0], cols[1], cols[2]});
  }
  return rows;
}

static void handle_docs_index(SOCKET client_fd) {
  auto rows = load_docs_index();
  std::ostringstream html;
  html
    << "<!doctype html><meta charset=utf-8>"
    << "<title>Docs — SnackBox</title>"
    << "<style>body{font-family:system-ui;margin:2rem}a{text-decoration:none} .muted{color:#666} .grid{display:grid;gap:.8rem} .card{background:#f6f7f9;padding:.9rem 1rem;border-radius:.8rem} .t{font-weight:600}</style>"
    << "<h1>SnackBox Docs</h1><p class=muted>Index from <code>data/docs/index.tsv</code></p>"
    << "<div class=grid>";
  for (auto& r : rows){
    html << "<div class=card><div class=t><a href=\"/docs/" << r.slug << "\">" << r.title
         << "</a></div><div>" << r.summary << "</div></div>";
  }
  html << "</div>";
  send_response(client_fd, 200, "OK", "text/html; charset=utf-8", html.str());
}

static void handle_docs_slug(SOCKET client_fd, const std::string& slug) {
  // sanitize slug: only allow [a-z0-9-_]
  for (char c : slug) {
    if (!(std::isalnum((unsigned char)c) || c=='-' || c=='_')) {
      send_response(client_fd, 400, "Bad Request", "text/plain; charset=utf-8", "Invalid slug");
      return;
    }
  }
  std::string content, full;
  if (load_from_data("docs/" + slug + ".html", content, full)) {
    send_response(client_fd, 200, "OK", "text/html; charset=utf-8", content);
  } else {
    send_404(client_fd, "/docs/" + slug);
  }
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
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
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

    // Read request
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

    // Parse first line
    std::string method = "GET", target = "/";
    if (!request.empty()) {
      auto end = request.find("\r\n");
      std::string first = request.substr(0, end);
      std::istringstream iss(first);
      std::string httpver;
      iss >> method >> target >> httpver;
    }

    // Path without query
    std::string path = target;
    size_t qpos = path.find('?');
    if (qpos != std::string::npos) path = path.substr(0, qpos);

    // ---- Strict routing ----
    if (path == "/" || path.empty()) {
      const std::string body =
        "<!doctype html><meta charset=utf-8>"
        "<h1>Hello Snack Box!</h1>"
        "<ul>"
        "<li>Static UI: <a href=\"/public/index.html\">/public/index.html</a></li>"
        "<li>Local search API: <code>/search?q=router&type=doc</code></li>"
        "<li>Docs index: <a href=\"/docs\">/docs</a></li>"
        "<li>Anything else returns 404</li>"
        "</ul>";
      send_response(client_fd, 200, "OK", "text/html; charset=utf-8", body);

    } else if (path == "/public" || path == "/public/") {
      std::string content, full;
      if (load_from_public("index.html", content, full)) {
        send_response(client_fd, 200, "OK", guess_type(full), content);
      } else {
        send_404(client_fd, target);
      }

    } else if (path.rfind("/public/", 0) == 0) {
      std::string rel = path.substr(8);
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

    } else if (path == "/search") {
      handle_search(client_fd, target);

    } else if (path == "/docs") {
      handle_docs_index(client_fd);

    } else if (path.rfind("/docs/", 0) == 0) {
      std::string slug = path.substr(std::string("/docs/").size());
      handle_docs_slug(client_fd, slug);

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