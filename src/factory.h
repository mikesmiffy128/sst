/* This file is dedicated to the public domain. */

#ifndef INC_FACTORY_H
#define INC_FACTORY_H

/* Access to game and engine factories obtained on plugin load */

typedef void *(*ifacefactory)(const char *name, int *ret);
extern ifacefactory factory_client, factory_server, factory_engine,
	   factory_inputsystem;

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
