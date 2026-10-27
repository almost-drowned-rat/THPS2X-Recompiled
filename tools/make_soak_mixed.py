"""make_soak_mixed.py <out.txt> [END RUN cycles=30] -- tests/input/soak_mixed.txt: END RUN cycles (pause -> END RUN -> YES ->
statistics -> menu on RETRY -> down -> LEVEL GOALS -> checklist -> level set up again), then timer cycles (A every 4 s).
Career Hangar, first session from the career opening. Shot every 30 s. Verified 2026-10-17 (36 set-ups in 2700 s)."""
import sys
out = sys.argv[1]
n_end = int(sys.argv[2]) if len(sys.argv) > 2 else 20
L = ["# mixed soak: %d END RUN cycles (60 s each) from 80 s, then timer cycles (A every 4 s)" % n_end,
     "3000:start:200", "6000:start:200", "9000:start:200", "12000:start:200", "15000:start:200",
     "21000:a:300", "25000:a:300", "33000:a:300", "41000:a:300", "49000:a:300",
     "@include snippets/skate_loop.txt 55000"]
t = 80000
for k in range(n_end):
    L += ["%d:start:200" % t,                      # pause
          "%d:down:200" % (t + 2500), "%d:down:200" % (t + 4000), "%d:down:200" % (t + 5500),
          "%d:a:300" % (t + 7000),                  # END RUN -> "really end?" (NO)
          "%d:up:200" % (t + 9000), "%d:a:300" % (t + 10500),   # YES -> statistics
          "%d:a:300" % (t + 16000),                 # statistics -> menu (RETRY)
          "%d:down:200" % (t + 20000),              # LEVEL GOALS
          "%d:a:300" % (t + 22000),                 # checklist
          "%d:a:300" % (t + 28000),                 # accept -> level set up again
          "@include snippets/skate_loop.txt %d" % (t + 38000)]
    t += 60000
end = 3500000
for a in range(t + 4000, end, 4000):
    L.append("%d:a:300" % a)
for s in range(30000, end, 30000):
    L.append("%d:shot=m_%04d" % (s, s // 1000))
open(out, "w", newline="\n").write("\n".join(L) + "\n")
print("END RUN cycles until %d s, timer cycles after" % (t // 1000))
