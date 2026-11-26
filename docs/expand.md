# Expander

The expander turns **raw word strings** from the AST into the final argument
list (and redirection paths) that builtins / `execve` actually see.

It does **not** run at parse time. The lexer keeps `$`, quotes, and `*` inside
`T_WORD` values; the parser stores those strings on `N_CMD.words` / redir
targets. Expansion happens when a command is about to run.

**Files:** `src/expand/expand.c`, `src/expand/expand_dollar.c`,
`src/expand/expand_utils.c` (+ `src/mandatory/glob.c` or `src/bonus/glob_bonus.c`)  
**Main entries:**
- `expand_args` — words → `char **argv` (commands)
- `expand_word` — one word → field list (redirs)
- `expand_heredoc_line` — `$` only, for unquoted heredoc bodies

**Types:** [`t_exp`](structs.md#t_exp). Related: [heredoc.md](heredoc.md),
[executor.md](executor.md), [ast.md](ast.md).

---

## Place in the pipeline

```text
lex → parse → AST → read_heredocs → exec_node
                                        │
                     ┌──────────────────┴──────────────────┐
                     ▼                                     ▼
              expand_args(words)                    expand_word(target)
                     │                                     │
                     ▼                                     ▼
                 sh->argv                            path for < > >>
                     │
                     ▼
              builtin / execve
```

| Call site | Function | Result |
|-----------|----------|--------|
| `exec_command` / `exec_in_child` | `expand_args` | `sh->argv` |
| `redir_path` in `redirs.c` | `expand_word` | one path, or `ambiguous redirect` |
| `heredoc_write` | `expand_heredoc_line` | one line written to the temp file |

---

## User-facing behaviour

| Topic | Behaviour |
|-------|-----------|
| Single quotes `'...'` | Everything literal; no `$`, no glob |
| Double quotes `"..."` | `$VAR` / `$?` expand; no word-split; `*` stays literal |
| Unquoted `$VAR` | Value inserted; whitespace in the value **splits** into several fields |
| `$?` | Last foreground status (`sh->status`) |
| `$1` / digit after `$` | Skipped (positional params not implemented) |
| Lone `$` or `$` + non-name | Kept as literal `$`… |
| Unquoted `*` | Marked for glob; mandatory restores `*`, bonus expands filenames |
| Empty expand | May yield empty argv / empty field (e.g. unset `$EMPTY`) |

Examples:

```sh
USER=serg  X="a b"

echo "$USER"      # one arg: serg
echo $X           # two args: a , b
echo '$USER'      # one arg: $USER
echo "*.c"        # one arg: *.c  (literal)
echo *.c          # matches in cwd (bonus) or literal * (mandatory stub)
```

---

## Working state: `t_exp`

```c
typedef struct s_exp
{
	t_shell	*sh;      /* env + $? */
	t_list	*fields;  /* finished argv fields */
	char	*buf;     /* field currently being built */
	int		active;   /* buf has content / field open */
	int		error;    /* malloc failure */
}	t_exp;
```

```text
  characters ──▶ exp_push ──▶ e.buf
                                   │ exp_end_field (space / end of word)
                                   ▼
                              e.fields ──▶ list_to_strs ──▶ argv
```

---

## `expand_args` / `expand_word`

```text
words (t_list of raw strings)
   │
   ▼ for each word
expand_word
   │  ft_bzero(t_exp) ; e.sh = sh
   │  exp_word(&e, word)     ← walk quotes / $ / chars
   │  free leftover e.buf
   │  expand_glob(&e.fields) ← mandatory: restore *; bonus: match files
   │
   ▼
fields (t_list)
   │
   ▼ ft_lstadd_back into all
list_to_strs(all) → char **
```

`expand_word` returns `0` on success and fills `*out` with the field list.
On error it clears fields and returns non-zero (caller prints / aborts).

---

## `exp_word` — one raw token

Walks the string with a simple quote state (`0`, `'`, or `"`):

```text
for each char s[i]:
  open / close quote     → toggle quote (quote chars themselves are dropped)
  '$' and quote != '\''  → exp_dollar(...)
  else                   → exp_push(c, quoted?)
then exp_end_field()
```

Important details:

- Quote characters are **removed** here (lexer left them in the string).
- Inside `'...'`, `$` is not expanded.
- Inside `"..."`, `$` expands but `exp_push` / `exp_value` use `quoted != 0`
  so there is **no** field split and `*` is not turned into `GLOB_MARK`.
- Unquoted spaces from `$` values call `exp_end_field` (word-splitting).

---

## `$` — `exp_dollar`

```text
$ ?
  │
  ├─ $?     → ft_itoa(sh->status), always "quoted" (no split)
  ├─ $0-9   → skip ($ + digit), insert nothing
  ├─ $' / $" when unquoted → eat only `$` (quote opens next)
  ├─ $NAME  → env_get; exp_value(..., quote != 0)
  └─ else   → push literal `$`
```

`exp_value` copies the env string into `e.buf`. If `quoted == 0`, each
whitespace character ends the current field and starts a new one.

---

## Glob marks

Unquoted `*` is not expanded immediately. `exp_push` replaces it with
`GLOB_MARK` (ASCII 31) so a literal `*` from `"*"` or from a variable value
inside quotes stays a real star.

| Build | `expand_glob` |
|-------|----------------|
| Mandatory (`src/mandatory/glob.c`) | `restore_marks` → put `*` back |
| Bonus (`src/bonus/glob_bonus.c`) | Match names in `.`; no match → restore `*` |

Dotfiles match only if the pattern itself starts with `.`.

---

## Heredoc lines — `expand_heredoc_line`

Used only when the heredoc delimiter was **unquoted**.

Differences from `expand_word`:

| | `expand_word` | `expand_heredoc_line` |
|--|---------------|------------------------|
| Quotes in text | Syntax (`'` / `"` strip) | Literal characters |
| Word-split | Yes (unquoted `$`) | No (always like `"..."`) |
| Glob | Yes (marks) | No (`exp_push(..., 1)`) |
| Result | `t_list` fields | Single `char *` |
| Ownership | Does not free input word | Frees input `line` |

```text
line → walk chars → on '$' call exp_dollar(..., '"')
                  → else exp_push(..., 1)
     → free(line) → return e.buf (or "" / NULL)
```

See [heredoc.md](heredoc.md) for when this runs.

---

## Worked example

### 1. Double-quoted word: `"hi $USER"`

AST word (quotes still in the string): `"hi $USER"`  
Env: `USER=serg`

`exp_word` walks the string. Quote characters open/close state but are
**not** pushed into `e.buf`.

| step | char | `quote` before | action | `e.buf` after | `active` |
|------|------|----------------|--------|---------------|----------|
| 0 | `"` | `0` | open `"` → `quote = '"'` | *(empty)* | 1 |
| 1 | `h` | `"` | `exp_push('h', quoted)` | `"h"` | 1 |
| 2 | `i` | `"` | `exp_push('i', quoted)` | `"hi"` | 1 |
| 3 | ` ` | `"` | `exp_push(' ', quoted)` — space stays (quoted) | `"hi "` | 1 |
| 4 | `$` | `"` | `exp_dollar(..., quote='"')` | — | — |
| 4a | | | name = `USER`, `env_get` → `"serg"` | | |
| 4b | | | `exp_value(..., quoted=1)` — no split | `"hi serg"` | 1 |
| 5 | `"` | `"` | close `"` → `quote = 0` | `"hi serg"` | 1 |
| end | | `0` | `exp_end_field` | → fields | |

```text
input:  "  h  i     $  U  S  E  R  "
        │  │  │  │  └──────────────┘
        │  │  │  │         exp_dollar → exp_value("serg", quoted)
        │  └─┴──┴── literal pushes (quoted=1)
        └ open / close "  (dropped)

e.buf     = "hi serg"
e.fields  = [ "hi serg" ]     ← one argv field
```

Why one field? Inside `"..."`, `exp_value` / `exp_push` run with
`quoted != 0`, so whitespace from the variable value would not split
either — and the space between `hi` and `$USER` is a normal quoted
character, not a field separator.

---

### 2. Unquoted variable with spaces: `$X`

AST word: `$X`  
Env: `X=a b` (value contains a space)

| step | char / value | action | `e.buf` | `e.fields` |
|------|--------------|--------|---------|------------|
| 0 | `$` | `exp_dollar(..., quote=0)` | | |
| 0a | | name = `X`, value = `"a b"` | | |
| 0b | `'a'` | `exp_push('a', unquoted)` | `"a"` | `[]` |
| 0c | `' '` | `ft_isspace` → `exp_end_field` | `NULL` | `[ "a" ]` |
| 0d | `'b'` | `exp_push('b', unquoted)` | `"b"` | `[ "a" ]` |
| end | | `exp_end_field` | `NULL` | `[ "a", "b" ]` |

```text
$ X  →  exp_value("a b", quoted=0)
         │
         ├─ 'a'  →  buf = "a"
         ├─ ' '  →  exp_end_field  →  fields: [ "a" ]
         └─ 'b'  →  buf = "b"
end              →  exp_end_field  →  fields: [ "a", "b" ]
```

Result: **two** argv fields (`"a"` and `"b"`). Same mechanism as shell
word-splitting on unquoted expansions.

---

### 3. Side-by-side

Same env (`USER=serg`, `X=a b`):

| AST word(s) | fields after expand | Why |
|-------------|---------------------|-----|
| `"hi $USER"` | `[ "hi serg" ]` | quoted: `$` expands, no split |
| `hi$USER` | `[ "hiserg" ]` | one token; value glued, no space in value |
| `$X` | `[ "a", "b" ]` | unquoted value → word-split on space |
| `"$X"` | `[ "a b" ]` | quoted: space stays inside one field |
| `'$USER'` | `[ "$USER" ]` | single quotes: `$` never expanded |
