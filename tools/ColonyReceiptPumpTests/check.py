"""Run default, explicit enabled, opt-out and a disabled negative control."""
import os
from pathlib import Path
import subprocess
import sys

project = Path(__file__).resolve().parent
command = ["dotnet", "run", "--project", str(project), "-c", "Release", "-p:ReceiptPumpPython=" + sys.executable]
environment = os.environ.copy()
environment.pop("MA_COLONY_BYPRODUCT_PUMP", None)
environment.pop("MA_COLONY_BYPRODUCT_PUMP_LOG", None)
environment.pop("MA_TEST_RECEIPT_PUMP_DISABLED", None)
subprocess.run(command, env=environment, check=True)
command += ["--no-build"]
environment["MA_COLONY_BYPRODUCT_PUMP"] = "1"
environment["MA_COLONY_BYPRODUCT_PUMP_LOG"] = "1"
subprocess.run(command, env=environment, check=True)
environment["MA_COLONY_BYPRODUCT_PUMP"] = "0"
negative = subprocess.run(command, env=environment, text=True, capture_output=True)
if negative.returncode == 0 or "idle2ms prefix differs" not in negative.stderr:
    raise RuntimeError("Disabled negative control did not reject the enabled-prefix oracle:\n" + negative.stdout + negative.stderr)
print("PASS disabled negative control rejects the enabled idle-prefix oracle", flush=True)
subprocess.run(command + ["--", "--disabled"], env=environment, check=True)
