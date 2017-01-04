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

typedef struct pb_session_t {
  int  active;
  char *nick;
  char *host;
  char *master;
  int  s;
} PB_SESSION;

#define BSIZE        4096
static char *my_key= "KillerBot";

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
  len = write (s->s, buf, len);
  va_end (arg);
  
  return len;
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
pb_process_msg (PB_SESSION *s, char *buffer) {
  char *prefix, *cmd, *from, *to, *aux, *pars = NULL; 
  
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
	  s->active = 0;
	
	pb_printf (s, "PRIVMSG %s :Bye\n", from);
      }
      
    }
  }
  return 0;
}

int
main (int argc, char *argv[]) {
  PB_SESSION pb_s, *s;
  char       buffer[BSIZE];
  
  printf ("picoBot v 0.1\n");
  s = &pb_s;
  s->host = strdup (argv[1]);
  s->master = strdup ("picoflamingo");
  s->active = 1;
  
  s->s = pb_connect (s->host, "6667");
  pb_irc_register (s, "picoBot", "Too sexy for this server");
  pb_irc_join (s, "test");
  pb_printf (s, "PRIVMSG %s :My key is %s\n", s->master, my_key);
  pb_printf (s, "%s", "PRIVMSG #test :Hello Everyone!\n");
  
  while (s->active) {
    memset (buffer, 0, BSIZE);
    if (read(s->s, buffer, BSIZE) <= 0) break;
    printf ("< %s", buffer);
    pb_process_msg (s, buffer);
  }
  close (s->s);
  
  return 0;
}
