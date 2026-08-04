import serial, sys, time, glob

ports = glob.glob("/dev/cu.usbmodem*")
if not ports:
    sys.stderr.write("no usbmodem port\n")
    sys.exit(2)
port = ports[0]
dur = int(sys.argv[1]) if len(sys.argv) > 1 else 90

p = serial.Serial(port, 115200, timeout=1)
p.dtr = True
p.rts = True
sys.stderr.write("capturing %ds on %s (DTR asserted)\n" % (dur, port))
sys.stderr.flush()

end = time.time() + dur
while time.time() < end:
    line = p.readline()
    if line:
        sys.stdout.write(line.decode(errors="replace"))
        sys.stdout.flush()
p.close()
