# Lexer

The lexer turns one input **line** (`char *`) into a flat linked list of **tokens** (`t_token *`).

It does **not** decide grammar (what may follow what). It only **cuts** the string into pieces: words and operators. Building structure (`left | right`, `&&`, subshells) is the [parser](parser.md)’s job. Tree overview: [ast.md](ast.md). Types: [structs.md](structs.md).

**Entry point:** `lex(char *s, t_token **tokens)` in `src/lexer/lexer.c`  
**Caller:** `process_line` in `src/shell/main.c`

```c
if (lex(sh->line, &tokens) != 0)
{
	sh->status = 2;
	return ;
}
```

---

## Place in the pipeline

```text
┌────────────┐    ┌────────────┐    ┌────────────┐
│  sh->line  │───▶│    lex     │───▶│   parse    │───▶ AST
│  char *    │    │  t_token*  │    │  t_node*   │
└────────────┘    └────────────┘    └────────────┘
```

| Responsibility | Lexer | Parser |
|----------------|-------|--------|
| Skip spaces | yes | no (already gone) |
| Recognize `\|` `>>` `&&` … | yes (`T_*`) | consumes them into `N_*` |
| Quotes | keep inside the word string | does not interpret |
| `$VAR` / glob | leave as text | expand later at exec |
| Syntax validity | only unclosed quotes / malloc | unexpected tokens, empty cmds |

---

## What a token is

```c
typedef struct s_token
{
	t_tok			type;   /* T_WORD, T_PIPE, … */
	char			*value; /* heap text for T_WORD; NULL for operators */
	struct s_token	*next;
}	t_token;
```

```text
head
 │
 ▼
┌──────────┬──────┐    ┌──────────┬──────┐    ┌──────────┬──────┐
│ T_WORD   │ next │───▶│ T_PIPE   │ next │───▶│ T_WORD   │ next │──▶ NULL
│ "ls"     │      │    │ value=∅  │      │    │ "wc"     │      │
└──────────┴──────┘    └──────────┴──────┘    └──────────┴──────┘
```

### `t_tok` kinds

| Kind | Lexeme | `value` | Notes |
|------|--------|---------|-------|
| `T_WORD` | any non-operator chunk | copied substring | may contain `'…'` / `"…"` |
| `T_PIPE` | `\|` | `NULL` | becomes `N_PIPE` later |
| `T_IN` | `<` | `NULL` | |
| `T_OUT` | `>` | `NULL` | |
| `T_APPEND` | `>>` | `NULL` | |
| `T_HEREDOC` | `<<` | `NULL` | |
| `T_AND` | `&&` | `NULL` | bonus |
| `T_OR` | `\|\|` | `NULL` | bonus |
| `T_LPAREN` | `(` | `NULL` | bonus |
| `T_RPAREN` | `)` | `NULL` | bonus |

**T_ vs N_:** `T_PIPE` is a **lexeme in a list**. `N_PIPE` is an **AST node** with two children. The lexer never creates `N_*`.

---

## `lex` — walk the whole line

```c
int	lex(char *s, t_token **tokens)
{
	*tokens = NULL;
	i = 0;
	while (s[i])
	{
		len = 1;      /* default: one char (used for spaces) */
		ret = 0;
		quote = 0;
		if (!ft_isspace(s[i]))
			ret = lex_step(&s[i], tokens, &len, &quote);
		if (ret != 0)
			return (lex_error(tokens, quote));
		i += len;
	}
	return (0);
}
```

| Variable | Role |
|----------|------|
| `s` | full input line |
| `i` | index of the next character to consider |
| `&s[i]` | pointer at that character (same as `s + i`) |
| `len` | how far to advance `i` after this iteration |
| `ret` | `0` ok, nonzero = error from `lex_step` |
| `quote` | if unclosed quote, which one (`'` or `"`); else `0` |

Whitespace: no token is created; `len` stays `1`, so `i` moves one character.

Empty / all-space line: loop never adds tokens → `*tokens == NULL`, return `0`. Caller treats that as “do nothing”.

---

## `lex_step` — one token

Called only on a non-space character.

```text
lex_step(s, tokens, &len, &quote)
  │
  ├─ op_len(s, &type)  ──▶ length > 0?
  │         yes → token_add(operator), set *len, return
  │
  └─ word_len(s, &quote)
            │
            ├─ < 0  → unclosed quote → return 1
            └─ else → ft_substr + token_add(T_WORD), *len = word length
```

### Full trace: `"ls | wc"`

| `i` | `s[i]` | path | `len` after | tokens |
|-----|--------|------|-------------|--------|
| 0 | `l` | word `"ls"` | 2 | `[WORD "ls"]` |
| 2 | ` ` | space (no `lex_step`) | 1 | same |
| 3 | `\|` | `op_len` → `T_PIPE` | 1 | `… → [PIPE]` |
| 4 | ` ` | space | 1 | same |
| 5 | `w` | word `"wc"` | 2 | `… → [WORD "wc"]` |
| 7 | `\0` | stop | — | return `0` |

