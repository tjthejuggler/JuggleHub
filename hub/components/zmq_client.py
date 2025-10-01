import zmq
import juggler_pb2
import logging
import os

# Configure logging based on JUGGLEHUB_DEBUG environment variable
debug_enabled = os.environ.get('JUGGLEHUB_DEBUG', '0') == '1'
logging.basicConfig(
    level=logging.DEBUG if debug_enabled else logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)

class ZMQClient:
    def __init__(self, sub_port="5555", req_port="5565"):
        self.context = zmq.Context()
        self.sub_socket = self.context.socket(zmq.SUB)
        self.sub_socket.connect(f"tcp://localhost:{sub_port}")
        self.sub_socket.setsockopt_string(zmq.SUBSCRIBE, "")
        logging.info(f"ZMQ SUB socket connected to tcp://localhost:{sub_port}")
        
        self.req_socket = self.context.socket(zmq.REQ)
        self.req_socket.connect(f"tcp://localhost:{req_port}")
        logging.info(f"ZMQ REQ socket connected to tcp://localhost:{req_port}")

    def receive_frame_data(self):
        try:
            raw_data = self.sub_socket.recv(flags=zmq.NOBLOCK)
            logging.debug(f"Received raw data: {len(raw_data)} bytes")
            
            frame_data = juggler_pb2.FrameData()
            frame_data.ParseFromString(raw_data)
            
            logging.debug(f"Parsed FrameData: frame_number={frame_data.frame_number}, "
                          f"num_balls={len(frame_data.balls)}, "
                          f"num_hands={len(frame_data.hands)}, "
                          f"num_raw_detections={len(frame_data.raw_detections)}")
            
            # Log hand details
            if len(frame_data.hands) > 0:
                for i, hand in enumerate(frame_data.hands):
                    logging.debug(f"Hand {i}: id={hand.id}, visible={hand.is_visible}, "
                                f"num_keypoints={len(hand.keypoints)}, "
                                f"wrist_3d=({hand.wrist_pos_3d.x:.2f}, {hand.wrist_pos_3d.y:.2f}, {hand.wrist_pos_3d.z:.2f})")
                    if len(hand.keypoints) > 0:
                        logging.debug(f"  First keypoint 2D: ({hand.keypoints[0].pos_2d.x:.1f}, {hand.keypoints[0].pos_2d.y:.1f})")
            else:
                logging.debug("No hands detected in this frame")
            
            if len(frame_data.raw_detections) > 0 and len(frame_data.balls) == 0:
                logging.warning("Received raw detections but no tracked balls.")
            
            return frame_data
        except zmq.Again:
            return None
        except Exception as e:
            logging.error(f"Error receiving or parsing frame data: {e}", exc_info=True)
            return None

    def send_command(self, command):
        self.req_socket.send(command.SerializeToString())
        response_raw = self.req_socket.recv()
        
        response = juggler_pb2.CommandResponse()
        response.ParseFromString(response_raw)
        return response
