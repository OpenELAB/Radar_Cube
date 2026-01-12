# find_max_lost.py
import re

# 1. 把所有 idx 提出来（按出现顺序）
idx_list = []
with open('./test_data/MAMP2+Mode2+delay5.txt', 'r', encoding='utf-8') as f:
    for line in f:
        # 匹配行首数字（idx）
        m = re.match(r'^\s*(\d+)', line)
        if m:
            idx_list.append(int(m.group(1)))

if not idx_list:
    print('文件里没有找到有效 idx！')
    exit()

# 2. 计算相邻差值
gaps = [idx_list[i+1] - idx_list[i] for i in range(len(idx_list)-1)]

# 3. 最大连续丢包数 = 最大差值 - 1
max_lost = max(gaps) - 1 if gaps else 0

print(f'接收到的包数：{len(idx_list)}')
print(f'最大连续丢包数：{max_lost}')
print(f'丢包位置示例：')
for i, g in enumerate(gaps):
    if g > 1:
        print(f'  包 {idx_list[i]} → {idx_list[i+1]} 之间丢了 {g-1} 个')