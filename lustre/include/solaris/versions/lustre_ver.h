#ifndef __LUSTRE_SOLARIS_VER_H__
#define __LUSTRE_SOLARIS_VER_H__

#define LUSTRE_MAJOR            1
#define LUSTRE_MINOR            10
#define LUSTRE_PATCH            0
#define LUSTRE_FIX              43
#define LUSTRE_VERSION_STRING   "1.10.0.43"
#define CLIENT_URN              "LUSTRE-200-CLT"
#define MDS_URN                 "LUSTRE-200-MDS"
#define MGS_URN                 "LUSTRE-200-MGS"
#define OSS_URN                 "LUSTRE-200-OSS"

#define LUSTRE_VERSION_CODE OBD_OCD_VERSION(LUSTRE_MAJOR,LUSTRE_MINOR,LUSTRE_PATCH,LUSTRE_FIX)

/* liblustre clients are only allowed to connect if their LUSTRE_FIX mismatches
 * by this amount (set in lustre/autoconf/lustre-version.ac). */
#define LUSTRE_VERSION_ALLOWED_OFFSET OBD_OCD_VERSION(0,0,1,32)

#ifdef __KERNEL__
/* If lustre version of client and servers it connects to differs by more
 * than this amount, client would issue a warning.
 * (set in lustre/autoconf/lustre-version.ac) */
#define LUSTRE_VERSION_OFFSET_WARN OBD_OCD_VERSION(0,2,0,0)
#else
/* If liblustre version of client and servers it connects to differs by more
 * than this amount, client would issue a warning.
 * (set in lustre/autoconf/lustre-version.ac) */
#define LUSTRE_VERSION_OFFSET_WARN OBD_OCD_VERSION(0,0,1,32)
#endif

#endif /* __LUSTRE_SOLARIS_VER_H__ */
