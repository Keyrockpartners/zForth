# ZGo examples

Each directory is a ZGo program (`main.zgo`) with the output it must print
(`expected.txt`) and the compiler's committed output (`main.zf`, the Forth,
and `main.json`, the manifest). Some also have `host.zf` (Linux stand-ins for
host words the program binds) and `calls.txt` (Forth lines that play the
firmware, calling exported handlers after `z-init` and `z-main`).

```
build/zgoc run examples/thermostat/main.zgo     # run as source
make examples                                    # build every example as a binary
build/examples/thermostat/thermostat -q -x -e z-init -e z-main examples/thermostat/calls.txt
build/zgoc test examples/*                       # check run and image mode
```

| Example | Shows |
| --- | --- |
| `hello` | the smallest program |
| `numbers` | every numeric type, wraparound, conversions, timestamps, float32 vs float64 |
| `control` | `for`, `if`, `switch`, `break`, `continue`, labels, `fallthrough` |
| `functions` | recursion, mutual recursion, multiple and named results |
| `arrays` | arrays, slices, `range`, `copy`, 2D arrays, a ring buffer |
| `strings` | UTF-8, comparison, runes, `dev.Format`, byte-slice views |
| `globals` | package variables, `init`, state across firmware calls |
| `handlers` | exported handlers, an exported variable, the manifest |
| `host` | binding host words with `//zf:forth` |
| `structs` | structs, methods, pointers |
| `thermostat` | a complete device script |

See `LANGUAGE.md` for the language.
