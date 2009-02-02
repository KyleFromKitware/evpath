#ifndef GEN_THREAD_H
#include "gen_thread.h"
#endif
#define EV_INTERNAL_H

typedef struct ev_free_block_rec {
    int ref_count;
    CManager cm;
    void *free_arg;
    void *block;
    FMFormat ioformat;
/*    EControlContext locking_context;*/
    attr_list attrs;
    struct free_block_rec *next;
} *ev_free_block_rec_p;

typedef enum { Event_App_Owned,  Event_Freeable, Event_CM_Owned } event_pkg_contents;

typedef struct _event_item {
    int ref_count;
    int event_encoded;
    event_pkg_contents contents;
    void *encoded_event;
    int event_len;
    void *decoded_event;
    FFSEncodeVector encoded_eventv;
    FMFormat reference_format;
    FFSBuffer ioBuffer;
    CMFormat format;
    attr_list attrs;

    /* used for malloc/free */
    CManager cm;
    void *free_arg;
    EVFreeFunction free_func;
} event_item, *event_queue;

typedef enum { Action_NoAction = 0, Action_Output, Action_Terminal, Action_Filter, Action_Immediate, Action_Multi, Action_Decode, Action_Encode_to_Buffer, Action_Split, Action_Store, Action_Congestion } action_value;

typedef enum {Immediate, Immediate_and_Multi, Output, Congestion} action_class;

/*!
 * The prototype of a specific queued handler funcion.
 *
 * This function prototype is used by the EVPath internal "response"
 * interface.  At some point, the response interface will likely become
 * external so that EVPath's response to unknown data can be customized.
 * However, at the moment this is an internal interface.
 */
struct queue_item;  /* forward decl */
struct _queue;
typedef int (*EVMultiHandlerFunc) ARGS((CManager cm, 
                                         struct _queue *queue,
                                         struct queue_item *item,
					 void *client_data,
					 int out_count,
					 int *out_stones));

typedef struct bridge_action_struct {
    CMConnection conn;
    int remote_stone_id;
    int remote_path_len;
    char *remote_path;
    int conn_failed;
} bridge_action_vals;

typedef struct decode_action_struct {
    FFSTypeHandle decode_format; /* has conversion registered */
    FMFormat target_reference_format;
    FFSContext context;
} decode_action_vals;

typedef struct immediate_cache_vals {
    EVImmediateHandlerFunc handler;
    void *client_data;
} immediate_cache_vals;

typedef struct multi_cache_vals {
    EVMultiHandlerFunc handler;
    void *client_data;
} multi_cache_vals;

typedef struct immediate_action_struct {
    void *mutable_response_data;
    int output_count;
    int *output_stone_ids;
} immediate_action_vals;

typedef struct queue_item {
    event_item *item;
    int action_id;
    struct queue_item *next;
} queue_item;

typedef struct _queue {
    queue_item *queue_head;
    queue_item *queue_tail;
} queue_struct, *queue_ptr;

struct terminal_proto_vals {
    EVSimpleHandlerFunc handler;
    void *client_data;
    int target_stone_id;
};

typedef struct _storage_queue *storage_queue_ptr;
typedef struct _storage_queue_ops {
    void (*init)(CManager cm, storage_queue_ptr queue, attr_list attrs);
    void (*cleanup)(CManager cm, storage_queue_ptr queue);
    void (*enqueue)(CManager cm, storage_queue_ptr queue, event_item *item);
    event_item *(*dequeue)(CManager cm, storage_queue_ptr queue);
    void (*empty)(CManager cm, storage_queue_ptr queue);
} storage_queue_ops, *storage_queue_ops_ptr;

typedef struct _storage_queue {
    union {
        void *data;
        queue_struct queue;
    } u;
    struct _storage_queue_ops *ops;
} storage_queue;

struct storage_proto_vals {
    int target_stone_id;
    int is_paused;
    int is_sending;
    int max_stored;
    int num_stored;
    storage_queue queue;
};

typedef enum {Accepts_All, Requires_Decoded, Requires_Contig_Encoded, Requires_Vector_Encoded} encode_state;

typedef struct _proto_action {
    action_value action_type;
    FMStructDescList input_format_requirements;
    FMFormat *matching_reference_formats;
    union {
	struct terminal_proto_vals term;
	bridge_action_vals out;
	decode_action_vals decode;
	immediate_action_vals imm;
	int *split_stone_targets;
        struct storage_proto_vals store;
    }o;
    encode_state data_state;
    attr_list attrs;
    double event_length_sum;  /*in KBytes*/
} proto_action;

