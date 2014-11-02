/* Stub protocol definition for the NFS ACL protocol which isn't in an RFC */
/* Just provide the NULL procedures for testing whether it's alive */

program NFS_ACL_PROGRAM {
    version NFS_ACL_V2 {
        void
        ACLPROC2_NULL(void) = 0;
    } = 2;
    version NFS_ACL_V3 {
        void
        ACLPROC3_NULL(void) = 0;
    } = 3;
} = 100227;
