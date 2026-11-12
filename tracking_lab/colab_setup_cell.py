"""Colab setup cell - NGROK version (stable; replaces the cloudflared cell).

Paste this cell into your Colab notebook (T4/L4 GPU runtime) and run it.
It installs SSH, sets a fresh random root password, configures ngrok and
prints a tcp address like `0.tcp.eu.ngrok.io:12345`.

The ngrok token is NOT stored in this file (it used to be, in plaintext).
Either
  * add a Colab secret named NGROK_TOKEN (key icon in the left sidebar, turn
    on "Notebook access"), or
  * just run the cell and paste the token at the prompt.
Locally the token lives in tracking_lab/ngrok_token.txt, which is gitignored.

Do not write the printed address or password into notes or docs: anyone who
has both gets a root shell on the runtime. Keep the tab open during training.
"""
import getpass

try:
    from google.colab import userdata
    NGROK_TOKEN = userdata.get("NGROK_TOKEN")
except Exception:  # not on Colab, secret missing, or notebook access is off
    NGROK_TOKEN = None
if not NGROK_TOKEN:
    NGROK_TOKEN = getpass.getpass("ngrok authtoken: ").strip()
if not NGROK_TOKEN:
    raise SystemExit("No ngrok token given - nothing was started.")

import random
import string
import subprocess

pw = "".join(random.choices(string.ascii_letters + string.digits, k=16))
subprocess.run("apt-get update -qq && apt-get install -qq -y openssh-server "
               "openssh-client -y", shell=True, capture_output=True)
subprocess.run(f'echo "root:{pw}" | chpasswd', shell=True)
subprocess.run("mkdir -p /var/run/sshd", shell=True)
subprocess.run("sed -i 's/#\\?PermitRootLogin.*/PermitRootLogin yes/' "
               "/etc/ssh/sshd_config", shell=True)
subprocess.run("service ssh start", shell=True)
# ngrok
subprocess.run("wget -q -nc https://bin.equinox.io/c/bNyj1mQVY4c/"
               "ngrok-v3-stable-linux-amd64.tgz", shell=True)
subprocess.run("tar xzf ngrok-v3-stable-linux-amd64.tgz -C /usr/local/bin",
               shell=True, capture_output=True)
subprocess.run(f"/usr/local/bin/ngrok config add-authtoken {NGROK_TOKEN}",
               shell=True, capture_output=True)
# launch ngrok TCP tunnel on port 22 in background
subprocess.Popen("/usr/local/bin/ngrok tcp 22 --log stdout --log-format json "
                 "> /tmp/ngrok.log 2>&1", shell=True)

print("ROOT PASSWORD (save it):", pw)
import time, json, urllib.request
addr = None
for _ in range(30):
    time.sleep(2)
    try:
        with urllib.request.urlopen("http://127.0.0.1:4040/api/tunnels") as r:
            tunnels = json.loads(r.read())["tunnels"]
        for t in tunnels:
            addr = t.get("public_url", "")      # e.g. tcp://0.tcp.eu.ngrok.io:12345
            if addr:
                break
        if addr:
            break
    except Exception:
        pass
print("SSH ADDRESS (give to Roo):", addr or "NOT READY - rerun this cell")
print("(keep this notebook tab open during training)")
