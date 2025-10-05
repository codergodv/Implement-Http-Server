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

static std::string CodeToDesc(int code) {
    std::string desc;
    switch (code) {
        case 200:
            desc = "OK";
            break;
        case 400:
            desc = "Bad Request";
            break;
        case 404:
            desc = "Not Found";
            break;
        case 500:
            desc = "Internal Server Error";
            break;
        default:
            break;
    }
    return desc;
}

static std::string SuffixToDesc(const std::string& suffix)
{
    static std::unordered_map<std::string, std::string> suffix_to_desc = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/x-javascript"},
        {".jpg", "application/x-jpg"},
        {".xml", "text/xml"}
    };
    auto iter = suffix_to_desc.find(suffix);
    if(iter != suffix_to_desc.end()){
        return iter->second;
    }
    return "text/html"; //所给后缀未找到则默认该资源为html文件
}

class HttpRequest {
    public:
        std::string _request_line;
        std::vector<std::string> _request_header;
        std::string _blank;
        std::string _request_body;

        std::string _method;
        std::string _uri;
        std::string _version;
        std::unordered_map<std::string, std::string> _header_kv;
        int _content_length;
        std::string _path;
        std::string _query_string;
        
        bool _cgi;

    public:
        HttpRequest()
            :_content_length(0);
            ,_cgi(false);
        {}

        ~HttpRequest()
        {}
};

class HttpResponse {
    public:
        std::string _status_line;
        std::vector<std::string> _response_header;
        std::string _blank;
        std::string _response_body;

        int _status_code;
        int _fd;
        int _size;
        std::string _suffix;
    public:
        HttpResponse() 
            :_blank(LINE_END)
             ,_status_code(OK)
             ,fd(-1)
             ,_size(0)
        {}
        ~HttpResponse()
        {}
};

class EndPoint {
    private:
        int _sock;
        HttpRequest _http_request;
        HttpResponse _http_response;
        bool _stop;
    private:
        bool RecvHttpRequestLine() {
            auto& line = _http_request._request_line;
            if (Util::ReadLine(_sock, line) > 0) {
                line.resize(line.size() - 1);
            }
            else {
                _stop = true;
            }
            return _stop;
        }

        bool RecvHttpRequestHeader() {
            std::string line;
            while (true) {
                line.clear();
                if (Util::ReadLine(sock, line) <= 0) {
                    _stop = true;
                    break;
                }
                if (line == "\n") {
                    _http_request._blank = line;
                    break;
                }

                line.resize(line.size() - 1);
                _http_request._request_header.push_back(line);
            }
            return stop();
        }

        void ParseHttpRequestLine() {
            auto& line = _http_request._request_line;

            std::stringstream ss(line);
            ss >> _http_request._method >> _http_request._uri >> _http_request._version;

            auto& method = _http_request._method;
            std::transform(method.begin(), method.end(), method.begin(), toupper);
        }

        void ParseHttpRequestHeader() {
            std::string key;
            std::string value;
            for (auto& iter : _http_request._request_header) {
                if (Util::CutString(iter, key, value, SEP)) {
                    _http_request._header_kv.insert({key, value});
                }
            }
        }

        bool IsNeedRecvHttpRequestBody() {
            auto& method = _http_request._method;
            if (method == "POST") {
                auto& header_kv = _http_request._header_kv;
                auto iter = _header_kv.find("Content-Length");
                if (iter != header_kv.end()) {
                    _http_request._content_length = atoi(iter->second.c_str());
                    return true;
                }
            }
            return false;
        }

        bool RecvHttpRequestBody() {
            if (IsNeedRecvHttpRequestBody()) {
                int Content_length = _http_request._content_length;
                auto& body = _http_request._request_body;

                char ch = 0;
                while(Content_length) {
                    ssize_t size = recv(_sock, &ch, 1, 0);
                    if (size > 0) {
                        body.push_back(ch);
                        Content_length--;
                    }
                    else {
                        _stop = true;
                        break;
                    }
                }
            }
            return _stop;
        }
        
