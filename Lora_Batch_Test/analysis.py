# find_max_lost.py
import re

# 1. Pull out all idx (in order of appearance)
idx_list = []
with open('./test_data/MAMP2+Mode2+delay5.txt', 'r', encoding='utf-8') as f:
    for line in f:
        # Match the first number of the line (idx)
        m = re.match(r'^\s*(\d+)', line)
        if m:
            idx_list.append(int(m.group(1)))

if not idx_list:
    print('No valid idx value was found in the file!')
    exit()

# 2. Calculate adjacent differences
gaps = [idx_list[i+1] - idx_list[i] for i in range(len(idx_list)-1)]

# 3. Maximum number of consecutive packet losses = Maximum difference - 1
max_lost = max(gaps) - 1 if gaps else 0

print(f'Received packets: {len(idx_list)}')
print(f'Maximum consecutive packet losses: {max_lost}')
print(f'Examples of packet loss positions:')
for i, g in enumerate(gaps):
    if g > 1:
        print(f'  Between packets {idx_list[i]} and {idx_list[i+1]}, {g-1} were lost')
