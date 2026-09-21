# PARAMS: editing parameters {#user_params}

@verbinclude params.txt

The parameters page is the fifth page, it shows a table of parameters. 
They may be edited, and the change may be persisted to memory. 
The table is scrollable, it supports searching, by typing texty directly in the parameters page.


## Keyboard shortcuts

| key                  | what it does                               |
|----------------------|--------------------------------------------|
| `Up`, `Ctrl+P`       | move the selection up                      |
| `Down`, `Ctrl+N`     | move the selection down                    |
| `PageUp`, `PageDown` | a screenful at a time                      |
| `Home`               | the first row                              |
| `End`                | the last row                               |
| `Left`, `Right`      | cycle the view shown in the legend         |
| any text             | filter on it; the legend shows the pattern |
| `Backspace`          | edit the filter                            |
| `Esc`                | clear the filter                           |
| `Alt+W`              | copy the selected cell to the kill ring    |
| `Enter`              | start editing the selected parameter       |
| `Ctrl+O`             | save every unsaved edit                    |

While editing a value, `Enter` commits it and `Esc` abandons it. A committed
edit is live at once and unsaved: it is lost on the next reboot unless
`Ctrl+O` here, or `params save`, writes it to storage.

## Library parameters

TODO: fill in with parameters defined at the library level