#include "minishell.h"

static int	token_add(t_token **head, t_tok type, char *value)
{
	t_token	*token;
	t_token	*cur;

	if (type == T_WORD && !value)
		return (1);
	token = malloc(sizeof(t_token));
	if (!token)
	{
		free(value);
		return (1);
	}
	token->type = type;
	token->value = value;
	token->next = NULL;
	if (!*head)
		*head = token;
	else
	{
		cur = *head;
		while (cur->next)
			cur = cur->next;
		cur->next = token;
	}
	return (0);
}

static int	word_len(char *s, char *unclosed)
{
	int		i;
	char	quote;
	t_tok	type;

	i = 0;
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
}

static int	lex_error(t_token **tokens, char quote)
{
	char	str[2];

	free_tokens(*tokens);
	*tokens = NULL;
	if (!quote)
		return (err_ret("malloc", strerror(ENOMEM), NULL, 1));
	str[0] = quote;
	str[1] = '\0';
	print_msg("unexpected EOF while looking for matching `", str, "'", NULL);
	return (1);
}

static int	lex_step(char *s, t_token **tokens, int *len, char *quote)
{
	t_tok	type;

	*len = op_len(s, &type);
	if (*len > 0)
		return (token_add(tokens, type, NULL));
	*len = word_len(s, quote);
	if (*len < 0)
		return (1);
	return (token_add(tokens, T_WORD, ft_substr(s, 0, *len)));
}

int	lex(char *s, t_token **tokens)
{
	int		i;
	int		len;
	int		ret;
	char	quote;

	*tokens = NULL;
	i = 0;
	while (s[i])
	{
		len = 1;
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