typedef struct response_cache_element {
    FMFormat reference_format;
    action_class stage;
    action_value action_type;		/* if -1, no action */
    int proto_action_id;
    int requires_decoded;
    union {
	decode_action_vals decode;
	immediate_cache_vals imm;
	multi_cache_vals multi;
    }o;
} response_cache_element;

typedef enum {
    Stall_None      = 0,
    Stall_Overload  = 1 << 0, /* too many queued messages */
    Stall_Squelch   = 1 << 1, /* squelched by remote stone */
    Stall_Requested = 1 << 2, /* requested explicitly (EVstall/unstall_stone) */
    Stall_Upstream  = 1 << 3, /* upstream stalled while we were stalled; thus we need to make
                                 special considerations when unstalling */
} stall_source;

typedef struct _stall_callback {
    CManager cm;
    EVSubmitCallbackFunc cb;
    void *user_data;
    struct _stall_callback *next;
} stall_callback;

typedef struct _stone {
    int local_id;
    int default_action;
    int is_frozen;
    int is_processing;
    int is_outputting;
    int is_draining; /* this is bizarrely trivalued (0, 1, 2) */
    int is_stalled; /* for backpressure */
    stall_source stall_from; /* for backpressure */
    int queue_size; /* for backpressure */
    int pending_output; /* for storage; do we have pending events to push */
    int response_cache_count;
    response_cache_element *response_cache;
    queue_ptr queue;
    int new_enqueue_flag;
    int write_callback;
    int proto_action_count;
    struct _proto_action *proto_actions;
    CMTaskHandle periodic_handle;
    attr_list stone_attrs;

    CMConnection last_remote_source;
    int squelch_depth;
    stall_callback *unstall_callbacks;
} *stone_type;
    
typedef struct _event_path_data {
    int stone_count;
    int stone_base_num;
    stone_type stone_map;
    void *as;
    FMContext fmc;
    FFSContext ffsc;
    queue_item *queue_items_free_list;
    event_item *current_event_item;
    queue_item *taken_events_list;
    thr_mutex_t lock;
    int use_backpressure;
} *event_path_data;

struct _EVSource {
    CManager cm;
    CMFormat format;
    FMFormat reference_format;
    int local_stone_id;
    int preencoded;
    EVFreeFunction free_func;
    void *free_data;
};


extern void EVPinit(CManager cm);
extern FMFormat
EVregister_format_set(CManager cm, FMStructDescList list);

extern int
internal_path_submit(CManager cm, int local_path_id, event_item *event);
extern void INT_EVsubmit(EVsource source, void *data, attr_list attrs);
extern EVaction
INT_EVassoc_raw_terminal_action(CManager cm, EVstone stone_num, 
				EVRawHandlerFunc handler,
				void *client_data);
extern int
INT_EVsubmit_or_wait(EVsource source, void *data, attr_list attrs,
		     EVSubmitCallbackFunc cb, void *user_data);
extern int INT_EVsubmit_encoded_or_wait ( CManager cm, EVstone stone, void *data, int data_len, attr_list attrs, EVSubmitCallbackFunc cb, void *user_data );
extern EVstone INT_EVcreate_bridge_action(CManager cm, attr_list contact_list, EVstone remote_stone);
extern EVstone INT_EVcreate_immediate_action(CManager cm, char *action_spec, EVstone *target_list);
extern EVstone INT_EVcreate_split_action(CManager cm, EVstone *target_list);
extern EVstone INT_EVcreate_terminal_action(CManager cm, FMStructDescList format_list, 
					    EVSimpleHandlerFunc handler, 
					    void *client_data);
extern EVstone INT_EVcreate_auto_stone(CManager cm, int period_sec, 
				       int period_usec, char *action_spec, 
				       EVstone out_stone);
extern EVstone INT_EVcreate_store_action(CManager cm, EVstone out_tsone, int store_limit);
extern EVaction
INT_EVassoc_mutated_multi_action(CManager cm, EVstone stone_id, EVaction act_num,
				  EVMultiHandlerFunc func, void *client_data, 
				  FMFormat *reference_formats);
extern EVaction
INT_EVassoc_congestion_action(CManager cm, EVstone stone_num, 
			      char *action_spec, void *client_data);

