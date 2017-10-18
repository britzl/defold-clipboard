# Defold Clipboard
[Defold](https://www.defold.com) native extension to access the clipboard.

## System requirements
The extension currently supports OSX, Windows, iOS and Android. Linux support will be added once Defold native extensions are supported on that OS.

## Limitations
The extension will not support HTML5 as clipboard access is restricted in browsers due to security reasons.

## Installation
You can use the Clipboard extension in your own project by adding this project as a [Defold library dependency](http://www.defold.com/manuals/libraries/). Open your game.project file and in the dependencies field under project add:

https://github.com/britzl/defold-clipboard/archive/master.zip

Or point to the ZIP file of a [specific release](https://github.com/britzl/defold-clipboard/releases).

## Usage

### clipboard.copy(text)
Copy text to the clipboard.

**PARAMETERS**
* ```text``` (string)

### clipboard.paste()
Get text from the clipboard.

**RETURNS**
* ```text``` (string)
