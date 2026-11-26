#!/usr/bin/env python3
import os
import random
import subprocess
import sys
import tempfile

binary = sys.argv[1]
iterations = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
random.seed(42)

pieces = ["echo", "cat", "ls", "export", "unset", "cd", "pwd", "env", "exit",
          "|", "||", "&&", "(", ")", "<", ">", ">>", "<<", "'", '"', "$",
          "$?", "$HOME", "$NOPE", "$A", "*", "a*", "x", "EOF", "-n", "=",
          "A=1", "A+=2", "A=\"x y\"", " ", "\t", "''", '""', "$\"", "$'",
          "f", "/tmp", "..", "nosuch", "1A=2", "&", ";", "\\"]

crashes = 0
with tempfile.TemporaryDirectory() as work:
    for i in range(iterations):
        lines = []
        for _ in range(random.randint(1, 4)):
            lines.append("".join(random.choice(pieces) + random.choice(["", " "])
                                 for _ in range(random.randint(1, 10))))
        script = "\n".join(lines) + "\nEOF\nEOF\n"
        script = script.replace("exit", "exit_")
        try:
            proc = subprocess.run([binary], input=script.encode(),
                                  capture_output=True, timeout=10, cwd=work,
                                  env={"PATH": os.environ["PATH"],
                                       "HOME": work,
                                       "ASAN_OPTIONS": "detect_leaks=0"})
        except subprocess.TimeoutExpired:
            print("TIMEOUT:", repr(script))
            crashes += 1
            continue
        err = proc.stderr.decode(errors="replace")
        if proc.returncode < 0 or proc.returncode >= 128 \
                or "AddressSanitizer" in err or "runtime error" in err:
            crashes += 1
            print("CRASH rc=%d: %r" % (proc.returncode, script))
            print(err[-1500:])
            if crashes > 5:
                break

print("iterations: %d, crashes: %d" % (iterations, crashes))
sys.exit(1 if crashes else 0)