        int ProcessCgi()
        {
            int code = OK; //要返回的状态码，默认设置为200

            auto& bin = _http_request._path;      //需要执行的CGI程序
            auto& method = _http_request._method; //请求方法

            //需要传递给CGI程序的参数
            auto& query_string = _http_request._query_string; //GET
            auto& request_body = _http_request._request_body; //POST

            int content_length = _http_request._content_length;  //请求正文的长度
            auto& response_body = _http_response._response_body; //CGI程序的处理结果放到响应正文当中

            //1、创建两个匿名管道（管道命名站在父进程角度）
            //创建从子进程到父进程的通信信道
            int input[2];
            if(pipe(input) < 0){ //管道创建失败，则返回对应的状态码
                LOG(ERROR, "pipe input error!");
                code = INTERNAL_SERVER_ERROR;
                return code;
            }
            //创建从父进程到子进程的通信信道
            int output[2];
            if(pipe(output) < 0){ //管道创建失败，则返回对应的状态码
                LOG(ERROR, "pipe output error!");
                code = INTERNAL_SERVER_ERROR;
                return code;
            }

            //2、创建子进程
            pid_t pid = fork();
            if(pid == 0){ //child
                //子进程关闭两个管道对应的读写端
                close(input[0]);
                close(output[1]);

                //将请求方法通过环境变量传参
                std::string method_env = "METHOD=";
                method_env += method;
                putenv((char*)method_env.c_str());

                if(method == "GET"){ //将query_string通过环境变量传参
                    std::string query_env = "QUERY_STRING=";
                    query_env += query_string;
                    putenv((char*)query_env.c_str());
                    LOG(INFO, "GET Method, Add Query_String env");
                }
                else if(method == "POST"){ //将正文长度通过环境变量传参
                    std::string content_length_env = "CONTENT_LENGTH=";
                    content_length_env += std::to_string(content_length);
                    putenv((char*)content_length_env.c_str());
                    LOG(INFO, "POST Method, Add Content_Length env");
                }
                else{
                    //Do Nothing
                }

                //3、将子进程的标准输入输出进行重定向
                dup2(output[0], 0); //标准输入重定向到管道的输入
                dup2(input[1], 1);  //标准输出重定向到管道的输出

                //4、将子进程替换为对应的CGI程序
                execl(bin.c_str(), bin.c_str(), nullptr);
                exit(1); //替换失败
            }
            else if(pid < 0){ //创建子进程失败，则返回对应的错误码
                LOG(ERROR, "fork error!");
                code = INTERNAL_SERVER_ERROR;
                return code;
            }
            else{ //father
                //父进程关闭两个管道对应的读写端
                close(input[1]);
                close(output[0]);

                if(method == "POST"){ //将正文中的参数通过管道传递给CGI程序
                    const char* start = request_body.c_str();
                    int total = 0;
                    int size = 0;
                    while(total < content_length && (size = write(output[1], start + total, request_body.size() - total)) > 0){
                        total += size;
                    }
                }

                //读取CGI程序的处理结果
                char ch = 0;
                while(read(input[0], &ch, 1) > 0){
                    response_body.push_back(ch);
                } //不会一直读，当另一端关闭后会继续执行下面的代码

                //等待子进程（CGI程序）退出
                int status = 0;
                pid_t ret = waitpid(pid, &status, 0);
                if(ret == pid){
                    if(WIFEXITED(status)){ //正常退出
                        if(WEXITSTATUS(status) == 0){ //结果正确
                            LOG(INFO, "CGI program exits normally with correct results");
                            code = OK;
                        }
                        else{
                            LOG(INFO, "CGI program exits normally with incorrect results");
                            code = BAD_REQUEST;
                        }
                    }
                    else{
                        LOG(INFO, "CGI program exits abnormally");
                        code = INTERNAL_SERVER_ERROR;
                    }
                }
                
                //关闭两个管道对应的文件描述符
                close(input[0]);
                close(output[1]);
            }
            return code; //返回状态码
        }
        
      int ProcessCgi() {
          _http_response._fd = open(_http_request._path.c_str(), O_RDONLY);
          if (_http_response._fd >= 0) {
              return OK;
          }
          return INTERNAL_SERVER_ERROR; 
      }
            
      void BuildOkResponse() {
          std::string content_type = "Content-Type: ";
          content_type += SuffixToDesc(_http_response._suffix);
          content_type += LINE_END;
          _http_response._reponse_header.push_back(content_type);

          std::string content_length = "Content-Length: ";
          if (_http_request._cgi) {
              content_length += std::to_string(_http_response._response_body.size());
          }
          else {
              content_length += std::to_string(_http_response._size);
          }
          content_length += LINE_END;
          _http_response._reponse_header.push_back(content_length);
      }


      void HandlerError(std::string page) {
          _http_response._fd = open(page.c_str(), O_RDONLY);
          if (_http_response._fd > 0) {
              struct stat st;
              stat(page.c_str(), &st);

              std::string content_type = "Content-Type: text/html";
              content_type += LINE_END;
              _http_response._response_header.push_back(content_type);

              std::string content_length = "Content-Length: ";
              content_length += std::to_string(st.st_size);
              Content_length += LINE_END;
              _http_response._response_header.push_back(content_length);

              _http_response._size = st.st_size;
          }
      }

    public:
        EndPoint(int sock)
            :_sock(sock)
            ,_stop(false)
        {}
       
        bool IsStop() {
            return _stop;
        }
        
        //拉取请求
        void RecvHttpRequest() {
            if (RecvHttpRequestLine() && RecvHttpRequestHeader()) {
                ParseHttpRequestLine();
                ParseHttpRequestHeader();
                RecvHttpRequestBody();
            }
        }

