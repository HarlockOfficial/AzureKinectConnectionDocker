import zmq
import json
import numpy as np
import sounddevice as sd

ctx = zmq.Context()
sub = ctx.socket(zmq.SUB)
sub.connect("tcp://localhost:5555")
sub.setsockopt(zmq.SUBSCRIBE, b"audio")

AUDIO_SAMPLE_RATE = 48000 # set same value as kinect_access_server/server_native/src/main.cpp:AUDIO_SAMPLE_RATE

print("Client connected, waiting audio frames")
while True:
    try:
        parts = sub.recv_multipart()
        # parts[0] topic, parts[1] header_json, parts[2] color, parts[3] depth, parts[4] bodies_json, parts[5] imu_bytes
        header = json.loads(parts[1].decode('utf-8'))
        audio_bytes = parts[2]
        channel_count = header.get("audio_channels", 1)
        # 4 * channel count because each sample is a float32 (4 bytes) and there are channel_count channels
        frames_count = header.get("audio_frames", len(audio_bytes) // (4 * channel_count))
        audio_np = np.frombuffer(audio_bytes, dtype=np.float32, count=frames_count * channel_count)
        audio_np = audio_np.reshape((frames_count, channel_count))
        sd.play(audio_np, samplerate=AUDIO_SAMPLE_RATE)
    except (KeyboardInterrupt, SystemExit) as e:
        print("Quitting")
        break
    except Exception as e:
        print("Error, reason:", e)
