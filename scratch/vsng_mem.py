import pymem
import socket
import math
import time
import struct

# === PENGATURAN UDP KE FLYPT MOVER ===
UDP_IP = "127.0.0.1"
UDP_PORT = 4444
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# === ADDRESS MEMORI VIRTUAL SAILOR NG ===
HEADING_ADDRESS = 0xBB9AD4
SPEED_ADDRESS = 0xBB9AD0

def main():
    print("==================================================", flush=True)
    print("Mencari proses Virtual Sailor NG (vsf_ng.exe)...", flush=True)
    
    try:
        pm = pymem.Pymem('vsf_ng.exe')
        base_address = pm.base_address
        print("✅ Berhasil konek ke memori game!", flush=True)
        print(f"📡 Mengirim data BINARY ke FlyPT Mover di {UDP_IP}:{UDP_PORT}", flush=True)
        print("Tekan Ctrl+C pada keyboard untuk berhenti.", flush=True)
        print("==================================================", flush=True)
    except Exception as e:
        print("❌ GAME TIDAK DITEMUKAN!", flush=True)
        return

    # --- PENGATURAN SKALA OMBAK ---
    wave_phase = 0.0
    wave_speed = 0.03    # Kecepatan ombak (Makin besar makin cepat/mabuk)
    wave_degrees = 10.0  # Kemiringan Pitch & Roll (Derajat)
    wave_heave_m = 0.2   # Naik turun Heave (Meter, 0.2 = 20cm)

    while True:
        try:
            # 1. BACA MEMORI GAME
            heading = pm.read_float(base_address + HEADING_ADDRESS)
            speed = pm.read_float(base_address + SPEED_ADDRESS)

            # 2. BIKIN OMBAK PALSU
            wave_phase += wave_speed
            pitch = math.sin(wave_phase) * wave_degrees
            roll = math.sin(wave_phase + 1.57) * wave_degrees
            heave = math.sin(wave_phase * 0.8) * wave_heave_m

            # 3. PACKING KE BINER (5 data Float 32-bit = format 'fffff')
            # Urutan: Pitch, Roll, Yaw(Heading), Heave, Speed
            packed_data = struct.pack('fffff', pitch, roll, heading, heave, speed)
            
            # 4. TEMBAK DATA RAW KE FLYPT
            sock.sendto(packed_data, (UDP_IP, UDP_PORT))

            time.sleep(0.016)

        except KeyboardInterrupt:
            print("\n🛑 Dihentikan oleh user.", flush=True)
            break
        except Exception as e:
            print("\n❌ Terputus dari memori game. Error:", e, flush=True)
            break

if __name__ == "__main__":
    main()
