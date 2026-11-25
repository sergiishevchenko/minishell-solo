#include "minishell.h"

t_node	*node_new(t_parser *p, t_ntype type)
{
	t_node	*node;

	node = ft_calloc(1, sizeof(t_node));
	if (!node)
		parse_alloc_error(p);
	else
		node->type = type;
	return (node);
}

int	syntax_error(t_parser *p)
{
	if (!p->error)
		print_msg("syntax error near unexpected token `",
			token_str(p->cur), "'", NULL);
	p->error = 1;
	return (1);
}

int	parse_alloc_error(t_parser *p)
{
	if (!p->error)
		print_error("malloc", strerror(ENOMEM), NULL);
	p->error = 1;
	return (1);
}

t_node	*parse_pipeline(t_parser *p)
{
	t_node	*left;
	t_node	*node;

	left = parse_command(p);
	if (p->error || !p->cur || p->cur->type != T_PIPE)
		return (left);
	node = node_new(p, N_PIPE);
	if (!node)
		return (left);
	node->left = left;
	p->cur = p->cur->next;
	node->right = parse_pipeline(p);
	return (node);
}

t_node	*parse(t_token *tokens)
{
	t_parser	p;
	t_node		*root;

	p.cur = tokens;
	p.error = 0;
	root = parse_line(&p);
	if (!p.error && p.cur)
		syntax_error(&p);
	if (p.error)
	{
		free_ast(root);
		return (NULL);
	}
	return (root);
}
