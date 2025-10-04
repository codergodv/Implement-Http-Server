#pragma once

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "Log.hpp"

#define BACKLOG 5

//Tcp服务器
class TcpServer {
    private:
        int _port;
        int _listen_port;
        static TcpServer* _svr;
        TcpServer(int port) {
           :_port(port);
           ,_listen_port(-1);
        {}
        TcpServer(const TcpServer&) = delete;
        TcpServer& operator=(const TcpServer&) = delete;
    public:
        static TcpServer* GetInstance(int port) {
            static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
            if (_svr == nullptr) {
                pthread_mutex_lock(&mtx);
                if (_svr == nullptr) {
                    _svr = new TcpServer(port);
                    _svr->InitServer()
                }
                pthread_mutex_unlock(&mtx);
            }
            return _svr;
        }

        void InitServer() {
            Socket();
            Bind();
            Listen();
            LOG(INFO, "tcp_server init ... success");
        }

        void Socket() {
            _listen_port = socket(AF_INET, SOCK_STREAM, 0);
            if (_listen_port < 0) {
                LOG(FATAL, "socket error!");
                exit(1);
            }
        }

        void Bind() {
            struct sockaddr_in local;
            memset(&local, 0, sizeof(local));
            local.sin_family = AF_INET;
            local.sin_port = htons(_port);
            local.sin_addr.s_addr = INADDR_ANY;
            if (bind(_listen_port, (struct sockaddr*)&local, sizeof(local)) < 0) {
                LOG(FATAL, "bind error!");
                exit(2);
            }
            LOG(INFO, "bind socket ... success");
        }

        void Listen() {
            if (Listen(_listen_port, BACKLOG) < 0) {
                LOG(FATAL, "listen error!");
            }
            LOG(INFO, "Listen socket ... success");
        }

        int sock() {
            return _listen_port;
        }

        ~TcpServer() {
            if (_listen_port >= 0) {
                close(_listen_port);
            }
        }
};

TcpServer* TcpServer::_svr = nullptr;
