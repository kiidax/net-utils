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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#ifdef READLINE
#include <readline.h>
#include <history.h>
#endif

int lftocrlf(char **bufp, const char *buf, int len);
void server_main(int rfd, int wfd);

int fd = -1;
int sock = -1;

void
sig_term(int signum)
{
  if (fd != -1) close(fd);
  if (sock != -1) close(sock);
  fprintf(stderr, "Closed by user\n");
  exit (2);
}

void
usage(void)
{
  fprintf(stderr, "Usage: rtelnet [port]\n");
  exit (2);
}

int
main(int argc, char *argv[])
{
  struct sockaddr_in sa;
  struct sockaddr_in ca;
  int ca_len;
  int cia;

  if (argc != 2) usage();

  signal (SIGTERM, sig_term);

  memset(&sa, 0, sizeof sa);
  sa.sin_addr.s_addr = htonl(INADDR_ANY);
  sa.sin_family = AF_INET;
  sa.sin_port = htons(atoi(argv[1]));

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1)
    {
      perror(NULL);
      exit(2);
    }

  if (bind(sock, &sa, sizeof sa) == -1)
    {
      perror(NULL);
      exit(2);
    }

  printf("Reverse Telnet\n");
  printf("Listening to port: %d\n\n", ntohs(sa.sin_port));

  while (1)
    {
      if (listen(sock, 0) == -1)
	{
	  perror(NULL);
	  exit(2);
	}

      memset(&ca, 0, sizeof ca);
      ca_len = sizeof ca;
      fd = accept(sock, &ca, &ca_len);
      if (fd == -1)
	{
	  perror(NULL);
	  exit(2);
	}

      cia = ntohl(ca.sin_addr.s_addr);

      printf("Accepted: %d.%d.%d.%d\n\n",
	     (cia >> 24) & 255,
	     (cia >> 16) & 255,
	     (cia >> 8) & 255,
	     cia & 255);

      server_main(fd, fd);

      printf("\nClosed\n\n");
    }
  close(sock);

  exit(0);
}

void
server_main(int rfd, int wfd)
{
#ifdef READLINE
  char *rbuf;
#endif
  char buf[1024];
  fd_set rfds;
  fd_set wfds;
  char *tbuf;
  int tlen;
  int len;
  int retval;

  while (1)
    {
      FD_ZERO(&rfds);
      FD_ZERO(&wfds);
      FD_SET(rfd, &rfds);
      FD_SET(0, &rfds);
      retval = select(rfd + 1, &rfds, NULL, NULL, NULL);
      if (FD_ISSET(rfd, &rfds))
	{
	  len = read(rfd, buf, sizeof buf);
	  if (len == -1)
	    {
	      perror("network");
	      break;
	    }
	  if (len == 0) break;
	  write(1, buf, len);
	}
      if (FD_ISSET(0, &rfds))
	{
#ifdef READLINE
	  rbuf = readline(NULL);
	  len = strlen(buf);
#else
	  len = read(0, buf, sizeof buf);
#endif
	  if (len == -1)
	    {
	      perror("console");
	      break;
	    }
	  tlen = lftocrlf(&tbuf, buf, len);
	  if (tlen == 0) break;
	  write(wfd, tbuf, tlen);
	  free(tbuf);
#ifdef READLINE
	  free(rbuf);
#endif
	}
    }

  close(rfd);
  if (wfd != rfd)
    {
      close(wfd);
    }
}    

int
lftocrlf(char **bufp, const char *buf, int len)
{
  int i;
  int j = 0;
  char *tbuf;
  tbuf = malloc(len * 2); /* Large enough size */
  for (i = 0; i < len; i++)
    {
      if (buf[i] == '\n')
	{
	  tbuf[j++] = '\r';
	  tbuf[j++] = '\n';
	}
      else
	{
	  tbuf[j++] = buf[i];
	}
    }
  *bufp = tbuf;
  return j;
}
