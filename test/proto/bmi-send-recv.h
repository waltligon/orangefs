
int send_msg(bmi_op_id_t i, 
		bmi_addr_t s, 
		void *msg, 
		int size, 
		int f, 
		bmi_msg_tag_t t, 
		void *in_test_user_ptr,
		bmi_context_id context);

int recv_msg(bmi_op_id_t i,
				bmi_addr_t a,
				void *buffer,
				bmi_size_t s,
				bmi_size_t *as,
				int f,
				bmi_msg_tag_t m,
				void *in_test_user_ptr,
				bmi_context_id context);
