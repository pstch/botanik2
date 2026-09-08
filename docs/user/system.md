# SYSTEM: the unit itself {#user_system}

@verbinclude system.txt

The system status page is the eighth page in the console, it shows 12 fields defined in the library that describe the state of the system, and another set of 12 fields that can be set at the site-level, usually used for electrical supply status.


## Keyboard shortcuts

| key     | what it does                                               |
|---------|------------------------------------------------------------|
| `Enter` | refresh the values now, without waiting for the next sweep |

That is the only one. Both pages are readouts: they take no other key, so
everything else falls through to the console's own - see @ref user for those.

The values refresh on a timer of their own rather than every beat, which is why
there is a key to ask for one: a field read after a change would otherwise show
the old number until the sweep came round.
