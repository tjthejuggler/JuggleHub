import zmq
import sys
import argparse
sys.path.append('../hub')
from hub.juggler_pb2 import CommandRequest

def send_command(module_name, ip, port):
    context = zmq.Context()
    socket = context.socket(zmq.REQ)
    socket.connect("tcp://127.0.0.1:5565")

    command = CommandRequest()
    command.type = CommandRequest.LOAD_MODULE
    command.module_name = module_name
    if ip:
        command.module_args["ip"] = ip
    if port:
        command.module_args["port"] = str(port)

    socket.send(command.SerializeToString())
    response_raw = socket.recv()
    print(f"Received reply: {response_raw}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("module_name", help="Name of the module to load")
    parser.add_argument("--ip", help="IP address of the ball")
    parser.add_argument("--port", type=int, help="Port of the ball")
    args = parser.parse_args()
    
    send_command(args.module_name, args.ip, args.port)