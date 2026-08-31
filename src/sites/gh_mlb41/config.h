/**
 * @file config.h
 * @brief The site's build-time configuration.
 *
 * Values, limits and feature flags come from `site.toml` by way of
 * `scripts/tables.py config`. What is left here is the one thing a table
 * cannot carry: the assertion that this site assumes every optional feature,
 * which is what stops a half-configured build reserving rows the library never
 * registers and misaligning every register after them.
 */

#ifndef BOTANIK_SITE_CONFIG_H
#define BOTANIK_SITE_CONFIG_H

#include "generated/config.gen.h"

#endif
