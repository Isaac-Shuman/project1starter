#!/usr/bin/bash

function say_hi() {
    echo "hi"

}

function run_tests() {
    set -x

    kill -9 $(jobs -p)
    make
    rm *.bin
    head -c 20000 /dev/urandom > /tmp/test.bin
    ./server 8080 < /tmp/test.bin > server.bin &
    ./client localhost 8080 < /tmp/test.bin > client.bin &

    sleep 10

    wc server.bin
    wc client.bin
    diff server.bin client.bin

    set +x
}