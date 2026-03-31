n = 200_000
i = 0
m = {}

while i < n:
    m[i] = i + 1
    i += 1

hits = 0
i = 0
while i < n:
    if i in m:
        hits += 1
    i += 1

s = set()
i = 0
while i < n:
    s.add(i % 1000)
    i += 1

print(hits + len(s))
