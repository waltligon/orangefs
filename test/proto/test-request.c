/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#define USE_BMI_MSGS 1
#include <bmi.h>
#include <pvfs2-req-proto.h>
#include <gossip.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <PINT-reqproto-encode.h>
#ifdef USE_BMI_MSGS
#include <bmi-send-recv.h>
#endif


#define MAX_MSGS 10

#define RET_CHECK(__name) if(ret != 0) {printf(__name);printf("ret=%d\n",ret);exit(-1);}

#define DEFAULT_ADDRESS "tcp://speed.parl.clemson.edu:3334"
#define TEST_STR "`1234567890-=qwertyuiop[]asdfghjkl;'zxcvbnm,./"
#define NUM_DATAFILES 10

void arrow(int direction)
{
	if (!direction)
	{
		printf("=<=");
	}
	else
	{
		printf("=>=");
	}
}

void print_request(struct PVFS_server_req *my_req, int direction)
{
	int i;
        // print shit
	arrow(direction);
        printf("======================================\n");
	arrow(direction);
        printf("struct address = %d\n", (int)my_req );
        switch(my_req->op)
        {
                case PVFS_SERV_READDIR:
			arrow(direction);
                        printf("PVFS_SERV_READDIR structure:\n");
			arrow(direction);
                        printf("op = %d \n",(int) my_req->op);
			arrow(direction);
                        printf("rsize = %d\n",(int) my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n", (int)my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n",(int) my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n",(int) my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_readdir->handle = %d\n",(int) my_req->u.readdir.handle );
			arrow(direction);
                        printf("PVFS_servreq_readdir->fs_id = %d\n",(int) my_req->u.readdir.fs_id );
			arrow(direction);
                        printf("PVFS_servreq_readdir->token = %d\n",(int) my_req->u.readdir.token );
			arrow(direction);
                        printf("PVFS_servreq_readdir->dirent_count = %d\n",(int) my_req->u.readdir.dirent_count );
			arrow(direction);
                        printf("======================================\n");
                        break;

                case PVFS_SERV_GETATTR:
			arrow(direction);
                        printf("PVFS_SERV_GETATTR structure:\n");
			arrow(direction);
                        printf("op = %d \n", (int)my_req->op);
			arrow(direction);
                        printf("rsize = %d\n", (int)my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n", (int)my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n",(int) my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n",(int) my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_getattr->handle = %d\n", (int)my_req->u.getattr.handle );
			arrow(direction);
                        printf("PVFS_servreq_getattr->fs_id = %d\n",(int) my_req->u.getattr.fs_id );
			arrow(direction);
                        printf("PVFS_servreq_getattr->attrmask = %d\n", (int)my_req->u.getattr.attrmask );
			arrow(direction);
                        printf("======================================\n");
                        break;
                case PVFS_SERV_REMOVE:
			arrow(direction);
                        printf("PVFS_SERV_REMOVE structure:\n");
			arrow(direction);
                        printf("op = %d \n", (int)my_req->op);
			arrow(direction);
                        printf("rsize = %d\n", (int)my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n", (int)my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n", (int)my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n", (int)my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_remove->handle = %d\n", (int)my_req->u.remove.handle );
			arrow(direction);
                        printf("PVFS_servreq_remove->fs_id  = %d\n", (int)my_req->u.remove.fs_id );
			arrow(direction);
                        printf("======================================\n");
                        break;

                case PVFS_SERV_TRUNCATE:
			arrow(direction);
                        printf("PVFS_SERV_TRUNCATE structure:\n");
			arrow(direction);
                        printf("op = %d \n", (int)my_req->op);
			arrow(direction);
                        printf("rsize = %d\n",(int) my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n",(int) my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n", (int)my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n", (int)my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_truncate->handle = %d\n", (int)my_req->u.truncate.handle );
			arrow(direction);
                        printf("PVFS_servreq_truncate->fs_id = %d\n",(int) my_req->u.truncate.fs_id );
			arrow(direction);
                        printf("PVFS_servreq_truncate->size = %d\n", (int)my_req->u.truncate.size );
			arrow(direction);
                        printf("======================================\n");
                        break;

                case PVFS_SERV_CREATE:
			arrow(direction);
                        printf("PVFS_SERV_CREATE structure:\n");
			arrow(direction);
                        printf("op = %d \n", (int)my_req->op);
			arrow(direction);
                        printf("rsize = %d\n", (int)my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n", (int)my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n",(int) my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n", (int)my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_create->requested_handle = %d\n", (int)my_req->u.create.requested_handle );
			arrow(direction);
                        printf("PVFS_servreq_create->fs_id = %d\n",(int) my_req->u.create.fs_id );
			arrow(direction);
                        printf("PVFS_servreq_create->object_type = %d\n", (int)my_req->u.create.object_type );
			arrow(direction);
                        printf("======================================\n");
                        break;

                case PVFS_SERV_GETCONFIG:
			arrow(direction);
                        printf("PVFS_SERV_GETCONFIG structure:\n");
			arrow(direction);
                        printf("op = %d \n", (int)my_req->op);
			arrow(direction);
                        printf("rsize = %d\n",(int) my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n", (int)my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n", (int)my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n", (int)my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_getconfig.config_buf_size = %d\n", (int)my_req->u.getconfig.config_buf_size );
			arrow(direction);
			arrow(direction);
                        printf("======================================\n");
                        break;

		case PVFS_SERV_LOOKUP_PATH:
			arrow(direction);
                        printf("PVFS_SERV_LOOKUP_PATH structure:\n");
			arrow(direction);
                        printf("op = %d \n", (int)my_req->op);
			arrow(direction);
                        printf("rsize = %d\n", (int)my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n", (int)my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n", (int)my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n", (int)my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_lookup_path.path = %s\n", my_req->u.lookup_path.path );
			arrow(direction);
                        printf("PVFS_servreq_lookup_path.fs_id = %d\n",(int) my_req->u.lookup_path.fs_id );
			arrow(direction);
                        printf("PVFS_servreq_lookup_path.starting_handle = %d\n", (int)my_req->u.lookup_path.starting_handle );
			arrow(direction);
                        printf("PVFS_servreq_lookup_path.attrmask = %d\n", (int)my_req->u.lookup_path.attrmask );
			arrow(direction);
                        printf("======================================\n");
                        break;

		case PVFS_SERV_CREATEDIRENT:
			arrow(direction);
                        printf("PVFS_SERV_CREATEDIRENT structure:\n");
			arrow(direction);
                        printf("op = %d \n", (int)my_req->op);
			arrow(direction);
                        printf("rsize = %d\n", (int)my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n", (int)my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n", (int)my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n",(int) my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_crdirent.name = %s\n", my_req->u.crdirent.name );
			arrow(direction);
                        printf("PVFS_servreq_crdirent.new_handle = %d\n", (int)my_req->u.crdirent.new_handle );
			arrow(direction);
                        printf("PVFS_servreq_crdirent.parent_handle = %d\n", (int)my_req->u.crdirent.parent_handle );
			arrow(direction);
                        printf("PVFS_servreq_crdirent.fs_id = %d\n", (int)my_req->u.crdirent.fs_id );
			arrow(direction);
                        printf("======================================\n");
                        break;

		case PVFS_SERV_RMDIRENT:
			arrow(direction);
                        printf("PVFS_SERV_RMDIRENT structure:\n");
			arrow(direction);
                        printf("op = %d \n", (int)my_req->op);
			arrow(direction);
                        printf("rsize = %d\n", (int)my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n", (int)my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n", (int)my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n", (int)my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_rmdirent.entry = %s\n", my_req->u.rmdirent.entry );
			arrow(direction);
                        printf("PVFS_servreq_rmdirent.parent_handle = %d\n", (int)my_req->u.rmdirent.parent_handle );
			arrow(direction);
                        printf("PVFS_servreq_rmdirent.fs_id = %d\n", (int)my_req->u.rmdirent.fs_id );
			arrow(direction);
                        printf("======================================\n");
                        break;

		case PVFS_SERV_SETATTR:
			arrow(direction);
                        printf("PVFS_SERV_SETATTR structure:\n");
			arrow(direction);
                        printf("op = %d \n", (int)my_req->op);
			arrow(direction);
                        printf("rsize = %d\n",(int) my_req->rsize);
			arrow(direction);
                        printf("credentials.uid = %d\n", (int)my_req->credentials.uid);
			arrow(direction);
                        printf("credentials.gid = %d\n", (int)my_req->credentials.gid);
			arrow(direction);
                        printf("credentials.perms = %d\n", (int)my_req->credentials.perms);
			arrow(direction);
                        printf("PVFS_servreq_setattr.handle = %d\n", (int)my_req->u.setattr.handle );
			arrow(direction);
                        printf("PVFS_servreq_setattr.fs_id = %d\n", (int)my_req->u.setattr.fs_id );
			arrow(direction);
                        printf("PVFS_servreq_setattr.attr.mask = %d\n",(int) my_req->u.setattr.attr.mask );
			arrow(direction);
                        printf("PVFS_servreq_setattr.attr.owner = %d\n", (int)my_req->u.setattr.attr.owner );
			arrow(direction);
                        printf("PVFS_servreq_setattr.attr.group = %d\n", (int)my_req->u.setattr.attr.group );
			arrow(direction);
                        printf("PVFS_servreq_setattr.attr.perms = %d\n", (int)my_req->u.setattr.attr.perms );
			arrow(direction);
                        printf("PVFS_servreq_setattr.attr.atime = %d\n", (int)my_req->u.setattr.attr.atime );
			arrow(direction);
                        printf("PVFS_servreq_setattr.attr.mtime = %d\n", (int)my_req->u.setattr.attr.mtime );
			arrow(direction);
                        printf("PVFS_servreq_setattr.attr.ctime = %d\n", (int)my_req->u.setattr.attr.ctime );
			arrow(direction);
                        printf("PVFS_servreq_setattr.attr.objtype = %d\n", (int)my_req->u.setattr.attr.objtype );
			arrow(direction);
			switch (my_req->u.setattr.attr.objtype)
			{
				case PVFS_TYPE_METAFILE:
                        		printf("PVFS_servreq_setattr.attr.objtype = PVFS_TYPE_METAFILE\n" );
			arrow(direction);
                        		printf("PVFS_servreq_setattr.attr.u.meta.dfile_count = %d\n", (int)my_req->u.setattr.attr.u.meta.dfile_count );
					for( i=0; i<my_req->u.setattr.attr.u.meta.dfile_count; i++ )
					{
						arrow(direction);
                        			printf("PVFS_servreq_setattr.attr.u.meta.dfile_array[%d] = %d\n",(int)i, (int)my_req->u.setattr.attr.u.meta.dfile_array[i] );
					}
					break;
				case PVFS_TYPE_DATAFILE:
                        		printf("PVFS_servreq_setattr.attr.objtype = PVFS_TYPE_DATAFILE\n" );
					arrow(direction);
                        		printf("PVFS_servreq_setattr.attr.u.data.size = %d\n", (int)my_req->u.setattr.attr.u.data.size );
					arrow(direction);
					break;
				case PVFS_TYPE_DIRECTORY:
                        		printf("PVFS_servreq_setattr.attr.objtype = PVFS_TYPE_DIRECTORY\n" );
					arrow(direction);
					printf("PVFS_directory_attr_s is undefined\n");
        				//request->u.setattr.attr.u.dir = ;
					break;
				case PVFS_TYPE_SYMLINK:
                        		printf("PVFS_servreq_setattr.attr.objtype = PVFS_TYPE_SYMLINK\n" );
					arrow(direction);
					printf("PVFS_symlink_attr_s is undefined\n");
        				//request->u.setattr.attr.u.sym = ;
					break;
				default:
					printf("attribute object type %d undefined\n", my_req->u.setattr.attr.objtype);
					break;
			}
        		//request->u.setattr.extended = ;
                        printf("======================================\n");
                        break;


                default:
                        printf("op def not included for op: %d\n",my_req->op);
                        break;
        }
}


