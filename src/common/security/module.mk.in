DIR := src/common/security
SERVERSRC += $(DIR)/security-util.c \
             $(DIR)/pint-uid-map.c

NEEDCACHE = $(or ENABLE_CAPCACHE, ENABLE_CERTCACHE, ENABLE_CERTCACHE)

ifdef NEEDCACHE
SERVERSRC += $(DIR)/seccache.c
endif

ifdef ENABLE_CAPCACHE
SERVERSRC += $(DIR)/capcache.c
endif

LIBSRC += $(DIR)/security-util.c \
          $(DIR)/getugroups.c \
	  $(DIR)/client-capcache.c

ifdef ENABLE_SECURITY_KEY
SERVERSRC += $(DIR)/pint-security.c \
             $(DIR)/security-hash.c

ifdef ENABLE_CREDCACHE
SERVERSRC += $(DIR)/credcache.c
endif

else ifdef ENABLE_SECURITY_CERT
SERVERSRC += $(DIR)/pint-security.c \
             $(DIR)/security-hash.c \
             $(DIR)/pint-cert.c \
             $(DIR)/cert-util.c \
             $(DIR)/pint-ldap-map.c

LIBSRC += $(DIR)/cert-util.c

ifdef ENABLE_CREDCACHE
SERVERSRC += $(DIR)/credcache.c
endif

ifdef ENABLE_CERTCACHE
SERVERSRC += $(DIR)/certcache.c
endif

# compile/link ldap
MODCFLAGS_$(DIR)/pint-ldap-map.c := -DLDAP_DEPRECATED=1
else
SERVERSRC += $(DIR)/security-stubs.c
endif
