class User:
    def __init__(self, ident: int):
        self.id = ident


n = 80_000
i = 0

by_name = {}
while i < n:
    key = str(i)
    by_name[key] = i
    i += 1

hits = 0
i = 0
while i < n:
    key = str(i)
    if key in by_name:
        hits += 1
    i += 1

by_user = {}
users = []
i = 0
while i < n:
    u = User(i)
    users.append(u)
    by_user[u] = i + 1
    i += 1

obj_hits = 0
i = 0
while i < n:
    if users[i] in by_user:
        obj_hits += 1
    i += 1

print(hits + obj_hits)
