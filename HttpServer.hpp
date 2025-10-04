#pragma once 

#include <iostream>
#include <signal.h>
#include "TcpServer.hpp"
#include "Task.hpp"
#include "ThreadPool.hpp"
#include "Log.hpp"

#define PORT 8081

class HttpServer {
    private:
        int _port;
    public:
        HttpServer(int port) 
            :_port(port)
        {}

        void Loop() {
            LOG(INFO, "loop begin");
            TcpServer* tsvr = TcpServer::GetInstance(_port);
            int listen_sock = tsvr->Sock();
            while (true) {
                struct sockaddr_in peer;
                memset(&peer, 0, sizeof(peer));
                socklen_t len = sizeof(peer);
                int sock = accept(listen_sock, (struct sockaddr*)&peer, &len);
                if (sock < 0) {
                    continue;
                }

                std::string client_ip = inet_ntoa(peer.sin_addr);
                int client_port = ntohs(peer.sin_port);
                LOG(INFO, "get a new link:[" + client_ip + ":" + std::to_string(client_port) + "]");

                int* p = new int(sock);
                pthread_t tid;
                pthread_create(&tid, nullptr, CallBack::HandlerRequest, (void*)p);
                pthread_detach(tid);
            }
        }

        ~HttpServer()
        {}
};
