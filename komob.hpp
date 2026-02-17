// komob.hpp: Minimal Modbus Server
// Created by Sanshiro Enomoto on 31 January 2026.
// Version: 260213 //


#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>
#include <cerrno>


#if 0
#define KOMOB_DEBUG(x) x
#else
#define KOMOB_DEBUG(x)
#endif


namespace komob {


class RegisterTable {
  public:
    virtual ~RegisterTable() {}
    virtual bool read(unsigned /*address*/, unsigned& /*value*/) { return false; }
    virtual bool write(unsigned /*address*/, unsigned /*value*/) { return false; }
};


enum class DataWidth { W16, W32 };
        
    
class Server {
  public:
    Server(
        std::shared_ptr<RegisterTable> register_table=nullptr,
        DataWidth width=DataWidth::W32,
        int keepalive_idle_sec=3600,
        int packet_timeout_msec = 1000
    );
    inline Server& add(std::shared_ptr<RegisterTable> register_table);
    inline int run(int argc, char** argv);
    inline void serve(unsigned port=502);
  private:
    inline void set_nonblocking(int fd);
    inline void set_sock_timeouts_ms(int fd, int rcv_ms, int snd_ms);
    inline void set_keepalive(int fd, int idle, int interval, int count);
    inline bool handle_single_request(int fd);
    inline std::vector<uint8_t> dispatch_pdu(const std::vector<uint8_t>& request);
    inline std::vector<uint8_t> exception_pdu(uint8_t function_code, uint8_t exception_code);
    inline std::vector<uint8_t> read_holding_registers(const std::vector<uint8_t>& request);
    inline std::vector<uint8_t> write_single_register(const std::vector<uint8_t>& request);
    inline std::vector<uint8_t> write_multiple_registers(const std::vector<uint8_t>& request);
  private:
    DataWidth data_width;
    int keepalive_idle, keepalive_interval, keepalive_count;
    int timeout_ms;
    std::vector<std::shared_ptr<RegisterTable>> register_tables;

  private:
    static constexpr uint8_t EX_ILLEGAL_FUNCTION = 0x01;
    static constexpr uint8_t EX_ILLEGAL_ADDRESS  = 0x02;
    static constexpr uint8_t EX_ILLEGAL_VALUE    = 0x03;
    static constexpr uint8_t EX_SLAVE_FAILURE    = 0x04;
    static constexpr uint8_t FC_READ_HOLDING_REGISTERS   = 0x03;
    static constexpr uint8_t FC_WRITE_SINGLE_REGISTER    = 0x06;
    static constexpr uint8_t FC_WRITE_MULTIPLE_REGISTERS = 0x10;
    
    inline uint16_t get_u16(const uint8_t* p) {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }
    inline uint32_t get_u32(const uint8_t* p) {
        return static_cast<uint32_t>((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
    }
    inline void push_u16(std::vector<uint8_t>& out, uint16_t v) {
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        out.push_back(static_cast<uint8_t>(v & 0xff));
    }
    inline void push_u32(std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        out.push_back(static_cast<uint8_t>(v & 0xff));
    }
    inline bool read_exact(int fd, uint8_t* buf, size_t n) {
        size_t off = 0;
        while (off < n) {
            ssize_t recv_size = ::recv(fd, buf + off, n - off, 0);
            if (recv_size == 0) {
                return false;          // connection closed
            }
            if (recv_size < 0) {
                if (errno == EINTR) {
                    continue;
                }
                else {
                    return false;
                }
            }
            off += static_cast<size_t>(recv_size);
        }
        
        return true;
    }
    inline bool write_exact(int fd, const uint8_t* buf, size_t n) {
        size_t off = 0;
        while (off < n) {
            ssize_t sent_size = ::send(fd, buf + off, n - off, 0);
            if (sent_size <= 0) {
                if (sent_size < 0 && errno == EINTR) {
                    continue;
                }
                else {
                    return false;
                }
            }
            off += static_cast<size_t>(sent_size);
        }
        
        return true;
    }
};



inline Server::Server(std::shared_ptr<RegisterTable> register_table, DataWidth width, int keepalive_idle_sec, int packet_timeout_ms)
{
    if (register_table) {
        register_tables.push_back(register_table);
    }

    data_width = width;
    
    keepalive_idle = keepalive_idle_sec;
    keepalive_interval = 30;
    keepalive_count = 3;
    
    timeout_ms = packet_timeout_ms;
}
    
    
inline Server& Server::add(std::shared_ptr<RegisterTable> register_table)
{
    if (register_table) {
        register_tables.push_back(register_table);
    }
    return *this;
}
    

inline int Server::run(int argc, char** argv)
{
    unsigned port = 502;
    if (argc >= 2) {
        port = std::stoi(argv[1]);
    }

    try {
        serve(port);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return -1;
    }
    
    return 0;
}


inline void Server::serve(unsigned port)
{
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        throw std::runtime_error("socket() failed");
    }
    int yes = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int err = errno;
        ::close(listen_fd);
        if (err == EACCES) {
            throw std::runtime_error(
                    "bind() failed: permission denied."
                    "privileged port <1024 requires root."
            );
        }
        else if (err == EADDRINUSE) {
            throw std::runtime_error(
                    "bind() failed: port already in use."
            );
        }
        else {
            throw std::runtime_error(
                    std::string("bind() failed: ") + std::strerror(err)
            );
        }
    }
    if (::listen(listen_fd, 16) < 0) {
        ::close(listen_fd);
        throw std::runtime_error("listen() failed");
    }
    set_nonblocking(listen_fd);
    std::cout << "Modbus TCP server ";
    std::cout << (data_width == DataWidth::W32 ? "(32bit mode)" : "(16bit mode)") << " ";
    std::cout << "listening on port " << port << "\n";

