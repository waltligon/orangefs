DIR := src/common/security
SERVERSRC += $(DIR)/security-util.c \
             $(DIR)/pint-uid-map.c
LIBSRC += $(DIR)/security-util.c \
          $(DIR)/getugroups.c

# TODO: change for certs
ifdef ENABLE_SECURITY_KEY
SERVERSRC += $(DIR)/pint-security.c \
             $(DIR)/security-hash.c
else ifdef ENABLE_SECURITY_CERT
SERVERSRC += $(DIR)/pint-security.c \
             $(DIR)/security-hash.c \
             $(DIR)/pint-cert.c \
             $(DIR)/cert-util.c \
             $(DIR)/pint-ldap-map.c
# compile/link ldap
MODCFLAGS_$(DIR)/pint-ldap-map.c := -DLDAP_DEPRECATED=1
else
SERVERSRC += $(DIR)/security-stubs.c
endif
