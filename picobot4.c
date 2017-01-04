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

typedef int (*CMD_FUNC) (PB_SESSION *, char *, void*);
typedef struct pb_cmd_t
{
  char     *id;
  CMD_FUNC f;
} *PB_CMD;

typedef struct pb_cmd_mng_t
{
  int     n;
  PB_CMD  *cmd;
} PB_CMD_MNG;

typedef struct pb_irc_msg_t
{
  char *b, *cmd, *from, *to, *pars; 
} PB_IRC_MSG;

static  PB_CMD_MNG     *ctrl_cm = NULL;
static  PB_CMD_MNG     *irc_cm = NULL;
static  PB_CMD_MNG     *bot_cm = NULL;
static  PB_CMD_MNG     *bot_sec_cm = NULL;

static  char           *my_key= "KillerBot";
static  PB_SESSION     ses[MAX_CONN];
static  struct pollfd  pfd[MAX_CONN];
static  int            running = 1;

// Prototypes
int pb_add_session (char *host, char *nick, char *channel, char *master);

/* IRC Message parsing */
int
pb_irc_msg_parse (PB_IRC_MSG *m, char *buffer) {
  char *prefix, *aux; 
  
  printf ("** Buffer: '%s'\n", buffer);
  m->b = strdup (buffer);
  if (m->b[0] == ':') { // Process Prefix 
    prefix = strtok (m->b, " ");
    m->cmd = strtok (NULL, " ");
    m->to = strtok (NULL, "\r");
    if (m->to && (aux = strchr (m->to, ':'))) {
      *aux++ = 0;
      m->pars = strdup (aux);
    }
    m->from = strtok (prefix + 1, "!");
  }

  return 0;
}

int
pb_irc_msg_free (PB_IRC_MSG *m) {
  if (!m) return -1;
  if (m->pars) free (m->pars);
  if (m->b) free (m->b);
  return 0;
}

/* Cmd manager helper functions */
PB_CMD
pb_cmd_new (char *id, CMD_FUNC f) {
  PB_CMD p = NULL;

  if (!id) return NULL;
  p = malloc (sizeof (struct pb_cmd_t));
  p->id = strdup (id);
  p->f = f;

  return p;
}

PB_CMD_MNG*
pb_cmd_mng_new () {
  PB_CMD_MNG *p;

  p = malloc (sizeof (struct pb_cmd_mng_t));
  p->n = 0;
  p->cmd = NULL;

  return p;
}

int
pb_cmd_mng_add (PB_CMD_MNG *cm, char *id, CMD_FUNC f) {
  PB_CMD    *aux;

  if ((aux = realloc (cm->cmd, sizeof(struct pb_cmd_t) * (cm->n + 1))) == NULL)
    return -1;
  cm->cmd = aux;
  cm->cmd[cm->n] = pb_cmd_new (id, f);
  cm->n++;

  return 0;
}

int
pb_cmd_mng_run (PB_CMD_MNG *cm, PB_SESSION *s, char *buffer, void *arg) {
  int i;
  for (i = 0; i < cm->n; i++) {
    if (!strncasecmp (cm->cmd[i]->id, buffer, strlen(cm->cmd[i]->id)))
      {
	cm->cmd[i]->f (s, buffer, arg);
	return 0;
      }
  }
  fprintf (stderr, "E: cmd '%s' unknown\n", buffer);
  return 1; // The higher level do something with the command
}

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


/* Actions on IRC messages */
int
cmd_irc_join (PB_SESSION *s, char *buffer, void *arg) {
  PB_IRC_MSG *m = (PB_IRC_MSG*) arg;

  if (!strncasecmp (m->cmd, "JOIN", 4) && 
      strncmp (m->from, s->nick, strlen(s->nick))) {
    pb_printf (s, "PRIVMSG %s :Welcome %s\n", m->pars, m->from);
    if (!strncmp (m->from, s->master, strlen(s->master)))
      pb_printf (s, "PRIVMSG %s :Glad to see you again Master. My key is %s\n", 
		 s->master, my_key);
  }

  return 0;
}

int
cmd_irc_part (PB_SESSION *s, char *buffer, void *arg) {
  PB_IRC_MSG *m = (PB_IRC_MSG*) arg;

  pb_printf (s, "PRIVMSG %s :Bye %s\n", m->to, m->from);
  return 0;
}

/* Bot Public Commands implementation */
int
cmd_bot_chat (PB_SESSION *s, char *buffer, void *arg) {
  PB_IRC_MSG *m = (PB_IRC_MSG*) arg;

  fprintf (stderr, "I: Chat mode '%s'\n", buffer);
  if (strcasestr (buffer, s->nick))
    pb_printf (s, "PRIVMSG %s :Hey %s sup\n", m->to, m->from);

  return 0;
}

