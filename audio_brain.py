import socket
import time
import math
import random

UDP_IP = "127.0.0.1"
UDP_PORT = 1337

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print("--- PYTHON AUDIO BRAIN V2 (ORGANIC DJ MODE) ---")
print(f"Broadcasting to {UDP_IP}:{UDP_PORT} at 60Hz...")

t = 0.0
while True:
    # 1. ORGANIC BASS (The Kick Drum)
    # Base sine wave, but we randomly suppress some beats to create "groove"
    raw_bass = math.sin(t * math.pi * 4.0)
    if raw_bass > 0.9:
        # When the kick hits, give it a random strength!
        bass = random.uniform(0.7, 1.0)
        # 10% chance to completely drop the bass (a musical "rest")
        if random.random() < 0.1: bass = 0.0 
    else:
        bass = 0.0 # Silence between kicks

    # 2. ORGANIC MID (The Synths/Vocals)
    # Slow, wobbly sine waves combined with Perlin-style noise
    mid = max(0.0, math.sin(t * math.pi * 1.5)) * random.uniform(0.4, 0.9)
    
    # 3. ORGANIC TREBLE (The Hi-Hats)
    # Fast, frantic, and highly randomized
    treble = random.uniform(0.2, 1.0) if random.random() > 0.5 else 0.1

    # Format and fire!
    payload = f"{bass:.3f},{mid:.3f},{treble:.3f}"
    sock.sendto(payload.encode('utf-8'), (UDP_IP, UDP_PORT))

    t += 0.016 
    time.sleep(0.016)
