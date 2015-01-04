/* This file is part of net-utils

net-utils is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

net-utils is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with net-utils; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA. */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <fcntl.h>

#define NUM_SESSIONS 100

#define my_max(a, b) ((a) > (b) ? (a) : (b))

#define FD_TARGET_R_READY (1 << 0)
#define FD_TARGET_W_READY (1 << 1)
#define FD_CLIENT_R_READY (1 << 2)
#define FD_CLIENT_W_READY (1 << 3)

typedef struct
{
  int target_fd;
  int client_fd;
  int log_fd;
  int flags;
} session_t;

typedef struct
{
  int listen_fd;
  struct sockaddr_in target;
} proxy_t;

static session_t session_table[NUM_SESSIONS];
static proxy_t sproxy;
static int log_mode = 0;
static int log_id = 1;
static int sec_wait = 0;

static int
init_session(void)
{
  int i;
  for (i = 0; i < NUM_SESSIONS; i++)
    {
      session_table[i].target_fd = -1;
      session_table[i].client_fd = -1;
    }
  return 0;
}

static int
init_proxy(const char *target_host, int target_port, int port)
{
  proxy_t *proxy = &sproxy;
  struct hostent *hp;
  struct sockaddr_in serv;
  int fd;

  hp = gethostbyname(target_host);
  if (hp == NULL) return -1;

  fd = socket (PF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  memset (&serv, 0, sizeof (serv));
  serv.sin_family = AF_INET;
  serv.sin_addr.s_addr = htonl (INADDR_ANY);
  serv.sin_port = htons (port);
  if (bind (fd, &serv, sizeof (serv)) < 0) return -1;
    
  if (listen (fd, 1) < 0) return -1;

  proxy->listen_fd = fd;

  memset (&proxy->target, 0, sizeof (proxy->target));
  memcpy (&proxy->target.sin_addr.s_addr, hp->h_addr, hp->h_length);
  proxy->target.sin_family = hp->h_addrtype;
  proxy->target.sin_port = htons (target_port);

  return 0;
}

static int
open_session(proxy_t *proxy)
{
  int i;
  struct sockaddr_in client_addr;
  int client_addr_len;
  session_t *session;
  int target_fd;
  int client_fd;
  int log_fd;
  char buf[10];

  for (i = 0; i < NUM_SESSIONS; i++)
    {
      session = &session_table[i];
      if (session->target_fd == -1) break;
    }
  if (i == NUM_SESSIONS) return -1;
  
  client_addr_len = sizeof client_addr;
  target_fd = accept (proxy->listen_fd, &client_addr, &client_addr_len);
  if (target_fd < 0) return -1;

  if (listen (proxy->listen_fd, 1) < 0) return -1;
  
  client_fd = socket (proxy->target.sin_family, SOCK_STREAM, 0);
  if (client_fd < 0)
    {
      close(target_fd);
      return -1;
    }
  
  if (connect (client_fd, &proxy->target, sizeof proxy->target) < 0)
    {
      close(target_fd);
      return -1;
    }

  if (log_mode)
    {
      sprintf(buf, "proxy%03d.log", log_id++);
      log_fd = open(buf, O_WRONLY | O_CREAT, 0666);
      
      if (log_fd < 0)
	{
	  close(client_fd);
	  close(target_fd);
	  return -1;
	}
    }
  else
    {
      log_fd = -1;
    }

  session->target_fd = target_fd;
  session->client_fd = client_fd;
  session->log_fd = log_fd;

  return i; /* Session ID */
}

static int
close_session(int sid)
{
  session_t *session = &session_table[sid];
  if (session->target_fd != -1)
    {
      close(session->target_fd);
    }
  session->target_fd = -1;
  if (session->client_fd != -1)
    {
      close(session->client_fd);
    }
  if (session->log_fd != -1)
    {
      close(session->log_fd);
    }
  session->client_fd = -1;

  return 0;
}

void
usage(void)
{
  fprintf(stderr, "Usage: simple_proxy [-f] [-x] [target host] [target port] [port]\n");
  exit(3);
}

void
cleanup(int signum)
{
  int i;
  fprintf(stderr, "cleanup: not implemented yet.\n");
  for (i = 0; i < NUM_SESSIONS; i++)
    {
      close_session(i);
    }
  if (sproxy.listen_fd >= 0) close(sproxy.listen_fd);
  exit(0);
}

int
detach_tty (void)
{
  int retval = fork();
  if (retval == -1) return -1;
  if (retval > 0)
    {
      exit (0);
    }
  return 0;
}

int
main (int argc, char *argv[])
{
  char buf[512];
  char *target_host;
  int target_port;
  int port;
  int fdetach = 0;

  proxy_t *proxy = &sproxy;
  fd_set rfds;
  int retval;

  while (1)
    {
      int c;
      c = getopt(argc, argv, "fxw:");
      if (c == -1) break;
      switch (c)
	{
	case 'f':
	  fdetach = 1;
	  break;
	case 'x':
	  log_mode = 1;
	  break;
	case 'w':
	  sec_wait = atoi(optarg);
	  break;
	}
    }

  if (argc - optind != 3) usage();

  target_host = argv[optind];
  target_port = atoi (argv[optind + 1]);
  port = atoi (argv[optind + 2]);

  printf("%s %d %d\n", target_host, target_port, port);

  signal(SIGTERM, cleanup);
  signal(SIGINT, cleanup);

  init_session();

  if (init_proxy (target_host, target_port, port) < 0)
    {
      perror(target_host);
      exit(2);
    }
  
  if (fdetach && detach_tty () == -1)
    {
      perror("detach_tty");
      exit(2);
    }

  while (1)
    {
      int i = 0;
      int len;
      int fd_max = -1;
      session_t *session;
      FD_ZERO (&rfds);
      FD_SET (proxy->listen_fd, &rfds);
      fd_max = my_max(fd_max, proxy->listen_fd);

      for (i = 0; i < NUM_SESSIONS; i++)
	{
	  session = &session_table[i];
	  if (session->target_fd != -1)
	    {
	      FD_SET (session->target_fd, &rfds);
	      fd_max = my_max(fd_max, session->target_fd);
	      FD_SET (session->client_fd, &rfds);
	      fd_max = my_max(fd_max, session->client_fd);
	    }
	}

      retval = select (fd_max + 1, &rfds, NULL, NULL, NULL);
      if (!retval) continue;

      for (i = 0; i < NUM_SESSIONS; i++)
	{
	  session = &session_table[i];
	  if (session->target_fd != -1)
	    {
	      if (FD_ISSET(session->target_fd, &rfds))
		{
		  len = read(session->target_fd, buf, sizeof buf);
		  if (len == 0 || len == -1)
		    {
		      close_session(i);
		      continue;
		    }
		  else
		    {
		      write(session->client_fd, buf, len);
		      if (session->log_fd != -1)
			write(session->log_fd, buf, len);
		      if (sec_wait > 0) sleep(sec_wait);
		    }
		}
	  
	      if (FD_ISSET(session->client_fd, &rfds))
		{
		  len = read(session->client_fd, buf, sizeof buf);
		  if (len == 0 || len == -1)
		    {
		      close_session(i);
		      continue;
		    }
		  else
		    {
		      write(session->target_fd, buf, len);
		      if (session->log_fd != -1)
			write(session->log_fd, buf, len);
		      if (sec_wait > 0) sleep(sec_wait);
		    }
		}
	    }
	}

      if (FD_ISSET (proxy->listen_fd, &rfds))
	{
	  open_session(proxy);
	}
    }

  return 0;
}
