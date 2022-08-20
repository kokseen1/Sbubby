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

`ESC` - Clear command buffer

#### Basic movement

`j` - Seek backward

`J` - Seek backward (small)

`k` - Seek forward

`K` - Seek forward (small)

`w` - Seek to next sub

`b` - Seek to previous sub

##### With quantifiers:

`10k` - Seek 10s forward

`5J` - Seek 0.5s backward

#### Subtitling

`a` - Add sub and enter INSERT mode

`i` - Enter INSERT mode

`h` - Set start of sub at current time

`l` - Set end of sub at current time

`o` - Seek to start of sub

`O` - Seek to end of sub

`dd` - Delete sub

##### With quantifiers:

`2o` - Seek to 2s before start of sub

`2O` - Seek to 2s before end of sub

`2i` - Enter INSERT mode on sub `#2`

### INSERT Mode

`Ctrl` + `w`/`BACKSPACE` - Delete last word

`ESC` - Exit INSERT mode

### Ex mode

Enter Ex mode by entering `:` in NORMAL mode. Press the enter key to execute commands in Ex mode.

`:{pos}` - Seek to `pos` seconds of the video; `:100` seeks to `00:01:40.000`, etc.

`:w` - Save the current subtitle as `<video.mp4>.srt`

`:q` - Quit without saving

`:wq` - Save the current subtitle and quit

### Additional controls

#### Movement

`s` - Frame step forward

`S` - Frame step backward

`gg` - Seek to start

`W` - Switch focus to next sub without seeking

`B` - Switch focus to previous sub without seeking

#### Subtitling

`Hj` - Shift start of sub 0.1s backward

`Hk` - Shift start of sub 0.1s forward

`Lj` - Shift end of sub 0.1s backward

`Lk` - Shift end of sub 0.1s forward

##### With quantifiers:

`3Hj` - Shift start of sub 0.3s backward

`2Lk` - Shift end of sub 0.2s forward

## Building from source

### Prerequisites

- MinGW-w64
- Make
- DLL search path contains binaries found in `external/bin`

### Steps

1. Clone the repo

```
git clone https://github.com/kokseen1/Sbubby
```

2. Build

```
make
make clean
```

3. Run

```
sbubby.exe
```
