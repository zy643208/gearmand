/* Gearman server and library
 * Copyright (C) 2008 Brian Aker, Eric Day
 * All rights reserved.
 *
 * Use and distribution licensed under the BSD license.  See
 * the COPYING file in the parent directory for full text.
 */

/**
 * @file
 * @brief Gearman State Definitions
 */

#include "common.h"

/**
 * @addtogroup gearman_state Static Gearman Declarations
 * @ingroup state
 * @{
 */

gearman_state_st *gearman_state_create(gearman_state_st *state, gearman_options_t *options)
{
  if (state == NULL)
  {
    state= malloc(sizeof(gearman_state_st));
    if (state == NULL)
      return NULL;

    state->options.allocated= true;
  }
  else
  {
    state->options.allocated= false;
  }

  { // Set defaults on all options.
    state->options.dont_track_packets= false;
    state->options.non_blocking= false;
    state->options.stored_non_blocking= false;
  }

  if (options)
  {
    while (*options != GEARMAN_MAX)
    {
      /**
        @note Check for bad value, refactor gearman_add_options().
      */
      gearman_add_options(state, *options);
      options++;
    }
  }

  state->verbose= 0;
  state->con_count= 0;
  state->packet_count= 0;
  state->pfds_size= 0;
  state->sending= 0;
  state->last_errno= 0;
  state->timeout= -1;
  state->con_list= NULL;
  state->packet_list= NULL;
  state->pfds= NULL;
  state->log_fn= NULL;
  state->log_context= NULL;
  state->event_watch_fn= NULL;
  state->event_watch_context= NULL;
  state->workload_malloc_fn= NULL;
  state->workload_malloc_context= NULL;
  state->workload_free_fn= NULL;
  state->workload_free_context= NULL;
  state->last_error[0]= 0;

  return state;
}

gearman_state_st *gearman_state_clone(gearman_state_st *destination, const gearman_state_st *source)
{
  gearman_connection_st *con;

  destination= gearman_state_create(destination, NULL);

  if (! source || ! destination)
  {
    return destination;
  }

  (void)gearman_set_option(destination, GEARMAN_NON_BLOCKING, source->options.non_blocking);
  (void)gearman_set_option(destination, GEARMAN_DONT_TRACK_PACKETS, source->options.dont_track_packets);

  destination->timeout= source->timeout;

  for (con= source->con_list; con != NULL; con= con->next)
  {
    if (gearman_connection_clone(destination, NULL, con) == NULL)
    {
      gearman_state_free(destination);
      return NULL;
    }
  }

  /* Don't clone job or packet information, this is state information for
     old and active jobs/connections. */

  return destination;
}

void gearman_state_free(gearman_state_st *state)
{
  gearman_free_all_cons(state);
  gearman_free_all_packets(state);

  if (state->pfds != NULL)
    free(state->pfds);

  if (state->options.allocated)
    free(state);
}

gearman_return_t gearman_set_option(gearman_state_st *state, gearman_options_t option, bool value)
{
  switch (option)
  {
  case GEARMAN_NON_BLOCKING:
    state->options.non_blocking= value;
    break;
  case GEARMAN_DONT_TRACK_PACKETS:
    state->options.dont_track_packets= value;
    break;
  case GEARMAN_MAX:
  default:
    return GEARMAN_INVALID_COMMAND;
  }

  return GEARMAN_SUCCESS;
}

int gearman_timeout(gearman_state_st *state)
{
  return state->timeout;
}

void gearman_set_timeout(gearman_state_st *state, int timeout)
{
  state->timeout= timeout;
}

void gearman_set_log_fn(gearman_state_st *state, gearman_log_fn *function,
                        const void *context, gearman_verbose_t verbose)
{
  state->log_fn= function;
  state->log_context= context;
  state->verbose= verbose;
}

void gearman_set_event_watch_fn(gearman_state_st *state,
                                gearman_event_watch_fn *function,
                                const void *context)
{
  state->event_watch_fn= function;
  state->event_watch_context= context;
}

void gearman_set_workload_malloc_fn(gearman_state_st *state,
                                    gearman_malloc_fn *function,
                                    const void *context)
{
  state->workload_malloc_fn= function;
  state->workload_malloc_context= context;
}

void gearman_set_workload_free_fn(gearman_state_st *state,
                                  gearman_free_fn *function,
                                  const void *context)
{
  state->workload_free_fn= function;
  state->workload_free_context= context;
}

void gearman_free_all_cons(gearman_state_st *state)
{
  while (state->con_list != NULL)
    gearman_connection_free(state->con_list);
}

gearman_return_t gearman_flush_all(gearman_state_st *state)
{
  gearman_connection_st *con;
  gearman_return_t ret;

  for (con= state->con_list; con != NULL; con= con->next)
  {
    if (con->events & POLLOUT)
      continue;

    ret= gearman_connection_flush(con);
    if (ret != GEARMAN_SUCCESS && ret != GEARMAN_IO_WAIT)
      return ret;
  }

  return GEARMAN_SUCCESS;
}

