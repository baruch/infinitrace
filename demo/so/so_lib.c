/*
 * so_lib.c
 * A simple shared-object library that produces traces
 *
 *  Created on: Nov 26, 2013
 *  Copyright by infinidat (http://infinidat.com)
 *  Author:     Yitzik Casapu, Infinidat
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
 */

#include "../../trace_user.h"

int trace_write_simple_from_so(int arg)
{
    INFO("Writing 1st trace from the library", arg);
    ++arg;
    WARN("Argument value has increased to", arg);
    return arg;
}

