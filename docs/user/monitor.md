# MONITOR: watching and forcing rows {#user_monitor}

The monitor page is the sixth page, it shows a table of input, output and computed variables. It can be filtered to show only invalid or overriden variables, and it supports searching variable names, just by typing text while the page is open.
@verbinclude monitor.txt

## Keyboard shortcuts

| key                  | what it does                                      |
|----------------------|---------------------------------------------------|
| `Up`, `Ctrl+P`       | move the selection up                             |
| `Down`, `Ctrl+N`     | move the selection down                           |
| `PageUp`, `PageDown` | a screenful at a time                             |
| `End`                | the last row                                      |
| `Left`, `Right`      | cycle the view shown in the legend                |
| any text             | filter on it; the legend shows the pattern        |
| `Backspace`          | edit the filter                                   |
| `Esc`                | clear the filter                                  |
| `Alt+W`              | copy the selected cell to the kill ring           |
| `Ctrl+O`             | start forcing the selected row                    |
| `Ctrl+V`             | toggle the forced quality between good and failed |
| `Ctrl+R`             | clear the force                                   |
| `Ctrl+S`             | save the force, so it survives a reboot           |

While editing, `Enter` commits and `Esc` abandons. A force shadows the live
value: the row keeps being written by whatever drives it, and the force is what
is read until it is cleared.
