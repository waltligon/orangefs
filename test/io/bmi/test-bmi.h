
/*
 * This is a common header file for both a client and server that use
 * the BMI library 
 */

#ifndef __TEST_BMI_H
#define __TEST_BMI_H

#include <bmi.h>

/* default hostid of server when none is given */
#define DEFAULT_HOSTID  "tcp://localhost:3334"
#define DEFAULT_HOSTID_GM  "gm://webwulf"
#define DEFAULT_SERVERID  "tcp://NULL:3334"
#define DEFAULT_SERVERID_GM  "gm://NULL"

/* test server request format */
struct server_request{
	bmi_size_t size;
};

/* test server acknowledgement format */
struct server_ack{
	bmi_flag_t status;
};

#endif /* __TEST_BMI_H */
