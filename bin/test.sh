#!/bin/bash

count=10
fname=nginx-logo.png
for((i=1;i<=$count;i++))
do
./http_data_tunnel_client 127.0.0.1 4080 test-tunnel${i} 127.0.0.1 80 &
done

sleep  1
for((i=1;i<=$count;i++))
do
curl -v -H "DATA-SID: test-tunnel${i}" -o $fname$i http://127.0.0.1:4080/$fname &
done
