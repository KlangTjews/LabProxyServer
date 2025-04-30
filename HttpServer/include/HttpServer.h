#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <getopt.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include "ThreadPool.h"
#include "ConnectionManager.h"

#define MAX_EVENTS 1024

std::string c_ip;
int c_port;
int c_threads;

int set_nonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void parse_args(int argc, char* argv[]){
    static struct option long_opts[] = {
        {"ip",      required_argument, nullptr, 'i'},
        {"port",    required_argument, nullptr, 'p'},
        {"threads", required_argument, nullptr, 't'},
        {0, 0, nullptr, 0}
    };

    int opt, idx;
    while((opt = getopt_long(argc, argv, "i:p:t:", long_opts, &idx)) != -1){
        switch (opt)
        {
        case 'i':
            c_ip = optarg;
            break;
        case 'p':
            c_port = std::atoi(optarg);
            break;
        case 't':
            c_threads = std::atoi(optarg);
            break;
        case 0:
            break;
        default:
            std::cerr << "[ERROR] Usage: " << argv[0] << " --ip <IP> --port <PORT> --threads <THREADS>" << std::endl;
        }
    }

    if (c_ip.empty() || c_port <= 0 || c_threads <= 0){
        std::cerr << "[ERROR] Missing required parameters" << std::endl;
        std::exit(EXIT_FAILURE);
    }
}