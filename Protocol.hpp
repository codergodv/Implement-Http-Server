#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "Util.hpp"
#include "Log.hpp"

#define SEP ": "
#define WEB_ROOT "wwwroot"
#define HOME_PAGE "index.html"
#define HTTP_VERSION "HTTP/1.0"
#define LINE_END "\r\n"

#define PAGE_400 "400.html"
#define PAGE_404 "404.html"
#define PAGE_500 "500.html"

#define OK 200
#define BAD_REQUEST 400
#define NOT_FOUND 404
#define INTERNAL_SERVER_ERROR 500

class EndPoint {
    private:
        int _sock;
        HttpRequest _http_request;
        HttpResponse _http_response;
    public:
        EndPoint(int sock)
            :_sock(sock)
        {}
        
        //拉取请求
        void RecvHttpRequest();

        //处理请求
        void HandlerHttpRequest();

        //构建响应
        void BuildHttpResponse();

        //发送响应
        void SendHttpResponse();

        ~EndPoint()
        {}
};
