#include "minishell.h"

void	free_tokens(t_token *token)
{
	t_token	*next;

	while (token)
	{
		next = token->next;
		free(token->value);
		free(token);
		token = next;
	}
}

char	*token_str(t_token *token)
{
	if (!token)
		return ("newline");
	if (token->type == T_WORD)
		return (token->value);
	if (token->type == T_PIPE)
		return ("|");
	if (token->type == T_IN)
		return ("<");
	if (token->type == T_OUT)
		return (">");
	if (token->type == T_APPEND)
		return (">>");
	if (token->type == T_HEREDOC)
		return ("<<");
	if (token->type == T_AND)
		return ("&&");
	if (token->type == T_OR)
		return ("||");
	if (token->type == T_LPAREN)
		return ("(");
	return (")");
}
