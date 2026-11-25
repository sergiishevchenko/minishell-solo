#include "minishell.h"

static int	set_op(t_tok *type, t_tok value, int len)
{
	*type = value;
	return (len);
}

static int	op_len_logic(char *s, t_tok *type)
{
	if (s[0] == '&' && s[1] == '&')
		return (set_op(type, T_AND, 2));
	if (s[0] == '|' && s[1] == '|')
		return (set_op(type, T_OR, 2));
	if (s[0] == '(')
		return (set_op(type, T_LPAREN, 1));
	if (s[0] == ')')
		return (set_op(type, T_RPAREN, 1));
	return (0);
}

int	op_len(char *s, t_tok *type)
{
	int	len;

	len = op_len_logic(s, type);
	if (len)
		return (len);
	if (s[0] == '<' && s[1] == '<')
		return (set_op(type, T_HEREDOC, 2));
	if (s[0] == '>' && s[1] == '>')
		return (set_op(type, T_APPEND, 2));
	if (s[0] == '<')
		return (set_op(type, T_IN, 1));
	if (s[0] == '>')
		return (set_op(type, T_OUT, 1));
	if (s[0] == '|')
		return (set_op(type, T_PIPE, 1));
	return (0);
}
