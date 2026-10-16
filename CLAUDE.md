# Project rules

## No PII or sensitive data

This repository is public. Do not put personal or sensitive data in any part of it. This includes:

- Source code, comments, and test data.
- Specs, plans, and other documents in `docs/`.
- README files and screenshots.
- Commit messages, branch names, pull request titles and descriptions, review comments, and issues.
- File metadata (image EXIF or text chunks, STL headers, build output).

Examples of data that must not appear:

- Real names (other than public GitHub usernames and credits for third-party work), email addresses, and phone numbers.
- Passwords, Wi-Fi names and passwords, API keys, tokens, and private keys.
- Local file paths that contain a username or home folder (for example `/home/<user>/...`).
- Device identifiers: MAC addresses, USB serial numbers, and serial ports with IDs.
- Private IP addresses and host names of a real network. Use placeholders such as `192.168.1.20`.
- Screenshots that show a desktop, a network name, or any private window.

Rules:

1. Use placeholders in docs and examples, for example `<MAC>`, `<user>`, and `<ssid>`.
2. Make commits with the GitHub noreply email (`<id>+<username>@users.noreply.github.com`), not a personal email.
3. Check every diff, commit message, and PR description for this data before you commit or push.
4. If you find such data, stop and tell the user. Removing it from a later commit is not enough: it stays in git history. The history must be rewritten.
