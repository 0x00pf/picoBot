/*
 * picoBot: A Educational IRC Bot
 * Copyright (c) 2017 pico
 *
 * This file is part of picoBot
 *
 * picoBot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * picoBot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with picoBot.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <poll.h>

#define BSIZE        4096
#define MAX_CONN     32
#define PB_TMO       500 // miliseconds

struct pb_session_t;
typedef int (*PROC_MSG) (struct pb_session_t *, char *);

typedef struct pb_session_t {
  char     *nick;
  char     *host;
  char     *master;
  int      fd;
  PROC_MSG func;
} PB_SESSION;

static  char           *my_key= "KillerBot";
static  PB_SESSION     ses[MAX_CONN];
static  struct pollfd  pfd[MAX_CONN];
static  int            running = 1;

// Prototypes
int pb_add_session (char *host, char *nick, char *channel, char *master);

/* Poll helper function*/
int
pb_add_fd (int fd) {
  int i;

  for (i = 0; i < MAX_CONN && pfd[i].fd != -1;i++);
  if (i == MAX_CONN) return -1;
  pfd[i].fd = fd;
  pfd[i].events = POLLIN || POLLHUP;

  ses[i].fd = fd;

  return i;
}

int
pd_del_index (int i) {
  pfd[i].fd = -1;
  close (ses[i].fd);
  ses[i].fd = -1;
  if (ses[i].host) free (ses[i].host);
  if (ses[i].nick) free (ses[i].nick);
  if (ses[i].master) free (ses[i].master);

  return i; 
}

int
pd_del_fd (int fd) {
  int i;

  for (i = 0; i < MAX_CONN && pfd[i].fd != fd; i++);
  if (i == MAX_CONN) return -1;

  pd_del_index (i);
  return i;
}

int
pd_find_fd (int fd) {
  int i;

  for (i = 0; i < MAX_CONN && pfd[i].fd != fd; i++);
  if (i == MAX_CONN) return -1;

  return i;
}

int
pb_printf (PB_SESSION *s, char *fmt,...) {
  char    buf[BSIZE];
  int     len;
  va_list arg;
  
  if (!s) return -1;
  if (!fmt) return -1;

  va_start (arg, fmt);
  
  memset (buf, 0, BSIZE);
  if ((len = vsnprintf (buf, BSIZE, fmt, arg)) > BSIZE) {
    fprintf (stderr, "Output truncated!!!\n");
    buf[BSIZE - 1] = 0;
  }
  len = write (s->fd, buf, len);
  va_end (arg);
  
  return len;
}

int
pb_server (int port) {
  struct sockaddr_in server;
  int                s, ops = 1;

  if ((s = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
      perror ("pb_server (socket):");
      return -1;
    }

  server.sin_family = AF_INET;
  server.sin_port = htons (port);

  server.sin_addr.s_addr = INADDR_ANY;
  if ((setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &ops, sizeof(ops))) < 0)
    perror ("pb_server (reuseaddr):");
  
  if ((bind (s, (struct sockaddr *) &server, sizeof(server)))< 0) {
      perror ("pb_server (bind):");
      goto cleanup;
    }

  if ((listen (s, 10)) < 0) {
      perror ("pb_server (listen):");
      goto cleanup;
    }

  return s;
 cleanup:
  close (s);
  return -1;
}

