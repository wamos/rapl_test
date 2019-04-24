#!/usr/bin/env bash

# This script is intended to set up end host for HPC environment

# Machine configuraiton
CPU_CORE_COUNT=24
#NIC_IF=enp101s0
#TARGET_MTU=9000

# Set CPU frequency to max
: '
echo "Setting CPU governor to performance ..."
if ! dpkg -s cpufrequtils 2>&1 | grep -q 'installed$'; then
    echo "Package cpufrequtils not installed. Installing ..."
    sudo apt-get install -y cpufrequtils
fi
echo 'GOVERNOR="performance"' | sudo tee /etc/default/cpufrequtils > /dev/null
sudo systemctl disable ondemand
echo "Done"
echo
'

# Disable Hyper-Threading

echo "Disabling Hyper-Threading ..."
#from=$((CPU_CORE_COUNT))
#to=$((2 * CPU_CORE_COUNT - 1))
from=1
to=23
for i in $(seq $from $to); do
    echo 1 | sudo tee --append /sys/devices/system/cpu/cpu$i/online > /dev/null
    cat "/sys/devices/system/cpu/cpu$i/online"
done
max_cpu=$((CPU_CORE_COUNT - 1))
if ! lscpu | grep -q "On-line CPU(s) list:  0-$max"; then
    echo "[Warning] HT not disabled correctly."
fi
echo "Done"
echo


