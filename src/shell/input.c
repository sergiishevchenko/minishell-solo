#include "minishell.h"

static char	*read_line_fd(int fd)
{
	char	*line;
	char	c;
	ssize_t	ret;

	line = NULL;
	ret = read(fd, &c, 1);
	while (ret > 0 && c != '\n')
	{
		if (str_push(&line, c) != 0)
			return (NULL);
		ret = read(fd, &c, 1);
	}
	if (ret <= 0 && !line)
		return (NULL);
	if (!line)
		line = ft_strdup("");
	return (line);
}

char	*get_prompt(void)
{
	return ("\001\033[1;32m\002minishell"
		"\001\033[1;36m\002$ \001\033[0m\002");
}

char	*get_hd_prompt(void)
{
	return ("\001\033[1;36m\002> \001\033[0m\002");
}

char	*read_input(t_shell *sh, char *prompt)
{
	if (sh->interactive)
		return (readline(prompt));
	return (read_line_fd(STDIN_FILENO));
}