int 
pb_connect (char *host, char* port) {
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int             sfd, s;
  
  if (!host) return -1;
  if (!port) return -1;
  printf ("Connecting to '%s':%s\n", host, port);
  
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM; /* Stream socket */
  
  if ((s = getaddrinfo(host, port, NULL, &result)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    return -1;
  }
  
  // Try all the returned IPs
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    if ((sfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0) continue;
    if (connect (sfd, rp->ai_addr, rp->ai_addrlen) == 0) break;
    close (sfd);
  }
  if (rp == NULL) {
    fprintf (stderr, "Cannot connect to host '%s'\n", host);
    return -1;
  }
  freeaddrinfo(result); 
  
  return sfd;
}

int
pb_irc_register (PB_SESSION *s, char *nick, char *desc) {
  if (!s) return -1;
  if (!nick) return -1;
  if (!desc) return -1;
  
  s->nick = strdup (nick);
  pb_printf (s, "user %s  0 *: %s\n", nick, desc);
  pb_printf (s, "nick %s\n", nick);
  
  return 0;
}

int
pb_irc_join (PB_SESSION *s, char *channel) {
  if (!s) return -1;
  if (!channel) return -1;
  
  pb_printf (s, "join #%s\n", channel);
  return 0;
}

int
pb_process_msg (PB_SESSION *s, char *buffer1) {
  char *prefix, *cmd, *from, *to, *aux, *pars = NULL; 
  char buffer[BSIZE];

  memset (buffer, 0, BSIZE);
  if (read(s->fd, buffer, BSIZE) <= 0) return -1;
  printf ("< %s", buffer);

  
  if (!strncasecmp (buffer, "ping", 4)) {
    pb_printf (s, "PONG\n");
    return 0;
  }
  
  if (buffer[0] == ':') { // Process Prefix 
    prefix = strtok (buffer, " ");
    cmd = strtok (NULL, " ");
    to = strtok (NULL, "\r");
    if ((aux = strchr (to, ':'))) {
      *aux++ = 0;
      pars = strdup (aux);
    }
    from = strtok (prefix + 1, "!");
  }
  
  if (!strncasecmp (cmd, "JOIN", 4) && strncmp (from, s->nick, strlen(s->nick))) {
    pb_printf (s, "PRIVMSG %s :Welcome %s\n", pars, from);
    if (!strncmp (from, s->master, strlen(s->master)))
      pb_printf (s, "PRIVMSG %s :Glad to see you again Master. My key is %s\n", s->master, my_key);
  }
  else   if (!strncasecmp (cmd, "PART", 4))
    pb_printf (s, "PRIVMSG %s :Bye %s\n", to, from);
  else if (!strncasecmp (cmd, "PRIVMSG", 7)) {
    if (! strncasecmp (from, s->nick, strlen(s->nick))) return 0;
    if (to[0] == '#') {
      if (!strncasecmp (pars, "@help", 5))
	pb_printf (s, "PRIVMSG %s :Cannot help you %s. I'm Under Development. Sorry about that\n", to, from);
    } else {
      if (!strncmp (from, s->master, strlen(s->master)) && 
	  !strncasecmp (pars, my_key, strlen(my_key))) {
	pb_printf (s, "PRIVMSG %s :Ready master. Running cmd '%s'\n", from, pars + strlen (my_key)+1);
	if (!strncasecmp (pars + strlen (my_key)+1, "@quit", 5))
	  {
	    pb_printf (s, "PRIVMSG %s :Bye\n", from);
	    pd_del_fd (s->fd);

	  }
      }
      
    }
  }
  return 0;
}


int
proc_ctrl_msg (PB_SESSION *s, char *buffer1) {
 char buffer[BSIZE];
 int  i;

  memset (buffer, 0, BSIZE);
  if (read(s->fd, buffer, BSIZE) <= 0) {
    fprintf (stderr,"I: Control Connection dropped\n");
    pd_del_fd (s->fd);
    return -1;
  }
  printf ("< %s", buffer);

  if (!strncasecmp (buffer, "help", strlen("help"))) {
      pb_printf (s, "< Command list:\n< connect host nick channel master\n< list\n< quit\n");
    }
  else if (!strncasecmp (buffer, "quit", strlen("quit"))) {
      pd_del_fd (s->fd);
    }
  else if (!strncasecmp (buffer, "list", strlen("list"))) {
    for (i = 0; i < MAX_CONN; i++)
      if (ses[i].fd != -1) pb_printf (s, "< [%s@%s]\t : Master <%s>\n",
				      ses[i].nick, ses[i].host, ses[i].master);	      
  }
  else if (!strncasecmp (buffer, "connect", strlen("connect")))
    {
      char host[1024], nick[1024], channel[1024], master[1024];
      int  fd1;

      sscanf (buffer + strlen("connect "), "%s %s %s %s", 
	      host, nick, channel, master);
      if (pb_add_session (host, nick, channel, master) < 0)
	pb_printf (s, "< Cannot initiate instance\n");
      else
	pb_printf (s, "< Bot instance '%s@host' running\n", nick, host);
    }
  return 0;
}


int
pb_ctrl_accept (PB_SESSION *s, char *buffer) {
  struct sockaddr_in client;
  socklen_t          slen = sizeof(struct sockaddr_in);
  int                cfd, i;
  char               name[1024];
  
  fprintf (stderr, "I: Accepting connection\n");
  if ((cfd = accept (s->fd,  (struct sockaddr*)&client, &slen)) < 0) {
    perror ("pb_ctrl_accept:");
    return -1;
  }

  i = pb_add_fd (cfd);
  ses[i].func = proc_ctrl_msg;
  ses[i].host = strdup ("N/A");
  ses[i].master = strdup ("N/A");
  snprintf (name, 1024, "C&C_Client-%02d", i);
  ses[i].nick = strdup (name);

  return 0;
}

int
pb_add_session (char *host, char *nick, char *channel, char *master) {
  int        i, fd1;
  PB_SESSION *s;

  if ((fd1 = pb_connect (host, "6667")) < 0) return -1;
    

  if ((i = pb_add_fd (fd1)) < 0) return -1;

  ses[i].host = strdup (host);
  ses[i].master = strdup (master);
  ses[i].func = pb_process_msg;

  s = &ses[i];  

  pb_irc_register (s, nick, "Too sexy for this server");
  pb_irc_join (s, channel);
  pb_printf (s, "PRIVMSG %s :My key is %s\n", s->master, my_key);
  pb_printf (s, "PRIVMSG #%s :Hello Everyone!\n", channel);
      
  return 0;
}

int
main (int argc, char *argv[]) {
  PB_SESSION     pb_s, *s;
  char           buffer[BSIZE];
  int            i, n, r, fd1;

  printf ("picoBot v 0.2\n");
  for (i = 0; i < MAX_CONN; ses[i].fd = pfd[i++].fd = -1);


  // Create control channel
  fd1 = pb_server (1337);
  if ((i = pb_add_fd (fd1)) < 0) {
    fprintf (stderr, "Cannot add file descriptoor\n");
    exit (1);
  }
  ses[i].host = strdup ("localhost");
  ses[i].nick = strdup ("C&C");
  ses[i].master = strdup ("N/A");
  ses[i].func = pb_ctrl_accept;

  while (running) {
     n = MAX_CONN;
     if ((r = poll (pfd, n, PB_TMO)) < 0) {
	 perror ("poll:");
	 exit (1);
       }

     if (r == 0) continue; // Timeout. Add Idle Function

     for (i = 0; i < n; i++)  {
	 if (pfd[i].revents & POLLHUP) pd_del_index (i);
	 if (pfd[i].revents & POLLIN) ses[i].func (&ses[i], buffer);
       }

  }
  // Cleanup
  
  return 0;
}
