import zmq
import json
import numpy as np
import cv2
import struct
from dataclasses import dataclass

@dataclass
class KinectFrame:
    seq: int
    timestamp_ms: int
    color: np.ndarray  # view into buffer (H,W,4) BGRA uint8
    depth: np.ndarray  # view into buffer (H,W) uint16
    bodies: list       # list of dicts {id, joints: [x,y,z,conf,...]}
    imu_samples: list  # list of tuples (timestamp_us, ax,ay,az,gx,gy,gz)

ctx = zmq.Context()
sub = ctx.socket(zmq.SUB)
sub.connect("tcp://localhost:5555")
sub.setsockopt(zmq.SUBSCRIBE, b"frame")

print("Client connected, waiting frames")
try:
    while True:
        parts = sub.recv_multipart()
        # parts[0] topic, parts[1] header_json, parts[2] color, parts[3] depth, parts[4] bodies_json, parts[5] imu_bytes
        header = json.loads(parts[1].decode('utf-8'))
        color_bytes = parts[2]
        depth_bytes = parts[3]
        bodies_json = parts[4].decode('utf-8') if len(parts) > 4 and parts[4] else "[]"
        imu_bytes = parts[5] if len(parts) > 5 else b''

        color = None
        if header.get("color_bytes", 0) > 0:
            w = header["color_w"]; h = header["color_h"]
            arr = np.frombuffer(color_bytes, dtype=np.uint8, count=header["color_bytes"])
            color = arr.reshape((h, w, 4))
        depth = None
        if header.get("depth_bytes", 0) > 0:
            dw = header["depth_w"]; dh = header["depth_h"]
            darr = np.frombuffer(depth_bytes, dtype=np.uint16, count=header["depth_bytes"]//2)
            depth = darr.reshape((dh, dw))

        bodies = json.loads(bodies_json)
        # deserialize imu bytes
        imu_samples = []
        if header.get("imu_count", 0) > 0 and imu_bytes:
            offset = 0
            for _ in range(header["imu_count"]):
                ts = struct.unpack_from("<Q", imu_bytes, offset)[0]; offset += 8
                vals = struct.unpack_from("<6f", imu_bytes, offset); offset += 6*4
                imu_samples.append((ts, ) + vals)

        frame = KinectFrame(seq=header["seq"], timestamp_ms=header["ts_ms"], color=color, depth=depth, bodies=bodies, imu_samples=imu_samples)

        # Example display
        skeleton_frame = None
        if frame.color is not None:
            bgr = cv2.cvtColor(frame.color, cv2.COLOR_BGRA2BGR)
            skeleton_frame = bgr.copy()
            cv2.imshow("color", bgr)
        if frame.depth is not None:
            d = frame.depth.copy().astype(np.float32)
            d = np.clip(d, 0, 4000)
            vis = ((d / 4000.0) * 255).astype(np.uint8)
            vis = cv2.applyColorMap(vis, cv2.COLORMAP_JET)
            cv2.imshow("depth", vis)

        # draw skeletons if present (simple)
        if skeleton_frame is not None and len(frame.bodies) > 0:
            for b in frame.bodies:
                joints = b["joints"]
                for j in range(0, len(joints), 4):
                    x = int(joints[j])
                    y = int(joints[j+1])
                    conf = joints[j+3]
                    if conf > 0.5:
                        cv2.circle(skeleton_frame, (x, y), 5, (0, 255, 0), -1)
            cv2.imshow("color_with_skeleton", skeleton_frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break
except (KeyboardInterrupt, SystemExit, Exception) as e:
    print("Exiting, reason:", e)
finally:
    cv2.destroyAllWindows()
