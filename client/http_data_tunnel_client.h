#ifndef HTTP_TUNNEL_CLIENT_H
#define HTTP_TUNNEL_CLIENT_H

#define DATA_SESSION_ID "DATA-SID"

#include <string>
#include <memory>
#include <map>
#include <list>
#include <functional>
#include <boost/beast.hpp>
#include <boost/asio/ip/tcp.hpp>
using namespace std;
typedef boost::asio::io_context IoContext;
typedef boost::asio::ip::tcp::resolver Resolver;
typedef boost::asio::ip::tcp::resolver::results_type ResolverResult;

typedef boost::asio::ip::tcp::endpoint Endpoint;
typedef boost::asio::ip::tcp::socket TcpSocket;

typedef boost::asio::coroutine Coroutine;
using namespace boost::beast;

typedef http::request<http::string_body> StrRequest;
typedef http::response<http::string_body> StrResponse;
typedef http::request_parser<http::buffer_body> ReqParser;
typedef http::response_parser<http::buffer_body> ResParser;
typedef http::request_serializer<http::buffer_body> ReqSerializer;
typedef http::response_serializer<http::buffer_body> ResSerializer;

//最终结果回调
typedef std::function<void (boost::system::error_code ec)> RunCallback;
class HttpDataTunnelClient;
typedef std::shared_ptr<HttpDataTunnelClient> HttpDataTunnelClientPtr;

class HttpDataTunnelClient : std::enable_shared_from_this<HttpDataTunnelClient>
{
public:
    HttpDataTunnelClient(IoContext& ioc);
    ~HttpDataTunnelClient();

    void async_run(string host, uint16_t port, string session_id,
                   string local_ip, uint16_t local_port,
                   RunCallback cb);

private:
    void loop_run(boost::system::error_code ec);

private:
    IoContext& m_ioc;
    TcpSocket m_socket;
    Resolver m_resolver;

    TcpSocket m_to_socket;

    string m_local_ip = "0.0.0.0";
    uint16_t m_local_port = 9108;

    RunCallback m_cb;

    string m_host;
    uint16_t m_port;
    string m_session_id;

    Coroutine m_co;

    ResolverResult m_resolve_result;
    StrRequest m_req;
    StrResponse m_res;

    boost::beast::flat_buffer m_read_buffer;

    ReqParser m_req_parser;
    ResParser m_res_parser;
    std::unique_ptr<ReqSerializer> m_req_sr;
    std::unique_ptr<ResSerializer> m_res_sr;
    vector<char> m_body_buffer;
};

#endif // HTTP_TUNNEL_CLIENT_H
