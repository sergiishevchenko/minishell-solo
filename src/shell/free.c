#include "minishell.h"

static void	free_redirs(t_redir *redir)
{
	t_redir	*next;

	while (redir)
	{
		next = redir->next;
		if (redir->fd >= 0)
			close(redir->fd);
		free(redir->target);
		free(redir);
		redir = next;
	}
}

void	free_ast(t_node *node)
{
	if (!node)
		return ;
	free_ast(node->left);
	free_ast(node->right);
	ft_lstclear(&node->words, free);
	free_redirs(node->redirs);
	free(node);
}

void	shell_cleanup(t_shell *sh)
{
	free_ast(sh->ast);
	sh->ast = NULL;
	free_strs(sh->argv);
	sh->argv = NULL;
	free(sh->line);
	sh->line = NULL;
	env_free(sh->env);
	sh->env = NULL;
	rl_clear_history();
}

void	child_exit(t_shell *sh, int status)
{
	shell_cleanup(sh);
	exit(status);
}
