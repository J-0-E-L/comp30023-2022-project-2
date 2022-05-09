#!/bin/bash
./server 4 8080 ./www &
curl --http1.0 -v http://127.0.0.1:8080/index.html --path-as-is
echo -e "\n\n"

./server 4 3000 ./www &
curl --http1.0 -v http://127.0.0.1:3000/subdir/other.html --path-as-is
echo -e "\n\n"

./server 6 3000 ./www &
curl --http1.0 -v http://[::1]:3000/subdir/other.html --path-as-is
echo -e "\n\n"

./server 4 3050 ./www &
curl --http1.0 -v http://127.0.0.1:3050/image.jpg --path-as-is
echo -e "\n\n"
