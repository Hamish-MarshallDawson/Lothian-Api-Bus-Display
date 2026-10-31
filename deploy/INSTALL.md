# Installing the bus display service

## 1. Settings file

`~/.config/bustracker/env` must contain at least:

    BUSTRACKER_KEY=<subscription key>
    BUS_ADDRESS=<home address>

Keep it private: `chmod 600 ~/.config/bustracker/env`. The service reads it
through `EnvironmentFile=`, and the key is never printed by the program.
See `src/config.example.env` for the other variables.

## 2. Build and install the program

    cd ~/agents/<build folder>/src              # or a fresh checkout
    make && make test                             # the tests need no network
    sudo install -m 755 busTrack /usr/local/bin/busTrack

Snapshot mode is the safe check before the service starts:

    set -a; . ~/.config/bustracker/env; set +a
    busTrack --fetch <atcocode>                   # one live board, prints status

## 3. Install the unit

    sudo install -m 644 deploy/bustracker.service /etc/systemd/system/bustracker.service
    sudo systemctl daemon-reload
    sudo systemctl enable --now bustracker.service

Check it:

    systemctl status bustracker.service
    journalctl -u bustracker.service -n 100 --no-pager

Stop it: `sudo systemctl disable --now bustracker.service`.

## Notes

- The unit sets `User=hamis`. If the display needs a group (for example `gpio`
  for wiringPi), add the user to it with `sudo usermod -aG gpio hamis`, then
  log in again. This is not done automatically.
- `Restart=on-failure` restarts after a crash, after 10 s. The start limit stops
  it looping if it fails 5 times within 5 minutes.
- The service uses the system's local time zone. Check it with `timedatectl`.
- Snapshot and `--fetch` modes do not touch the display, so they are safe to run
  while the service is stopped.
