Import("env")
import hashlib
import os
import shlex

# Load .env file and inject values as build flags
STRING_DEFINES = {"WIFI_SSID", "WIFI_PASSWORD", "MDNS_HOSTNAME"}
defines = {}
env_file = os.path.join(env.subst("$PROJECT_DIR"), ".env")
if os.path.isfile(env_file):
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, _, value = line.partition("=")
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            if key and value:
                defines[key] = value
                if key == "OTA_PASSWORD":
                    digest = hashlib.md5(value.encode()).hexdigest()
                    env.Append(CPPDEFINES=[("OTA_PASSWORD_HASH", env.StringifyMacro(digest))])
                    print(f"  .env: {key} = ****")
                    continue
                define_value = env.StringifyMacro(value) if key in STRING_DEFINES else value
                env.Append(CPPDEFINES=[(key, define_value)])
                print(f"  .env: {key} = ****")

if env.subst("$PIOENV").endswith("_ota"):
    ota_port = defines.get("OTA_PORT", "3232")
    upload_flags = [f"--port={ota_port}"]
    if defines.get("OTA_PASSWORD"):
        upload_flags.append(f"--auth={shlex.quote(defines['OTA_PASSWORD'])}")
    env.Replace(UPLOAD_FLAGS=upload_flags)
