# TASKS: the task table {#user_tasks}

The tasks page is the seventh page, it shows the list of components and programs registered in the firmware's scheduler. 
It supports search like other pages, and can be filtered to show only faulted or healthy tasks. For each task, we show :
 - the task name
 - its period
 - its health
 - its phase
 - the mean runtime, in microseconds
 - the amount of missed activations
 
@verbinclude tasks.txt

## Keyboard shortcuts

| key                  | what it does                               |
|----------------------|--------------------------------------------|
| `Up`, `Ctrl+P`       | move the selection up                      |
| `Down`, `Ctrl+N`     | move the selection down                    |
| `PageUp`, `PageDown` | a screenful at a time                      |
| `End`                | the last row                               |
| `Left`, `Right`      | cycle the view shown in the legend         |
| any text             | filter on it; the legend shows the pattern |
| `Backspace`          | edit the filter                            |
| `Esc`                | clear the filter                           |
| `Alt+W`              | copy the selected cell to the kill ring    |

The page has no verbs of its own: it reports. `Left` and `Right` cycle the
health filter between FAULTED, ALL and HEALTHY.
