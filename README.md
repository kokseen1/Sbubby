# <img src="https://i.imgur.com/LiCHcgF.png" width="100"> Sbubby
A minimal yet ambitious subtitle editor.

Sbubby is a keyboard-only subtitle editor written in C, which aims to be efficient yet precise. Interaction with Sbubby is done via Vim-like keybindings which provide an efficient way of navigating and editing, through the use of operators and motions.

## Installation

Pre-built binaries can be found [here](https://github.com/kokseen1/Sbubby/releases), along with installation instructions.

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