int main(int argc, char **argv)
{
	struct PVFS_server_req *request;
	struct PVFS_server_resp *response;
	struct PINT_encoded_msg encoded;
	struct PINT_decoded_msg decoded;
	bmi_addr_t me;
	int ret = 0;
	char* somechars = NULL;
	int mylen = 0;
	int i = 0;
	PVFS_handle* datafiles;
#ifdef USE_BMI_MSGS
        bmi_op_id_t client_ops[2];
        bmi_size_t actual_size;
        void* bmi_resp;
        void* in_test_user_ptr = &me;
		  bmi_context_id context;
#endif

	request = (struct PVFS_server_req *) malloc(sizeof(struct PVFS_server_req));
	datafiles = (PVFS_handle*) malloc( NUM_DATAFILES * sizeof( PVFS_handle ) );
	response = (struct PVFS_server_resp *) malloc( sizeof(struct PVFS_server_resp) );

	ret = BMI_initialize("bmi_tcp", NULL, 0);

	RET_CHECK("BMI init Error\n")

	ret = BMI_open_context(&context);
	
	RET_CHECK("BMI_open_context\n")

	//mylen = strlen(DEFAULT_ADDRESS);
	//server = (char*) malloc(mylen);
	//strncpy(server, DEFAULT_ADDRESS, mylen);

	mylen = 25;//strlen(TEST_STR);
	somechars = (char*) malloc(mylen + 1);
	//strncpy( somechars, TEST_STR, mylen );
	//strncpy( ((char*)somechars + mylen), "\0", 1 );

	//printf("before BMI_lookup\n");
	ret = BMI_addr_lookup(&(encoded.dest), DEFAULT_ADDRESS);
	ret = BMI_addr_lookup(&me, DEFAULT_ADDRESS);

	RET_CHECK("BMI lookup Error\n")

	// Create

	for(i = 0; i < MAX_MSGS; i++)
	{
	sprintf(somechars, "this is iteration %d\n", i);

	printf("before PINT_encode\n");

	switch( i )
	{
	case 0:
	request->op = PVFS_SERV_GETCONFIG;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
	request->u.getconfig.config_buf_size = 666;
	break;

	// simple ops

	case 1:
	request->op = PVFS_SERV_CREATE;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
	request->u.create.requested_handle = 69;
	request->u.create.fs_id = 11111;
	request->u.create.object_type = 11111;
	break;

	case 3:
        request->op = PVFS_SERV_READDIR;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
        request->u.readdir.handle = 11111;
        request->u.readdir.fs_id = 22222;
        request->u.readdir.token = 33333;
        request->u.readdir.dirent_count = 44444 ;
	break;

	case 4:
        request->op =  PVFS_SERV_GETATTR;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
        request->u.getattr.handle = 11111;
        request->u.getattr.fs_id  = 22222;
        request->u.getattr.attrmask  = 33333;
	break;

	case 5:
        request->op = PVFS_SERV_REMOVE;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
        request->u.remove.handle = 11111;
        request->u.remove.fs_id  = 22222;
	break;

	case 6:
        request->op = PVFS_SERV_TRUNCATE;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
        request->u.truncate.handle = 11111;
        request->u.truncate.fs_id = 22222;
        request->u.truncate.size = 33333;
	break;

	case 7:
	request->op = PVFS_SERV_LOOKUP_PATH;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
        request->u.lookup_path.path = somechars;
        request->u.lookup_path.fs_id = 11111;
        request->u.lookup_path.starting_handle = 22222;
        request->u.lookup_path.attrmask = 33333;
	break;

	case 8:
        request->op = PVFS_SERV_CREATEDIRENT;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
        request->u.crdirent.name = somechars;
        request->u.crdirent.new_handle = 111111;
        request->u.crdirent.parent_handle = 222222;
        request->u.crdirent.fs_id = 333333;
	break;

	case 9:
        request->op = PVFS_SERV_RMDIRENT;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
        request->u.rmdirent.entry = somechars;
        request->u.rmdirent.parent_handle = 111111;
        request->u.rmdirent.fs_id  = 2222222;
	break;

	default:
		printf("subscript out of range jackass\n");
		break;
	
/*
        request->op = PVFS_SERV_SETATTR;
	request->rsize = 666;
	request->credentials.uid = 420;
	request->credentials.gid = 420;
	request->credentials.perms = 420;
        request->u.setattr.handle = 111111;
        request->u.setattr.fs_id  = 2222222;
        request->u.setattr.attrmask  = 333333;
        request->u.setattr.attr.owner = 14201;
        request->u.setattr.attr.group = 14201;
        request->u.setattr.attr.perms = 14201;
        request->u.setattr.attr.atime = 16661;
        request->u.setattr.attr.mtime = 16661;
        request->u.setattr.attr.ctime = 16661;
        request->u.setattr.attr.objtype = PVFS_TYPE_METAFILE;
       		request->u.setattr.attr.u.meta.nr_datafiles = NUM_DATAFILES;
		for( i=0; i<request->u.setattr.attr.u.meta.nr_datafiles; i++ )
			datafiles[i] = i;
		request->u.setattr.attr.u.meta.dfh = datafiles;
        //request->u.setattr.attr.objtype = PVFS_TYPE_DATAFILE;
	//	request->u.setattr.attr.u.data.size = 100;

        // -
	// - should test these structs, but they're undefined at present
	// -
	//request->u.setattr.attr.u.dir = ;
        //request->u.setattr.attr.u.sym = ;
        //request->u.setattr.extended = ;
*/

	} // end of switch statement


	printf("struct before enc:\n");
	print_request( request, 0 );
	ret = PINT_encode(request,
							PINT_ENCODE_REQ,
							&encoded,
							me,
							0);
	RET_CHECK("Error in Encoding Create Req\n")

#ifdef USE_BMI_MSGS
        bmi_resp = BMI_memalloc(me,encoded.size_list[0],BMI_RECV);
        ret = send_msg(client_ops[1],
                                                me,
                                                encoded.buffer_list[0],
                                                encoded.size_list[0],
                                                BMI_PRE_ALLOC,
                                                0,
                                                in_test_user_ptr,
																context);

        /* post a recv for the server acknowledgement */

        ret = recv_msg(client_ops[0],
                                                me,
                                                bmi_resp,
                                                encoded.size_list[0],
                                                &actual_size,
                                                BMI_PRE_ALLOC,
                                                0,
                                                in_test_user_ptr,
																context);

        //free(encoded.buffer_list[0]);
   //encoded.buffer_list[0] = bmi_resp;
#endif
	//printf("before decode:\n");

	//ret = PINT_decode(encoded.buffer_list[0],
	ret = PINT_decode(bmi_resp,
							PINT_ENCODE_REQ,
							&decoded,
							me,
							actual_size,
							NULL);
	RET_CHECK("Error in Decoding Create Resp\n")

	print_request( (struct PVFS_server_req *)decoded.buffer , 1 );

	PINT_encode_release(	&encoded,
				PINT_ENCODE_REQ,
        			0);

	PINT_decode_release(	&decoded,
				PINT_ENCODE_REQ,
        			0);
#ifdef USE_BMI_MSGS
        BMI_memfree(me,
                                        bmi_resp,
                                        encoded.size_list[0],
                                        BMI_RECV
                                        );
	BMI_close_context(context);
	BMI_finalize();
#endif

	}// end of for loop

	free(request);
	free(response);
	free(somechars);
	free(datafiles);

	return(0);
}
