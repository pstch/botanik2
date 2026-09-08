# HOME: the overview screen {#user_home}

@verbinclude home.txt

The home page is the first page of the console, and it's the page visible by default when the unit starts. It shows :
 - a logo, the name of the site, and the current code version.
 - a set of fields that can be defined at the site-level
 - a small region with the last log lines


## Keyboard shortcuts

| key     | what it does                                               |
|---------|------------------------------------------------------------|
| `Enter` | refresh the values now, without waiting for the next sweep |

That is the only one. Both pages are readouts: they take no other key, so
everything else falls through to the console's own - see @ref user for those.

The values refresh on a timer of their own rather than every beat, which is why
there is a key to ask for one: a field read after a change would otherwise show
the old number until the sweep came round.
