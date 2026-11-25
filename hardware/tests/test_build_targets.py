"""Check board and transport selection, separate build outputs and build-only BLE targets."""
import os
from pathlib import Path
import subprocess
import tempfile
import sys

hardware = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(hardware / "tools"))
from usb_ota_image import is_ota_app, is_usb_app

flash = hardware / "flash.sh"
with tempfile.TemporaryDirectory() as directory:
    cli = Path(directory) / "arduino-cli"
    cli.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
    cli.chmod(0o755)
    env = {**os.environ, "PATH": directory + os.pathsep + os.environ["PATH"]}

    for options in ([], ["--abs"], ["--ble-hid"], ["--abs", "--ble-hid"], ["--note4"], ["--debug", "--note4"],
                    ["--name", "Lab1", "--ble-hid"], ["--ble-hid", "--note4"], ["--stopwatch"],
                    ["--a7670e"], ["--a7670e-v1"]):
        a7670e = "--a7670e" in options or "--a7670e-v1" in options
        a7670e_v1 = "--a7670e-v1" in options
        note4 = "--note4" in options
        stopwatch = "--stopwatch" in options
        ble = a7670e or note4 or stopwatch or "--ble-hid" in options
        size = "16M" if (a7670e or note4 or stopwatch) else "8M"
        psram = "qspi" if a7670e else "opi"
        result = subprocess.run(
            ["bash", str(flash), *options, "--build-only"],
            env=env, capture_output=True, text=True, check=True,
        )
        arguments = result.stdout.splitlines()
        assert arguments.count("compile") == 1 and "upload" not in arguments
        partition = "app3M_fat9M_16MB" if size == "16M" else "default_8MB"
        assert f"FlashSize={size},PSRAM={psram},PartitionScheme={partition}" in arguments[2]
        assert arguments[2].startswith("esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,")
        assert "-include " in result.stdout and "nimble_cfg.h" in result.stdout
        assert ("-DWAVESHARE_A7670E=1" in result.stdout) == a7670e
        assert ("-DZECTRIX_NOTE4=1" in result.stdout) == note4
        assert ("-DM5_STOPWATCH=1" in result.stdout) == stopwatch
        assert ("-DHID_TRANSPORT_BLE=1" in result.stdout) == ble
        assert ("-DABS_POINTER=1" in result.stdout) == ("--abs" in options or a7670e)
        output = ("build/a7670e-v1" if a7670e_v1 else "build/a7670e" if a7670e else
                  "build/note4" if note4 else "build/stopwatch" if stopwatch else
                  "build/ble-hid" if ble else "build")
        assert arguments[arguments.index("--output-dir") + 1] == output

    dual = subprocess.run(["bash", str(flash), "--dual-hid", "--abs", "--build-only"], env=env,
                          capture_output=True, text=True, check=True)
    args = dual.stdout.splitlines()
    assert "-DHID_DUAL=1" in dual.stdout and "-DHID_TRANSPORT_BLE=1" in dual.stdout and "-DABS_POINTER=1" in dual.stdout
    assert args[args.index("--output-dir") + 1] == "build/dual-hid"

    for flags in (["--note4", "--stopwatch"], ["--a7670e", "--note4"], ["--a7670e", "--stopwatch"]):
        refused = subprocess.run(["bash", str(flash), *flags, "--build-only"], env=env, capture_output=True)
        assert refused.returncode != 0 and b"different boards" in refused.stderr and not refused.stdout

image = bytearray(304)
image[0], image[3], image[12] = 0xe9, 0x3f, 9
image[32:36] = bytes.fromhex("3254cdab")
image[80:112] = b"arduino-lib-builder".ljust(32, b"\0")
assert is_usb_app(image)
image[288:304] = b"ESPA:usb-8m".ljust(16, b"\0")
assert is_usb_app(image)
for target in (b"ESPA:ble-8m", b"ESPA:note4-ble", b"ESPA:dual-8m", b"ESPA:stopwatch", b"ESPA:a7670e", b"ESPA:unknown"):
    image[288:304] = target.ljust(16, b"\0")
    assert not is_usb_app(image)
print("Build target isolation, flash safeguards and OTA image checks passed")

if "--built-images" in sys.argv:
    for directory, target in (("", b"ESPA:usb-8m"), ("ble-hid", b"ESPA:ble-8m"), ("note4", b"ESPA:note4-ble"),
                              ("a7670e", b"ESPA:a7670e"), ("a7670e-v1", b"ESPA:a7670e")):
        path = hardware / "build" / directory / "hid_bridge.ino.bin"
        if path.exists():
            data = path.read_bytes()
            assert data[288:304] == target.ljust(16, b"\0"), path
            assert is_usb_app(data) == (directory == ""), path
    print("Target marker and USB OTA checks passed across built images")