    std::vector<pollfd> pollfd_list;  // first one is for listen()
    pollfd_list.push_back(pollfd{listen_fd, POLLIN, 0});

    while (true) {
        int n = ::poll(pollfd_list.data(), static_cast<nfds_t>(pollfd_list.size()), -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "poll() failed: continue processing\n";
            continue;
        }

        // new connection ([0] is the listenning port)
        if (pollfd_list[0].revents & POLLIN) {
            while (true) {
                sockaddr_in client{};
                socklen_t client_size = sizeof(client);
                int fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client), &client_size);
                if (fd < 0) {
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        break;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    std::cerr << "accept() failed\n";
                    break;
                }

                set_keepalive(fd, keepalive_idle, keepalive_interval, keepalive_count);
                set_sock_timeouts_ms(fd, /*rcv*/timeout_ms, /*snd*/timeout_ms);

                char ipbuf[64];
                ::inet_ntop(AF_INET, &client.sin_addr, ipbuf, sizeof(ipbuf));
                std::cout << "Client connected: " << ipbuf << ":" << ntohs(client.sin_port) << "\n";

                pollfd_list.push_back(pollfd{fd, POLLIN, 0});
            }
        }

        // connected ports ([1:])
        for (size_t i = 1; i < pollfd_list.size(); ) {
            auto &pollfd = pollfd_list[i];
            int fd = pollfd.fd;

            bool close_this = false;
            if (pollfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close_this = true;
            }

            if (! close_this && (pollfd.revents & POLLIN)) {
                try {
                    if (!handle_single_request(fd)) {
                        close_this = true; // timeout/close/error -> close
                    }
                } catch (...) {
                    close_this = true;
                }
            }

            if (close_this) {
                ::close(fd);
                pollfd_list[i] = pollfd_list.back();
                pollfd_list.pop_back();
                std::cout << "Client disconnected.\n";
                continue;
            }
            ++i;
        }
    }
}


inline void Server::set_nonblocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


inline void Server::set_sock_timeouts_ms(int fd, int rcv_ms, int snd_ms)
{
    timeval rcv{};
    rcv.tv_sec  = rcv_ms / 1000;
    rcv.tv_usec = (rcv_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));

    timeval snd{};
    snd.tv_sec  = snd_ms / 1000;
    snd.tv_usec = (snd_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof(snd));
}


inline void Server::set_keepalive(int fd, int idle, int interval, int count)
{
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &count,   sizeof(count));
}