### Trace: `"echo 'a|b'"`

One word, because `|` is inside quotes:

| step | detail |
|------|--------|
| `op_len` at `e` | 0 |
| `word_len` | scans `echo`, then `'…'`, inside quotes skips until `'`, includes `a|b` |
| result | one `T_WORD` with value `echo 'a|b'` (depending on spaces — here `"echo"` then separately if space… ) |

For `"echo 'a|b'"` with a space after `echo`:

1. word `echo`
2. space
3. word `'a|b'` (length 5 including quotes)

The `|` never becomes `T_PIPE`.

---

## Operators: `op_len`

Returns how many characters the operator occupies, and writes the kind into `*type`. Returns `0` if the current character is not an operator start.

**Order matters:** longer forms first.

| Check order | Match | Type | Length |
|-------------|-------|------|--------|
| 1 (bonus) | `&&` | `T_AND` | 2 |
| 2 (bonus) | `\|\|` | `T_OR` | 2 |
| 3 (bonus) | `(` | `T_LPAREN` | 1 |
| 4 (bonus) | `)` | `T_RPAREN` | 1 |
| 5 | `<<` | `T_HEREDOC` | 2 |
| 6 | `>>` | `T_APPEND` | 2 |
| 7 | `<` | `T_IN` | 1 |
| 8 | `>` | `T_OUT` | 1 |
| 9 | `\|` | `T_PIPE` | 1 |

If `<` were tested before `<<`, then `<<EOF` would become two `T_IN` tokens.

Mandatory implementation: `src/mandatory/lexer_ops.c`  
Bonus: `src/bonus/lexer_ops_bonus.c` (linked with `make bonus`)

Helper `set_op(type, value, len)` assigns `*type = value` and returns `len`.

---

## Words: `word_len`

```c
while (s[i] && !ft_isspace(s[i]) && op_len(s + i, &type) == 0)
{
	if (s[i] == '\'' || s[i] == '"')
	{
		quote = s[i];
		i++;
		while (s[i] && s[i] != quote)
			i++;
		if (!s[i])
		{
			*unclosed = quote;
			return (-1);
		}
	}
	i++;
}
return (i);
```

### Outer loop stop conditions

Stop before a character that is:

- `\0`, or
- whitespace, or
- the start of an operator (`|`, `>`, `&&`, …).

### Quote handling

1. See `'` or `"` → remember it in local `quote`.
2. Advance past the opener.
3. Scan until the **same** closing quote (the other quote type is literal inside).
4. If end of string with no closer → write `*unclosed`, return `-1`.
5. After the closer, the outer `i++` moves past it.

Quotes are **not** stripped here. The substring still contains `'` / `"` characters; expand removes them later.

### Examples

| Input at cursor | Return | Notes |
|-----------------|--------|-------|
| `ls|wc` | `2` | stops before `|` |
| `'hello world'` | `13` | spaces inside quotes count |
| `"x'y"` | `4` | single quote inside doubles is literal |
| `hi'` | `-1` | `*unclosed = '\''` |

---

## `token_add` — append to the list

1. Reject `T_WORD` with `value == NULL` (failed `ft_substr`).
2. `malloc` a `t_token`; on failure `free(value)` and return `1`.
3. Fill `type`, `value`, `next = NULL`.
4. If list empty → `*head = token`.
5. Else walk to the last node (`while (cur->next)`) and set `cur->next = token`.

Operators always pass `value = NULL`.

---

## Errors: `lex_error`

Always:

1. `free_tokens(*tokens)`
2. `*tokens = NULL`

Then:

| `quote` | Meaning | Message |
|---------|---------|---------|
| `0` | typically malloc | `minishell: malloc: Cannot allocate memory` |
| `'` or `"` | unclosed quote | `unexpected EOF while looking for matching \`'\`` (or `"`) |

Return value of `lex` is `1`. `process_line` sets `sh->status = 2`.

---

## Helpers in `lexer_utils.c`

### `free_tokens`

Walk the list; for each node `free(value)` then `free(token)`. Safe on `NULL`.

### `token_str`

Used by the **parser** for error messages (not by `lex` itself):

| Token | String |
|-------|--------|
| `NULL` | `"newline"` |
| `T_WORD` | `token->value` |
| `T_PIPE` | `"\|"` |
| … | matching lexeme |

---

## What the lexer deliberately does *not* do

- Does not check that `|` has a command on both sides (parser / syntax).
- Does not expand `$HOME` or remove quotes.
- Does not open heredoc files (heredoc stage after parse).
- Does not distinguish “command name” vs “argument” — everything non-operator is `T_WORD`.

---

## Files

| File | Role |
|------|------|
| `src/lexer/lexer.c` | `lex`, `lex_step`, `word_len`, `token_add`, `lex_error` |
| `src/lexer/lexer_utils.c` | `free_tokens`, `token_str` |
| `src/mandatory/lexer_ops.c` | `op_len` mandatory operators |
| `src/bonus/lexer_ops_bonus.c` | `op_len` + `&&` `\|\|` `()` |

Next stage: [parser.md](parser.md).
