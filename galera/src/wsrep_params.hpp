//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef WSREP_PARAMS_HPP
#define WSREP_PARAMS_HPP

#ifndef ENOTRECOVERABLE
#define ENOTRECOVERABLE 131
#endif

#include "wsrep_api.h"

#include "galerautils.hpp"
#include "replicator.hpp"

void
wsrep_set_params (galera::Replicator& repl, const char* params)
    throw (gu::Exception);
char* wsrep_get_params(const galera::Replicator& repl);
#endif /* WSREP_PARAMS_HPP */
