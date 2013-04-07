/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "lib-signals.h"
#include "env-util.h"
#include "execv-const.h"
#include "array.h"
#include "net.h"
#include "istream.h"
#include "ostream.h"

#include "script-client-private.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>


struct script_client_local {
	struct script_client client;

	pid_t pid;
};

static void exec_child
(const char *bin_path, const char *const *args, const char *const *envs,
	int in_fd, int out_fd)
{
	ARRAY_TYPE(const_string) exec_args;

	if ( in_fd < 0 ) {
		in_fd = open("/dev/null", O_RDONLY);

		if ( in_fd == -1 )
			i_fatal("open(/dev/null) failed: %m");
	}

	if ( out_fd < 0 ) {
		out_fd = open("/dev/null", O_WRONLY);

		if ( out_fd == -1 )
			i_fatal("open(/dev/null) failed: %m");
	}

	if ( dup2(in_fd, STDIN_FILENO) < 0 )
		i_fatal("dup2(stdin) failed: %m");
	if ( dup2(out_fd, STDOUT_FILENO) < 0 )
		i_fatal("dup2(stdout) failed: %m");

	/* Close all fds */
	if ( close(in_fd) < 0 )
		i_error("close(in_fd) failed: %m");
	if ( (out_fd != in_fd) && close(out_fd) < 0 )
		i_error("close(out_fd) failed: %m");

	t_array_init(&exec_args, 16);
	array_append(&exec_args, &bin_path, 1);
	if ( args != NULL ) {
		for (; *args != NULL; args++)
			array_append(&exec_args, args, 1);
	}
	(void)array_append_space(&exec_args);

	env_clean();
	if ( envs != NULL ) {
		for (; *envs != NULL; envs++)
			env_put(*envs);
	}

	args = array_idx(&exec_args, 0);
	execvp_const(args[0], args);
}

static int script_client_local_connect
(struct script_client *sclient)
{
	struct script_client_local *slclient = 
		(struct script_client_local *) sclient;
	int fd[2] = { -1, -1 };

	if ( sclient->input != NULL || sclient->output != NULL ) {
		if ( socketpair(AF_UNIX, SOCK_STREAM, 0, fd) < 0 ) {
			i_error("socketpair() failed: %m");
			return -1;
		}
	}

	if ( (slclient->pid = fork()) == (pid_t)-1 ) {
		i_error("fork() failed: %m");
		if ( fd[0] >= 0 && close(fd[0]) < 0 ) {
			i_error("close(pipe_fd[0]) failed: %m");
		}
		if ( fd[1] >= 0 && close(fd[1]) < 0 ) {
			i_error("close(pipe_fd[1]) failed: %m");
		}
		return -1;
	}

	if ( slclient->pid == 0 ) {
		unsigned int count;
		const char *const *envs = NULL;

		/* child */
		if ( fd[1] >= 0 && close(fd[1]) < 0 ) {
			i_error("close(pipe_fd[1]) failed: %m");
		}

		if ( array_is_created(&sclient->envs) )
			envs = array_get(&sclient->envs, &count);

		exec_child(sclient->path, sclient->args, envs,
			( sclient->input != NULL ? fd[0] : -1 ),
			( sclient->output != NULL ? fd[0] : -1 ));
		i_unreached();
	}

	/* parent */
	if ( fd[0] >= 0 && close(fd[0]) < 0 ) {
		i_error("close(pipe_fd[0]) failed: %m");
	}

	if ( fd[1] >= 0 ) {
		net_set_nonblock(fd[1], TRUE);
		sclient->fd_in = ( sclient->output != NULL ? fd[1] : -1 );
		sclient->fd_out = ( sclient->input != NULL ? fd[1] : -1 );
	}
	script_client_init_streams(sclient);
	return script_client_script_connected(sclient);
}

static int script_client_local_close_output(struct script_client *sclient)
{
	/* Shutdown output; script stdin will get EOF */
	if ( sclient->fd_out >= 0 && shutdown(sclient->fd_out, SHUT_WR) < 0 ) {
		i_error("shutdown(%s, SHUT_WR) failed: %m", sclient->path);
		return -1;
	}
	sclient->fd_out = -1;
	return 1;
}

