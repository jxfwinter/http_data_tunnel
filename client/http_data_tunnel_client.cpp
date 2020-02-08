#include "http_data_tunnel_client.h"
#include <ctime>
#include <chrono>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <iostream>

static void set_socket_opt(TcpSocket& socket)
{
    //boost::asio::socket_base::keep_alive opt_keep_alive(true);
    //socket.set_option(opt_keep_alive);

    int flags = 1;
    int tcp_keepalive_time = 20;
    int tcp_keepalive_probes = 3;
    int tcp_keepalive_intvl = 3;

    int ret = 0;
    ret = setsockopt(socket.native_handle(), SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));
    if(ret < 0)
    {
        KK_PRT("setsockopt SO_KEEPALIVE failed");
    }
    ret = setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_KEEPIDLE, &tcp_keepalive_time, sizeof(tcp_keepalive_time));
    if(ret < 0)
    {
        KK_PRT("setsockopt TCP_KEEPIDLE failed");
    }
    ret = setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_KEEPINTVL, &tcp_keepalive_intvl, sizeof(tcp_keepalive_intvl));
    if(ret < 0)
    {
        KK_PRT("setsockopt TCP_KEEPINTVL failed");
    }
    ret = setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_KEEPCNT, &tcp_keepalive_probes, sizeof(tcp_keepalive_probes));
    if(ret < 0)
    {
        KK_PRT("setsockopt TCP_KEEPCNT failed");
    }
}

HttpDataTunnelClient::HttpDataTunnelClient(IoContext& ioc) :
    m_ioc(ioc), m_socket(m_ioc), m_resolver(m_ioc), m_to_socket(m_ioc)
{
    //最大200M
    m_req_parser.body_limit(1024*1024*200);
    m_res_parser.body_limit(1024*1024*200);
}

HttpDataTunnelClient::~HttpDataTunnelClient()
{

}

void HttpDataTunnelClient::async_run(string host, uint16_t port, string session_id,
               string local_ip, uint16_t local_port,
               RunCallback cb)
{
    m_host = std::move(host);
    m_port = port;
    m_session_id = std::move(session_id);

    m_local_ip = std::move(local_ip);
    m_local_port = local_port;
    m_cb = std::move(cb);

    loop_run({});
}

