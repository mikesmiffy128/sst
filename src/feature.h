/*
 * Copyright © Michael Smith <mikesmiffy128@gmail.com>
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

#define _FEATURE_CAT1(a, b) a##b
#define _FEATURE_CAT(a, b) _FEATURE_CAT1(a, b)

/*
 * Declares that this translation unit implements a "feature" - a unit of plugin
 * functionality.
 *
 * At build time, the code generator automatically creates the glue code
 * required to make features load and unload in the correct order, subject to
 * compatibility with each supported game and engine version. As such, there is
 * no need to manually plug a new feature into SST's initialisation code as long
 * as dependency information is properly declared (see the macros below).
 *
 * desc specifies a string to be displayed to the user in the console printout
 * that gets displayed when the plugin finishes loading. Omit this to declare an
 * internal feature, which won't be displayed to users but will still be
 * available for other features to call into and build functionality on top of.
 */
#define FEATURE(... /*desc*/)

/*
 * Declares that this feature should only be loaded for games matching the given
 * gametype tag. gametype.h must be included to use this as it defines the tag
 * values. Console variables and commands created using DEF_FEAT_* macros will
 * not be registered if SST is loaded by some other game.
 *
 * This also enables a build-time optimisation to elide REQUIRE_GAMEDATA()
 * checks as well as has_* conditionals. As such, it is wise to still specify
 * gamedata dependencies correctly, so that the definitions can be changed in
 * the data files without breaking code.
 */
#define GAMESPECIFIC(tag) \
	/* impl note: see comment in gamedata.h */ \
	__attribute((unused)) \
	static const int _gamedata_feattags = _gametype_tag_##tag;

/*
 * Indicates that the specified feature is required for this feature to
 * function. If that feature fails to initialise, this feature will not be
 * enabled.
 *
 * By convention, the name of the feature is the name of its implementation
 * source file, minus the .c extension. For instance, foo.c would be referred to
 * by REQUIRE(foo).
 */
#define REQUIRE(feature)

/*
 * Indicates that the specified feature should be initialised before this one,
 * but is not a hard requirement.
 *
 * Presence of a feature can in turn be tested for using has_<featname>.
 */
#define REQUEST(feature) extern bool has_##feature;

/*
 * Indicates that the specified gamedata entry is required for this feature to
 * function. If that entry is missing, this feature will not be enabled.
 *
 * Note that optional gamedata doesn't need to be specified here as it has no
 * effect on whether this feature is loaded. It can simply be tested for using
 * has_<entryname>.
 */
#define REQUIRE_GAMEDATA(entry)

/*
 * Indicates that this feature requires a global variable (such as a factory or
 * globally-exposed engine interface) to be non-null in order to function. If
 * the variable has a null/zero value prior to feature initialisation, this
 * feature will not be enabled.
 *
 * Note that this really only works for variables known to engineapi.c which is
 * kind of a bad abstraction, but it's currently just necessary in practice.
 *
 * Correct usage of this macro will generally be very similar to other usages
 * found elsewhere in the codebase already, so use those as a reference.
 */
#define REQUIRE_GLOBAL(varname)

/* status values for INIT and PREINIT below */
enum {
	FEAT_SKIP = -1, /* feature isn't useful here, pretend it doesn't exist */
	FEAT_OK, /* feature successfully initialised/enabled */
	FEAT_FAIL, /* error in starting up feature */
	FEAT_INCOMPAT, /* feature is incompatible with this game/engine version */
	_FEAT_INTERNAL_STATUSES // internal detail, do not use
};

/*
 * Defines the special feature initialisation function which is unique to this
 * translation unit. All features are required to specify this function.
 *
 * The defined function must return FEAT_OK on success, FEAT_FAIL on failure due
 * to some transient error, FEAT_INCOMPAT to indicate incompatibility with the
 * current game/engine version, or FEAT_SKIP to indicate that the feature is
 * useless or unnecessary.
 *
 * If a value other than FEAT_OK is returned, END (see below) will not be called
 * later and other features that depend on this feature will be disabled. If
 * this feature provides other functions as API, they can be assumed not to get
 * called unless initialisation is successful.
 *
 * For features with a description (see FEATURE() above), all return values with
 * the exception of FEAT_SKIP will cause a corresponding status message to be
 * displayed in the listing after the plugin finishes loading, while FEAT_SKIP
 * will simply hide the feature from the listing. Features with no description
 * will not be displayed anyway.
 *
 * Features which either fail to initialise or elect to skip loading will cause
 * dependent features not to be enabled.
 */
#define INIT int _FEATURE_CAT(_feat_init_, MODULE_NAME)() // { code... }

/*
 * Defines the special, optional feature shutdown function which is unique to
 * this translation unit. This does not return a value, and may be either
 * specified once, or left out if no cleanup is required for this feature.
 */
#define END void _FEATURE_CAT(_feat_end_, MODULE_NAME)() // { code... }

/*
 * Defines a special feature pre-init function which performs early feature
 * initialisation, the moment the plugin is loaded. If the plugin is autoloaded
 * via VDF, this will be called long before the deferred initialisation that
 * usually happens after the client and VGUI have spun up. Since most of the
 * rest of SST is also deferred, care must be taken not to call anything else
 * that is not yet initialised.
 *
 * When in doubt, do not use this; it exists only to serve a couple of fringe
 * cases.
 *
 * Like INIT above, the function created by this macro is expected to return one
 * of FEAT_OK, FEAT_FAIL, FEAT_INCOMPAT, or FEAT_SKIP. If a value other than
 * FEAT_OK is returned, the INIT block won't be run afterwards.
 *
 * Features that use this macro are currently disallowed from using REQUIRE()
 * and REQUEST(), as well as GAMESPECIFIC(), because it's not clear how the
 * semantics of doing so should work. It is still possible to use
 * REQUIRE_GAMEDATA and REQUIRE_GLOBAL, however these only apply to the INIT
 * block, *NOT* the PREINIT.
 */
#define PREINIT int _FEATURE_CAT(_feat_preinit_, MODULE_NAME)() // {...}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