static int script_client_local_disconnect
(struct script_client *sclient, bool force)
{
	struct script_client_local *slclient = 
		(struct script_client_local *) sclient;
	pid_t pid = slclient->pid;
	time_t runtime, timeout = 0;
	int status;
	
	i_assert( pid >= 0 );
	slclient->pid = -1;

	/* Calculate timeout */
	runtime = ioloop_time - sclient->start_time;
	if ( !force && sclient->set->input_idle_timeout_secs > 0 &&
		runtime < sclient->set->input_idle_timeout_secs )
		timeout = sclient->set->input_idle_timeout_secs - runtime;

	if ( sclient->debug ) {
		i_debug("waiting for program `%s' to finish after %llu seconds",
			sclient->path, (unsigned long long int)runtime);
	}

	/* Wait for child to exit */
	force = force ||
		(timeout == 0 && sclient->set->input_idle_timeout_secs > 0);
	if ( !force )
		alarm(timeout);
	if ( force || waitpid(pid, &status, 0) < 0 ) {
		if ( !force && errno != EINTR ) {
			i_error("waitpid(%s) failed: %m", sclient->path);
			(void)kill(pid, SIGKILL);
			return -1;
		}

		/* Timed out */
		force = TRUE;
		if ( sclient->error == SCRIPT_CLIENT_ERROR_NONE )
			sclient->error = SCRIPT_CLIENT_ERROR_RUN_TIMEOUT;
		if ( sclient->debug ) {
			i_debug("program `%s' execution timed out after %llu seconds: "
				"sending TERM signal", sclient->path,
				(unsigned long long int)sclient->set->input_idle_timeout_secs);
		}

		/* Kill child gently first */
		if ( kill(pid, SIGTERM) < 0 ) {
			i_error("failed to send SIGTERM signal to program `%s'", sclient->path);
			(void)kill(pid, SIGKILL);
			return -1;
		} 
			
		/* Wait for it to die (give it some more time) */
		alarm(5);
		if ( waitpid(pid, &status, 0) < 0 ) {
			if ( errno != EINTR ) {
				i_error("waitpid(%s) failed: %m", sclient->path);
				(void)kill(pid, SIGKILL); 
				return -1;
			}

			/* Timed out again */
			if ( sclient->debug ) {
				i_debug("program `%s' execution timed out: sending KILL signal",
					sclient->path);
			}

			/* Kill it brutally now */
			if ( kill(pid, SIGKILL) < 0 ) {
				i_error("failed to send SIGKILL signal to program `%s'",
					sclient->path);
				return -1;
			}

			/* Now it will die immediately */
			if ( waitpid(pid, &status, 0) < 0 ) {
				i_error("waitpid(%s) failed: %m", sclient->path);
				return -1;
			}
		}
	}	
	
	/* Evaluate child exit status */
	sclient->exit_code = -1;
	if ( WIFEXITED(status) ) {
		/* Exited */
		int exit_code = WEXITSTATUS(status);
				
		if ( exit_code != 0 ) {
			i_info("program `%s' terminated with non-zero exit code %d", 
				sclient->path, exit_code);
			sclient->exit_code = 0;
			return 0;
		}

		sclient->exit_code = 1;
		return 1;	

	} else if ( WIFSIGNALED(status) ) {
		/* Killed with a signal */
		
		if ( force ) {
			i_error("program `%s' was forcibly terminated with signal %d",
				sclient->path, WTERMSIG(status));
		} else {
			i_error("program `%s' terminated abnormally, signal %d",
				sclient->path, WTERMSIG(status));
		}
		return -1;

	} else if ( WIFSTOPPED(status) ) {
		/* Stopped */
		i_error("program `%s' stopped, signal %d",
			sclient->path, WSTOPSIG(status));
		return -1;
	} 

	/* Something else */
	i_error("program `%s' terminated abnormally, return status %d",
		sclient->path, status);
	return -1;
}

static void script_client_local_failure
(struct script_client *sclient, enum script_client_error error)
{
	switch ( error ) {
	case SCRIPT_CLIENT_ERROR_RUN_TIMEOUT:
		i_error("program `%s' execution timed out (> %d secs)",
			sclient->path, sclient->set->input_idle_timeout_secs);
		break;
	default:
		break;
	}
}

struct script_client *script_client_local_create
(const char *bin_path, const char *const *args,
	const struct script_client_settings *set)
{
	struct script_client_local *sclient;
	pool_t pool;

	pool = pool_alloconly_create("script client local", 1024);
	sclient = i_new(struct script_client_local, 1);
	script_client_init(&sclient->client, pool, bin_path, args, set);
	sclient->client.connect = script_client_local_connect;
	sclient->client.close_output = script_client_local_close_output;
	sclient->client.disconnect = script_client_local_disconnect;
	sclient->client.failure = script_client_local_failure;
	sclient->pid = -1;

	return &sclient->client;
}

