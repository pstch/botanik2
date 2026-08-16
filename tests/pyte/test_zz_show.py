import keys
from helpers import shell

def test_show(spawn):
    term = shell(spawn, quiet_logs=True)
    term.send(b"overrides set cli_screen 1" + keys.ENTER); term.run(4)
    term.send(b"vars get cli_screen" + keys.ENTER)
    term.until(lambda: "Default" in term.view())
    print(term.snapshot())
    term.send(b"vars get grid_L1_V" + keys.ENTER)
    term.until(lambda: "Plausible" in term.view())
    print(term.snapshot())
