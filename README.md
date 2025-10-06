# cli-modplayer

A console MOD tracker player for Linux inspired by tools like ProTracker. The app streams audio through PortAudio, loads songs with libopenmpt, and shows a live channel grid in the terminal using FTXUI.
[Watch the showcase video](https://www.youtube.com/watch?v=P0_rXYO8r1w)
## Features

- Plays the tracker formats handled by libopenmpt (`.mod`, `.xm`, `.s3m`, `.it`, ...).
- Color grid with vertical channel panels that fall like in trackers, fitting the visible channels, paging through the rest, listing the next rows, and fading notes for earlier and upcoming beats.
- Live VU bars above every column to display channel power with peak markers that decay over time.
- Keyboard shortcuts for pause, order jumps, and fast row scrubbing.
- Toggle overlay with tracker info, counts of instruments and samples, and a slice of the module message.
- Gentle status strip that confirms actions without covering the pattern view.
- Runs entirely inside the terminal.

<img src="https://i.imgur.com/lp31HsJ.png">

## Install
cli-modplayer is available on [AUR](https://aur.archlinux.org/packages/cli-modplayer). You can install it using this command:
```sh
yay -S cli-modplayer
```
or
```sh
paru -S cli-modplayer
```
For other Linux distributions you should build it yourself. Maybe later I will add it somewhere else, who knows

## Prerequisites

Install packages with your distro manager. Example for Debian and Ubuntu systems:

```sh
sudo apt install g++ cmake libopenmpt-dev portaudio19-dev libncursesw5-dev
```

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/cli-modtracker /path/to/song.mod
```
New thing: Simple mode
```sh
./build/cli-modtracker /path/to/song.mod --simple
```
It has no pattern view, only metadata, seekbar and some key bindings for comfortable use.

Or you can download one of the prebuilt binaries in the "Releases"
Or download one from GitHub workflow artifacts.

Key bindings inside the UI:

- `Space` — pause or resume playback
- `←` / `→` (or `h` / `l`) — jump to the previous or next order
- `[` / `]` — move backward or forward by 8 rows inside the order list
- `PgUp` / `PgDn` (or `u` / `d`) — page through channel columns when the module has more than four channels
- `N` — show or hide the info overlay
- `Q` — quit the program


### TODO
add crossplatform support (windows and mac)
uhh idk