        //处理请求
        void HandlerHttpRequest() {
            auto& code = _http_response._status_code;

            if (_http_request._method != "GET" && _http_request._method != "POST") {
                LOG(WARNING, "method is not right");
                code = BAD_REQUEST;
                return;
            }

            if (_http_request._method == "GET") {
                size_t pos = _http_request._uri.find('?');
                if (pos != std::string::npos) {
                    Util::CutString(_http_request._uri, _http_request._path, _http_request._query_string, "?");
                    _http_request._cgi = true;
                }
                else {
                    _http_request._path = _http_request._uri;
                }
            }
            else if (_http_request._method == "POST") {
                _http_request._path  = _http_request._uri;
                _http_request._cgi = true;
            }
            else {

            }

            std::string path = _http_request._path;
            _http_request._path = WEB_ROOT;
            _http_request._path += path;

            if (_http_request._path[_http_request._path.size() - 1] == '/') {
                _http_request._path += HOME_PAGE;
            }

            struct stat st;
            if (stat(_http_request._path.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    _http_request._path += "/";
                    _http_request._path += HOME_PAGE;
                    stat(_http_request._path.c_str(), &st);
                }
                else if (st.st_mode & S_IXUSR || st.st_mode & S_IXGRP || st.st_mode & S_IXOTH) {
                    _http_request._cgi = true;
                }
                _http_response._size = st.st_size; //设置请求资源文件的大小
            }
            else{ //属性信息获取失败，可以认为该资源不存在
                LOG(WARNING, _http_request._path + " NOT_FOUND");
                code = NOT_FOUND; //设置对应的状态码，并直接返回
                return;
            }

            //获取请求资源文件的后缀
            size_t pos = _http_request._path.rfind('.');
            if(pos == std::string::npos){
                _http_response._suffix = ".html"; //默认设置
            }
            else{
                _http_response._suffix = _http_request._path.substr(pos);
            }

            //进行CGI或非CGI处理
            if(_http_request._cgi == true){
                code = ProcessCgi(); //以CGI的方式进行处理
            }
            else{
                code = ProcessNonCgi(); //简单的网页返回，返回静态网页
            }
   
        }

        //构建响应
        void BuildHttpResponse() {
            int code = _http_response._status_code;

            auto& status_line = _http_response._status_line;
            status_line += HTTP_VERSION;
            status_line += " ";
            status_line += std::to_string(code);
            status_line += " ";
            status_line += CodeToDesc(code);
            status_line += LINE_END;
            
            std::string path = WEB_ROOT;
            path += "/";
            switch (code) {
                case OK:
                    BulidResponse();
                    break;
                case NOT_FOUND:
                    path += PAGE_404;
                    HandlerError(path);
                    break;
                case BAD_REQUEST:
                    path += PAGE_400;
                    HandlerError(path);
                    break;
                case INTERNAL_SERVER_ERROR:
                    path += PAGE_500;
                    HandlerError(path);
                    break;
                default:
                    break;
            } 
        }

        //发送响应
        void SendHttpResponse() {
            if (send(_sock, _http_response._status_line.c_str(), _http_response._status_line.size(), 0) <= 0) {
                _stop = true;
            }
            if (!_stop) {
                for (auto& iter : _http_response._response_header) {
                    if (send(_sock, iter.c_str(), iter.size(), 0) <= 0) {
                        _stop = true;
                        break;
                    }
                }
            }
            if (!_stop) {
                if (send(_sock, _http_response._blank.c_str(), _http_response._blank.size(), 0) <= 0) {
                    _stop = true;
                }
            }
            if (_http_request._cgi) {
               if (!_stop) {
                    auto& response_body = _http_response._response_body;
                    const char* start = response_body.c_str();
                    size_t size = 0;
                    size_t total = 0;
                    while (total < response_body.size() && (size = send(_sock, start + total, response_body.size() - total, 0)) > 0) {
                        total += size;
                    }
               }
            }
            else {
                if (!_stop) {
                    if (sendfile(_sock, _http_response._fd, nullptr, _http_response._size) <= 0) {
                        _stop = true;
                    }
                }
                close(_http_response._fd);
            }
            return _stop;
        }

        ~EndPoint()
        {}
};

class CallBack() {
    public:
        static void* HandlerRequest(void* arg) {
            LOG(INFO, "handler request begin");
            int sock = *(int*) arg;
            
            EndPoint* ep = new EndPoint(sock);
            ep->RecvHttpRequest();
            if (ep->IsStop()) {
                LOG(WARNING, "Recv Error, Stop Handler Request");
            }
            else {
                LOG(INFO, "Recv No Error, Begin Handler Request");
                ep->HandlerHttpRequest();
                ep->BuildHttpResponse();
                ep->SendHttpResponse();
            }

            close(sock);
            delete ep;

            LOG(INFO, "handler request end");
            return nullptr;
        }
};


