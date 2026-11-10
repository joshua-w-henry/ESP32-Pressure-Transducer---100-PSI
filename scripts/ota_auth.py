# PlatformIO pre-upload script: pass password from local untracked secrets.h
# to espota. Do not store the password in platformio.ini or in Git.
from pathlib import Path
import re

Import("env")  # PlatformIO/SCons injects this function

header = Path(env.subst("$PROJECT_DIR")) / "include" / "secrets.h"
if not header.exists():
    raise RuntimeError("Missing include/secrets.h. Copy secrets.example.h first.")

source = header.read_text(encoding="utf-8")
match = re.search(r'^\s*#define\s+OTA_PASSWORD\s+"([^"]+)"\s*$', source, re.MULTILINE)
if not match:
    raise RuntimeError("OTA_PASSWORD must be a quoted literal in secrets.h.")
password = match.group(1)
if len(password) < 8 or password == "CHANGE_ME":
    raise RuntimeError("Set a strong OTA_PASSWORD (8+ characters) in secrets.h.")

env.Append(UPLOAD_FLAGS=["--auth=" + password])
