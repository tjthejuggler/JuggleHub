import zmq
import juggler_pb2

class ZMQClient:
    def __init__(self, sub_port="5555", req_port="5565"):
        self.context = zmq.Context()
        self.sub_socket = self.context.socket(zmq.SUB)
        self.sub_socket.connect(f"tcp://localhost:{sub_port}")
        self.sub_socket.setsockopt_string(zmq.SUBSCRIBE, "")
        
        self.req_socket = self.context.socket(zmq.REQ)
        self.req_socket.connect(f"tcp://localhost:{req_port}")

    def receive_frame_data(self):
        try:
            raw_data = self.sub_socket.recv(flags=zmq.NOBLOCK)
            frame_data = juggler_pb2.FrameData()
            frame_data.ParseFromString(raw_data)
            return frame_data
        except zmq.Again:
            return None

    def send_command(self, command):
        self.req_socket.send(command.SerializeToString())
        response_raw = self.req_socket.recv()
        
        response = juggler_pb2.CommandResponse()
        response.ParseFromString(response_raw)
        return response
