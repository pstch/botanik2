/**
 * @file config.h
 * @brief The site's build-time configuration.
 *
 * Every value, limit and feature flag comes from `site.toml` by way of
 * `scripts/tables.py config`, so this file is the SHIM the build needs and
 * nothing else: CMake includes it PUBLIC, before any library default, which is
 * what makes a site's override win.
 *
 * It cannot hold the assertion that the site assumes every optional feature -
 * that is read before the flags have defaults to test. It lives in vars.h.
 */

#ifndef BOTANIK_SITE_CONFIG_H
#define BOTANIK_SITE_CONFIG_H

#include "generated/config.gen.h"

#endif
