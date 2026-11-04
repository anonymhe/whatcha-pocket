"""Append CW2017 SOC/VCELL/TEMP samples to a file, for as long as the port stays open.

Two hard constraints shaped this tool:

* The CW2017 datasheet says it treats the battery as "a free one relaxed more than
  0.5 hours" at every power-up and derives the first SOC from the 14-bit voltage as
  an OCV. Resetting the device therefore discards charge-tracking state and re-runs
  that lookup, which is why reboot-based sampling kept reporting an identical
  98.74/256. This tool never resets the device.
* Light sleep powers down the USB PHY, so the console disappears during STANDBY.
  Logging only happens while the device is awake, and the device must be kept awake
  by activity (or a PAGE that holds caffeine) for an unattended charge capture.

Samples are appended, so an interrupted run keeps everything already collected.
The gauge exposes no current register; dV/dt at 312.5 uV/LSB is the only proxy.
"""
import io, re, sys, time, serial

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
MINUTES = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
OUT = sys.argv[3] if len(sys.argv) > 3 else "build/battery_trace.log"

TRACE = re.compile(
    r"电量轨迹 SOC=(\d+)\.(\d+)/256 VCELL=0x([0-9A-F]{4})\((-?\d+) mV\) "
    r"TEMP=(-?\d+)\.(\d+) C|TEMP=n/a")
DIAG = re.compile(
    r"profile=(\S+?)\(0x([0-9A-F]{8})\).*?SOC=(\d+)\.(\d+)/256 "
    r"VCELL=0x([0-9A-F]{4})\((\d+) uV\) TEMP=0x([0-9A-F]{2})\((-?\d+)\.(\d+) C\)")

s = serial.Serial(PORT, 115200, timeout=0.5)
s.dtr = False
s.rts = False
s.reset_input_buffer()

first_v = None
first_soc = None
deadline = time.time() + MINUTES * 60
pending = b""

with open(OUT, "a", encoding="utf-8") as f:
    f.write(f"\n# --- capture started {time.strftime('%Y-%m-%d %H:%M:%S')} "
            f"port={PORT} minutes={MINUTES} ---\n")
    f.flush()
    print(f"appending to {OUT}; Ctrl-C keeps what is written", flush=True)

    while time.time() < deadline:
        pending += s.read(4096)
        while b"\n" in pending:
            raw, pending = pending.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").strip()
            now = time.strftime("%H:%M:%S")
            d = DIAG.search(line)
            t = TRACE.search(line)
            if d:
                # groups: profile, hash, whole, frac, vhex, uV, temp_hex, t_int, t_frac
                fp, _hash, sh, sf, vh, _uuv, thex, th, tf = d.groups()
                f.write(f"{now}\tDIAG\tprofile={fp}\tSOC={sh}.{sf}\t"
                        f"VTEMP_HEX={thex}\tTEMP={th}.{tf}\n")
                f.flush()
                print(f"{now} diag  SOC={sh}.{sf} VCELL=0x{vh} TEMP={th}.{tf}C "
                      f"profile={fp}", flush=True)
            elif t:
                sh, sf, vh, mv, th, tf = t.groups()
                # TEMP=n/a（温度读失败）时后两组为 None：照常记电压与 SOC，
                # 温度一栏写 n/a，不要把整行丢掉。
                temp_text = f"{th}.{tf}" if th is not None else "n/a"
                v = int(vh, 16)
                if first_v is None:
                    first_v, first_soc = v, int(sh) * 256 + int(sf)
                dv = v - first_v
                ds = int(sh) * 256 + int(sf) - first_soc
                f.write(f"{now}\tTRACE\tSOC={sh}.{sf}\tVRAW={v}\tMV={mv}\t"
                        f"TEMP={temp_text}\tDV_LSB={dv}\tDSOC_RAW={ds}\n")
                f.flush()
                print(f"{now} SOC={sh}.{sf} VCELL=0x{vh}({v}) {mv}mV "
                      f"dV={dv:+d}LSB dSOC={ds:+d}/256 {temp_text}C", flush=True)
s.close()
print("port released; samples flushed to", OUT)
