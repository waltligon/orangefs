
#include <pvfs-request.h>
#include <pint-request.h>

main(int argc, char **argv)
{
	struct PINT_Request *r;
	struct PINT_Request *r_packed;
	int r_size, an_int;

	/* build a request */
	PVFS_Request_vector(16, 4, 64, PVFS_DOUBLE, &r);
	PVFS_Dump_request(r);

	/* pack the request */
	r_size = PINT_REQUEST_PACK_SIZE(r);
	r_packed = (struct PINT_Request *)malloc(r_size);
	an_int = 0; /* this must be zero */
	PINT_Request_commit(r_packed, r, &an_int);
	PVFS_Dump_request(r_packed);

	/* now prepare for sending on wire */
	PINT_Request_encode(r_packed);

	{
		struct PINT_Request *r2;
		r2 = (struct PINT_Request *)malloc(r_size);
		memcpy(r2, r_packed, r_size); /* simulates sending on wire */

		/* now we'll unencode and see what we have */
		PINT_Request_decode(r2);

		/* for now we'll just dump the request */
		PVFS_Dump_request(r2);

		/* we're done */
		free(r2);
	}
}
