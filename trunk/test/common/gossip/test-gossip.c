#include <gossip.h>

enum{
	DEBUG_SOME_STUFF = 1,
	DEBUG_OTHER_STUFF = 2,
	DEBUG_BAD_STUFF = 4
};

int main(int argc, char **argv)	{

	int foo = 8675309;
	
	/* try the stderr method first */
	gossip_enable_stderr();

	/* turn on debugging and look for some stuff or other stuff :) */
	gossip_set_debug_mask(1, DEBUG_SOME_STUFF|DEBUG_OTHER_STUFF);

	/* try a normal debugging message */
	gossip_debug(DEBUG_SOME_STUFF, "Hello, normal debugging.\n");

	/* try it with line numbers */
	gossip_ldebug(DEBUG_SOME_STUFF, "Debugging w/ line numbers.\n");

	/* use printf style arguments */
	gossip_ldebug(DEBUG_SOME_STUFF, "Value of foo: %d\n", foo);

	/* try to log something that doesn't match the mask we set */
	gossip_debug(DEBUG_BAD_STUFF, "This shouldn't print!\n");

	/* try printing a critical error */
	gossip_err("Critical error message.\n");


	/* now, let's turn off debugging on the fly */
	gossip_set_debug_mask(0,0);

	/* try a normal debugging message w/ line numbers */
	gossip_ldebug(DEBUG_SOME_STUFF, "This shouldn't print!\n");

	/* try printing a critical error */
	gossip_lerr("Error messages print even when debugging is off!\n");
       
	
	/* switch to file logging on the fly */
	gossip_enable_file("test.log", "w");

	/* turn debugging back on for some stuff */
	gossip_set_debug_mask(1,DEBUG_SOME_STUFF);

	/* print a normal debugging message */
	gossip_ldebug(DEBUG_SOME_STUFF, "This should print into a file.\n");


	/* switch to syslog logging on the fly */
	gossip_enable_syslog(LOG_USER);

	/* print a normal debugging message */
	/* commented out because it is annoying if you don't redirect stderr */
	/*
	gossip_ldebug(DEBUG_SOME_STUFF, "This should print in syslog.\n");
	*/

	/* shutdown gossip */
	gossip_disable();

	return(0);
}
