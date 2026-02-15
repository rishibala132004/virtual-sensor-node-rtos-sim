import re
import matplotlib.pyplot as plt

LOG_PATH = '../logs/run2.log'
time_temp, vals_temp = [], []
time_filt, vals_filt = [], []

with open(LOG_PATH, 'r') as f:
    for line in f:
        mtemp = re.search(r'(\d+) ms TEMP: Temp reading ([\d.-]+)', line)
        if mtemp:
            t, v = int(mtemp.group(1)), float(mtemp.group(2))
            time_temp.append(t)
            vals_temp.append(v)
            continue
        
        mbg = re.search(r'(\d+) ms BG: Filtered temp ([\d.-]+)', line)
        if mbg:
            t, v = int(mbg.group(1)), float(mbg.group(2))
            time_filt.append(t)
            vals_filt.append(v)

plt.figure(figsize=(10, 4))
plt.plot(time_temp, vals_temp, label='Raw Temp', alpha=0.7)
plt.plot(time_filt, vals_filt, label='Filtered Temp', linewidth=2)
plt.xlabel('Time (ms)')
plt.ylabel('Temperature (°C)')
plt.title('Raw vs Filtered Temperature (EMA Filter)')
plt.legend()
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.show()