int
cmd_irc_privmsg (PB_SESSION *s, char *buffer, void *arg) {
  PB_IRC_MSG *m = (PB_IRC_MSG*) arg;

  // Ignore my own messages
  if (!strncasecmp (m->from, s->nick, strlen(s->nick))) return 0;

  if (m->to[0] == '#') {
    if (pb_cmd_mng_run (bot_cm, s, m->pars, m))
      cmd_bot_chat (s, m->pars, m);

  } else {
    if (!strncmp (m->from, s->master, strlen(s->master)) && 
	!strncasecmp (m->pars, my_key, strlen(my_key))) {
      pb_printf (s, "PRIVMSG %s :Ready master. Running cmd '%s'\n", 
		 m->from, m->pars + strlen (my_key)+1);
      pb_cmd_mng_run (bot_sec_cm, s, m->pars + strlen (my_key)+1, m);
    }
    
  }  
  return 0;
}

int
cmd_bot_quit (PB_SESSION *s, char *buffer, void *arg) {
  pd_del_fd (s->fd);
  return 0;
}

int
cmd_bot_help (PB_SESSION *s, char *buffer, void *arg) {
  PB_IRC_MSG *m = (PB_IRC_MSG*) arg;

  pb_printf (s, "PRIVMSG %s :Cannot help you %s. I'm Under Development. "
	     "Sorry about that\n", m->to, m->from);
  return 0;
}

int
pb_process_msg (PB_SESSION *s, char *buffer1) {
  char *prefix, *cmd, *from, *to, *aux, *pars = NULL; 
  char buffer[BSIZE];
  PB_IRC_MSG m1, *m;

  memset (buffer, 0, BSIZE);
  if (read(s->fd, buffer, BSIZE) <= 0) return -1;
  printf ("< %s", buffer);
  
  if (!strncasecmp (buffer, "ping", 4)) {
    pb_printf (s, "PONG\n");
    return 0;
  }

  m = &m1;
  memset (m, 0, sizeof (PB_IRC_MSG));
  pb_irc_msg_parse (m, buffer);
  pb_cmd_mng_run (irc_cm, s, m->cmd, m);
 
  pb_irc_msg_free (m);
  return 0;
}

/* Control Channel Message */
int
cmd_ctrl_help (PB_SESSION *s, char *buffer, void *arg) {
  pb_printf (s, "< Command list:\n< connect host nick channel master\n< list\n< quit\n");
  return 0;
}

int
cmd_ctrl_quit (PB_SESSION *s, char *buffer, void *arg) {
  pd_del_fd (s->fd);
  return 0;
}

int
cmd_ctrl_list (PB_SESSION *s, char *buffer, void *arg) {
  int i;
  
  for (i = 0; i < MAX_CONN; i++)
    if (ses[i].fd != -1) pb_printf (s, "< [%s@%s]\t : Master <%s>\n",
				    ses[i].nick, ses[i].host, ses[i].master);
  
  return 0;
}

int
cmd_ctrl_connect (PB_SESSION *s, char *buffer, void *arg) {
  char host[1024], nick[1024], channel[1024], master[1024];
  int  fd1;
  
  sscanf (buffer + strlen("connect "), "%s %s %s %s", 
	  host, nick, channel, master);
  if (pb_add_session (host, nick, channel, master) < 0)
    pb_printf (s, "< Cannot initiate instance\n");
  else
    pb_printf (s, "< Bot instance '%s@host' running\n", nick, host);
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
  pb_cmd_mng_run (ctrl_cm, s, buffer, NULL);

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

  printf ("picoBot v 0.4\n");
  for (i = 0; i < MAX_CONN; ses[i].fd = pfd[i++].fd = -1);

  // Create command managers
  // Control Command Manager
  ctrl_cm = pb_cmd_mng_new ();
  pb_cmd_mng_add (ctrl_cm, "list", cmd_ctrl_list);
  pb_cmd_mng_add (ctrl_cm, "help", cmd_ctrl_help);
  pb_cmd_mng_add (ctrl_cm, "quit", cmd_ctrl_quit);
  pb_cmd_mng_add (ctrl_cm, "connect", cmd_ctrl_connect);

  // IRC command manager
  irc_cm = pb_cmd_mng_new ();
  pb_cmd_mng_add (irc_cm, "join", cmd_irc_join);
  pb_cmd_mng_add (irc_cm, "part", cmd_irc_part);
  pb_cmd_mng_add (irc_cm, "privmsg", cmd_irc_privmsg);


  // Bot Public command manager
  bot_cm = pb_cmd_mng_new ();
  pb_cmd_mng_add (bot_cm, "@help", cmd_bot_help);

  // Bot Private command manager
  bot_sec_cm = pb_cmd_mng_new ();
  pb_cmd_mng_add (bot_sec_cm, "@quit", cmd_bot_quit);

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
