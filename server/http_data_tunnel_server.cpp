#include "http_data_tunnel_server.h"

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
        log_error_ext("setsockopt SO_KEEPALIVE failed");
    }
    ret = setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_KEEPIDLE, &tcp_keepalive_time, sizeof(tcp_keepalive_time));
    if(ret < 0)
    {
        log_error_ext("setsockopt TCP_KEEPIDLE failed");
    }
    ret = setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_KEEPINTVL, &tcp_keepalive_intvl, sizeof(tcp_keepalive_intvl));
    if(ret < 0)
    {
        log_error_ext("setsockopt TCP_KEEPINTVL failed");
    }
    ret = setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_KEEPCNT, &tcp_keepalive_probes, sizeof(tcp_keepalive_probes));
    if(ret < 0)
    {
        log_error_ext("setsockopt TCP_KEEPCNT failed");
    }
}

TunnelSessionInfo::TunnelSessionInfo(IoContext& ioc) :
    tmp_socket(ioc), tunnel_socket(ioc), http_socket(ioc),timer(ioc)
{

}

HttpDataTunnelServer::HttpDataTunnelServer(IoContext &ioc):
    m_ioc(ioc), m_http_acceptor(m_ioc), m_http_socket(m_ioc)
{
    m_http_listen_ep = Endpoint{boost::asio::ip::make_address(g_cfg->http_listen_addr), g_cfg->http_listen_port};
}

HttpDataTunnelServer::~HttpDataTunnelServer()
{

}

void HttpDataTunnelServer::start()
{
    //监听http端口
    BSErrorCode ec;
    m_http_acceptor.open(m_http_listen_ep.protocol(), ec);
    if(ec)
    {
        log_error_ext(ec.message());
        return;
    }

    m_http_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    m_http_acceptor.bind(m_http_listen_ep, ec);
    if(ec)
    {
        log_error_ext(ec.message());
        return;
    }
    m_http_acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
    if(ec)
    {
        log_error_ext(ec.message());
        return;
    }

    loop_accept({});
}

void HttpDataTunnelServer::stop()
{
    BSErrorCode ec;
    m_http_acceptor.close(ec);
}

void HttpDataTunnelServer::start_session(TcpSocket s)
{
    TunnelSessionInfoPtr co_info(new TunnelSessionInfo(m_ioc));
    co_info->tmp_socket = std::move(s);
    set_socket_opt(co_info->tmp_socket);
    loop_session({}, std::move(co_info));
}

void HttpDataTunnelServer::continue_session(TunnelSessionInfoPtr join_ses, TunnelSessionInfoPtr ses)
{
    ses->req_parser = std::move(join_ses->req_parser);
    ses->res_parser = std::move(join_ses->res_parser);
    ses->http_socket = std::move(join_ses->http_socket);
    ses->buffer = std::move(join_ses->buffer);
    ses->co = Coroutine();
    loop_transfer_data({}, std::move(ses));
}

#include <boost/asio/yield.hpp>
void HttpDataTunnelServer::loop_accept(BSErrorCode ec)
{
    reenter(m_http_accept_co)
    {
        for(;;)
        {
            yield m_http_acceptor.async_accept(m_http_socket, [this](BSErrorCode ec) {
                this->loop_accept(ec);
            });
            if(ec == boost::asio::error::no_descriptors)
            {
                log_error_ext(ec.message());
                continue;
            }
            else if(ec)
            {
                log_error_ext(ec.message());
                throw boost::system::system_error(ec);
            }
            start_session(std::move(m_http_socket));
        }
    }
}

