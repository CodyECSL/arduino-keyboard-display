"""After `pio run -t upload`, clear RTC_CNTL_FORCE_DOWNLOAD_BOOT and reset the board.

flash_mode_enter() sets that bit. It survives esptool's reset, so a board flashed over
USB-Serial-JTAG (303a:1001) boots the ROM download mode again and the app never starts.
If the port is gone (the board already restarted into the app), this step does nothing.
"""
import os
import subprocess
import sys
import time

Import("env")  # noqa: F821  (provided by PlatformIO)

OPTION1_REG = "0x6000812C"


def clear_bit(source, target, env):
    port = env.subst("$UPLOAD_PORT")
    if not port:
        return
    time.sleep(2)  # let the post-flash reset finish
    if not os.path.exists(port):
        return
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    cmd = [sys.executable, esptool, "--chip", "esp32s3", "--port", port, "--after", "hard_reset",
           "write_mem", OPTION1_REG, "0", "0xffffffff"]
    print("Clearing FORCE_DOWNLOAD_BOOT")
    subprocess.run(cmd, check=False)


env.AddPostAction("upload", clear_bit)  # noqa: F821
