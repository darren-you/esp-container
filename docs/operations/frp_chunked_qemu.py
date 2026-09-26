import os
import signal
import subprocess
import sys
import time

firmware, logfile, duration = sys.argv[1], sys.argv[2], float(sys.argv[3])
with open(logfile, 'wb') as output:
    process = subprocess.Popen(
        ['idf.py', '-C', firmware, 'qemu', '--qemu-extra-args=-no-reboot'],
        stdout=output, stderr=subprocess.STDOUT, start_new_session=True,
    )
    try:
        process.wait(timeout=duration)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=10)
print(f'qemu_launcher_exit={process.returncode} duration_s={duration} log={logfile}')
