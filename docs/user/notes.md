# NOTES: the notepad {#user_notes}

<!-- source of truth: pages/notes.h; a full-screen editor over one stored note -->

The notes page is the ninth page of the console, it provides a persistent scratch buffer that can be used to store notes about the deployment.

@verbinclude notes.txt

## Keyboard shortcuts

| key                    | what it does                                   |
|------------------------|------------------------------------------------|
| `Ctrl+O`               | save the note                                  |
| `Enter`, `Shift+Enter` | a new line                                     |
| `Backspace`            | delete backwards                               |
| `Delete`, `Ctrl+D`     | delete forwards                                |
| `Left`, `Ctrl+B`       | back one character                             |
| `Right`, `Ctrl+F`      | forward one character                          |
| `Up`, `Ctrl+P`         | up one line                                    |
| `Down`, `Ctrl+N`       | down one line                                  |
| `PageUp`, `PageDown`   | a screenful at a time                          |
| `Ctrl+A`               | the start of the line                          |
| `End`, `Ctrl+E`        | the end of the line                            |
| `Alt+B`, `Alt+F`       | back or forward one word                       |
| `Ctrl+Space`           | set the mark                                   |
| `Ctrl+G`               | clear the mark                                 |
| `Ctrl+W`               | kill the region, or the word behind the cursor |
| `Alt+W`                | copy the region                                |
| `Ctrl+K`               | kill to the end of the line                    |
| `Ctrl+Y`               | yank the last kill back                        |

The kill ring is the console's, not this page's: a line cut here yanks at the
shell prompt, and the other way about. The footer shows how much of the note is
used, and whether it is saved.
