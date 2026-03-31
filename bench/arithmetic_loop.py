n = 2_000_000
i = 0
total = 0

while i < n:
    total = total + (i * 3) - (i % 7)
    i += 1

print(total)
