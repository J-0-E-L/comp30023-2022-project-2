#!/bin/bash
./server 4 8080 ./www &
curl --http1.0 -v http://127.0.0.1:8080/index.html --path-as-is
echo -e "\n\n"

./server 4 3005 ./www &
curl --http1.0 -v http://127.0.0.1:3005/subdir/other.html --path-as-is
echo -e "\n\n"
