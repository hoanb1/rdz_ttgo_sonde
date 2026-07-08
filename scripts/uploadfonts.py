#!/usr/bin/env python3
import sys
import subprocess

if len(sys.argv) < 3:
  print("Usage: uploadfonts <font.bin> <partition.csv>")
  exit(1)

fontbin = sys.argv[1]
partition = sys.argv[2]

OFFSET = -1
SIZE = -1

# Fetch font partition info
with open(partition, "r") as file:
  for line in file:
    print(line.strip())
    if line.startswith("fonts"):
      l = line.split(",")
      OFFSET = l[3]
      SIZE = l[4]

print("Using offset", OFFSET, "; size is", SIZE)

chip = "auto"
cmd = [
  sys.executable,
  "-m",
  "esptool",
  "--chip", chip,
  "--baud", "921600",
  "--before", "default_reset",
  "--after", "hard_reset",
  "write_flash", "-z",
  "--flash_mode", "dio",
  "--flash_freq", "80m",
  "--flash_size", "detect",
  str(OFFSET), fontbin
]
print("Running command:", " ".join(cmd))
subprocess.run(cmd, check=True)




