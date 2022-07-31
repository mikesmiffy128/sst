/*
 * Copyright © 2022 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED “AS IS” AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef INC_FEATURE_H
#define INC_FEATURE_H

#include <stdbool.h>

#define _FEATURE_CAT1(a, b) a##b
#define _FEATURE_CAT(a, b) _FEATURE_CAT1(a, b)

/*
 * Declares that this translation unit implements a "feature" - a unit of
 * plugin functionality.
 *
 * desc specifies a string to be displayed to the user. Omit this to declare an
 * internal feature, which won't be advertised, but will be available to other
 * features.
 */
#define FEATURE(... /*desc*/)

/*
 * Indicates that the specified feature is required for this feature to function.
 * If that feature fails to initialise, this feature will not be enabled.
 */
#define REQUIRE(feature)

/*
 * Indicates that the specified feature should be initialised before this one,
 * but is not a hard requirement.
 *
 * Presence of a feature can be tested for using has_<featurename>.
 */
#define REQUEST(featname) extern bool has_##featname;

/*
 * Indicates that the specified gamedata entry is required for this feature to
 * function. If that entry is missing, this feature will not be enabled.
 *
 * Note that optional gamedata doesn't need to be specified here as it has no
 * effect on whether this feature is loaded. It can simply be tested for using
 * has_<entryname>.
 */
#define REQUIRE_GAMEDATA(feature)

/*
 * Indicates that this feature requires a global variable (such as a factory or
 * globally-exposed engine interface) to be non-null in order to function. If
 * the variable has a null/zero value prior to feature initialisation, this
 * feature will not be enabled.
 */
#define REQUIRE_GLOBAL(varname)

/*
 * Defines the special feature init function which is unique to this translation
 * unit. This should return true to indicate success, or false to indicate
 * failure. Features which start to load will cause dependent features not to be
 * started.
 *
 * Features are required to specify this function.
 */
#define INIT bool _FEATURE_CAT(_feature_init_, MODULE_NAME)(void) // { code... }

/*
 * Defines the special, optional feature shutdown function which is unique to
 * this translation unit. This does not return a value, and may be either
 * specified once, or left out if no cleanup is required for this feature.
 */
#define END void _FEATURE_CAT(_feature_end_, MODULE_NAME)(void) // { code... }

/*
 * Defines a conditional check to run prior to checking other requirements for
 * this feature. This can be used to match a certain game type or conditionally
 * register console variables, and should return true or false to indicate
 * whether the feature should continue to initialise.
 */
#define PREINIT bool _FEATURE_CAT(_feature_preinit_, MODULE_NAME)(void) // {...}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
