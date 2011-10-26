/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * 
 *  Gearmand client and server library.
 *
 *  Copyright (C) 2011 Data Differential, http://datadifferential.com/
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *      * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following disclaimer
 *  in the documentation and/or other materials provided with the
 *  distribution.
 *
 *      * The names of its contributors may not be used to endorse or
 *  promote products derived from this software without specific prior
 *  written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include <config.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

#include <libgearman/gearman.h>
#include <boost/program_options.hpp>
#include "util/string.hpp"

#ifndef __INTEL_COMPILER
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

int main(int args, char *argv[])
{
  in_port_t port;
  int timeout;
  time_t epoch;
  std::string host;
  std::string text_to_echo;

  boost::program_options::options_description desc("Options");
  desc.add_options()
    ("help", "Options related to the program.")
    ("host,h", boost::program_options::value<std::string>(&host)->default_value("localhost"),"Connect to the host")
    ("port,p", boost::program_options::value<in_port_t>(&port)->default_value(GEARMAN_DEFAULT_TCP_PORT), "Port number use for connection")
    ("timeout,u", boost::program_options::value<int>(&timeout)->default_value(-1), "Timeout in milliseconds")
    ("epoch", boost::program_options::value<time_t>(&epoch)->default_value(10), "Seconds forward in time for task to run.")
    ("text", boost::program_options::value<std::string>(&text_to_echo), "Text used for echo")
            ;

  boost::program_options::positional_options_description text_options;
  text_options.add("text", -1);

  boost::program_options::variables_map vm;
  try
  {
    boost::program_options::store(boost::program_options::command_line_parser(args, argv).
                                  options(desc).positional(text_options).run(), vm);
    boost::program_options::notify(vm);
  }
  catch(std::exception &e)
  { 
    std::cout << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  if (vm.count("help"))
  {
    std::cout << desc << std::endl;
    return EXIT_SUCCESS;
  }

  if (text_to_echo.empty())
  {
    while(std::cin.good())
    { 
      char buffer[1024];

      std::cin.read(buffer, sizeof(buffer));
      text_to_echo.append(buffer, std::cin.gcount());
    }

    if (text_to_echo.empty())
    {
      std::cerr << "No text was provided for --text or via stdin" << std::endl;
      std::cerr << desc << std::endl;
      return EXIT_FAILURE;
    }
  }

  gearman_client_st client;
  if (gearman_client_create(&client) == NULL)
  {
    std::cerr << "Memory allocation failure on client creation" << std::endl;
    return EXIT_FAILURE;
  }

  gearman_return_t ret;
  ret= gearman_client_add_server(&client, host.c_str(), port);
  if (ret != GEARMAN_SUCCESS)
  {
    std::cerr << gearman_client_error(&client) << std::endl;
    return EXIT_FAILURE;
  }

  if (timeout >= 0)
    gearman_client_set_timeout(&client, timeout);

  gearman_task_attr_t workload= gearman_task_attr_init_epoch(time(NULL) +epoch, GEARMAN_JOB_PRIORITY_NORMAL);

  gearman_task_st *task;
  gearman_argument_t value= gearman_argument_make(0, 0, text_to_echo.c_str(), text_to_echo.size());

  if (not (task= gearman_execute(&client, util_literal_param("reverse"), NULL, 0, &workload, &value, 0)))
  {
    std::cerr << gearman_client_error(&client) << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "Background Job Handle=" << gearman_task_job_handle(task) << std::endl;

  int exit_code= EXIT_SUCCESS;
  bool is_known;
  do
  {
    bool is_running;
    uint32_t numerator;
    uint32_t denominator;

    ret= gearman_client_job_status(&client, gearman_task_job_handle(task),
                                   &is_known, &is_running,
                                   &numerator, &denominator);
    if (gearman_continue(ret)) // Non-blocking event occurred, try again
    {
      continue;
    }
    else if (gearman_failed(ret))
    {
      std::cerr << gearman_client_error(&client) << std::endl;
      exit_code= EXIT_FAILURE;
      break;
    }

    std::cout << std::boolalpha 
      << "Known =" << is_known
      << ", Running=" << is_running
      << ", Percent Complete=" << numerator << "/" <<  denominator << std::endl;

  } while (is_known);

  gearman_client_free(&client);

  return exit_code;
}