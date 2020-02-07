#ifndef KCONFIG_H
#define KCONFIG_H

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/date_time.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/crc.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/gregorian/greg_duration.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/unordered_map.hpp>
#include <boost/thread.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include "logger.h"

using namespace std;
using namespace boost;
using namespace boost::posix_time;
using namespace boost::uuids;
using namespace boost::property_tree;


typedef boost::asio::io_context IoContext;
typedef boost::asio::ip::tcp::acceptor Acceptor;
typedef boost::asio::ip::tcp::endpoint Endpoint;
typedef boost::asio::ip::tcp::socket TcpSocket;
typedef boost::asio::coroutine Coroutine;
typedef boost::asio::ip::tcp::resolver Resolver;
typedef boost::asio::ip::tcp::resolver::results_type ResolverResult;
typedef boost::asio::steady_timer STimer;
using namespace boost::asio::ip;

typedef boost::system::error_code BSErrorCode;
namespace http = boost::beast::http;
typedef http::request<http::buffer_body> BufferRequest;
typedef http::response<http::buffer_body> BufferResponse;
typedef http::request<http::string_body> StrRequest;
typedef http::response<http::string_body> StrResponse;
typedef http::request_parser<http::buffer_body> ReqParser;
typedef http::response_parser<http::buffer_body> ResParser;
typedef http::request_serializer<http::buffer_body> ReqSerializer;
typedef http::response_serializer<http::buffer_body> ResSerializer;


#define DATA_SESSION_ID "DATA-SID"

struct ConfigParams
{
    string http_listen_addr = "0.0.0.0";
    uint16_t http_listen_port = 3080;

    uint16_t thread_pool = 3;

    string log_path = "./http_data_rproxy.log";
    boost::log::trivial::severity_level log_level = boost::log::trivial::debug;
};

//初始化参数
bool init_params(int argc, char** argv, ConfigParams& params);

extern ConfigParams* g_cfg;

#endif
