#pragma once

/** Reboot into the ROM download mode (ROM USB download mode, 303a:0009). Does not return. */
void flash_mode_enter(void);

/** Call once at boot. Logs the reset reason and clears the force-download bit left by flash_mode_enter(). */
void flash_mode_boot(void);