#include <boost/asio/yield.hpp>
void HttpDataTunnelClient::loop_run(boost::system::error_code ec)
{
    auto self(shared_from_this());
    reenter(m_co)
    {
        KK_PRT("start resolve");
        yield m_resolver.async_resolve(m_host, "", [self, this](boost::system::error_code ec, ResolverResult r) {
            if(!ec)
            {
                m_resolve_result = std::move(r);
            }
            this->loop_run(ec);
        });
        if(ec)
        {
            m_cb(ec);
            return;
        }
        yield
        {
            Endpoint ep = (*m_resolve_result.begin()).endpoint();
            ep.port(m_port);
            m_socket.async_connect(ep, [self, this](boost::system::error_code ec) {
                this->loop_run(ec);
            });
        }
        if(ec)
        {
            m_cb(ec);
            return;
        }
        set_socket_opt(m_socket);

        //建立隧道
        m_req = {};
        m_req.method(http::verb::connect);
        m_req.keep_alive(true);
        m_req.set(DATA_SESSION_ID, m_session_id);
        m_req.target("setup.data.tunnel");
        m_req.content_length(0);
        KK_PRT("start setup");
        yield http::async_write(m_socket, m_req, [self, this](boost::system::error_code ec, size_t) {
            this->loop_run(ec);
        });
        if(ec)
        {
            m_cb(ec);
            return;
        }
        yield http::async_read(m_socket, m_read_buffer, m_res, [self, this](boost::system::error_code ec, size_t) {
            this->loop_run(ec);
        });
        if(ec)
        {
            m_cb(ec);
            return;
        }
        if((int)m_res.result() >= 300 || (int)m_res.result() < 200)
        {
            KK_PRT("setup error:%d", (int)m_res.result());
            m_cb(ec = boost::beast::http::error::bad_status);
            return;
        }
        //建立本地转发连接
        yield m_to_socket.async_connect(Endpoint{boost::asio::ip::make_address(m_local_ip, ec), m_local_port},
                                        [self, this](boost::system::error_code ec) {
            this->loop_run(ec);
        });
        if(ec)
        {
            m_cb(ec);
            return;
        }

        //开始转发请求
        yield http::async_read_header(m_socket, m_read_buffer, m_req_parser,
                                      [self, this](boost::system::error_code ec, std::size_t) {
            this->loop_run(ec);
        });
        if(ec)
        {
            m_cb(ec);
            return;
        }
        m_req_sr.reset(new ReqSerializer(m_req_parser.get()));
        for(;;)
        {
            if(m_req_parser.is_done())
            {
                {
                    auto& req = m_req_parser.get();
                    req.body().data = nullptr;
                    req.body().more = false;
                }
                yield http::async_write(m_to_socket, *m_req_sr,
                                        [self, this](boost::system::error_code ec, std::size_t) {
                    this->loop_run(ec);
                });
                break;
            }
            else
            {
                {
                    m_body_buffer.resize(1024);
                    auto& req = m_req_parser.get();
                    req.body().data = m_body_buffer.data();
                    req.body().size = m_body_buffer.size();
                }
                yield http::async_read(m_socket, m_read_buffer, m_req_parser,
                                       [self, this](boost::system::error_code ec, std::size_t) {
                    this->loop_run(ec);
                });
                if(ec == http::error::need_buffer)
                {
                    ec = {};
                }
                if(ec)
                {
                    m_cb(ec);
                    return;
                }
                {
                    auto& req = m_req_parser.get();
                    m_body_buffer.resize(m_body_buffer.size() - req.body().size);
                    req.body().size = m_body_buffer.size();
                    req.body().more = true;
                }
                yield http::async_write(m_to_socket, *m_req_sr,
                                        [self, this](boost::system::error_code ec, std::size_t) {
                    this->loop_run(ec);
                });
                if(ec == http::error::need_buffer)
                {
                    ec = {};
                }
                if(ec)
                {
                    m_cb(ec);
                    return;
                }
            }
        }

        //读取http响应头
        yield http::async_read_header(m_to_socket, m_read_buffer, m_res_parser,
                                      [self, this](boost::system::error_code ec, std::size_t) {
            this->loop_run(ec);
        });
        if(ec)
        {
            m_cb(ec);
            return;
        }
        //转发http响应
        m_res_sr.reset(new ResSerializer(m_res_parser.get()));
        for(;;)
        {
            if(m_res_parser.is_done())
            {
                {
                    auto& res = m_res_parser.get();
                    res.body().data = nullptr;
                    res.body().more = false;
                }
                yield http::async_write(m_socket, *m_res_sr,
                                        [self, this](boost::system::error_code ec, std::size_t) {
                    this->loop_run(ec);
                });
                break;
            }
            else
            {
                {
                    m_body_buffer.resize(1024);
                    auto& res = m_res_parser.get();
                    res.body().data = m_body_buffer.data();
                    res.body().size = m_body_buffer.size();
                }
                yield http::async_read(m_to_socket, m_read_buffer, m_res_parser,
                                       [self, this](boost::system::error_code ec, std::size_t) {
                    this->loop_run(ec);
                });
                if(ec == http::error::need_buffer)
                {
                    ec = {};
                }
                if(ec)
                {
                    m_cb(ec);
                    return;
                }
                {
                    auto& res = m_res_parser.get();
                    m_body_buffer.resize(m_body_buffer.size() - res.body().size);
                    res.body().size = m_body_buffer.size();
                    res.body().more = true;
                }
                yield http::async_write(m_socket, *m_res_sr,
                                        [self, this](boost::system::error_code ec, std::size_t) {
                    this->loop_run(ec);
                });
                if(ec == http::error::need_buffer)
                {
                    ec = {};
                }
                if(ec)
                {
                    m_cb(ec);
                    return;
                }
            }
        }

        m_cb({});
    }
}

#include <boost/asio/unyield.hpp>
