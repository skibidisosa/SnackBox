#pragma once
#include "router.hpp"
#include <atomic>

namespace sb {

    class Server {
    public:
        explicit Server(int port=8080);
        void set_router(Router* r) { router_ = r; }
        void set_public_dir(std::string dir) { public_dir_ = std::move(dir); }
        void run();      // blocking
        void stop();     // request stop

    private:
        int port_;
        Router* router_{nullptr};
        std::string public_dir_{"public"};
        std::atomic<bool> running_{true};

        static int create_listen_socket(int port);
        static void set_nonblock(int fd, bool nb);
        static std::string read_all(int fd);
        static void write_all(int fd, const std::string& data);
        Response serve_static(const std::string& path);
    };

} // namespace sb