gearman_return_t gearman_wait(gearman_state_st *state)
{
  gearman_connection_st *con;
  struct pollfd *pfds;
  nfds_t x;
  int ret;
  gearman_return_t gret;

  if (state->pfds_size < state->con_count)
  {
    pfds= realloc(state->pfds, state->con_count * sizeof(struct pollfd));
    if (pfds == NULL)
    {
      gearman_state_set_error(state, "gearman_wait", "realloc");
      return GEARMAN_MEMORY_ALLOCATION_FAILURE;
    }

    state->pfds= pfds;
    state->pfds_size= state->con_count;
  }
  else
    pfds= state->pfds;

  x= 0;
  for (con= state->con_list; con != NULL; con= con->next)
  {
    if (con->events == 0)
      continue;

    pfds[x].fd= con->fd;
    pfds[x].events= con->events;
    pfds[x].revents= 0;
    x++;
  }

  if (x == 0)
  {
    gearman_state_set_error(state, "gearman_wait", "no active file descriptors");
    return GEARMAN_NO_ACTIVE_FDS;
  }

  while (1)
  {
    ret= poll(pfds, x, state->timeout);
    if (ret == -1)
    {
      if (errno == EINTR)
        continue;

      gearman_state_set_error(state, "gearman_wait", "poll:%d", errno);
      state->last_errno= errno;
      return GEARMAN_ERRNO;
    }

    break;
  }

  if (ret == 0)
  {
    gearman_state_set_error(state, "gearman_wait", "timeout reached");
    return GEARMAN_TIMEOUT;
  }

  x= 0;
  for (con= state->con_list; con != NULL; con= con->next)
  {
    if (con->events == 0)
      continue;

    gret= gearman_connection_set_revents(con, pfds[x].revents);
    if (gret != GEARMAN_SUCCESS)
      return gret;

    x++;
  }

  return GEARMAN_SUCCESS;
}

gearman_connection_st *gearman_ready(gearman_state_st *state)
{
  gearman_connection_st *con;

  /* We can't keep state between calls since connections may be removed during
     processing. If this list ever gets big, we may want something faster. */

  for (con= state->con_list; con != NULL; con= con->next)
  {
    if (con->options.ready)
    {
      con->options.ready= false;
      return con;
    }
  }

  return NULL;
}

/**
  @note gearman_state_push_blocking is only used for echo (and should be fixed
  when tricky flip/flop in IO is fixed).
*/
static inline void gearman_state_push_blocking(gearman_state_st *state)
{
  state->options.stored_non_blocking= state->options.non_blocking;
  state->options.non_blocking= false;
}

gearman_return_t gearman_echo(gearman_state_st *state, const void *workload,
                              size_t workload_size)
{
  gearman_connection_st *con;
  gearman_packet_st packet;
  gearman_return_t ret;

  ret= gearman_packet_create_args(state, &packet, GEARMAN_MAGIC_REQUEST,
                                  GEARMAN_COMMAND_ECHO_REQ, &workload,
                                  &workload_size, 1);
  if (ret != GEARMAN_SUCCESS)
    return ret;

  gearman_state_push_blocking(state);

  for (con= state->con_list; con != NULL; con= con->next)
  {
    ret= gearman_connection_send(con, &packet, true);
    if (ret != GEARMAN_SUCCESS)
    {
      gearman_packet_free(&packet);
      gearman_state_pop_non_blocking(state);

      return ret;
    }

    (void)gearman_connection_recv(con, &(con->packet), &ret, true);
    if (ret != GEARMAN_SUCCESS)
    {
      gearman_packet_free(&packet);
      gearman_state_pop_non_blocking(state);

      return ret;
    }

    if (con->packet.data_size != workload_size ||
        memcmp(workload, con->packet.data, workload_size))
    {
      gearman_packet_free(&(con->packet));
      gearman_packet_free(&packet);
      gearman_state_pop_non_blocking(state);
      gearman_state_set_error(state, "gearman_echo", "corruption during echo");

      return GEARMAN_ECHO_DATA_CORRUPTION;
    }

    gearman_packet_free(&(con->packet));
  }

  gearman_packet_free(&packet);
  gearman_state_pop_non_blocking(state);

  return GEARMAN_SUCCESS;
}

void gearman_free_all_packets(gearman_state_st *state)
{
  while (state->packet_list != NULL)
    gearman_packet_free(state->packet_list);
}

/*
 * Local Definitions
 */

void gearman_state_set_error(gearman_state_st *state, const char *function,
                       const char *format, ...)
{
  size_t size;
  char *ptr;
  char log_buffer[GEARMAN_MAX_ERROR_SIZE];
  va_list args;

  size= strlen(function);
  ptr= memcpy(log_buffer, function, size);
  ptr+= size;
  ptr[0]= ':';
  size++;
  ptr++;

  va_start(args, format);
  size+= (size_t)vsnprintf(ptr, GEARMAN_MAX_ERROR_SIZE - size, format, args);
  va_end(args);

  if (state->log_fn == NULL)
  {
    if (size >= GEARMAN_MAX_ERROR_SIZE)
      size= GEARMAN_MAX_ERROR_SIZE - 1;

    memcpy(state->last_error, log_buffer, size + 1);
  }
  else
  {
    state->log_fn(log_buffer, GEARMAN_VERBOSE_FATAL,
                    (void *)state->log_context);
  }
}

void gearman_log(gearman_state_st *state, gearman_verbose_t verbose,
                 const char *format, va_list args)
{
  char log_buffer[GEARMAN_MAX_ERROR_SIZE];

  if (state->log_fn == NULL)
  {
    printf("%5s: ", gearman_verbose_name(verbose));
    vprintf(format, args);
    printf("\n");
  }
  else
  {
    vsnprintf(log_buffer, GEARMAN_MAX_ERROR_SIZE, format, args);
    state->log_fn(log_buffer, verbose, (void *)state->log_context);
  }
}