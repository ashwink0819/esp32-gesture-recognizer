# Gesture capture: records 2 s windows from the ESP32 and saves Edge Impulse-ready CSVs.
# Usage: python tools\capture.py <label> <person>
#   e.g. python tools\capture.py circle ashwin
import csv, os, sys, time
import serial

PORT = "COM3"
BAUD = 115200
DURATION = 2.0      # seconds per sample
RATE_HZ = 100       # must match the sketch

def read_window(ser):
    ser.reset_input_buffer()
    rows, t0 = [], time.time()
    while time.time() - t0 < DURATION:
        line = ser.readline().decode(errors="ignore").strip()
        parts = line.split(",")
        if len(parts) != 6:
            continue
        try:
            rows.append([float(p) for p in parts])
        except ValueError:
            continue
    return rows

def main():
    if len(sys.argv) < 3:
        print("Usage: python tools\\capture.py <label> <person>")
        return
    label, person = sys.argv[1], sys.argv[2]
    outdir = os.path.join("data", person, label)
    os.makedirs(outdir, exist_ok=True)

    ser = serial.Serial(PORT, BAUD, timeout=1)
    print("Board is resetting - keep it STILL for 4 s (gyro calibration)...")
    time.sleep(4)

    n = len([f for f in os.listdir(outdir) if f.endswith(".csv")])
    last = None
    while True:
        cmd = input(f"\n[{label}/{person}] {n} saved. Enter=record, d=delete last, q=quit: ").strip().lower()
        if cmd == "q":
            break
        if cmd == "d":
            if last and os.path.exists(last):
                os.remove(last); n -= 1; print(f"Deleted {last}"); last = None
            else:
                print("Nothing to delete.")
            continue
        for c in ("3", "2", "1"):
            print(c, end=" ", flush=True); time.sleep(0.4)
        print("GO!")
        rows = read_window(ser)
        expected = int(DURATION * RATE_HZ)
        if len(rows) < expected * 0.8:
            print(f"WARNING: only {len(rows)}/{expected} rows - check the board. Not saved.")
            continue
        fname = os.path.join(outdir, f"{label}.{person}_{n:03d}.csv")
        with open(fname, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["timestamp", "ax", "ay", "az", "gx", "gy", "gz"])
            for i, r in enumerate(rows):
                w.writerow([i * (1000 // RATE_HZ)] + r)
        last = fname
        n += 1
        print(f"Saved {fname} ({len(rows)} rows)")
    ser.close()

if __name__ == "__main__":
    main()