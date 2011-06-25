
/* Copyright 1998 by the Massachusetts Institute of Technology.
 * Copyright (C) 2008-2010 by Daniel Stenberg
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 */


#include "ares_setup.h"

#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif

#include "ares.h"
#include "ares_data.h"
#include "inet_net_pton.h"
#include "ares_private.h"


int ares_get_servers(ares_channel channel,
                     struct ares_addr_node **servers)
{
  struct ares_addr_node *srvr_head = NULL;
  struct ares_addr_node *srvr_last = NULL;
  struct ares_addr_node *srvr_curr;
  int status = ARES_SUCCESS;
  int i;

  if (!channel)
    return ARES_ENODATA;

  for (i = 0; i < channel->nservers; i++)
    {
      /* Allocate storage for this server node appending it to the list */
      srvr_curr = ares_malloc_data(ARES_DATATYPE_ADDR_NODE);
      if (!srvr_curr)
        {
          status = ARES_ENOMEM;
          break;
        }
      if (srvr_last)
        {
          srvr_last->next = srvr_curr;
        }
      else
        {
          srvr_head = srvr_curr;
        }
      srvr_last = srvr_curr;

      /* Fill this server node data */
      srvr_curr->family = channel->servers[i].addr.family;
      if (srvr_curr->family == AF_INET)
        memcpy(&srvr_curr->addrV4, &channel->servers[i].addr.addrV4,
               sizeof(srvr_curr->addrV4));
      else
        memcpy(&srvr_curr->addrV6, &channel->servers[i].addr.addrV6,
               sizeof(srvr_curr->addrV6));
    }

  if (status != ARES_SUCCESS)
    {
      if (srvr_head)
        {
          ares_free_data(srvr_head);
          srvr_head = NULL;
        }
    }

  *servers = srvr_head;

  return status;
}


int ares_set_servers(ares_channel channel,
                     struct ares_addr_node *servers)
{
  struct ares_addr_node *srvr;
  int num_srvrs = 0;
  int i;

  if (ares_library_initialized() != ARES_SUCCESS)
    return ARES_ENOTINITIALIZED;

  if (!channel)
    return ARES_ENODATA;

  ares__destroy_servers_state(channel);

  for (srvr = servers; srvr; srvr = srvr->next)
    {
      num_srvrs++;
    }

  if (num_srvrs > 0)
    {
      /* Allocate storage for servers state */
      channel->servers = malloc(num_srvrs * sizeof(struct server_state));
      if (!channel->servers)
        {
          return ARES_ENOMEM;
        }
      channel->nservers = num_srvrs;
      /* Fill servers state address data */
      for (i = 0, srvr = servers; srvr; i++, srvr = srvr->next)
        {
          channel->servers[i].addr.family = srvr->family;
          if (srvr->family == AF_INET)
            memcpy(&channel->servers[i].addr.addrV4, &srvr->addrV4,
                   sizeof(srvr->addrV4));
          else
            memcpy(&channel->servers[i].addr.addrV6, &srvr->addrV6,
                   sizeof(srvr->addrV6));
        }
      /* Initialize servers state remaining data */
      ares__init_servers_state(channel);
    }

  return ARES_SUCCESS;
}

/* Incomming string format: host[:port][,host[:port]]... */
int ares_set_servers_csv(ares_channel channel,
                         const char* _csv)
{
  int i;
  char* csv = NULL;
  char* ptr;
  char* start_host;
  long port;
  bool found_port;
  int rv = ARES_SUCCESS;
  struct ares_addr_node *servers = NULL;
  struct ares_addr_node *last = NULL;

  if (ares_library_initialized() != ARES_SUCCESS)
    return ARES_ENOTINITIALIZED;

  if (!channel)
    return ARES_ENODATA;

  ares__destroy_servers_state(channel);

  i = strlen(_csv);
  if (i == 0)
     return ARES_SUCCESS; /* blank all servers */

  csv = malloc(i + 2);
  strcpy(csv, _csv);
  if (csv[i-1] != ',') { /* make parsing easier by ensuring ending ',' */
    csv[i] = ',';
    csv[i+1] = 0;
  }

  start_host = csv;
  found_port = false;
  for (ptr = csv; *ptr; ptr++) {
    if (*ptr == ',') {
      char* pp = ptr - 1;
      struct in_addr in4;
      struct ares_in6_addr in6;
      struct ares_addr_node *s = NULL;

      *ptr = 0; /* null terminate host:port string */
      /* Got an entry..see if port was specified. */
      while (pp > start_host) {
        if (*pp == ':')
          break; /* yes */
        if (!ISDIGIT(*pp)) {
          /* Found end of digits before we found :, so wasn't a port */
          pp = ptr;
          break;
        }
        pp--;
      }
      if ((pp != start_host) && ((pp + 1) < ptr)) {
        /* Found it. */
        found_port = true;
        port = strtol(pp + 1, NULL, 10);
        *pp = 0; /* null terminate host */
      }
      /* resolve host, try ipv4 first, rslt is in network byte order */
      rv = ares_inet_pton(AF_INET, start_host, &in4);
      if (!rv) {
        /* Ok, try IPv6 then */
        rv = ares_inet_pton(AF_INET6, start_host, &in6);
        if (!rv) {
          rv = ARES_EBADSTR;
          goto out;
        }
        /* was ipv6, add new server */
        s = malloc(sizeof(*s));
        if (!s) {
          rv = ARES_ENOMEM;
          goto out;
        }
        s->family = AF_INET6;
        memcpy(&s->addr, &in6, sizeof(struct ares_in6_addr));
      }
      else {
        /* was ipv4, add new server */
        s = malloc(sizeof(*s));
        if (!s) {
          rv = ARES_ENOMEM;
          goto out;
        }
        s->family = AF_INET;
        memcpy(&s->addr, &in4, sizeof(struct in_addr));
      }
      if (s) {
        /* TODO:  Add port to ares_addr_node and assign it here. */

        s->next = NULL;
        if (last) {
          last->next = s;
        }
        else {
          servers = s;
          last = s;
        }
      }

      /* Set up for next one */
      found_port = false;
      start_host = ptr + 1;
    }
  }

  rv = ares_set_servers(channel, servers);

  out:
  if (csv)
    free(csv);
  while (servers) {
    struct ares_addr_node *s = servers;
    servers = servers->next;
    free(s);
  }

  return rv;
}