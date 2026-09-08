# MONITOR: watching and forcing rows {#user_monitor}

@verbinclude monitor.txt

The monitor page is the sixth page, it shows a table of input, output and computed variables. It can be filtered to show only invalid or overriden variables, and it supports searching variable names, just by typing text while the page is open. It can also be used to set/clear overrides on variables.


## Keyboard shortcuts

| key                  | what it does                                      |
|----------------------|---------------------------------------------------|
| `Up`, `Ctrl+P`       | move the selection up                             |
| `Down`, `Ctrl+N`     | move the selection down                           |
| `PageUp`, `PageDown` | a screenful at a time                             |
| `Home`               | the first row                                     |
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
