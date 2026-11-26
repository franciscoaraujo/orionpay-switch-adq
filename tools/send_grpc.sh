#!/bin/bash
set -e
# Install pip and grpc tools (may ask for sudo password)
sudo apt-get update -qq || true
sudo apt-get install -y -qq python3-pip || true
python3 -m pip install --user grpcio grpcio-tools googleapis-common-protos || true
# Run the python test script
python3 /mnt/c/Users/chico/CLionProjects/orionpay-switch-adq/tools/send_grpc.py > /tmp/send_grpc_out.txt 2>&1 || true
cat /tmp/send_grpc_out.txt
