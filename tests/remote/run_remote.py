"""T-RUN-07: remote control over HTTP (P25b, SPEC-RUN-03).

Starts mini-daw with --remote on its own config, home folder and a free port,
sends commands as another program would, and checks the answers, the refusals
and the end. Used by ctest:

    python3 run_remote.py <mini-daw> <work folder> <folder with the test synth>

mini-daw opens a window: on Linux it needs a display (CI runs the tests under
xvfb-run); without one the test is skipped.
"""

import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

FAILURES = []


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        FAILURES.append(what)


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Client:
    def __init__(self, port):
        self.base = "http://127.0.0.1:%d" % port

    def request(self, method, path, body=None, headers=None, raw=None):
        data = None
        hdrs = dict(headers or {})
        if body is not None:
            data = json.dumps(body).encode()
            hdrs.setdefault("Content-Type", "application/json")
        elif raw is not None:
            data = raw.encode()
        req = urllib.request.Request(self.base + path, data=data, method=method, headers=hdrs)
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.status, json.loads(r.read().decode())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read().decode())

    def run(self, line, **kw):
        return self.request("POST", "/api/command", body={"line": line}, **kw)

    def state(self):
        status, body = self.request("GET", "/api/state")
        return body.get("state", {}) if status == 200 else {}


def main():
    exe, work, vst3dir = sys.argv[1:4]
    if sys.platform.startswith("linux") and not os.environ.get("DISPLAY"):
        print("SKIPPED: no display (DISPLAY is not set)")
        return 0
    shutil.rmtree(work, ignore_errors=True)
    for sub in ("home", "cfg", "takes", "project"):
        os.makedirs(os.path.join(work, sub))
    port = free_port()
    config = os.path.join(work, "cfg", "config.ini")
    with open(config, "w", encoding="utf-8") as f:
        f.write('[audio]\ninput_device = (none)\noutput_device = (none)\n'
                '[recording]\nfolder = "%s"\n[instruments]\nfolder1 = "%s"\n'
                '[remote]\nenabled = false\nport = %d\n' % (os.path.join(work, "takes"), vst3dir, port))
    project = os.path.join(work, "project")
    with open(os.path.join(project, "project.json"), "w", encoding="utf-8") as f:
        json.dump({"format": "mini-daw project", "version": 1, "sampleRate": 48000, "tracks": [
            {"number": 1, "name": "Keys", "kind": "midi", "midiInput": "", "midiChannel": 0, "midiOutput": "",
             "armed": False, "volumeDb": 0, "mute": False, "solo": False, "take": "", "takeStart": 0}]}, f)

    env = dict(os.environ, HOME=os.path.join(work, "home"), XDG_CONFIG_HOME="")
    proc = subprocess.Popen([exe, "--config", config, "--remote"], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    c = Client(port)
    try:
        # Up within 20 s.
        up = False
        for _ in range(200):
            try:
                if c.request("GET", "/api/state")[0] == 200:
                    up = True
                    break
            except (urllib.error.URLError, ConnectionError, OSError):
                pass
            time.sleep(0.1)
        check(up, "mini-daw answers on port %d" % port)
        if not up:
            return 1

        status, body = c.request("GET", "/api/commands")
        allowed = {x["name"]: x["allowed"] for x in body.get("commands", [])}
        check(status == 200 and allowed.get("export") is True and allowed.get("stay") is False,
              "commands: export allowed from this computer, stay not")

        status, body = c.run('open "%s"' % project)
        check(status == 200 and body["ok"], "open the project")
        check(c.state().get("project") == project, "the state names the project")
        status, body = c.run("add-track midi")
        check(body.get("ok") and body.get("note") == "track 2", "add-track midi -> track 2")
        check(any('Track 2 "MIDI 2" added' in l for l in body.get("log", [])), "the answer has the log line")
        status, body = c.run("expect tracks.2.kind midi")
        check(body.get("ok"), "expect tracks.2.kind midi")
        status, body = c.run("expect tracks.2.kind audio")
        check(status == 200 and body.get("ok") is False and body.get("note") == 'is "midi"', "a failed expect: ok false")
        status, body = c.run("scan")
        check(body.get("ok"), "scan")
        status, body = c.run('instrument last "mini-daw test synth"')
        check(body.get("ok") and "loaded" in body.get("note", ""), "instrument last (the track add-track made)")
        status, body = c.run("state")
        check(body.get("ok") and body.get("state", {}).get("tracks", {}).get("2", {}).get("loaded") is True,
              "state: the instrument is loaded")

        # One command at a time; the state can be read meanwhile.
        result = {}

        def slow():
            result["wait"] = c.run("wait 1.5")
        t = threading.Thread(target=slow)
        t.start()
        time.sleep(0.4)
        check(c.request("GET", "/api/state")[0] == 200, "the state while a command runs")
        status, body = c.run("echo too soon")
        check(status == 409, "a second command while one runs: 409")
        t.join()
        check(result["wait"][0] == 200 and result["wait"][1].get("ok"), "the first one ends ok")

        # What is refused.
        check(c.request("GET", "/api/state", headers={"Origin": "http://evil.example"})[0] == 403, "Origin: 403")
        check(c.request("GET", "/api/state", headers={"Host": "evil.example:%d" % port})[0] == 403, "a Host name: 403")
        check(c.request("POST", "/api/command", raw="record", headers={"Content-Type": "text/plain"})[0] == 415,
              "not JSON: 415")
        check(c.request("POST", "/api/command", raw="{", headers={"Content-Type": "application/json"})[0] == 400,
              "bad JSON: 400")
        check(c.run("play\nrecord")[0] == 400, "two lines: 400")
        check(c.run("plya")[0] == 400, "an unknown command: 400")
        check(c.run("stay")[0] == 403, "stay: 403")
        check(c.request("GET", "/nothing")[0] == 404, "another address: 404")
        check(c.state().get("recording") is False, "nothing refused was run (not recording)")

        status, body = c.run("quit")
        check(status == 200 and body.get("ok"), "quit")
        try:
            rc = proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            rc = None
        check(rc == 0, "mini-daw ended with 0 (was %s)" % rc)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        print(proc.stdout.read().decode(errors="replace"))

    # Commands from outside are the user's own: the project is remembered (unlike a script file's).
    with open(config, encoding="utf-8") as f:
        text = f.read()
    check(project in text, "the project is in config.ini as the last one")
    check("enabled = false" in text, "[remote] enabled is left as it was")
    log_path = os.path.join(work, "cfg", "mini-daw.log")
    log = open(log_path, encoding="utf-8", errors="replace").read() if os.path.exists(log_path) else ""
    check("Remote control: on, port %d" % port in log, "the log says it listened")
    check("Remote 127.0.0.1: add-track midi - ok" in log, "the log names each command")
    print("%d check(s) failed" % len(FAILURES))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
