/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <bmi.h>
#include <pvfs2-req-proto.h>
#include <gossip.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <PINT-reqproto-encode.h>
#ifdef USE_BMI_MSGS
#include <bmi-send-recv.h>
#endif

#define RET_CHECK(__name) if(ret != 0) {printf(__name);exit(-1);}


int main(int argc, char **argv)
{
	struct PVFS_server_req_s *request;
	struct PVFS_server_resp_s *response;
	struct PINT_encoded_msg encoded;
	struct PINT_decoded_msg decoded;
	int ret;
	bmi_addr_t me;
	PVFS_object_attr *obj_attr;
#ifdef USE_BMI_MSGS
	bmi_op_id_t client_ops[2];
	bmi_size_t actual_size;
	void* bmi_resp;
	void* in_test_user_ptr = &me;
	bmi_context_id context;
#endif

	request = (struct PVFS_server_req_s *) malloc(sizeof(struct PVFS_server_req_s));
	response = (struct PVFS_server_resp_s *) malloc(sizeof(struct PVFS_server_resp_s));

	ret = BMI_initialize("bmi_tcp", NULL, 0);

	RET_CHECK("BMI init Error")

	ret = BMI_open_context(&context);

	RET_CHECK("BMI_open_context Error")

	ret = BMI_addr_lookup(&me,"tcp://localhost:3334");

	RET_CHECK("BMI addr Error")
	// Create

	response->op = PVFS_SERV_CREATE;
	response->u.create.handle = 7;
	ret = PINT_encode(response,
							PINT_ENCODE_RESP,
							&encoded,
							me,
							0);
	RET_CHECK("Error in Encoding Create Resp\n")
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

	free(encoded.buffer_list[0]);
   encoded.buffer_list[0] = bmi_resp;	
	encoded.total_size = actual_size;
#endif

	ret = PINT_decode(encoded.buffer_list[0],
							PINT_ENCODE_RESP,
							&decoded,
							me,
							encoded.total_size,
							NULL);

#ifndef USE_BMI_MSGS
	PINT_encode_release(&encoded,
								PINT_ENCODE_RESP,
								0);
#endif

	//PINT_decode_release(&decoded,
								//PINT_ENCODE_RESP,
								//0);

	RET_CHECK("Error in Decoding Create Resp\n")

	if(((struct PVFS_server_resp_s *)decoded.buffer)->op != PVFS_SERV_CREATE)
		if(0)
		{
		printf("Op not correct after decoding Create Resp... Got %d\n",
					//((struct PVFS_serv_req_s *)decoded.buffer)­>op);
					((struct PVFS_server_resp_s *)decoded.buffer)->op);
		ret = -1;
		}
	if(((struct PVFS_server_resp_s *)decoded.buffer)->u.create.handle !=7)
	{
		printf("Handle not correct after decoding Create Resp... Got %lld\n",
					((struct PVFS_server_resp_s *)decoded.buffer)->u.create.handle);
		ret =-1;
	}

#ifdef VERBOSE_DEBUG
	if (ret != 0)
	{
		printf("Error! \n");
		display_pvfs_structure(decoded.buffer,2);
		display_pvfs_structure(response,2);
	}
	printf("\n\n-------=============Test 1 Completed============-----------\n\n\n");
#else
	if (ret != 0)
		printf("Create: %d\n",ret);
#endif

	//Get Config

	response->op = PVFS_SERV_GETCONFIG;
/* 	response->u.getconfig.fs_id = 7; */
/* 	response->u.getconfig.root_handle=9; */
/* 	response->u.getconfig.maskbits = 0; */
/* 	response->u.getconfig.meta_server_count=3; */
/* 	response->u.getconfig.meta_server_mapping="tcp://foo1:3334,tcp://foo2:3334"; */
/* 	response->u.getconfig.io_server_count=3; */
/* 	response->u.getconfig.io_server_mapping="tcp://bar1:3334,tcp://bar2:3334"; */

#ifdef USE_BMI_MSGS
	BMI_memfree(me,
					bmi_resp,
					encoded.size_list[0],
					BMI_RECV
					);
#endif
	ret = PINT_encode(response,
							PINT_ENCODE_RESP,
							&encoded,
							me,
							0);
	RET_CHECK("Error in Encoding GetConfig Resp\n")

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
   encoded.buffer_list[0] = bmi_resp;	
	encoded.total_size = actual_size;
#endif


	ret = PINT_decode(encoded.buffer_list[0],
							PINT_ENCODE_RESP,
							&decoded,
							me,
							encoded.total_size,
							NULL);
	RET_CHECK("Error in Decoding GetConfig Resp\n")
	
	if(((struct PVFS_server_resp_s *)decoded.buffer)->op != PVFS_SERV_GETCONFIG)
	{
		printf("Op not correct after decoding Getconfig... Got %d\n",
					((struct PVFS_server_resp_s *)decoded.buffer)->op);
		ret = -1;
	}
#ifdef VERBOSE_DEBUG
	if (ret != 0)
	{
		printf("Error! \n");
		printf("\n\n-------=============Sent============-----------\n\n\n");
		display_pvfs_structure(response,2);
		printf("\n\n-------=============Recv'd============-----------\n\n\n");
		display_pvfs_structure(decoded.buffer,2);
	}
	printf("\n\n-------=============Test 2 Completed============-----------\n\n\n");
#else
	if (ret != 0)
		printf("GetConfig: %d\n",ret);
#endif


#ifndef USE_BMI_MSGS
	PINT_encode_release(&encoded,
								PINT_ENCODE_RESP,
								0);
#endif

	//PINT_decode_release(&decoded,
								//PINT_ENCODE_RESP,
								//0);

	// Lookup Path

	response->op = PVFS_SERV_LOOKUP_PATH;

	response->u.lookup_path.attr_array = malloc(sizeof(PVFS_object_attr)*3);
	obj_attr = response->u.lookup_path.attr_array;
	obj_attr[0].owner = 100;
	obj_attr[1].owner = 101;
	obj_attr[2].owner = 102;
	obj_attr[0].group = 101;
	obj_attr[1].group = 102;
	obj_attr[2].group = 103;
	obj_attr[0].perms = 
		obj_attr[1].perms = 
			obj_attr[2].perms = O_READ | O_WRITE;
				

	//memcpy(response->u.lookup_path.attr_array,&obj_attr,sizeof(PVFS_object_attr)*3);

	response->u.lookup_path.handle_array = malloc( sizeof(PVFS_handle)*3);
	response->u.lookup_path.handle_array[0] = 5;
	response->u.lookup_path.handle_array[1] = 6;
	response->u.lookup_path.handle_array[2] = 7;
	response->u.lookup_path.count = 3;


#ifdef USE_BMI_MSGS
	BMI_memfree(me,
					bmi_resp,
					encoded.size_list[0],
					BMI_RECV
					);
#endif
	ret = PINT_encode(response,
							PINT_ENCODE_RESP,
							&encoded,
							me,
							0);
	RET_CHECK("Error in Encoding Lookup_path Resp\n")

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

	free(encoded.buffer_list[0]);
   encoded.buffer_list[0] = bmi_resp;	
	encoded.total_size = actual_size;
#endif


	ret = PINT_decode(encoded.buffer_list[0],
							PINT_ENCODE_RESP,
							&decoded,
							me,
							encoded.total_size,
							NULL);
	RET_CHECK("Error in Decoding Lookup Resp\n")
	
	if(((struct PVFS_server_resp_s *)decoded.buffer)->op != PVFS_SERV_LOOKUP_PATH)
	{
		printf("Op not correct after decoding Getconfig... Got %d\n",
					((struct PVFS_server_resp_s *)decoded.buffer)->op);
		ret = -1;
	}
#ifdef VERBOSE_DEBUG
		printf("\n\n-------=============Sent============-----------\n\n\n");
		display_pvfs_structure(response,2);
		printf("\n\n-------=============Recv'd============-----------\n\n\n");
		display_pvfs_structure(decoded.buffer,2);
	if (ret != 0)
	{
		printf("Error! \n");
		printf("\n\n-------=============Sent============-----------\n\n\n");
		display_pvfs_structure(response,2);
		printf("\n\n-------=============Recv'd============-----------\n\n\n");
		display_pvfs_structure(decoded.buffer,2);
	}
	printf("\n\n-------=============Test 3 Completed============-----------\n\n\n");
#else
	if (ret != 0)
		printf("Lookup_path: %d\n",ret);
#endif

#ifndef USE_BMI_MSGS
	PINT_encode_release(&encoded,
								PINT_ENCODE_RESP,
								0);
#endif

	//PINT_decode_release(&decoded,
								//PINT_ENCODE_RESP,
								//0);


#ifdef USE_BMI_MSGS
	BMI_memfree(me,
					bmi_resp,
					encoded.size_list[0],
					BMI_RECV
					);
	BMI_close_context(context);
	BMI_finalize();
#endif
	free(request);
	free(response);

	return(0);
}