extern EVevent_list extract_events_from_queue(CManager cm, queue_ptr que, EVevent_list list);
extern event_item * get_free_event(event_path_data evp);
extern void return_event(event_path_data evp, event_item *event);
extern void cod_encode_event(CManager cm, event_item *event);
extern event_item *cod_decode_event(CManager cm, int stone_num, int act_num, event_item *event);
extern void EVdiscard_queue_item(CManager cm, int stone, queue_item *item);

extern void INT_EVstall_stone(CManager cm, EVstone stone_id);
extern void INT_EVunstall_stone(CManager cm, EVstone stone_id);
extern void REVPinit(CManager cm);
extern int
internal_write_event(CMConnection conn, CMFormat format, 
		     void *remote_path_id, int path_len, event_item *event,
		     attr_list attrs);
extern EVaction
INT_EVassoc_mutated_imm_action(CManager cm, EVstone stone, EVaction act_num,
			       EVImmediateHandlerFunc func, void *client_data,
			       FMFormat reference_format);
extern void
INT_EVassoc_conversion_action(CManager cm, int stone_id, int stage, FMFormat target_format,
			      FMFormat incoming_format);
extern void
INT_EVaction_remove_split_target(CManager cm, EVstone stone, EVaction action,
			  EVstone target_stone);
extern EVaction
INT_EVassoc_bridge_action(CManager cm, EVstone stone, attr_list contact_list, 
			  EVstone remote_stone);
extern EVaction
INT_EVassoc_terminal_action(CManager cm, EVstone stone, FMStructDescList format_list, 
			    EVSimpleHandlerFunc handler, void* client_data);
extern int
INT_EVaction_add_split_target(CManager cm, EVstone stone, EVaction action,
			  EVstone target_stone);

extern int
INT_EVaction_set_output(CManager cm, EVstone stone, EVaction action, 
		    int output_index, EVstone output_stone);
extern EVaction
INT_EVassoc_filter_action(CManager cm, EVstone stone, 
			  FMStructDescList incoming_format_list, 
			  EVSimpleHandlerFunc handler, EVstone out_stone,
			  void* client_data);
extern void
INT_EVenable_auto_stone(CManager cm, EVstone stone_num, int period_sec, 
		    int period_usec);
extern void
INT_EVsubmit_general(EVsource source, void *data, EVFreeFunction free_func,
		 attr_list attrs);
extern void
INT_EVsubmit_encoded(CManager cm, EVstone stone, void *data, int data_len, attr_list attrs);
extern EVsource

INT_EVcreate_submit_handle_free(CManager cm, EVstone stone, FMStructDescList data_format,
			    EVFreeFunction free_func, void *client_data);
extern EVaction
INT_EVassoc_multi_action(CManager cm, EVstone stone, char *queue_spec, 
		      void *client_data);
extern EVaction
INT_EVassoc_immediate_action(CManager cm, EVstone stone, char *queue_spec, 
		      void *client_data);
extern void INT_EVfree_stone(CManager cm, EVstone stone);
extern EVstone INT_EValloc_stone(CManager cm);
extern void INT_EVsend_stored(CManager cm, EVstone stone, EVaction action);
extern void INT_EVclear_stored(CManager cm, EVstone stone, EVaction action);
extern EVaction INT_EVassoc_store_action(CManager cm, EVstone stone, EVstone out_stone, int store_limit); 
extern EVaction
INT_EVassoc_split_action(CManager cm, EVstone stone, EVstone *target_list);
extern EVsource
INT_EVcreate_submit_handle(CManager cm, EVstone stone, FMStructDescList data_format);
extern FMFormat INT_EVget_src_ref_format(EVsource source);
extern int INT_EVfreeze_stone(CManager cm, EVstone stone_id);
extern int INT_EVunfreeze_stone(CManager cm, EVstone stone_id);
extern int INT_EVdrain_stone(CManager cm, EVstone stone_id);
extern EVevent_list INT_EVextract_stone_events(CManager cm, EVstone stone_id);
extern attr_list INT_EVextract_attr_list(CManager cm, EVstone stone_id);
extern void INT_EVset_attr_list(CManager cm, EVstone stone_id, attr_list list);
extern void INT_EVset_store_limit(CManager cm, EVstone stone_num, EVaction action_num, int store_limit);
extern void INT_EVstore_start_send(CManager cm, EVstone stone_num, EVaction action_num);
extern int INT_EVstore_is_sending(CManager cm, EVstone stone_num, EVaction action_num);
extern int INT_EVstore_count(CManager cm, EVstone stone_num, EVaction action_num);
extern int INT_EVdestroy_stone(CManager cm, EVstone stone_id);
extern void INT_EVfree_source(EVsource source);
