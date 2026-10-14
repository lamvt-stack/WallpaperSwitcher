# Security policy

Please report suspected vulnerabilities privately (GitHub "Report a vulnerability" under the Security tab) instead of
opening a public issue. Include the Windows version, steps to reproduce, and any sample file involved.

Areas that handle untrusted input: image headers and metadata (EXIF / XMP) read through WIC, file names in the
selected folder, and the registry values under `HKCU\Software\WallpaperSwitcher`.
