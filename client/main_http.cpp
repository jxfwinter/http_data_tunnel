#include "http_data_tunnel_client.h"
#include <iostream>
#include <vector>
#include <thread>
using namespace std;
#include <boost/lexical_cast.hpp>

typedef std::shared_ptr<IoContext> IoContextPtr;
int main(int argc,char ** argv)
{
    if(argc != 6)
    {
        std::cerr <<
                     "Usage: " << argv[0] << " <host> <port> <session_id> <local_port> <count>\n" <<
                     "Example:\n    " << argv[0] <<
                     " 121.199.4.198 28080 test1122 3080 100\n";
        return EXIT_FAILURE;
    }

    string host = argv[1];
    uint16_t port = boost::lexical_cast<uint16_t>(string(argv[2]));
    string session_id = argv[3];
    uint16_t local_port = boost::lexical_cast<uint16_t>(string(argv[4]));
    int count = boost::lexical_cast<int>(string(argv[5]));

    int thread_pool = 3;
    std::vector<IoContextPtr> ioc_pool;
    for(int i = 0; i < thread_pool; ++i)
    {
        IoContextPtr ioc_ptr(new IoContext());
        ioc_pool.push_back(ioc_ptr);
    }

    for(int i=1; i<=count; ++i)
    {
        string session_id_tmp = session_id + boost::lexical_cast<string>(i);

        HttpDataTunnelClientPtr ht(new HttpDataTunnelClient(*ioc_pool[i%thread_pool]));
        ht->async_run(host, port, session_id_tmp, "127.0.0.1", local_port, [session_id_tmp](boost::system::error_code ec){
            std::cout << "session_id:" << session_id_tmp << "," ec.message() << "\n";
        });
    }

    typedef boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_context_work;
    std::vector<std::thread> v;
    v.reserve(thread_pool);
    for(int i = 0; i < thread_pool; ++i)
    {
        auto& ioc = *ioc_pool[i];
        v.push_back(std::thread([&ioc] {
            io_context_work ioc_worker = boost::asio::make_work_guard(ioc);
            ioc.run();
            ioc_worker.reset();
        }));
    }

    for(auto& t : v)
    {
        t.join();
    }

    return 0;
}