void HttpDataTunnelServer::loop_session(BSErrorCode ec, TunnelSessionInfoPtr co_info)
{
    reenter(co_info->co)
    {
        co_info->req_parser.reset(new ReqParser());
        co_info->req_parser->body_limit(g_cfg->body_limit);
        yield http::async_read_header(co_info->tmp_socket, co_info->buffer, *(co_info->req_parser),
                                      [co_info, this](const BSErrorCode& ec, std::size_t) {
            this->loop_session(ec, co_info);
        });
        if(ec)
        {
            log_warning_ext("loop_session failed, %1%", ec.message());
            return;
        }
        //LogDebug << co_info->req_parser->get();
        if(co_info->req_parser->get().method() == http::verb::connect)
        {
            co_info->tunnel_socket = std::move(co_info->tmp_socket);
            //为建立隧道请求
            {
                auto id_it = co_info->req_parser->get().find(DATA_SESSION_ID);
                if(id_it != co_info->req_parser->get().end())
                {
                    co_info->session_id = (*id_it).value().to_string();
                }
            }
            if(co_info->session_id.empty())
            {
                log_error_ext("not has session id");
                co_info->res.result(http::status::bad_request);
                co_info->res.keep_alive(false);
                co_info->res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                co_info->res.content_length(0);
                LogDebug << co_info->res;
                yield http::async_write(co_info->tunnel_socket, co_info->res,
                                        [co_info, this](const BSErrorCode& ec, std::size_t) {
                    this->loop_session(ec, co_info);
                });
                return;
            }
            LogDebug << "connect:" << co_info->session_id;
            co_info->res.result(http::status::ok);
            co_info->res.version(11);
            co_info->res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            co_info->res.content_length(0);
            co_info->res.keep_alive(false);
            LogDebug <<  co_info->session_id << "\n" << co_info->res;
            yield http::async_write(co_info->tunnel_socket, co_info->res,
                              [co_info, this](const BSErrorCode& ec, std::size_t) {
                this->loop_session(ec, co_info);
            });
            if(ec)
            {
                log_warning_ext("loop_session failed, %1%", ec.message());
                return;
            }
            add_session(co_info);
            //最多等待时长
            co_info->timer.expires_after(std::chrono::seconds(5));
            yield co_info->timer.async_wait([co_info, this](const BSErrorCode& ec) {
                this->loop_session(ec, co_info);
            });
            remove_session(co_info->session_id, co_info);
            return;
        }
        else
        {
            //为转发请求
            {
                auto id_it = co_info->req_parser->get().find(DATA_SESSION_ID);
                if(id_it != co_info->req_parser->get().end())
                {
                    co_info->session_id = (*id_it).value().to_string();
                }
            }
            if(co_info->session_id.empty())
            {
                LogErrorExt << "not has session id";
                co_info->res.result(http::status::bad_request);
                co_info->res.keep_alive(false);
                co_info->res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                co_info->res.content_length(0);
                LogDebug << co_info->res;
                yield http::async_write(co_info->tmp_socket, co_info->res,
                                        [co_info, this](const BSErrorCode& ec, std::size_t) {
                    this->loop_session(ec, co_info);
                });
                return;
            }
            while(1)
            {
                co_info->find_ses = find_and_remove_session(co_info->session_id);
                if(!co_info->find_ses)
                {
                    //最多等待1秒
                    co_info->already_wait += 20;
                    if(co_info->already_wait > 1000)
                    {
                        LogErrorExt << "not has session id:" << co_info->session_id;
                        co_info->res.result(http::status::not_found);
                        co_info->res.keep_alive(false);
                        co_info->res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                        co_info->res.content_length(0);
                        LogDebug << co_info->res;
                        yield http::async_write(co_info->tmp_socket, co_info->res,
                                                [co_info, this](const BSErrorCode& ec, std::size_t) {
                            this->loop_session(ec, co_info);
                        });
                        return;
                    }
                    else
                    {
                        co_info->timer.expires_after(std::chrono::milliseconds(20));
                        yield co_info->timer.async_wait([co_info, this](const BSErrorCode& ec) {
                            this->loop_session(ec, co_info);
                        });
                        continue;
                    }
                }
                else
                {
                    //找到关联的session
                    co_info->http_socket = std::move(co_info->tmp_socket);
                    break;
                }
            }

            continue_session(co_info, co_info->find_ses);
        }
    }
}