inline bool Server::handle_single_request(int fd)
{
    uint8_t header[7]; // MBAP: Modus Application Protocol
    if (! read_exact(fd, header, sizeof(header))) {
        return false;  // connection closed
    }
    
    unsigned transaction_id = get_u16(&header[0]);
    unsigned protocol_id = get_u16(&header[2]);
    unsigned length = get_u16(&header[4]);
    unsigned unit_id = header[6];
    
    KOMOB_DEBUG(std::cerr << std::dec << "#####" << std::endl);
    KOMOB_DEBUG(std::cerr << "RequestHeader(");
    KOMOB_DEBUG(std::cerr << "transaction_id=" << transaction_id << ",");
    KOMOB_DEBUG(std::cerr << "protocol_id=" << protocol_id << ",");
    KOMOB_DEBUG(std::cerr << "length=" << length << ",");
    KOMOB_DEBUG(std::cerr << "unitid=" << unit_id << ")" << std::endl);
    
    if (protocol_id != 0) {
        return false;   // not Modbus protocol: -> close
    }
    else if (length < 2) {            
        return false;   // bad Modbus packet: too short -> unrecoverable error -> close
    }
    
    // We already read UnitID in header; remaining bytes in "len" include:
    // UnitID(1) + PDU(...)
    // After reading header, we still need to read PDU length=length-1 (since UnitID is already in header)
    size_t pdu_length = length > 1 ? static_cast<size_t>(length-1) : 0;
    if (pdu_length > 256) {            
        return false;   // too large request (to prevent memory full) -> error -> close
    }
    
    std::vector<uint8_t> pdu(pdu_length);
    if (! read_exact(fd, pdu.data(), pdu.size())) {
        KOMOB_DEBUG(std::cerr << "ERROR: Connection closed during a request");
        return false;  // connection closed
    }
    unsigned function_code = pdu.empty() ? 0 : pdu[0];
    KOMOB_DEBUG(std::cerr << "RequestPDU(length=" << (pdu_length-1) << "+1,");
    KOMOB_DEBUG(std::cerr << "function_code=" << function_code << ")" << std::endl);
    
    std::vector<uint8_t> resp_pdu; // pdu[0] is function code
    try {
        resp_pdu = dispatch_pdu(pdu);
    }
    catch (...) {
        // As a fallback, send "Slave Device Failure" with function|0x80 if possible
        KOMOB_DEBUG(std::cerr << "ExceptionResponse" << std::endl);
        resp_pdu = exception_pdu(function_code, EX_SLAVE_FAILURE);
    }
    
    // Build response header (MBAP)
    // Response length = UnitID(1) + resp_pdu.size()
    std::vector<uint8_t> resp;
    resp.reserve(7 + resp_pdu.size());
    push_u16(resp, transaction_id);
    push_u16(resp, 0x0000); // protocol id (Modbus: 0)
    push_u16(resp, static_cast<uint16_t>(1 + resp_pdu.size()));
    resp.push_back(unit_id);
    resp.insert(resp.end(), resp_pdu.begin(), resp_pdu.end());
    
    if (! write_exact(fd, resp.data(), resp.size())) {
        return false;  // connection closed
    }

    return true;
}


inline std::vector<uint8_t> Server::dispatch_pdu(const std::vector<uint8_t>& request)
{
    if (request.size() < 1) {
        return exception_pdu(0, EX_ILLEGAL_FUNCTION);
    }
    unsigned function_code = request[0];
    
    switch (function_code) {
      case FC_READ_HOLDING_REGISTERS:
        return read_holding_registers(request);

      case FC_WRITE_SINGLE_REGISTER:
        return write_single_register(request);

      case FC_WRITE_MULTIPLE_REGISTERS:
        return write_multiple_registers(request);

      default:
        KOMOB_DEBUG(std::cerr << "Illigal function code" << std::endl);
        return exception_pdu(function_code, EX_ILLEGAL_FUNCTION);
    }
}


inline std::vector<uint8_t> Server::exception_pdu(uint8_t function_code, uint8_t exception_code)
{
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(function_code | 0x80));
    out.push_back(exception_code);
    
    return out;
}


