# Contributing

Thanks for helping with `sqflite_sqlcipher_aurora`!

## Commit messages

This repository uses [Conventional Commits](https://www.conventionalcommits.org/).
Every commit message must follow the format:

```
<type>(<optional scope>): <short summary in imperative mood>

<optional body: what changed and why, wrapped at 72 columns>

<optional footer: footers like "Refs #123" or "BREAKING CHANGE: ...">
```

### Types

| Type             | Use for                                                    |
| ---------------- | ---------------------------------------------------------- |
| `feat`           | A new feature (bumps MINOR version)                        |
| `fix`            | A bug fix (bumps PATCH version)                            |
| `docs`           | Documentation only                                         |
| `refactor`       | A change that neither fixes a bug nor adds a feature       |
| `perf`           | A performance improvement                                  |
| `test`           | Adding or correcting tests                                 |
| `build`          | Build system, prebuilt binaries, packaging (spec/CMake)    |
| `ci`             | CI configuration only                                      |
| `chore`          | Maintenance that doesn't fit elsewhere                     |
| `revert`         | Reverting a previous commit                                |

### Scopes (optional)

Useful scopes: `aurora` (native layer), `example`, `prebuilt`, `dart`.

### Examples

```
feat: enable encryption via the password open option

fix(plugin): handle missing database file in openDatabase
build(prebuilt): rebuild libsqlcipher 4.6.1 against OpenSSL 1.1.1w
docs: describe the RPM build in the README
```

### Rules

- The summary is in the imperative mood ("add", not "added"/"adds"),
  no trailing period, at most ~72 characters.
- `feat` and `fix` are the types that affect the changelog/version; use
  `BREAKING CHANGE:` in the footer when an API changes incompatibly.
- Keep the body to the *why*, not the *what* — the diff shows the what.

## Pull requests

- Keep PRs focused; one logical change per PR.
- Make sure the example app still builds:
  `flutter build aurora --release` from `example/`.
- If you touch `aurora/prebuilt/`, include the rebuild steps in the PR
  description (see `aurora/prebuilt/BUILD.md`).
