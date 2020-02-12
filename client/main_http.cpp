#include "http_data_tunnel_client.h"
#include <iostream>
#include <vector>
#include <thread>
using namespace std;
#include <boost/lexical_cast.hpp>

#define KK_PRT(fmt...)   \
    do {\
    time_t timep;\
    time(&timep);\
    tm* tt = localtime(&timep); \
    printf("[%d-%02d-%02d %02d:%02d:%02d][%s]-%d: ", tt->tm_year+1900,tt->tm_mon+1,tt->tm_mday,tt->tm_hour,tt->tm_min,tt->tm_sec, __FUNCTION__, __LINE__);\
    printf(fmt);\
    printf("\n");\
    }while(0)

int main(int argc,char ** argv)
{
    if(argc != 6)
    {
        std::cerr <<
                     "Usage: " << argv[0] << " <host> <port> <session_id> local_ip <local_port>\n" <<
                     "Example:\n    " << argv[0] <<
                     " 121.199.4.198 4080 test-tunnel1 127.0.0.1 80\n";
        return EXIT_FAILURE;
    }

    string host = argv[1];
    uint16_t port = boost::lexical_cast<uint16_t>(string(argv[2]));
    string session_id = argv[3];
    string local_ip = argv[4];
    uint16_t local_port = boost::lexical_cast<uint16_t>(string(argv[5]));

    IoContext ioc;
    HttpDataTunnelClientPtr ht(new HttpDataTunnelClient(ioc));
    ht->async_run(host, port, session_id, local_ip, local_port, [session_id](boost::system::error_code ec){
        if(!ec)
        {
            KK_PRT("session_id:%s, err:%s", session_id.c_str(), ec.message().c_str());
        }
    });

    ioc.run();
    return 0;
}
