# ![logo](https://github.com/kokseen1/Sbubby/blob/master/img/logo.png?raw=true) Sbubby

A minimal yet ambitious subtitle editor.

Sbubby is a keyboard-only subtitle editor written in C that aims to be efficient yet precise. Interaction with Sbubby is done via Vim-like keybindings which provide an efficient way of navigating and editing, through the use of operators and motions.

![demo](https://raw.githubusercontent.com/kokseen1/Sbubby/master/img/demo.gif?raw=True)

## Installation

Pre-built binaries can be found [here](https://github.com/kokseen1/Sbubby/releases), along with installation instructions.

## Usage

To create subtitles from scratch:

```
sbubby.exe <video.mp4>
```

To edit existing subtitles:

```
sbubby.exe <video.mp4> <subtitles.srt>
```

## Controls

Like Vim, Sbubby contains 2 main modes when interacting with the program: NORMAL and INSERT. NORMAL mode is used for navigating through the video and adding/deleting subtitles, while INSERT mode is used for editing text of the current subtitle in focus.

### NORMAL Mode

`SPACE` - Toggle play/pause

`ESC`/`Ctrl c` - Clear command buffer

`f` - Toggle fullscreen

#### Movement

`k` - Seek forward

`j` - Seek backward

`K` - Seek forward (small)

`J` - Seek backward (small)

`n` - Frame step forward

`N` - Frame step backward

`gg` - Seek to start

`G` - Seek to end

##### With quantifiers:

`10k` - Seek 10s forward

`5J` - Seek 0.5s backward

#### Subtitling

`a` - Add sub and enter INSERT mode

`i` - Enter INSERT mode

`I` - Enter INSERT mode at start of text

`w` - Seek to next sub

`b` - Seek to start of sub / Seek to previous sub

`h` - Set start of sub at current time

`l` - Set end of sub at current time

`e` - Seek to end of sub

`W` - Switch focus to next sub without seeking

`B` - Switch focus to previous sub without seeking

`r` - Manually reload subtitles

`dd` - Delete sub

##### With quantifiers:

`2i` - Enter INSERT mode on sub `#2`

`3w` - Seek `3` subs forward

### INSERT Mode

`Ctrl p` - Toggle play/pause

`LEFT`/`RIGHT` - Move cursor between characters

`Ctrl LEFT`/`Ctrl RIGHT` - Move cursor between words

`HOME` - Move cursor to start

`END` - Move cursor to end

`Ctrl w`/`Ctrl Backspace` - Delete word before cursor

`Ctrl Delete` - Delete word after cursor

`ESC`/`Ctrl c` - Exit INSERT mode

### Ex mode

Enter Ex mode by entering `:` in NORMAL mode. Press `ENTER` to execute commands in Ex mode.

`:w` - Save current subtitles as `<video.mp4>.srt`

`:q` - Quit without saving

`:wq` - Save current subtitles and quit

`ESC`/`Ctrl c` - Clear command buffer

## Building from source

### Prerequisites

- MinGW-w64
  - SDL2-devel
  - libmpv
- Make
- DLL search path contains:
  - `libmpv-2.dll`
  - `SDL2.dll`

### Steps

1. Clone the repo

```
git clone https://github.com/kokseen1/Sbubby
```

2. Build

```
cd Sbubby
make
make clean
```

3. Run

```
.\build\sbubby.exe <video.mp4>
```