inline std::vector<uint8_t> Server::read_holding_registers(const std::vector<uint8_t>& request)
{
    // Request: [FC(0x03)][Start Hi][Start Lo][Qty Hi][Qty Lo]
    uint8_t function_code = request[0];  // size has been tested to be greater than 1
    if (request.size() != 5) {
        return exception_pdu(function_code, EX_ILLEGAL_VALUE);
    }
    unsigned start = static_cast<unsigned>(get_u16(&request[1]));
    unsigned quantity = static_cast<unsigned>(get_u16(&request[3]));
    unsigned width = (data_width == DataWidth::W32) ? 2 : 1;
    KOMOB_DEBUG(std::cerr << "ReadHoldingRegister(start=" << start << ",quantity=" << quantity << ")" << std::endl);
    
    if (quantity % width != 0) {
        return exception_pdu(function_code, EX_ILLEGAL_VALUE);
    }
    if (quantity > 128) {
        return exception_pdu(function_code, EX_ILLEGAL_VALUE);  // return byte-count is 8-bit
    }
    
    try {
        std::vector<uint8_t> out;
        out.push_back(function_code);
        out.push_back(static_cast<uint8_t>(quantity * 2)); // byte count
        out.reserve(2 + quantity * 2);
        KOMOB_DEBUG(std::cerr << "  OutData: ");
        for (unsigned i = 0; i*width < quantity; i++) {
            unsigned address = start + i, value;
            bool handled = false;
            for (auto& register_table: register_tables) {
                if (register_table->read(address, value)) {
                    handled = true;
                    break;
                }
            }
            if (handled) {
                if (data_width == DataWidth::W32) {
                    push_u32(out, static_cast<uint32_t>(value));
                }
                else {
                    push_u16(out, static_cast<uint16_t>(value));
                }
                KOMOB_DEBUG(std::cerr << std::hex << "[0x" << address << "]=>0x" << value << " ");
            }
            else {
                return exception_pdu(function_code, EX_ILLEGAL_ADDRESS);
            }
        }
        KOMOB_DEBUG(std::cerr << std::endl);
        return out;
    }
    catch (...) {
        return exception_pdu(function_code, EX_SLAVE_FAILURE);
    }
}

    
inline std::vector<uint8_t> Server::write_single_register(const std::vector<uint8_t>& request)
{
    // Request: [FC(0x06))[Addr Hi][Addr Lo][Val Hi][Val Lo]
    uint8_t function_code = request[0];  // size has been tested to be greater than 1
    if (request.size() != 5) {
        return exception_pdu(function_code, EX_ILLEGAL_VALUE);
    }
    unsigned address = static_cast<unsigned>(get_u16(&request[1]));
    unsigned value = static_cast<unsigned>(get_u16(&request[3]));
    KOMOB_DEBUG(std::cerr << "WriteHoldingRegister(address=0x" << std::hex << address << ",value=0x" << value << ")" << std::endl);
    
    if (data_width != DataWidth::W16) {
        return exception_pdu(function_code, EX_ILLEGAL_VALUE);
    }

    try {
        bool handled = false;
        for (auto& register_table: register_tables) {
            if (register_table->write(address, value)) {
                handled = true;
                break;
            }
        }
        if (! handled) {
            return exception_pdu(function_code, EX_ILLEGAL_ADDRESS);
        }
        return request;  // Response echoes the request PDU per spec
    }
    catch (...) {
        return exception_pdu(function_code, EX_SLAVE_FAILURE);
    }
}


inline std::vector<uint8_t> Server::write_multiple_registers(const std::vector<uint8_t>& request) {
    // Request:
    // [FC(0x10)][Addr Hi][Addr Lo][Qty Hi][Qty Lo][ByteCount][Values...]
    uint8_t function_code = request[0];  // size has been tested to be greater than 1
    if (request.size() < 6) {
        return exception_pdu(function_code, EX_ILLEGAL_VALUE);
    }
    unsigned start = static_cast<unsigned>(get_u16(&request[1]));
    unsigned quantity = static_cast<unsigned>(get_u16(&request[3]));
    unsigned byte_count = static_cast<unsigned>(request[5]);
    unsigned width = (data_width == DataWidth::W32) ? 2 : 1;
    KOMOB_DEBUG(std::cerr << "WriteMultipleRegisters(start=" << start << ",quantity=" << quantity << ")" << std::endl);
    
    if (byte_count != quantity * 2) {
        return exception_pdu(function_code, EX_ILLEGAL_VALUE);
    }
    if (quantity % width != 0) {
        return exception_pdu(function_code, EX_ILLEGAL_VALUE);
    }
    if (request.size() != static_cast<size_t>(6 + byte_count)) {
        return exception_pdu(function_code, EX_ILLEGAL_VALUE);
    }
    
    try {
        KOMOB_DEBUG(std::cerr << "  InData: ");
        for (unsigned i = 0; i*width < quantity; i++) {
            unsigned offset = 6 + 2 * i * width;
            unsigned address = start + i, value;
            if (data_width == DataWidth::W32) {
                value = static_cast<unsigned>(get_u32(&request[offset]));
            }
            else {
                value = static_cast<unsigned>(get_u16(&request[offset]));
            }
            bool handled = false;
            for (auto& register_table: register_tables) {
                if (register_table->write(address, value)) {
                    handled = true;
                    break;
                }
            }
            if (handled) {
                KOMOB_DEBUG(std::cerr << std::hex << "[0x" << address << "]<=0x" << value << " ");
            }
            else {
                return exception_pdu(function_code, EX_ILLEGAL_ADDRESS);
            }
        }
        KOMOB_DEBUG(std::cerr << std::endl);

        // Response: [FC][Addr Hi][Addr Lo][Qty Hi][Qty Lo]
        std::vector<uint8_t> out;
        out.push_back(function_code);
        push_u16(out, static_cast<uint16_t>(start));
        push_u16(out, static_cast<uint16_t>(quantity));
        return out;
    }
    catch (...) {
        return exception_pdu(function_code, EX_SLAVE_FAILURE);
    }
}


} // namespace
