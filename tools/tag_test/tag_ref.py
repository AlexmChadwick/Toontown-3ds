#!/usr/bin/env python3

class TagSim:
    def __init__(self, it_id):
        self.it_id = it_id
        self.tag_back_timer = 0.0
        self.score = 0

    def step(self, dt):
        if self.tag_back_timer > 0:
            self.tag_back_timer = max(0.0, self.tag_back_timer - dt)

    def attempt_tag(self, tagger, target):
        if tagger != self.it_id:
            return False
        if self.tag_back_timer > 0:
            return False
        if tagger == target:
            return False
        self.it_id = target
        self.tag_back_timer = 2.0
        return True

    def grab_treasure(self):
        self.score += 2
        return self.score

def main():
    sim = TagSim(1001)
    print(f"init: it={sim.it_id}")
    # Tag 1002
    t1 = sim.attempt_tag(1001, 1002)
    print(f"tag(1001->1002): ok={t1} it={sim.it_id} cd={sim.tag_back_timer:.1f}")
    # Immediate tagback rejected
    t2 = sim.attempt_tag(1002, 1001)
    print(f"tag(1002->1001 immediate): ok={t2} it={sim.it_id}")
    # Advance time 1.0s (still rejected)
    sim.step(1.0)
    t3 = sim.attempt_tag(1002, 1001)
    print(f"tag(1002->1001 after 1s): ok={t3} it={sim.it_id}")
    # Advance time 1.5s (now accepted)
    sim.step(1.5)
    t4 = sim.attempt_tag(1002, 1001)
    print(f"tag(1002->1001 after 2.5s): ok={t4} it={sim.it_id}")
    # Grab treasures
    s1 = sim.grab_treasure()
    s2 = sim.grab_treasure()
    print(f"treasures: score={s2}")

if __name__ == '__main__':
    main()
