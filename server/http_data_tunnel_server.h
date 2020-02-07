#ifndef HTTP_DATA_TUNNEL_SERVER_H
#define HTTP_DATA_TUNNEL_SERVER_H

#include "kconfig.h"

struct TunnelSessionInfo;
typedef std::shared_ptr<TunnelSessionInfo> TunnelSessionInfoPtr;

struct TunnelSessionInfo
{
    Coroutine co;
    std::unique_ptr<ReqParser> req_parser;
    std::unique_ptr<ResParser> res_parser;
    std::unique_ptr<ReqSerializer> req_sr;
    std::unique_ptr<ResSerializer> res_sr;
    TcpSocket tmp_socket;
    TcpSocket tunnel_socket;
    TcpSocket http_socket;
    boost::beast::flat_buffer buffer;
    vector<char> recv_body_buf; //用来接收body的buffer
    //StrRequest req;
    //StrResponse res;
    string session_id;
    STimer timer;
    int already_wait = 0; //已等待毫秒
    TunnelSessionInfoPtr find_ses;

    TunnelSessionInfo(IoContext& ioc);
};


class HttpDataTunnelServer
{
public:
    HttpDataTunnelServer(IoContext& ioc);
    ~HttpDataTunnelServer();

    void start();

    void stop();

    //添加session
    void add_session(TunnelSessionInfoPtr session);
    //删除session
    void remove_session(const string& session_id, TunnelSessionInfoPtr session);

    TunnelSessionInfoPtr find_session(const string& session_id);

private:
    void start_session(TcpSocket s);
    void loop_session(BSErrorCode ec, TunnelSessionInfoPtr co_info);

    void continue_session(TunnelSessionInfoPtr join_ses, TunnelSessionInfoPtr ses);
    void loop_transfer_data(BSErrorCode ec, TunnelSessionInfoPtr co_info);

private:
    IoContext& m_ioc;
    Acceptor m_http_acceptor;
    TcpSocket m_http_socket;

    Endpoint m_http_listen_ep;
    Coroutine m_http_accept_co;

    //key为session id,临时的tunnel,用来等待请求连接
    std::unordered_map<string, TunnelSessionInfoPtr> m_tmp_tunnel_sessions;
    std::mutex m_mutex;
};


#endif // HTTP_DATA_TUNNEL_SERVER_H
