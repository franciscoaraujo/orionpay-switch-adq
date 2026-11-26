#!/usr/bin/env python3
import sys, subprocess, os, traceback

# Compile proto to Python stubs (outputs to /tmp)
proto = '/mnt/c/Users/chico/CLionProjects/orionpay-switch-adq/src/grpc/acquirer.proto'
outdir = '/tmp'
try:
    subprocess.check_call([sys.executable, '-m', 'grpc_tools.protoc', '-I' + os.path.dirname(proto), '--python_out=' + outdir, '--grpc_python_out=' + outdir, proto])
except Exception as e:
    print('protoc compile failed:', e)

# Ensure /tmp is on sys.path and import generated modules
sys.path.insert(0, outdir)
try:
    import acquirer_pb2, acquirer_pb2_grpc
except Exception as e:
    print('Importing generated stubs failed:', e)
    traceback.print_exc()
    sys.exit(2)

import grpc

channel = grpc.insecure_channel('localhost:50051')
stub = acquirer_pb2_grpc.AcquiringSwitchServiceStub(channel)
req = acquirer_pb2.AuthorizeRequest(
    id='py-1', merchant_id='m1', terminal_id='t1', amount='10.00', currency_code=986,
    pin_block_terminal='', pan_masked='123456******1234')

try:
    resp = stub.AuthorizeTransaction(req, timeout=10)
    print('RESPONSE:')
    print(resp)
except Exception as e:
    print('RPC ERROR:')
    traceback.print_exc()
