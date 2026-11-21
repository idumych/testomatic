# Test plan: Testomatic

## Fixtures
- A test image: `head -c 60000000 /dev/urandom > /tmp/test.iso` (Linux), the same with
  `dd if=/dev/random of=/tmp/test.iso bs=1M count=60` on Haiku.
- A file that stands in for the USB drive: `rm -f /tmp/drive.img; touch /tmp/drive.img`.
  Start the app with `TESTOMATIC_TEST_DRIVE=/tmp/drive.img`: it lists that file as
  "Test drive (4.0 GB)" and writes it with dd directly, with no pkexec and no unmounting.
  Never point it at a real device in a test.

## Fresh state
- Linux: `rm -rf ~/.config/cosmoe/Testomatic_settings ~/.cache/cosmoe/Testomatic`
- Haiku: `rm -rf ~/config/settings/Testomatic_settings ~/config/cache/Testomatic`

## Feature checks
### write-image
Steps: start `TESTOMATIC_TEST_DRIVE=/tmp/drive.img Testomatic /tmp/test.iso`. The "Build"
field shows "test.iso (60 MB)" and the "USB drive" field shows "Test drive (4.0 GB)". Click
"Start", then "Erase and write" in the alert. Wait until the board says "Topped out!"
and the button reads "Restart system". Do not click it.
PASS when: `cmp /tmp/test.iso /tmp/drive.img && echo "IMAGE MATCHES" > dist/V/evidence/{p}-write.txt`
writes the line, and the screenshot's OCR shows "Topped out".
