typedef struct queue_item
{
	struct PVFS_server_op *op;
	struct queue_item *next_east_west;
	struct queue_item *previous_east_west;
	struct queue_item *pending_operation_north;
} PINT_queue_item;

/* Prototypes */
int PINT_server_queue_shutdown(int flag);
int PINT_server_queue_halt(void);
int PINT_server_queue_init(void);
int PINT_server_enqueue(struct PVFS_server_op *op);
int PINT_server_dequeue(struct PVFS_server_op *op);
void PINT_server_check_dependancies(PINT_server_op *op, job_status_s *r);
