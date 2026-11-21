# Testomatic

![Testomatic downloading and writing a RenkuOS build](docs/screenshots/animation.gif)

Try RenkuOS from a USB drive. Testomatic lists the RenkuOS builds published on GitHub
(the latest is picked for you), downloads one and checks its SHA-256, or takes an image
file you already have (File ▸ Open image…, the "Image file…" item in the Build menu, or
drop an ISO on the window). Then it writes the image to the USB drive you choose and
offers to restart the computer.

The job plays out on a construction site: road signs for the steps, a crane that hoists
steel while the image downloads and pours concrete floors while it is written, and a
message board with a hazard-striped progress bar.

Native Haiku API, one source tree for Renku OS (`.hpkg`) and Linux through Hokku (AppImage).

## Building

    make            # on Renku/Haiku, or on Linux with Hokku (COSMOE_ROOT=/usr)
    make test       # portable core: release parsing, lsblk, dd progress

## How it works

| | Renku / Haiku | Linux |
|---|---|---|
| Builds | GitHub releases API of RenkuOS/Source, via `curl` | same |
| Drives | `/dev/disk/usb/*/0/raw`, not the boot disk | `lsblk`, USB disks that don't hold `/`, `/boot`, `/home`, swap |
| Writing | unmounts the drive's volumes, then `dd` | `pkexec` unmounts and runs `dd` |
| Restart | `shutdown -r` | `systemctl reboot` |
| Downloads kept in | `~/config/cache/Testomatic` | `~/.cache/cosmoe/Testomatic` |

Only the latest download is kept; starting a different build deletes the older one.

## Status

| Check | Renku/Haiku | Linux (X11) |
|---|---|---|
| Builds | type-checked against Haiku headers; not built or run yet | ran: real download of build 95, checksum verified, written to a test drive file byte for byte |
| Writing to a real USB drive | not tested | not tested (pkexec path) |
| Restart | not tested | not tested |

## Licence

MIT