void HttpDataTunnelServer::loop_transfer_data(BSErrorCode ec, TunnelSessionInfoPtr co_info)
{
    reenter(co_info->co)
    {
        //转发http请求
        co_info->req_sr.reset(new ReqSerializer(co_info->req_parser->get()));
        for(;;)
        {
            if(co_info->req_parser->is_done())
            {
                {
                    auto& req = co_info->req_parser->get();
                    req.body().data = nullptr;
                    req.body().more = false;
                }
                yield http::async_write(co_info->tunnel_socket, *(co_info->req_sr),
                                        [co_info, this](const BSErrorCode& ec, std::size_t) {
                    this->loop_transfer_data(ec, co_info);
                });
                break;
            }
            else
            {
                {
                    co_info->recv_body_buf.resize(1024);
                    auto& req = co_info->req_parser->get();
                    req.body().data = co_info->recv_body_buf.data();
                    req.body().size = co_info->recv_body_buf.size();
                }
                yield http::async_read(co_info->http_socket, co_info->buffer, *(co_info->req_parser),
                                       [co_info, this](const BSErrorCode& ec, std::size_t) {
                    this->loop_transfer_data(ec, co_info);
                });
                if(ec == http::error::need_buffer)
                {
                    ec = {};
                }
                if(ec)
                {
                    log_error_ext("loop_transfer_data failed, %1%", ec.message());
                    return;
                }
                {
                    auto& req = co_info->req_parser->get();
                    co_info->recv_body_buf.resize(co_info->recv_body_buf.size() - req.body().size);
                    req.body().size = co_info->recv_body_buf.size();
                    req.body().more = true;
                }
                yield http::async_write(co_info->tunnel_socket, *(co_info->req_sr),
                                        [co_info, this](const BSErrorCode& ec, std::size_t) {
                    this->loop_transfer_data(ec, co_info);
                });
                if(ec == http::error::need_buffer)
                {
                    ec = {};
                }
                if(ec)
                {
                    log_error_ext("loop_transfer_data failed, %1%", ec.message());
                    return;
                }
            }
        }

        //读取http响应头
        co_info->res_parser.reset(new ResParser());
        co_info->res_parser->body_limit(g_cfg->body_limit);
        yield http::async_read_header(co_info->tunnel_socket, co_info->buffer, *(co_info->res_parser),
                                      [co_info, this](const BSErrorCode& ec, std::size_t) {
            this->loop_transfer_data(ec, co_info);
        });
        if(ec)
        {
            log_warning_ext("loop_session failed, %1%", ec.message());
            return;
        }
        //转发http响应
        co_info->res_sr.reset(new ResSerializer(co_info->res_parser->get()));
        for(;;)
        {
            if(co_info->res_parser->is_done())
            {
                {
                    auto& res = co_info->res_parser->get();
                    res.body().data = nullptr;
                    res.body().more = false;
                }
                yield http::async_write(co_info->http_socket, *(co_info->res_sr),
                                        [co_info, this](const BSErrorCode& ec, std::size_t) {
                    this->loop_transfer_data(ec, co_info);
                });
                break;
            }
            else
            {
                {
                    co_info->recv_body_buf.resize(1024);
                    auto& res = co_info->res_parser->get();
                    res.body().data = co_info->recv_body_buf.data();
                    res.body().size = co_info->recv_body_buf.size();
                }
                yield http::async_read(co_info->tunnel_socket, co_info->buffer, *(co_info->res_parser),
                                       [co_info, this](const BSErrorCode& ec, std::size_t) {
                    this->loop_transfer_data(ec, co_info);
                });
                if(ec == http::error::need_buffer)
                {
                    ec = {};
                }
                if(ec)
                {
                    log_error_ext("loop_transfer_data failed, %1%", ec.message());
                    return;
                }
                {
                    auto& res = co_info->res_parser->get();
                    co_info->recv_body_buf.resize(co_info->recv_body_buf.size() - res.body().size);
                    res.body().size = co_info->recv_body_buf.size();
                    res.body().more = true;
                }
                yield http::async_write(co_info->http_socket, *(co_info->res_sr),
                                        [co_info, this](const BSErrorCode& ec, std::size_t) {
                    this->loop_transfer_data(ec, co_info);
                });
                if(ec == http::error::need_buffer)
                {
                    ec = {};
                }
                if(ec)
                {
                    log_error_ext("loop_transfer_data failed, %1%", ec.message());
                    return;
                }
            }
        }
    }
}

#include <boost/asio/unyield.hpp>

void HttpDataTunnelServer::add_session(TunnelSessionInfoPtr session)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto session_it = m_tmp_tunnel_sessions.find(session->session_id);
    if(session_it != m_tmp_tunnel_sessions.end())
    {
        LogWarn << "already has session:" << session->session_id;
    }
    m_tmp_tunnel_sessions[session->session_id] = session;
}

TunnelSessionInfoPtr HttpDataTunnelServer::find_and_remove_session(const string& session_id)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto session_it = m_tmp_tunnel_sessions.find(session_id);
    if(session_it == m_tmp_tunnel_sessions.end())
    {
        return nullptr;
    }
    auto res = session_it->second;
    m_tmp_tunnel_sessions.erase(session_it);
    return res;
}

void HttpDataTunnelServer::remove_session(const string& session_id, TunnelSessionInfoPtr session)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto session_it = m_tmp_tunnel_sessions.find(session_id);
    if(session_it == m_tmp_tunnel_sessions.end())
    {
        return;
    }
    if(session_it->second.get() != session.get())
    {
        return;
    }
    LogWarn << "remove session:" << session_id;
    m_tmp_tunnel_sessions.erase(session_it);
}
