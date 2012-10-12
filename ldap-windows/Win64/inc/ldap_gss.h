
#include "ldap.h"

#if defined(_WIN32)
	#define SECURITY_WIN32
	#include <security.h>
#else
	#include <gssapi/gssapi.h>
#endif 

LDAP_BEGIN_DECL

#if defined(_WIN32)
	typedef SEC_ENTRY gss_err_code;
#else
	typedef struct {
		OM_uint32 maj_stat;
		OM_uint32 min_stat;
		gss_name_t target_name;
	}gss_err_code;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LDAP_MAX_HOST_LEN 255

int ldap_gssbind(
	LDAP *ld,
	char *ldapHost,
	char *mechanism, 
	char *DN, 
	char *credential,
	gss_err_code *err_code);

char *ldap_gss_error(
	gss_err_code err);

#ifdef __cplusplus
};
#endif

/* GSS Errors (continued from LDAP_REFERRAL_LIMIT_EXCEEDED) */
#define LDAP_GSS_ERROR 0x62
#define LDAP_GSS_SECURITY_ERROR 0x63
#define LDAP_GSS_IMPORT_ERROR 0x64

LDAP_END_DECL
