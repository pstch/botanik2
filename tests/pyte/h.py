import re, keys
from helpers import shell
def test_h(spawn):
    term = shell(spawn, quiet_logs=True)
    term.run(60)                      # a fixed, small slice of uptime
    term.send(b"modbus stats" + keys.ENTER); term.settle()
    for l in term.view():
        if "Transactions" in l: print("|BEATS=60 " + l.strip())
