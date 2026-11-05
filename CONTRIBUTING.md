# Contributing

## Branches

`main` is always buildable. Work happens on short-lived branches, merged through a pull request.

Branch name: `<owner>/<task>`

| Part | Rule |
|---|---|
| `owner` | Handle of the member responsible for the task, e.g. `khangnh` |
| `task` | Feature or task being implemented: 2 to 5 lowercase words, kebab-case, ASCII only |

Examples: `khangnh/coreiot-rpc`, `khangnh/rfid-card-debounce`, `khangnh/fusion-degraded-tests`.

## Commits

Format: [Conventional Commits](https://www.conventionalcommits.org/).

```
<type>(<scope>): <summary>

<body: what and why, wrapped at 72 columns>

Refs: <issue or work item>
```

- Summary: imperative mood, lowercase, no trailing period, at most 50 characters.
- Body: optional; explain the reason for the change, not the diff.
- One logical change per commit. The tree must build after every commit.
- English only, ASCII only.

Examples:

```
feat(fusion): confirm flame with smoke or heat rise
fix(rfid): ignore missed REQA while card stays in field
test(fusion): cover degraded mode without LDR
build(sim): generate wokwi flash image after link
```

## Code comments

- English, short, state the intent or constraint. Do not restate the code.
- Use `/* */` blocks; file header is one line describing the module, then details if needed.
- No decorative separators, banners or long dash lines.
- `TODO(<scope>): <action>` for planned work.

## Before opening a pull request

```
west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf"
west twister -T tests -p native_sim/native/64
```

Pushing to the remote is done by the repository owner.
