#ifndef ZOID_H
#define ZOID_H

#define ZOID_MAX_EXPECTED_MSG (128 * 1024 * 1024)
#define ZOID_MAX_UNEXPECTED_MSG 8192
#define ZOID_MAX_EAGER_MSG 1024


#define ZOID_ADDR_SERVER_PID -1

struct zoid_addr
{
    int pid;
};

/* zoid.c */

extern int zoid_method_id;

/* server.c */

int BMI_zoid_server_initialize(void);
int BMI_zoid_server_finalize(void);
void* BMI_zoid_server_memalloc(bmi_size_t size);
void BMI_zoid_server_memfree(void* buffer);
int BMI_zoid_server_unexpected_free(void* buffer);
int BMI_zoid_server_testunexpected(int incount, int* outcount,
				   struct bmi_method_unexpected_info* info,
				   uint8_t class, int max_idle_time_ms);
int zoid_server_send_common(bmi_op_id_t* id, bmi_method_addr_p dest,
			    const void*const* buffer_list,
			    const bmi_size_t* size_list, int list_count,
			    bmi_size_t total_size, enum bmi_buffer_type
			    buffer_type, bmi_msg_tag_t tag, void* user_ptr,
			    bmi_context_id context_id, PVFS_hint hints);
int zoid_server_recv_common(bmi_op_id_t* id, bmi_method_addr_p src,
			    void *const* buffer_list, const bmi_size_t*
			    size_list, int list_count, bmi_size_t
			    total_expected_size, bmi_size_t* total_actual_size,
			    enum bmi_buffer_type buffer_type, bmi_msg_tag_t tag,
			    void* user_ptr, bmi_context_id context_id,
			    PVFS_hint hints);
int zoid_server_test_common(int incount, bmi_op_id_t* id_array,
			    int outcount_max, int* outcount, int* index_array,
			    bmi_error_code_t* error_code_array,
			    bmi_size_t* actual_size_array,
			    void** user_ptr_array, int max_idle_time_ms,
			    bmi_context_id context_id);
int BMI_zoid_server_cancel(bmi_op_id_t id, bmi_context_id context_id);
void zoid_server_free_client_addr(bmi_method_addr_p addr);
#endif
