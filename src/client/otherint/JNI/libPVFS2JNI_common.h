#ifndef LIBPVFS2JNI_COMMON_H
#define LIBPVFS2JNI_COMMON_H

/* Debug Ouput On/OFF */
#define JNI_DBG 0

/* NULL jobject */
#define NULL_OBJ ((jobject) 0)

/* Macro that helps print function info */
#define PFI if(JNI_DBG){jni_printf("function={%s}\n", __func__);}

/* Macro that flushes stderr. stdout if JNI_DBG is enabled */
#define JNI_FLUSH if(JNI_DBG){jni_fflush(stdout);} jni_fflush(stderr);

/* Macro Function returns T/F if the supplied directory entry name corresponds
 * to the dot directories.
 */
#define DOTDIR(DIRENT_NAME) \
    ( strcmp((DIRENT_NAME), ".") == 0) || (strcmp((DIRENT_NAME), "..") == 0 )

/* Macro Functions simplify getting/setting fields of an object instance */
#define GET_FIELD_ID(ENV, CLASS, FIELD_NAME, FIELD_TYPE)                       \
    ( (*(ENV))->GetFieldID((ENV), (CLASS), (FIELD_NAME), (FIELD_TYPE)) )

#define SET_CHAR_FIELD(ENV, OBJ, FIELD_ID, VAL)                                 \
    ( (*(ENV))->SetCharField((ENV), (OBJ), (FIELD_ID), (VAL)) )

#define SET_INT_FIELD(ENV, OBJ, FIELD_ID, VAL)                                 \
    ( (*(ENV))->SetIntField((ENV), (OBJ), (FIELD_ID), (VAL)) )

#define SET_LONG_FIELD(ENV, OBJ, FIELD_ID, VAL)                                \
    ( (*(ENV))->SetLongField((ENV), (OBJ), (FIELD_ID), (VAL)) )

#define SET_OBJECT_FIELD(ENV, OBJ, FIELD_ID, VAL)                                 \
    ( (*(ENV))->SetObjectField((ENV), (OBJ), (FIELD_ID), (VAL)) )

#endif
