/**
 * @file bank.h
 * @brief The store every generated read, write and resolve names.
 *
 * matiec lays a CONFIGURATION out as one translation unit per resource plus
 * one for the config, and each pulls `accessor.h`. They must all reach the
 * same bank, so the pointer is defined once and declared here.
 */
#ifndef BOTANIK_PLC_RUNTIME_BANK_H
#define BOTANIK_PLC_RUNTIME_BANK_H

#include "libbotanik/vars/model.h"

namespace Botanik { namespace Plc { namespace Runtime {
    /** @brief Set by Plc::Runtime::Plant before anything generated runs. */
    extern Vars::Bank* io;
}}}

#define BOTANIK_ST_IO (*::Botanik::Plc::Runtime::io)

#endif
